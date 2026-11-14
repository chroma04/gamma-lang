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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binary_6rg6s7q {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_binop_nrsq3mp {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_bytes_equal_cksjlgj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cmp_op_mflafbf {
    gm_frame h;
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_contains_nugmipn {
    gm_frame h;
    gm_t2jeg3bg7sfqa l6; /* s */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_equal_e5vf82h {
    gm_frame h;
    gm_t2jeg3bg7sfqa l20; /* sa */
    gm_t2jeg3bg7sfqa l21; /* sb */
    gm_tpautr4br7h3l l39; /* def */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_invalid_tag_1gif354 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_invalid_tag_text_f3l5hi7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_cmp_r2i4hv9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_less_nbfe87l {
    gm_frame h;
    gm_t2jeg3bg7sfqa l14; /* sa */
    gm_t2jeg3bg7sfqa l15; /* sb */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_op_sym_6olbepb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_seq_of_ifahtjs {
    gm_frame h;
    gm_tpekhfs32op0f l10; /* e */
    gm_tmc91gv7n9c0i ret;
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
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unary_sorkbs7 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_unop_qamfooj {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_binary_6rg6s7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_bytes_equal_cksjlgj(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
__attribute__((aligned(4))) void gm_f_format_aib5qn5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b946o7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bvot0a7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dmukbli(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gd3p27d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kfcf8tk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mq7e94u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_vno937p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vqmfrgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_int_binop_qrbogd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __int128 gm_f_int_lo_i8jbm6e(uint32_t a0);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_less_nbfe87l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_rd_int_iuioodt(void * a0, gm_ttovhfee8mf28 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_unary_sorkbs7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, void * a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_unop_qamfooj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s7ah1q5ah26rj[12];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sa2pnk1jujp5p[23];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_sqp26euqk4rkd[16];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcj10g653a00lt[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = gm_l3;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_cmp_r2i4hv9 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t6 != ((uint8_t)10ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t8 != ((uint8_t)11ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)12ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)13ULL)) goto pa11;
    goto po4;
pa11:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 26, 40);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
        F.ret = t5;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_f_get_u64_tgpb2vh(t1, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l3;
        uint8_t t12 = gm_f_aligned_q6sor4h(t10, t11);
        if (!t12) goto ie9;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            uint64_t t17 = gm_l2;
            void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
            if (!(GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto ie14;
            void * t19 = GM_LD(void *, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l4;
                void * t22 = ((uint8_t *)t21);
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_f_us_cuv01fp(t24);
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t27);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t t30 = gm_l2;
        uint64_t t31 = gm_l3;
        gm_tpekhfs32op0f t32;
        gm_f_fault_u31b92l((void *)&t32, t28, t29, t30, t31, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_8jbgndp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l2;
    gm_b16_8 t2 = t1;
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8);
    if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_srcnqtpfma6qe4a, 174, 81);
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t6)) + t3 * 8));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 174, 76);
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)) + t10 * 1));
    uint8_t t13 = (!(t12 == ((uint8_t)0ULL)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l4 = t10;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        uint64_t t15 = (uint64_t)t14;
        uint8_t t16 = (t12 >= t15);
        if (!t16) goto ie11;
        {
            uint8_t t18 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((((uint8_t *)t19) + 0) + 0));
        void * t21 = t20;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        gm_l5 = t25;
    }
    {
        void * t26 = gm_l5;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28;
        if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
            void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            uint8_t t30 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t28;
    }
    {
        uint64_t t31 = gm_l1;
        void * t32 = gm_l5;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
        uint64_t r34; bool o35 = __builtin_sub_overflow(t31, t33, &r34);
        if (o35) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t36 = r34;
        gm_l7 = t36;
    }
    {
        uint64_t t37 = gm_l7;
        void * t38 = gm_l5;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 16));
        unsigned __int128 n40 = (unsigned __int128)t39;
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t41 = (uint64_t)(t37 >> (int)n40);
        gm_l8 = t41;
    }
    {
        void * t42 = gm_l6;
        void * t43 = t42;
        uint64_t t44 = gm_l8;
        uint64_t t45 = gm_f_us_cuv01fp(t44);
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = (void *)((uint8_t *)t43 + t46 * 24);
        gm_l9 = t47;
    }
    {
        uint64_t t48 = gm_l7;
        uint64_t t49 = gm_l8;
        void * t50 = gm_l5;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
        unsigned __int128 n52 = (unsigned __int128)t51;
        if (n52 >= 64) gm_trap_shift((uint64_t)n52, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r53 = (uint64_t)((uint64_t)t49 << (int)n52);
        bool o54 = (uint64_t)(r53 >> (int)n52) != t49;
        if (o54) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t55 = r53;
        uint64_t r56; bool o57 = __builtin_sub_overflow(t48, t55, &r56);
        if (o57) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t58 = r56;
        gm_l10 = t58;
    }
    {
        uint8_t t60 = 0;
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        void * t63 = gm_l9;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 17));
        uint8_t t65 = (uint8_t)!t64;
        if (!t65) {
            uint64_t t66 = gm_l2;
            void * t67 = gm_l9;
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
            uint8_t t69 = (t66 > t68);
            t62 = t69;
        } else t62 = 1;
        if (!t62) {
            uint64_t t70 = gm_l10;
            void * t71 = gm_l9;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
            uint64_t t73 = gm_l2;
            uint64_t r74; bool o75 = __builtin_sub_overflow(t72, t73, &r74);
            if (o75) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t76 = r74;
            uint8_t t77 = (t70 > t76);
            t61 = t77;
        } else t61 = 1;
        if (!t61) {
            uint8_t t78 = 0;
            uint8_t t79 = gm_l3;
            if (t79) {
                void * t80 = gm_l9;
                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 18));
                uint8_t t82 = (uint8_t)!t81;
                t78 = t82;
            } else t78 = 0;
            t60 = t78;
        } else t60 = 1;
        if (!t60) goto ie59;
        {
            uint8_t t84 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t85 = gm_l9;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        void * t87 = t86;
        uint64_t t88 = gm_l10;
        uint64_t t89 = gm_f_us_cuv01fp(t88);
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = (void *)((uint8_t *)t87 + t90 * 1);
        void * t92 = t91;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_binary_6rg6s7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_binary_6rg6s7q F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    void * gm_l3; /* l */
    void * gm_l4; /* r */
    void * gm_l5; /* e */
    gm_b16_8 gm_l6; /* dst */
    uint64_t gm_l7; /* ln */
    uint64_t gm_l8; /* rn */
    gm_t51b2en4qp02s gm_l9; /* xb */
    gm_t51b2en4qp02s gm_l10; /* yb */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    gm_b16_8 gm_l12; /* x */
    gm_b16_8 gm_l13; /* y */
    GM_STACK_CHECK(&F, 1224u);
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
        {
            void * t3 = gm_l3;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
            uint64_t t5 = gm_f_us_cuv01fp(t4);
            gm_l7 = t5;
        }
        {
            void * t6 = gm_l4;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            uint64_t t8 = gm_f_us_cuv01fp(t7);
            gm_l8 = t8;
        }
        {
            uint64_t t10 = gm_l7;
            uint8_t t11 = (t10 <= ((uint64_t)16ULL));
            if (!t11) goto ie9;
            uint64_t t12 = gm_l8;
            uint8_t t13 = (t12 <= ((uint64_t)16ULL));
            if (!t13) goto ie9;
            {
            }
            {
            }
            {
                void * t15 = gm_l0;
                gm_tbl8il92l97ut t16 = gm_l1;
                void * t17 = gm_l3;
                void * t18 = ((uint8_t *)&gm_l9);
                uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t20 = gm_l7;
                uint64_t t21 = (uintptr_t)t20;
                if (t19 > t21 || t21 > (uintptr_t)16) gm_trap_slice(t19, t21, 16, gm_srcj10g653a00lt, 91, 24);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t18 + t19 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21 - t19));
                gm_tcfa6l7reoosn t23;
                gm_f_ev_25jfdar((void *)&t23, (void *)NULL, cx, NULL, t15, t16, t17, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t23))[0] != 0)) {
                    gm_tcfa6l7reoosn t24;
                    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                    ((uint8_t *)&t24)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t24 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t23) + 8)));
                    F.ret = t24;
                    goto gm_fret;
                }
                gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
            }
            {
                void * t26 = gm_l0;
                gm_tbl8il92l97ut t27 = gm_l1;
                void * t28 = gm_l4;
                void * t29 = ((uint8_t *)&gm_l10);
                uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t31 = gm_l8;
                uint64_t t32 = (uintptr_t)t31;
                if (t30 > t32 || t32 > (uintptr_t)16) gm_trap_slice(t30, t32, 16, gm_srcj10g653a00lt, 92, 24);
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t29 + t30 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t30));
                gm_tcfa6l7reoosn t34;
                gm_f_ev_25jfdar((void *)&t34, (void *)NULL, cx, NULL, t26, t27, t28, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t34))[0] != 0)) {
                    gm_tcfa6l7reoosn t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                    ((uint8_t *)&t35)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t35 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t34) + 8)));
                    F.ret = t35;
                    goto gm_fret;
                }
                gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 8));
            }
            {
                void * t37 = gm_l0;
                void * t38 = gm_l5;
                gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 112));
                gm_ts6oqppjh0ba3 t40 = gm_l2;
                void * t41 = gm_l3;
                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 96));
                void * t43 = gm_l4;
                gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t43) + 96));
                void * t45 = gm_l5;
                gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 96));
                void * t47 = ((uint8_t *)&gm_l9);
                uint64_t t48 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t49 = gm_l7;
                uint64_t t50 = (uintptr_t)t49;
                if (t48 > t50 || t50 > (uintptr_t)16) gm_trap_slice(t48, t50, 16, gm_srcj10g653a00lt, 93, 54);
                gm_b16_8 t51;
                GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t47 + t48 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50 - t48));
                void * t52 = ((uint8_t *)&gm_l10);
                uint64_t t53 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t54 = gm_l8;
                uint64_t t55 = (uintptr_t)t54;
                if (t53 > t55 || t55 > (uintptr_t)16) gm_trap_slice(t53, t55, 16, gm_srcj10g653a00lt, 93, 65);
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t52 + t53 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t53));
                gm_b16_8 t57 = gm_l6;
                gm_f_binop_nrsq3mp((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t37, t39, t40, t42, t44, t46, t51, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix9;
ie9:;
ix9:;
        }
        {
            void * t58 = gm_l0;
            gm_t2lqu8jsj511d t59 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t58));
            gm_l11 = t59;
        }
        {
            void * t60 = gm_l0;
            void * t61 = gm_l3;
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t61) + 104));
            gm_b16_8 t63 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t60, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t63;
        }
        {
            void * t64 = gm_l0;
            gm_tbl8il92l97ut t65 = gm_l1;
            void * t66 = gm_l3;
            gm_b16_8 t67 = gm_l12;
            gm_tcfa6l7reoosn t68;
            gm_f_ev_25jfdar((void *)&t68, (void *)NULL, cx, NULL, t64, t65, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t68))[0] != 0)) {
                gm_tcfa6l7reoosn t69;
                GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                ((uint8_t *)&t69)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t69 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t68) + 8)));
                F.ret = t69;
                goto gm_fret;
            }
            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 8));
        }
        {
            void * t71 = gm_l0;
            void * t72 = gm_l4;
            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 104));
            gm_b16_8 t74 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t71, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t74;
        }
        {
            void * t75 = gm_l0;
            gm_tbl8il92l97ut t76 = gm_l1;
            void * t77 = gm_l4;
            gm_b16_8 t78 = gm_l13;
            gm_tcfa6l7reoosn t79;
            gm_f_ev_25jfdar((void *)&t79, (void *)NULL, cx, NULL, t75, t76, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t79))[0] != 0)) {
                gm_tcfa6l7reoosn t80;
                GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                ((uint8_t *)&t80)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t80 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t79) + 8)));
                F.ret = t80;
                goto gm_fret;
            }
            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 8));
        }
        {
            void * t82 = gm_l0;
            void * t83 = gm_l5;
            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 112));
            gm_ts6oqppjh0ba3 t85 = gm_l2;
            void * t86 = gm_l3;
            gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + 96));
            void * t88 = gm_l4;
            gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t88) + 96));
            void * t90 = gm_l5;
            gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t90) + 96));
            gm_b16_8 t92 = gm_l12;
            gm_b16_8 t93 = gm_l13;
            gm_b16_8 t94 = gm_l6;
            gm_tcfa6l7reoosn t95;
            gm_f_binop_nrsq3mp((void *)&t95, (void *)NULL, cx, NULL, t82, t84, t85, t87, t89, t91, t92, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t95))[0] != 0)) {
                gm_tcfa6l7reoosn t96;
                GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                ((uint8_t *)&t96)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t96 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t95) + 8)));
                F.ret = t96;
                goto gm_fret;
            }
            gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 8));
        }
        {
            void * t98 = gm_l0;
            gm_t2lqu8jsj511d t99 = gm_l11;
            gm_f_release_jn5kqdi(t98, GM_CONV(gm_csc7lc6jlkqkuq, t99));
        }
    }
    gm_b0_1 t100;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t100);
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
__attribute__((aligned(4))) void gm_f_binop_nrsq3mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8) {
    struct gm_fr_binop_nrsq3mp F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* lt */
    gm_t88rcucg1b7uk gm_l4; /* rt */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* dst */
    uint8_t gm_l9; /* v */
    double gm_l10; /* p */
    double gm_l11; /* q */
    double gm_l12; /* r */
    uint64_t gm_l13; /* n */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    uint64_t gm_l15; /* esz */
    uint8_t gm_l16; /* scalar_r */
    gm_t88rcucg1b7uk gm_l17; /* ety */
    uint8_t gm_l18; /* is_mask */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* xs */
    gm_b16_8 gm_l23; /* ys */
    gm_b16_8 gm_l24; /* ds */
    gm_t88rcucg1b7uk gm_l25; /* u */
    GM_STACK_CHECK(&F, 2363u);
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
        {
            gm_ts6oqppjh0ba3 t4 = gm_l2;
            uint8_t t5 = gm_f_is_cmp_r2i4hv9(t4);
            if (!t5) goto ie3;
            {
                void * t7 = gm_l0;
                gm_tl874t2atnlh3 t8 = gm_l1;
                gm_ts6oqppjh0ba3 t9 = gm_l2;
                gm_t88rcucg1b7uk t10 = gm_l3;
                gm_b16_8 t11 = gm_l6;
                gm_b16_8 t12 = gm_l7;
                gm_t1ec8br9ddkav t13;
                gm_f_cmp_op_mflafbf((void *)&t13, (void *)NULL, cx, NULL, t7, t8, t9, t10, t11, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t13))[0] != 0)) {
                    gm_tcfa6l7reoosn t14;
                    GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                    ((uint8_t *)&t14)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t13) + 8)));
                    F.ret = t14;
                    goto gm_fret;
                }
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t13) + 8));
                gm_l9 = t15;
            }
            {
                uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcj10g653a00lt, 36, 9);
                uint8_t *t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t16 * 1);
                uint8_t t19;
                uint8_t t21 = gm_l9;
                if (!t21) goto ie20;
                uint8_t t23 = ((uint8_t)1ULL);
                t19 = t23;
                goto ix20;
ie20:;
                uint8_t t25 = ((uint8_t)0ULL);
                t19 = t25;
ix20:;
                GM_ST(uint8_t, t18, t19);
            }
            {
                gm_b0_1 t26;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t26);
                goto gm_fdone;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t27 = gm_l0;
            gm_t88rcucg1b7uk t28 = gm_l3;
            gm_t53at80pfdus3 t29;
            gm_f_kind_htv6093((void *)&t29, t27, GM_CONV(gm_csguankko4lmhd, t28));
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            switch (t37) { case ((uint8_t)0ULL): goto ma31; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma31; case ((uint8_t)3ULL): goto ma32; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma35; case ((uint8_t)7ULL): goto ma35; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma35; case ((uint8_t)10ULL): goto ma35; case ((uint8_t)11ULL): goto ma33; case ((uint8_t)12ULL): goto ma35; case ((uint8_t)13ULL): goto ma35; case ((uint8_t)14ULL): goto ma35; case ((uint8_t)15ULL): goto ma35; case ((uint8_t)16ULL): goto ma35; case ((uint8_t)17ULL): goto ma34; case ((uint8_t)18ULL): goto ma35; case ((uint8_t)19ULL): goto ma35; case ((uint8_t)20ULL): goto ma35; case ((uint8_t)21ULL): goto ma35; case ((uint8_t)22ULL): goto ma35; case ((uint8_t)23ULL): goto ma35; case ((uint8_t)24ULL): goto ma35; case ((uint8_t)25ULL): goto ma35; case ((uint8_t)26ULL): goto ma35; default: goto ma35; }
ma31:;
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            if (t41 != ((uint8_t)0ULL)) goto pa40;
            goto po39;
pa40:;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            if (t43 != ((uint8_t)1ULL)) goto pa42;
            goto po39;
pa42:;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            if (t45 != ((uint8_t)2ULL)) goto pa44;
            goto po39;
pa44:;
            goto mn38;
po39:;
            void * t46 = gm_l0;
            gm_tl874t2atnlh3 t47 = gm_l1;
            gm_ts6oqppjh0ba3 t48 = gm_l2;
            gm_t88rcucg1b7uk t49 = gm_l3;
            gm_t88rcucg1b7uk t50 = gm_l4;
            gm_t88rcucg1b7uk t51 = gm_l5;
            gm_b16_8 t52 = gm_l6;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_b16_8 t54 = gm_l7;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
            gm_b16_8 t56 = gm_l8;
            gm_f_int_binop_qrbogd9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t46, t47, t48, t49, t50, t51, t53, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me30;
mn38:;
ma32:;
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            if (t58 != ((uint8_t)3ULL)) goto mn57;
            {
                void * t60 = gm_l0;
                gm_t88rcucg1b7uk t61 = gm_l3;
                gm_b16_8 t62 = gm_l6;
                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                double t64 = gm_f_get_float_grmhlm4(t60, GM_CONV(gm_csguankko4lmhd, t61), t63);
                gm_l10 = t64;
            }
            {
                void * t65 = gm_l0;
                gm_t88rcucg1b7uk t66 = gm_l3;
                gm_b16_8 t67 = gm_l7;
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                double t69 = gm_f_get_float_grmhlm4(t65, GM_CONV(gm_csguankko4lmhd, t66), t68);
                gm_l11 = t69;
            }
            {
                double t70;
                uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t78) { case ((uint8_t)0ULL): goto ma72; case ((uint8_t)1ULL): goto ma73; case ((uint8_t)2ULL): goto ma74; case ((uint8_t)3ULL): goto ma76; case ((uint8_t)4ULL): goto ma75; case ((uint8_t)5ULL): goto ma76; case ((uint8_t)6ULL): goto ma76; case ((uint8_t)7ULL): goto ma76; case ((uint8_t)8ULL): goto ma76; case ((uint8_t)9ULL): goto ma76; case ((uint8_t)10ULL): goto ma76; case ((uint8_t)11ULL): goto ma76; case ((uint8_t)12ULL): goto ma76; case ((uint8_t)13ULL): goto ma76; default: goto ma76; }
ma72:;
                uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t80 != ((uint8_t)0ULL)) goto mn79;
                double t81 = gm_l10;
                double t82 = gm_l11;
                double t83 = (t81 + t82);
                t70 = t83;
                goto me71;
mn79:;
ma73:;
                uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t85 != ((uint8_t)1ULL)) goto mn84;
                double t86 = gm_l10;
                double t87 = gm_l11;
                double t88 = (t86 - t87);
                t70 = t88;
                goto me71;
mn84:;
ma74:;
                uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t90 != ((uint8_t)2ULL)) goto mn89;
                double t91 = gm_l10;
                double t92 = gm_l11;
                double t93 = (t91 * t92);
                t70 = t93;
                goto me71;
mn89:;
ma75:;
                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t95 != ((uint8_t)4ULL)) goto mn94;
                double t96 = gm_l10;
                double t97 = gm_l11;
                double t98 = (t96 / t97);
                t70 = t98;
                goto me71;
mn94:;
ma76:;
                void * t100 = gm_l0;
                gm_tl874t2atnlh3 t101 = gm_l1;
                gm_ts6oqppjh0ba3 t102 = gm_l2;
                gm_b16_8 t103 = gm_f_op_sym_6olbepb(t102);
                gm_tnpifv20irnph t104;
                GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
                gm_tabb710d9mq2e t105;
                gm_f_format_aib5qn5((void *)&t105, (void *)NULL, cx, NULL, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_srcj10g653a00lt, 49, 43);
                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                gm_t76123gfet73g t108;
                gm_f_trap_qh84l9d((void *)&t108, (void *)NULL, cx, NULL, t100, t101, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t108);
                goto gm_fdone;
                goto me71;
mn99:;
ma77:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 44, 18);
me71:;
                gm_l12 = t70;
            }
            {
                void * t109 = gm_l0;
                gm_t88rcucg1b7uk t110 = gm_l3;
                double t111 = gm_l12;
                gm_b16_8 t112 = gm_l8;
                gm_f_put_float_fiad72d(t109, GM_CONV(gm_csguankko4lmhd, t110), t111, t112);
            }
            goto me30;
mn57:;
ma33:;
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            if (t114 != ((uint8_t)11ULL)) goto mn113;
            uint64_t t115 = GM_LD(uint64_t, ((((uint8_t *)&t29) + 8) + 0));
            gm_l13 = t115;
            gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t29) + 8) + 8));
            gm_l14 = t116;
            {
                void * t118 = gm_l0;
                gm_t88rcucg1b7uk t119 = gm_l14;
                uint64_t t120 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t118, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t121 = gm_f_us_cuv01fp(t120);
                gm_l15 = t121;
            }
            {
                void * t122 = gm_l0;
                gm_t88rcucg1b7uk t123 = gm_l4;
                gm_t53at80pfdus3 t124;
                gm_f_kind_htv6093((void *)&t124, t122, GM_CONV(gm_csguankko4lmhd, t123));
                uint8_t t125;
                uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t124) + 0));
                if (t128 != ((uint8_t)11ULL)) goto mn127;
                t125 = ((uint8_t)0);
                goto me126;
mn127:;
                t125 = ((uint8_t)1);
                goto me126;
mn129:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 56, 25);
me126:;
                gm_l16 = t125;
            }
            {
                gm_t88rcucg1b7uk t130;
                uint8_t t132 = gm_l16;
                if (!t132) goto ie131;
                gm_t88rcucg1b7uk t134 = gm_l4;
                gm_t88rcucg1b7uk t135 = t134;
                t130 = t135;
                goto ix131;
ie131:;
                gm_t88rcucg1b7uk t137 = gm_l14;
                gm_t88rcucg1b7uk t138 = t137;
                t130 = t138;
ix131:;
                gm_l17 = t130;
            }
            {
                void * t139 = gm_l0;
                gm_t88rcucg1b7uk t140 = gm_l14;
                gm_t53at80pfdus3 t141;
                gm_f_kind_htv6093((void *)&t141, t139, GM_CONV(gm_csguankko4lmhd, t140));
                uint8_t t142 = ((GM_LD(uint8_t, ((uint8_t *)&t141) + 0) == ((uint8_t)4ULL)));
                gm_l18 = t142;
            }
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t144 = gm_l13;
                    uint64_t t145 = gm_f_us_cuv01fp(t144);
                    gm_l19 = t145;
                }
                {
                    for (;;) {
                        uint64_t t147 = gm_l20;
                        uint64_t t148 = gm_l19;
                        uint8_t t149 = (t147 < t148);
                        if (!t149) goto lb146;
                        {
                            uint64_t t152 = gm_l20;
                            uint64_t t155 = GM_LD(uint64_t, ((uint8_t *)&t152));
                            gm_l21 = t155;
                            {
                                uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                void * t158 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                                uint64_t t159 = gm_l21;
                                uint64_t t160 = gm_l15;
                                uint64_t r161; bool o162 = __builtin_mul_overflow(t159, t160, &r161);
                                if (o162) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 60, 25);
                                uint64_t t163 = r161;
                                uint64_t t164 = (uintptr_t)t163;
                                uint64_t t165 = gm_l21;
                                uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                                if (o167) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 60, 35);
                                uint64_t t168 = r166;
                                uint64_t t169 = gm_l15;
                                uint64_t r170; bool o171 = __builtin_mul_overflow(t168, t169, &r170);
                                if (o171) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 60, 34);
                                uint64_t t172 = r170;
                                uint64_t t173 = (uintptr_t)t172;
                                if (t164 > t173 || t173 > (uintptr_t)t157) gm_trap_slice(t164, t173, t157, gm_srcj10g653a00lt, 60, 23);
                                gm_b16_8 t174;
                                GM_ST(void *, (uint8_t *)&t174, (void *)(((uint8_t *)t158 + t164 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(t173 - t164));
                                gm_l22 = t174;
                            }
                            {
                                gm_b16_8 t175;
                                uint8_t t177 = gm_l16;
                                if (!t177) goto ie176;
                                gm_b16_8 t179 = gm_l7;
                                gm_b16_8 t180 = t179;
                                t175 = t180;
                                goto ix176;
ie176:;
                                uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                void * t183 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                                uint64_t t184 = gm_l21;
                                uint64_t t185 = gm_l15;
                                uint64_t r186; bool o187 = __builtin_mul_overflow(t184, t185, &r186);
                                if (o187) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 61, 50);
                                uint64_t t188 = r186;
                                uint64_t t189 = (uintptr_t)t188;
                                uint64_t t190 = gm_l21;
                                uint64_t r191; bool o192 = __builtin_add_overflow(t190, ((uint64_t)1ULL), &r191);
                                if (o192) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 61, 60);
                                uint64_t t193 = r191;
                                uint64_t t194 = gm_l15;
                                uint64_t r195; bool o196 = __builtin_mul_overflow(t193, t194, &r195);
                                if (o196) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 61, 59);
                                uint64_t t197 = r195;
                                uint64_t t198 = (uintptr_t)t197;
                                if (t189 > t198 || t198 > (uintptr_t)t182) gm_trap_slice(t189, t198, t182, gm_srcj10g653a00lt, 61, 48);
                                gm_b16_8 t199;
                                GM_ST(void *, (uint8_t *)&t199, (void *)(((uint8_t *)t183 + t189 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(t198 - t189));
                                gm_b16_8 t200 = t199;
                                t175 = t200;
ix176:;
                                gm_l23 = t175;
                            }
                            {
                                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                void * t202 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)));
                                uint64_t t203 = gm_l21;
                                uint64_t t204 = gm_l15;
                                uint64_t r205; bool o206 = __builtin_mul_overflow(t203, t204, &r205);
                                if (o206) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 62, 27);
                                uint64_t t207 = r205;
                                uint64_t t208 = (uintptr_t)t207;
                                uint64_t t209 = gm_l21;
                                uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1ULL), &r210);
                                if (o211) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 62, 37);
                                uint64_t t212 = r210;
                                uint64_t t213 = gm_l15;
                                uint64_t r214; bool o215 = __builtin_mul_overflow(t212, t213, &r214);
                                if (o215) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 62, 36);
                                uint64_t t216 = r214;
                                uint64_t t217 = (uintptr_t)t216;
                                if (t208 > t217 || t217 > (uintptr_t)t201) gm_trap_slice(t208, t217, t201, gm_srcj10g653a00lt, 62, 23);
                                gm_b16_8 t218;
                                GM_ST(void *, (uint8_t *)&t218, (void *)(((uint8_t *)t202 + t208 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(t217 - t208));
                                gm_l24 = t218;
                            }
                            {
                                uint8_t t220 = gm_l18;
                                if (!t220) goto ie219;
                                {
                                    uint64_t t222 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t223 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                    if (t222 >= (uintptr_t)(t223)) gm_trap_index(t222, t223, gm_srcj10g653a00lt, 65, 21);
                                    uint8_t t224;
                                    uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    switch (t230) { case ((uint8_t)0ULL): goto ma228; case ((uint8_t)1ULL): goto ma228; case ((uint8_t)2ULL): goto ma228; case ((uint8_t)3ULL): goto ma228; case ((uint8_t)4ULL): goto ma228; case ((uint8_t)5ULL): goto ma228; case ((uint8_t)6ULL): goto ma228; case ((uint8_t)7ULL): goto ma226; case ((uint8_t)8ULL): goto ma227; case ((uint8_t)9ULL): goto ma228; case ((uint8_t)10ULL): goto ma228; case ((uint8_t)11ULL): goto ma228; case ((uint8_t)12ULL): goto ma228; case ((uint8_t)13ULL): goto ma228; default: goto ma228; }
ma226:;
                                    uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t232 != ((uint8_t)7ULL)) goto mn231;
                                    uint64_t t233 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    if (t233 >= (uintptr_t)(t234)) gm_trap_index(t233, t234, gm_srcj10g653a00lt, 66, 36);
                                    uint8_t t235 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t233 * 1));
                                    uint64_t t236 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    if (t236 >= (uintptr_t)(t237)) gm_trap_index(t236, t237, gm_srcj10g653a00lt, 66, 44);
                                    uint8_t t238 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t236 * 1));
                                    uint8_t t239 = (uint8_t)(t235 & t238);
                                    t224 = t239;
                                    goto me225;
mn231:;
ma227:;
                                    uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    if (t241 != ((uint8_t)8ULL)) goto mn240;
                                    uint64_t t242 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t243 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    if (t242 >= (uintptr_t)(t243)) gm_trap_index(t242, t243, gm_srcj10g653a00lt, 67, 35);
                                    uint8_t t244 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t242 * 1));
                                    uint64_t t245 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    if (t245 >= (uintptr_t)(t246)) gm_trap_index(t245, t246, gm_srcj10g653a00lt, 67, 43);
                                    uint8_t t247 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t245 * 1));
                                    uint8_t t248 = (uint8_t)(t244 | t247);
                                    t224 = t248;
                                    goto me225;
mn240:;
ma228:;
                                    uint64_t t250 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t251 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    if (t250 >= (uintptr_t)(t251)) gm_trap_index(t250, t251, gm_srcj10g653a00lt, 68, 30);
                                    uint8_t t252 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t250 * 1));
                                    uint64_t t253 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t254 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                    if (t253 >= (uintptr_t)(t254)) gm_trap_index(t253, t254, gm_srcj10g653a00lt, 68, 38);
                                    uint8_t t255 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t253 * 1));
                                    uint8_t t256 = (uint8_t)(t252 ^ t255);
                                    t224 = t256;
                                    goto me225;
mn249:;
ma229:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 65, 29);
me225:;
                                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t222 * 1), t224);
                                }
                                goto ix219;
ie219:;
                                {
                                    void * t258 = gm_l0;
                                    gm_tl874t2atnlh3 t259 = gm_l1;
                                    gm_ts6oqppjh0ba3 t260 = gm_l2;
                                    gm_t88rcucg1b7uk t261 = gm_l14;
                                    gm_t88rcucg1b7uk t262 = gm_l17;
                                    gm_t88rcucg1b7uk t263 = gm_l14;
                                    gm_b16_8 t264 = gm_l22;
                                    gm_b16_8 t265 = gm_l23;
                                    gm_b16_8 t266 = gm_l24;
                                    gm_tcfa6l7reoosn t267;
                                    gm_f_binop_nrsq3mp((void *)&t267, (void *)NULL, cx, NULL, t258, t259, t260, t261, t262, t263, t264, t265, t266);
                                    if (gm_unw.kind) {
                                        goto gm_ll150;
                                    }
                                    if (((((uint8_t *)&t267))[0] != 0)) {
                                        gm_tcfa6l7reoosn t268;
                                        GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
                                        ((uint8_t *)&t268)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t268 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t267) + 8)));
                                        F.ret = t268;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t269 = GM_LD(gm_b0_1, (((uint8_t *)&t267) + 8));
                                }
ix219:;
                            }
                            goto me153;
mn154:;
                            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 59, 13);
me153:;
                        }
lc146:;
                        {
                            uint64_t t271 = gm_l20;
                            uint64_t r272; bool o273 = __builtin_add_overflow(t271, ((uint64_t)1ULL), &r272);
                            uint64_t t274 = r272;
                            gm_l20 = t274;
                        }
                    }
gm_ll150:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb146; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc146; }
                    }
                    goto gm_lad;
lb146:;
                }
            }
            goto me30;
mn113:;
ma34:;
            uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            if (t276 != ((uint8_t)17ULL)) goto mn275;
            {
                void * t278 = gm_l0;
                gm_t88rcucg1b7uk t279 = gm_l3;
                gm_tkn42u7na3t81 t280 = gm_f_bits_struct_ty_22ue20s(t278, GM_CONV(gm_csguankko4lmhd, t279));
                gm_t88rcucg1b7uk t281;
                if (((((uint8_t *)&t280))[4] != 0)) {
                    gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t280) + 0));
                    t281 = t282;
                } else {
                    void * t283 = gm_l0;
                    gm_tl874t2atnlh3 t284 = gm_l1;
                    gm_t6j6smj7qoctu t285;
                    gm_ts6oqppjh0ba3 t286 = gm_l2;
                    gm_b16_8 t287 = gm_f_op_sym_6olbepb(t286);
                    GM_ST(gm_b16_8, (uint8_t *)&t285 + 0, t287);
                    void * t288 = gm_l0;
                    void * t289 = GM_LD(void *, (((uint8_t *)t288) + 8));
                    gm_t88rcucg1b7uk t290 = gm_l3;
                    gm_b16_8 t291 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t289, t290);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t285 + 16, t291);
                    gm_tabb710d9mq2e t292;
                    gm_f_format_bvot0a7((void *)&t292, (void *)NULL, cx, NULL, &t285);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 8, gm_srcj10g653a00lt, 76, 65);
                    gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&t292) + 8));
                    gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
                    gm_t76123gfet73g t295;
                    gm_f_trap_qh84l9d((void *)&t295, (void *)NULL, cx, NULL, t283, t284, t294);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t295);
                    goto gm_fdone;
                }
                gm_l25 = t281;
            }
            {
                void * t296 = gm_l0;
                gm_tl874t2atnlh3 t297 = gm_l1;
                gm_ts6oqppjh0ba3 t298 = gm_l2;
                gm_t88rcucg1b7uk t299 = gm_l25;
                gm_t88rcucg1b7uk t300 = gm_l25;
                gm_t88rcucg1b7uk t301 = gm_l25;
                gm_b16_8 t302 = gm_l6;
                gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                gm_b16_8 t304 = gm_l7;
                gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                gm_b16_8 t306 = gm_l8;
                gm_f_int_binop_qrbogd9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t296, t297, t298, t299, t300, t301, t303, t305, t306);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me30;
mn275:;
ma35:;
            void * t308 = gm_l0;
            gm_tl874t2atnlh3 t309 = gm_l1;
            gm_t6j6smj7qoctu t310;
            gm_ts6oqppjh0ba3 t311 = gm_l2;
            gm_b16_8 t312 = gm_f_op_sym_6olbepb(t311);
            GM_ST(gm_b16_8, (uint8_t *)&t310 + 0, t312);
            void * t313 = gm_l0;
            void * t314 = GM_LD(void *, (((uint8_t *)t313) + 8));
            gm_t88rcucg1b7uk t315 = gm_l3;
            gm_b16_8 t316 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t314, t315);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t310 + 16, t316);
            gm_tabb710d9mq2e t317;
            gm_f_format_bvot0a7((void *)&t317, (void *)NULL, cx, NULL, &t310);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t317))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t317) + 8, gm_srcj10g653a00lt, 79, 35);
            gm_b16_8 t318 = GM_LD(gm_b16_8, (((uint8_t *)&t317) + 8));
            gm_b16_8 t319 = GM_LD(gm_b16_8, ((uint8_t *)&t318));
            gm_t76123gfet73g t320;
            gm_f_trap_qh84l9d((void *)&t320, (void *)NULL, cx, NULL, t308, t309, t319);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t320);
            goto gm_fdone;
            goto me30;
mn307:;
ma36:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 39, 5);
me30:;
        }
    }
    gm_b0_1 t321;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t321);
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
inline __attribute__((aligned(4))) uint8_t gm_f_bytes_equal_cksjlgj(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bytes_equal_cksjlgj F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = (!(t3 == t4));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l2 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l3;
                uint64_t t12 = gm_l2;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l3;
                    uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t16));
                    gm_l4 = t19;
                    {
                        uint64_t t22 = gm_l4;
                        uint64_t t23 = (uintptr_t)t22;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcj10g653a00lt, 383, 12);
                        uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t23 * 1));
                        uint64_t t26 = gm_l4;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcj10g653a00lt, 383, 24);
                        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 1));
                        uint8_t t30 = (!(t25 == t29));
                        if (!t30) goto ie21;
                        {
                            uint8_t t32 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 382, 5);
me17:;
                }
lc10:;
                {
                    uint64_t t34 = gm_l3;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l3 = t37;
                }
            }
lb10:;
        }
    }
    {
        uint8_t t38 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_cmp_op_mflafbf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_cmp_op_mflafbf F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* y */
    gm_ttovhfee8mf28 gm_l6; /* info */
    unsigned __int128 gm_l7; /* a */
    unsigned __int128 gm_l8; /* c */
    __int128 gm_l9; /* p */
    __int128 gm_l10; /* q */
    GM_STACK_CHECK(&F, 1419u);
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
        gm_t88rcucg1b7uk t4 = gm_l3;
        gm_tg5lql6itufk7 t5 = gm_f_int_info_ulcevdv(t3, GM_CONV(gm_csguankko4lmhd, t4));
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        gm_ttovhfee8mf28 t6 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t5));
        gm_l6 = t6;
        {
            void * t8 = gm_l0;
            gm_ttovhfee8mf28 t9 = gm_l6;
            gm_b16_8 t10 = gm_l4;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
            unsigned __int128 t12 = gm_f_rd_int_iuioodt(t8, t9, t11);
            gm_l7 = t12;
        }
        {
            void * t13 = gm_l0;
            gm_ttovhfee8mf28 t14 = gm_l6;
            gm_b16_8 t15 = gm_l5;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            unsigned __int128 t17 = gm_f_rd_int_iuioodt(t13, t14, t16);
            gm_l8 = t17;
        }
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            if (!t19) goto ie18;
            {
                unsigned __int128 t21 = gm_l7;
                __int128 t22;
                memset(&t22, 0, sizeof t22); memcpy(&t22, ((uint8_t *)&t21), sizeof t22 < 16 ? sizeof t22 : 16);
                gm_l9 = t22;
            }
            {
                unsigned __int128 t23 = gm_l8;
                __int128 t24;
                memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 16 ? sizeof t24 : 16);
                gm_l10 = t24;
            }
            {
                gm_t1ec8br9ddkav t25;
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma30; case ((uint8_t)9ULL): goto ma30; case ((uint8_t)10ULL): goto ma27; case ((uint8_t)11ULL): goto ma28; case ((uint8_t)12ULL): goto ma29; case ((uint8_t)13ULL): goto ma30; default: goto ma30; }
ma27:;
                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t34 != ((uint8_t)10ULL)) goto mn33;
                __int128 t35 = gm_l9;
                __int128 t36 = gm_l10;
                uint8_t t37 = (t35 < t36);
                gm_t1ec8br9ddkav t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t38 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t38 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t38 + 24, 0);
                ((uint8_t *)&t38)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t38 + 8), t37);
                t25 = t38;
                goto me26;
mn33:;
ma28:;
                uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t40 != ((uint8_t)11ULL)) goto mn39;
                __int128 t41 = gm_l9;
                __int128 t42 = gm_l10;
                uint8_t t43 = (t41 <= t42);
                gm_t1ec8br9ddkav t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t44 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t44 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t44 + 24, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t44 + 8), t43);
                t25 = t44;
                goto me26;
mn39:;
ma29:;
                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t46 != ((uint8_t)12ULL)) goto mn45;
                __int128 t47 = gm_l9;
                __int128 t48 = gm_l10;
                uint8_t t49 = (t47 > t48);
                gm_t1ec8br9ddkav t50;
                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t50 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t50 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
                ((uint8_t *)&t50)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t50 + 8), t49);
                t25 = t50;
                goto me26;
mn45:;
ma30:;
                __int128 t52 = gm_l9;
                __int128 t53 = gm_l10;
                uint8_t t54 = (t52 >= t53);
                gm_t1ec8br9ddkav t55;
                GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t55 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t55 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t55 + 24, 0);
                ((uint8_t *)&t55)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t55 + 8), t54);
                t25 = t55;
                goto me26;
mn51:;
ma31:;
                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 184, 20);
me26:;
                uint8_t t56 = ((((uint8_t *)&t25))[0] != 0);
                F.ret = t25;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            gm_t1ec8br9ddkav t57;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t64) { case ((uint8_t)0ULL): goto ma62; case ((uint8_t)1ULL): goto ma62; case ((uint8_t)2ULL): goto ma62; case ((uint8_t)3ULL): goto ma62; case ((uint8_t)4ULL): goto ma62; case ((uint8_t)5ULL): goto ma62; case ((uint8_t)6ULL): goto ma62; case ((uint8_t)7ULL): goto ma62; case ((uint8_t)8ULL): goto ma62; case ((uint8_t)9ULL): goto ma62; case ((uint8_t)10ULL): goto ma59; case ((uint8_t)11ULL): goto ma60; case ((uint8_t)12ULL): goto ma61; case ((uint8_t)13ULL): goto ma62; default: goto ma62; }
ma59:;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t66 != ((uint8_t)10ULL)) goto mn65;
            unsigned __int128 t67 = gm_l7;
            unsigned __int128 t68 = gm_l8;
            uint8_t t69 = (t67 < t68);
            gm_t1ec8br9ddkav t70;
            GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t70 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t70 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t70 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t70 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
            ((uint8_t *)&t70)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t70 + 8), t69);
            t57 = t70;
            goto me58;
mn65:;
ma60:;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t72 != ((uint8_t)11ULL)) goto mn71;
            unsigned __int128 t73 = gm_l7;
            unsigned __int128 t74 = gm_l8;
            uint8_t t75 = (t73 <= t74);
            gm_t1ec8br9ddkav t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t76 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t76 + 24, 0);
            ((uint8_t *)&t76)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t76 + 8), t75);
            t57 = t76;
            goto me58;
mn71:;
ma61:;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t78 != ((uint8_t)12ULL)) goto mn77;
            unsigned __int128 t79 = gm_l7;
            unsigned __int128 t80 = gm_l8;
            uint8_t t81 = (t79 > t80);
            gm_t1ec8br9ddkav t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t82 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 24, 0);
            ((uint8_t *)&t82)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t82 + 8), t81);
            t57 = t82;
            goto me58;
mn77:;
ma62:;
            unsigned __int128 t84 = gm_l7;
            unsigned __int128 t85 = gm_l8;
            uint8_t t86 = (t84 >= t85);
            gm_t1ec8br9ddkav t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t87 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 24, 0);
            ((uint8_t *)&t87)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t87 + 8), t86);
            t57 = t87;
            goto me58;
mn83:;
ma63:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 191, 16);
me58:;
            uint8_t t88 = ((((uint8_t *)&t57))[0] != 0);
            F.ret = t57;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t95) { case ((uint8_t)0ULL): goto ma93; case ((uint8_t)1ULL): goto ma93; case ((uint8_t)2ULL): goto ma93; case ((uint8_t)3ULL): goto ma93; case ((uint8_t)4ULL): goto ma93; case ((uint8_t)5ULL): goto ma93; case ((uint8_t)6ULL): goto ma93; case ((uint8_t)7ULL): goto ma93; case ((uint8_t)8ULL): goto ma93; case ((uint8_t)9ULL): goto ma93; case ((uint8_t)10ULL): goto ma90; case ((uint8_t)11ULL): goto ma92; case ((uint8_t)12ULL): goto ma91; case ((uint8_t)13ULL): goto ma93; default: goto ma93; }
ma90:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t97 != ((uint8_t)10ULL)) goto mn96;
        void * t98 = gm_l0;
        gm_tl874t2atnlh3 t99 = gm_l1;
        gm_t88rcucg1b7uk t100 = gm_l3;
        gm_b16_8 t101 = gm_l4;
        gm_b16_8 t102 = gm_l5;
        gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t98, t99, t100, t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me89;
mn96:;
ma91:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t104 != ((uint8_t)12ULL)) goto mn103;
        void * t105 = gm_l0;
        gm_tl874t2atnlh3 t106 = gm_l1;
        gm_t88rcucg1b7uk t107 = gm_l3;
        gm_b16_8 t108 = gm_l5;
        gm_b16_8 t109 = gm_l4;
        gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t105, t106, t107, t108, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me89;
mn103:;
ma92:;
        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t111 != ((uint8_t)11ULL)) goto mn110;
        {
            void * t114 = gm_l0;
            gm_tl874t2atnlh3 t115 = gm_l1;
            gm_t88rcucg1b7uk t116 = gm_l3;
            gm_b16_8 t117 = gm_l4;
            gm_b16_8 t118 = gm_l5;
            gm_t1ec8br9ddkav t119;
            gm_f_less_nbfe87l((void *)&t119, (void *)NULL, cx, NULL, t114, t115, t116, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t119))[0] != 0)) {
                gm_t1ec8br9ddkav t120;
                GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                ((uint8_t *)&t120)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t120 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t119) + 8)));
                F.ret = t120;
                goto gm_fret;
            }
            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t119) + 8));
            if (!t121) goto ie113;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                goto gm_fdone;
            }
            goto ix113;
ie113:;
ix113:;
        }
        {
            void * t123 = gm_l0;
            gm_tl874t2atnlh3 t124 = gm_l1;
            gm_t88rcucg1b7uk t125 = gm_l3;
            gm_b16_8 t126 = gm_l4;
            gm_b16_8 t127 = gm_l5;
            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t123, t124, t125, t126, t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me89;
mn110:;
ma93:;
        {
            void * t131 = gm_l0;
            gm_tl874t2atnlh3 t132 = gm_l1;
            gm_t88rcucg1b7uk t133 = gm_l3;
            gm_b16_8 t134 = gm_l5;
            gm_b16_8 t135 = gm_l4;
            gm_t1ec8br9ddkav t136;
            gm_f_less_nbfe87l((void *)&t136, (void *)NULL, cx, NULL, t131, t132, t133, t134, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t136))[0] != 0)) {
                gm_t1ec8br9ddkav t137;
                GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                ((uint8_t *)&t137)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t137 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t136) + 8)));
                F.ret = t137;
                goto gm_fret;
            }
            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t136) + 8));
            if (!t138) goto ie130;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                goto gm_fdone;
            }
            goto ix130;
ie130:;
ix130:;
        }
        {
            void * t140 = gm_l0;
            gm_tl874t2atnlh3 t141 = gm_l1;
            gm_t88rcucg1b7uk t142 = gm_l3;
            gm_b16_8 t143 = gm_l5;
            gm_b16_8 t144 = gm_l4;
            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t140, t141, t142, t143, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me89;
mn128:;
ma94:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 198, 5);
me89:;
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
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_contains_nugmipn F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* _elem_ty */
    gm_b16_8 gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* cty */
    gm_b16_8 gm_l5; /* c */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* elems */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* a */
    gm_b16_8 gm_l15; /* other */
    uint8_t gm_l16; /* incl */
    gm_tfbnciphf3261 gm_l17; /* lf */
    gm_tfbnciphf3261 gm_l18; /* hf */
    gm_b16_8 gm_l19; /* lo */
    gm_b16_8 gm_l20; /* hi */
    uint8_t gm_l21; /* ge_lo */
    uint8_t gm_l22; /* below */
    GM_STACK_CHECK(&F, 2054u);
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
        gm_t88rcucg1b7uk t3 = gm_l4;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma6:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t15 != ((uint8_t)11ULL)) goto pa14;
        goto po13;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t17 != ((uint8_t)10ULL)) goto pa16;
        goto po13;
pa16:;
        goto mn12;
po13:;
        {
            void * t19 = gm_l0;
            gm_tl874t2atnlh3 t20 = gm_l1;
            gm_t88rcucg1b7uk t21 = gm_l4;
            gm_b16_8 t22 = gm_l5;
            gm_tmc91gv7n9c0i t23;
            gm_f_seq_of_ifahtjs((void *)&t23, (void *)NULL, cx, NULL, t19, t20, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t23))[0] != 0)) {
                gm_t1ec8br9ddkav t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                ((uint8_t *)&t24)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t24 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t23) + 8)));
                F.ret = t24;
                goto gm_fret;
            }
            F.l6 = (*(gm_t2jeg3bg7sfqa *)(((uint8_t *)&t23) + 8));
        }
        {
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
            uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
            if (!t27) goto ie25;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                if (t31 == 0) gm_trap("division by zero", gm_srcj10g653a00lt, 399, 25);
                uint64_t t32 = (uint64_t)(t30 / t31);
                gm_l7 = t32;
            }
            {
                for (;;) {
                    uint64_t t34 = gm_l8;
                    uint64_t t35 = gm_l7;
                    uint8_t t36 = (t34 < t35);
                    if (!t36) goto lb33;
                    {
                        uint64_t t39 = gm_l8;
                        uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&t39));
                        gm_l9 = t42;
                        {
                            void * t45 = gm_l0;
                            gm_tl874t2atnlh3 t46 = gm_l1;
                            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 0));
                            uint64_t t48 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 24) + 8);
                            void * t49 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 24)));
                            uint64_t t50 = gm_l9;
                            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                            uint64_t r52; bool o53 = __builtin_mul_overflow(t50, t51, &r52);
                            if (o53) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 400, 53);
                            uint64_t t54 = r52;
                            uint64_t t55 = (uintptr_t)t54;
                            uint64_t t56 = gm_l9;
                            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                            if (o58) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 400, 65);
                            uint64_t t59 = r57;
                            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                            uint64_t r61; bool o62 = __builtin_mul_overflow(t59, t60, &r61);
                            if (o62) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 400, 64);
                            uint64_t t63 = r61;
                            uint64_t t64 = (uintptr_t)t63;
                            if (t55 > t64 || t64 > (uintptr_t)t48) gm_trap_slice(t55, t64, t48, gm_srcj10g653a00lt, 400, 46);
                            gm_b16_8 t65;
                            GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t49 + t55 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - t55));
                            gm_b16_8 t66 = gm_l3;
                            gm_t1ec8br9ddkav t67;
                            gm_f_equal_e5vf82h((void *)&t67, (void *)NULL, cx, NULL, t45, t46, t47, t65, t66);
                            if (gm_unw.kind) {
                                goto gm_ll37;
                            }
                            if (((((uint8_t *)&t67))[0] != 0)) {
                                gm_t1ec8br9ddkav t68;
                                GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                                ((uint8_t *)&t68)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t68 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t67) + 8)));
                                F.ret = t68;
                                goto gm_fret;
                            }
                            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t67) + 8));
                            if (!t69) goto ie44;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                                goto gm_fdone;
                            }
                            goto ix44;
ie44:;
ix44:;
                        }
                        goto me40;
mn41:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 399, 13);
me40:;
                    }
lc33:;
                    {
                        uint64_t t72 = gm_l8;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                        uint64_t t75 = r73;
                        gm_l8 = t75;
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
            goto gm_fdone;
        }
        goto me5;
mn12:;
ma7:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t77 != ((uint8_t)12ULL)) goto mn76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((((uint8_t *)&t4) + 8) + 0));
        gm_l10 = t78;
        {
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                gm_l11 = t81;
            }
            {
                for (;;) {
                    uint64_t t83 = gm_l12;
                    uint64_t t84 = gm_l11;
                    uint8_t t85 = (t83 < t84);
                    if (!t85) goto lb82;
                    {
                        uint64_t t88 = gm_l12;
                        uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&t88));
                        gm_l13 = t91;
                        {
                            void * t94 = gm_l0;
                            gm_tl874t2atnlh3 t95 = gm_l1;
                            uint64_t t96 = gm_l13;
                            uint64_t t97 = (uintptr_t)t96;
                            uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srcj10g653a00lt, 406, 38);
                            gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t97 * 4));
                            void * t100 = gm_l0;
                            gm_t88rcucg1b7uk t101 = gm_l4;
                            gm_b16_8 t102 = gm_l5;
                            uint64_t t103 = gm_l13;
                            uint32_t t104 = (uint32_t)t103;
                            gm_b16_8 t105 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t100, t101, t102, t104);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            gm_b16_8 t106 = gm_l3;
                            gm_t1ec8br9ddkav t107;
                            gm_f_equal_e5vf82h((void *)&t107, (void *)NULL, cx, NULL, t94, t95, t99, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            if (((((uint8_t *)&t107))[0] != 0)) {
                                gm_t1ec8br9ddkav t108;
                                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                                ((uint8_t *)&t108)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t108 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t107) + 8)));
                                F.ret = t108;
                                goto gm_fret;
                            }
                            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&t107) + 8));
                            if (!t109) goto ie93;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                                goto gm_fdone;
                            }
                            goto ix93;
ie93:;
ix93:;
                        }
                        goto me89;
mn90:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 405, 13);
me89:;
                    }
lc82:;
                    {
                        uint64_t t112 = gm_l12;
                        uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                        uint64_t t115 = r113;
                        gm_l12 = t115;
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
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
            goto gm_fdone;
        }
        goto me5;
mn76:;
ma8:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t117 != ((uint8_t)17ULL)) goto mn116;
        {
            void * t119 = gm_l0;
            gm_t88rcucg1b7uk t120 = gm_l4;
            void * t121 = gm_f_adt_of_8k5vqud(t119, GM_CONV(gm_csguankko4lmhd, t120));
            void * t122;
            if ((GM_LD(void *, ((uint8_t *)&t121)) != 0)) {
                void * t123 = GM_LD(void *, (((uint8_t *)&t121) + 0));
                t122 = t123;
            } else {
                void * t124 = gm_l0;
                gm_tl874t2atnlh3 t125 = gm_l1;
                gm_b16_8 t126;
                GM_ST(void *, (uint8_t *)&t126, (void *)(gm_sa2pnk1jujp5p));
                GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(22));
                gm_t76123gfet73g t127;
                gm_f_trap_qh84l9d((void *)&t127, (void *)NULL, cx, NULL, t124, t125, t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t127);
                goto gm_fdone;
            }
            gm_l14 = t122;
        }
        {
            void * t128 = gm_l14;
            uint8_t t129;
            if (!((GM_LD(uintptr_t, (((uint8_t *)t128) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)t128) + 0)), gm_seue7mq07v0oc, 5) == 0)) goto mn131;
            t129 = ((uint8_t)0);
            goto me130;
mn131:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)t128) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)t128) + 0)), gm_s43cq4krigs2b, 9) == 0)) goto mn132;
            t129 = ((uint8_t)1);
            goto me130;
mn132:;
            gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)t128) + 0));
            gm_l15 = t134;
            void * t135 = gm_l0;
            gm_tl874t2atnlh3 t136 = gm_l1;
            gm_b16_8 t137 = gm_l15;
            gm_tnpifv20irnph t138;
            GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
            gm_tabb710d9mq2e t139;
            gm_f_format_kfcf8tk((void *)&t139, (void *)NULL, cx, NULL, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_srcj10g653a00lt, 415, 47);
            gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
            gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
            gm_t76123gfet73g t142;
            gm_f_trap_qh84l9d((void *)&t142, (void *)NULL, cx, NULL, t135, t136, t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t142);
            goto gm_fdone;
            goto me130;
mn133:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 412, 21);
me130:;
            gm_l16 = t129;
        }
        {
            void * t143 = gm_l0;
            gm_t88rcucg1b7uk t144 = gm_l4;
            gm_tfbnciphf3261 t145 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t143, t144, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t145;
        }
        {
            void * t146 = gm_l0;
            gm_t88rcucg1b7uk t147 = gm_l4;
            gm_tfbnciphf3261 t148 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t146, t147, ((uint32_t)1ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t148;
        }
        {
            gm_b16_8 t149 = gm_l5;
            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&t149) + 8);
            void * t151 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t149)));
            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint64_t t153 = gm_f_us_cuv01fp(t152);
            uint64_t t154 = (uintptr_t)t153;
            uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint64_t t156 = gm_f_us_cuv01fp(t155);
            void * t157 = gm_l0;
            gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            uint64_t t159 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t157, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t160 = gm_f_us_cuv01fp(t159);
            uint64_t r161; bool o162 = __builtin_add_overflow(t156, t160, &r161);
            if (o162) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 419, 33);
            uint64_t t163 = r161;
            uint64_t t164 = (uintptr_t)t163;
            if (t154 > t164 || t164 > (uintptr_t)t150) gm_trap_slice(t154, t164, t150, gm_srcj10g653a00lt, 419, 19);
            gm_b16_8 t165;
            GM_ST(void *, (uint8_t *)&t165, (void *)(((uint8_t *)t151 + t154 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(t164 - t154));
            gm_l19 = t165;
        }
        {
            gm_b16_8 t166 = gm_l5;
            uint64_t t167 = GM_LD(uintptr_t, ((uint8_t *)&t166) + 8);
            void * t168 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t166)));
            uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint64_t t170 = gm_f_us_cuv01fp(t169);
            uint64_t t171 = (uintptr_t)t170;
            uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint64_t t173 = gm_f_us_cuv01fp(t172);
            void * t174 = gm_l0;
            gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 0));
            uint64_t t176 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t174, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t177 = gm_f_us_cuv01fp(t176);
            uint64_t r178; bool o179 = __builtin_add_overflow(t173, t177, &r178);
            if (o179) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 420, 33);
            uint64_t t180 = r178;
            uint64_t t181 = (uintptr_t)t180;
            if (t171 > t181 || t181 > (uintptr_t)t167) gm_trap_slice(t171, t181, t167, gm_srcj10g653a00lt, 420, 19);
            gm_b16_8 t182;
            GM_ST(void *, (uint8_t *)&t182, (void *)(((uint8_t *)t168 + t171 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(t181 - t171));
            gm_l20 = t182;
        }
        {
            void * t183 = gm_l0;
            gm_tl874t2atnlh3 t184 = gm_l1;
            gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_b16_8 t186 = gm_l3;
            gm_b16_8 t187 = gm_l19;
            gm_t1ec8br9ddkav t188;
            gm_f_less_nbfe87l((void *)&t188, (void *)NULL, cx, NULL, t183, t184, t185, t186, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t188))[0] != 0)) {
                gm_t1ec8br9ddkav t189;
                GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                ((uint8_t *)&t189)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t189 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t188) + 8)));
                F.ret = t189;
                goto gm_fret;
            }
            uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&t188) + 8));
            uint8_t t191 = (uint8_t)!t190;
            gm_l21 = t191;
        }
        {
            uint8_t t192;
            uint8_t t194 = gm_l16;
            if (!t194) goto ie193;
            void * t196 = gm_l0;
            gm_tl874t2atnlh3 t197 = gm_l1;
            gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_b16_8 t199 = gm_l20;
            gm_b16_8 t200 = gm_l3;
            gm_t1ec8br9ddkav t201;
            gm_f_less_nbfe87l((void *)&t201, (void *)NULL, cx, NULL, t196, t197, t198, t199, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t201))[0] != 0)) {
                gm_t1ec8br9ddkav t202;
                GM_ST(uint8_t, (uint8_t *)&t202 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t202 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
                ((uint8_t *)&t202)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t202 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t201) + 8)));
                F.ret = t202;
                goto gm_fret;
            }
            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&t201) + 8));
            uint8_t t204 = (uint8_t)!t203;
            uint8_t t205 = t204;
            t192 = t205;
            goto ix193;
ie193:;
            void * t207 = gm_l0;
            gm_tl874t2atnlh3 t208 = gm_l1;
            gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_b16_8 t210 = gm_l3;
            gm_b16_8 t211 = gm_l20;
            gm_t1ec8br9ddkav t212;
            gm_f_less_nbfe87l((void *)&t212, (void *)NULL, cx, NULL, t207, t208, t209, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t212))[0] != 0)) {
                gm_t1ec8br9ddkav t213;
                GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t213 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t213 + 4, 0);
                ((uint8_t *)&t213)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t213 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t212) + 8)));
                F.ret = t213;
                goto gm_fret;
            }
            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&t212) + 8));
            uint8_t t215 = t214;
            t192 = t215;
ix193:;
            gm_l22 = t192;
        }
        {
            uint8_t t216 = 0;
            uint8_t t217 = gm_l21;
            if (t217) {
                uint8_t t218 = gm_l22;
                t216 = t218;
            } else t216 = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t216);
            goto gm_fdone;
        }
        goto me5;
mn116:;
ma9:;
        void * t220 = gm_l0;
        gm_tl874t2atnlh3 t221 = gm_l1;
        void * t222 = gm_l0;
        void * t223 = GM_LD(void *, (((uint8_t *)t222) + 8));
        gm_t88rcucg1b7uk t224 = gm_l4;
        gm_b16_8 t225 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t223, t224);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t226;
        GM_ST(gm_b16_8, ((uint8_t *)&t226 + 0), t225);
        gm_tabb710d9mq2e t227;
        gm_f_format_kfcf8tk((void *)&t227, (void *)NULL, cx, NULL, t226);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t227))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t227) + 8, gm_srcj10g653a00lt, 425, 35);
        gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&t227) + 8));
        gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
        gm_t76123gfet73g t230;
        gm_f_trap_qh84l9d((void *)&t230, (void *)NULL, cx, NULL, t220, t221, t229);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t230);
        goto gm_fdone;
        goto me5;
mn219:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 395, 5);
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
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_equal_e5vf82h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* x */
    gm_b16_8 gm_l4; /* y */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* elems */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    uint64_t gm_l12; /* esz */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    gm_t2lqu8jsj511d gm_l17; /* pa */
    gm_t2lqu8jsj511d gm_l18; /* pb */
    uint8_t gm_l19; /* is_float */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_t88rcucg1b7uk gm_l25; /* inner */
    uint8_t gm_l26; /* pa */
    uint8_t gm_l27; /* pb */
    gm_t88rcucg1b7uk gm_l28; /* err */
    gm_t88rcucg1b7uk gm_l29; /* ok */
    uint8_t gm_l30; /* ea */
    gm_t88rcucg1b7uk gm_l31; /* u */
    gm_tcmkompbl09il gm_l32; /* u */
    uint32_t gm_l33; /* va */
    uint32_t gm_l34; /* vb */
    gm_t88rcucg1b7uk gm_l35; /* pt */
    uint64_t gm_l36; /* off */
    uint64_t gm_l37; /* n */
    void * gm_l38; /* a */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    gm_tfbnciphf3261 gm_l43; /* fl */
    uint64_t gm_l44; /* off */
    uint64_t gm_l45; /* n */
    GM_STACK_CHECK(&F, 4766u);
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
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma17; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma17; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma17; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma17; case ((uint8_t)24ULL): goto ma17; case ((uint8_t)25ULL): goto ma17; case ((uint8_t)26ULL): goto ma17; default: goto ma17; }
ma6:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t23 != ((uint8_t)0ULL)) goto pa22;
        goto po21;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t25 != ((uint8_t)1ULL)) goto pa24;
        goto po21;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t27 != ((uint8_t)2ULL)) goto pa26;
        goto po21;
pa26:;
        goto mn20;
po21:;
        {
            void * t29 = gm_l0;
            gm_t88rcucg1b7uk t30 = gm_l2;
            gm_tg5lql6itufk7 t31 = gm_f_int_info_ulcevdv(t29, GM_CONV(gm_csguankko4lmhd, t30));
            gm_ttovhfee8mf28 t32;
            if (((((uint8_t *)&t31))[8] != 0)) {
                gm_ttovhfee8mf28 t33 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t31) + 0));
                t32 = t33;
            } else {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            gm_l5 = t32;
        }
        {
            void * t34 = gm_l0;
            gm_ttovhfee8mf28 t35 = gm_l5;
            gm_b16_8 t36 = gm_l3;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            unsigned __int128 t38 = gm_f_rd_int_iuioodt(t34, t35, t37);
            void * t39 = gm_l0;
            gm_ttovhfee8mf28 t40 = gm_l5;
            gm_b16_8 t41 = gm_l4;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            unsigned __int128 t43 = gm_f_rd_int_iuioodt(t39, t40, t42);
            uint8_t t44 = ((t38 == t43));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t44);
            goto gm_fdone;
        }
        goto me5;
mn20:;
ma7:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t46 != ((uint8_t)3ULL)) goto mn45;
        void * t47 = gm_l0;
        gm_t88rcucg1b7uk t48 = gm_l2;
        gm_b16_8 t49 = gm_l3;
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        double t51 = gm_f_get_float_grmhlm4(t47, GM_CONV(gm_csguankko4lmhd, t48), t50);
        void * t52 = gm_l0;
        gm_t88rcucg1b7uk t53 = gm_l2;
        gm_b16_8 t54 = gm_l4;
        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
        double t56 = gm_f_get_float_grmhlm4(t52, GM_CONV(gm_csguankko4lmhd, t53), t55);
        uint8_t t57 = ((t51 == t56));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t57);
        goto gm_fdone;
        goto me5;
mn45:;
ma8:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t59 != ((uint8_t)4ULL)) goto mn58;
        uint64_t t60 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_srcj10g653a00lt, 295, 26);
        uint8_t t62 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t60 * 1));
        uint8_t t63 = (!(t62 == ((uint8_t)0ULL)));
        uint64_t t64 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcj10g653a00lt, 295, 45);
        uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t64 * 1));
        uint8_t t67 = (!(t66 == ((uint8_t)0ULL)));
        uint8_t t68 = ((t63 == t67));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t68);
        goto gm_fdone;
        goto me5;
mn58:;
ma9:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t72 != ((uint8_t)8ULL)) goto pa71;
        goto po70;
pa71:;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t74 != ((uint8_t)9ULL)) goto pa73;
        goto po70;
pa73:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t76 != ((uint8_t)7ULL)) goto pa75;
        goto po70;
pa75:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t78 != ((uint8_t)16ULL)) goto pa77;
        goto po70;
pa77:;
        goto mn69;
po70:;
        void * t79 = gm_l0;
        gm_b16_8 t80 = gm_l3;
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        uint64_t t82 = gm_f_ptr_of_b77b4c9(t79, t81);
        void * t83 = gm_l0;
        gm_b16_8 t84 = gm_l4;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        uint64_t t86 = gm_f_ptr_of_b77b4c9(t83, t85);
        uint8_t t87 = ((t82 == t86));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t87);
        goto gm_fdone;
        goto me5;
mn69:;
ma10:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t89 != ((uint8_t)5ULL)) goto mn88;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
        goto gm_fdone;
        goto me5;
mn88:;
ma11:;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t91 != ((uint8_t)12ULL)) goto mn90;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)&t4) + 8) + 0));
        gm_l6 = t92;
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                gm_l7 = t95;
            }
            {
                for (;;) {
                    uint64_t t97 = gm_l8;
                    uint64_t t98 = gm_l7;
                    uint8_t t99 = (t97 < t98);
                    if (!t99) goto lb96;
                    {
                        uint64_t t102 = gm_l8;
                        uint64_t t105 = GM_LD(uint64_t, ((uint8_t *)&t102));
                        gm_l9 = t105;
                        {
                            void * t108 = gm_l0;
                            gm_tl874t2atnlh3 t109 = gm_l1;
                            uint64_t t110 = gm_l9;
                            uint64_t t111 = (uintptr_t)t110;
                            uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_srcj10g653a00lt, 300, 42);
                            gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t111 * 4));
                            void * t114 = gm_l0;
                            gm_t88rcucg1b7uk t115 = gm_l2;
                            gm_b16_8 t116 = gm_l3;
                            uint64_t t117 = gm_l9;
                            uint32_t t118 = (uint32_t)t117;
                            gm_b16_8 t119 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t114, t115, t116, t118);
                            if (gm_unw.kind) {
                                goto gm_ll100;
                            }
                            void * t120 = gm_l0;
                            gm_t88rcucg1b7uk t121 = gm_l2;
                            gm_b16_8 t122 = gm_l4;
                            uint64_t t123 = gm_l9;
                            uint32_t t124 = (uint32_t)t123;
                            gm_b16_8 t125 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t120, t121, t122, t124);
                            if (gm_unw.kind) {
                                goto gm_ll100;
                            }
                            gm_t1ec8br9ddkav t126;
                            gm_f_equal_e5vf82h((void *)&t126, (void *)NULL, cx, NULL, t108, t109, t113, t119, t125);
                            if (gm_unw.kind) {
                                goto gm_ll100;
                            }
                            if (((((uint8_t *)&t126))[0] != 0)) {
                                gm_t1ec8br9ddkav t127;
                                GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                                ((uint8_t *)&t127)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t127 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t126) + 8)));
                                F.ret = t127;
                                goto gm_fret;
                            }
                            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t126) + 8));
                            uint8_t t129 = (uint8_t)!t128;
                            if (!t129) goto ie107;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                                goto gm_fdone;
                            }
                            goto ix107;
ie107:;
ix107:;
                        }
                        goto me103;
mn104:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 299, 13);
me103:;
                    }
lc96:;
                    {
                        uint64_t t132 = gm_l8;
                        uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                        uint64_t t135 = r133;
                        gm_l8 = t135;
                    }
                }
gm_ll100:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb96; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc96; }
                }
                goto gm_lad;
lb96:;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
            goto gm_fdone;
        }
        goto me5;
mn90:;
ma12:;
        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t137 != ((uint8_t)11ULL)) goto mn136;
        uint64_t t138 = GM_LD(uint64_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l10 = t138;
        gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 8));
        gm_l11 = t139;
        {
            void * t141 = gm_l0;
            gm_t88rcucg1b7uk t142 = gm_l11;
            uint64_t t143 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t144 = gm_f_us_cuv01fp(t143);
            gm_l12 = t144;
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                uint64_t t146 = gm_l10;
                uint64_t t147 = gm_f_us_cuv01fp(t146);
                gm_l13 = t147;
            }
            {
                for (;;) {
                    uint64_t t149 = gm_l14;
                    uint64_t t150 = gm_l13;
                    uint8_t t151 = (t149 < t150);
                    if (!t151) goto lb148;
                    {
                        uint64_t t154 = gm_l14;
                        uint64_t t157 = GM_LD(uint64_t, ((uint8_t *)&t154));
                        gm_l15 = t157;
                        {
                            void * t160 = gm_l0;
                            gm_tl874t2atnlh3 t161 = gm_l1;
                            gm_t88rcucg1b7uk t162 = gm_l11;
                            uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            void * t164 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                            uint64_t t165 = gm_l15;
                            uint64_t t166 = gm_l12;
                            uint64_t r167; bool o168 = __builtin_mul_overflow(t165, t166, &r167);
                            if (o168) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 50);
                            uint64_t t169 = r167;
                            uint64_t t170 = (uintptr_t)t169;
                            uint64_t t171 = gm_l15;
                            uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                            if (o173) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 307, 60);
                            uint64_t t174 = r172;
                            uint64_t t175 = gm_l12;
                            uint64_t r176; bool o177 = __builtin_mul_overflow(t174, t175, &r176);
                            if (o177) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 59);
                            uint64_t t178 = r176;
                            uint64_t t179 = (uintptr_t)t178;
                            if (t170 > t179 || t179 > (uintptr_t)t163) gm_trap_slice(t170, t179, t163, gm_srcj10g653a00lt, 307, 48);
                            gm_b16_8 t180;
                            GM_ST(void *, (uint8_t *)&t180, (void *)(((uint8_t *)t164 + t170 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(t179 - t170));
                            uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            void * t182 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                            uint64_t t183 = gm_l15;
                            uint64_t t184 = gm_l12;
                            uint64_t r185; bool o186 = __builtin_mul_overflow(t183, t184, &r185);
                            if (o186) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 77);
                            uint64_t t187 = r185;
                            uint64_t t188 = (uintptr_t)t187;
                            uint64_t t189 = gm_l15;
                            uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                            if (o191) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 307, 87);
                            uint64_t t192 = r190;
                            uint64_t t193 = gm_l12;
                            uint64_t r194; bool o195 = __builtin_mul_overflow(t192, t193, &r194);
                            if (o195) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 307, 86);
                            uint64_t t196 = r194;
                            uint64_t t197 = (uintptr_t)t196;
                            if (t188 > t197 || t197 > (uintptr_t)t181) gm_trap_slice(t188, t197, t181, gm_srcj10g653a00lt, 307, 75);
                            gm_b16_8 t198;
                            GM_ST(void *, (uint8_t *)&t198, (void *)(((uint8_t *)t182 + t188 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(t197 - t188));
                            gm_t1ec8br9ddkav t199;
                            gm_f_equal_e5vf82h((void *)&t199, (void *)NULL, cx, NULL, t160, t161, t162, t180, t198);
                            if (gm_unw.kind) {
                                goto gm_ll152;
                            }
                            if (((((uint8_t *)&t199))[0] != 0)) {
                                gm_t1ec8br9ddkav t200;
                                GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
                                ((uint8_t *)&t200)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t200 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t199) + 8)));
                                F.ret = t200;
                                goto gm_fret;
                            }
                            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t199) + 8));
                            uint8_t t202 = (uint8_t)!t201;
                            if (!t202) goto ie159;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                                goto gm_fdone;
                            }
                            goto ix159;
ie159:;
ix159:;
                        }
                        goto me155;
mn156:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 306, 13);
me155:;
                    }
lc148:;
                    {
                        uint64_t t205 = gm_l14;
                        uint64_t r206; bool o207 = __builtin_add_overflow(t205, ((uint64_t)1ULL), &r206);
                        uint64_t t208 = r206;
                        gm_l14 = t208;
                    }
                }
gm_ll152:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb148; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc148; }
                }
                goto gm_lad;
lb148:;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
            goto gm_fdone;
        }
        goto me5;
mn136:;
ma13:;
        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t210 != ((uint8_t)10ULL)) goto mn209;
        gm_t88rcucg1b7uk t211 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l16 = t211;
        {
            void * t213 = gm_l0;
            gm_b16_8 t214 = gm_l3;
            gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
            gm_t2lqu8jsj511d t216 = gm_f_slice_parts_1se5hu4(t213, t215);
            gm_l17 = t216;
        }
        {
            void * t217 = gm_l0;
            gm_b16_8 t218 = gm_l4;
            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
            gm_t2lqu8jsj511d t220 = gm_f_slice_parts_1se5hu4(t217, t219);
            gm_l18 = t220;
        }
        {
            uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint8_t t224 = (!(t222 == t223));
            if (!t224) goto ie221;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix221;
ie221:;
ix221:;
        }
        {
            void * t226 = gm_l0;
            gm_t88rcucg1b7uk t227 = gm_l16;
            gm_t53at80pfdus3 t228;
            gm_f_kind_htv6093((void *)&t228, t226, GM_CONV(gm_csguankko4lmhd, t227));
            uint8_t t229;
            uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&t228) + 0));
            if (t232 != ((uint8_t)3ULL)) goto mn231;
            t229 = ((uint8_t)1);
            goto me230;
mn231:;
            t229 = ((uint8_t)0);
            goto me230;
mn233:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 315, 25);
me230:;
            gm_l19 = t229;
        }
        {
            uint8_t t235 = 0;
            uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 0));
            uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 0));
            uint8_t t238 = ((t236 == t237));
            if (!t238) {
                uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t240 = ((t239 == ((uint64_t)0ULL)));
                t235 = t240;
            } else t235 = 1;
            if (!t235) goto ie234;
            uint8_t t241 = 0;
            uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint8_t t243 = ((t242 == ((uint64_t)0ULL)));
            if (!t243) {
                uint8_t t244 = gm_l19;
                uint8_t t245 = (uint8_t)!t244;
                t241 = t245;
            } else t241 = 1;
            if (!t241) goto ie234;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                goto gm_fdone;
            }
            goto ix234;
ie234:;
ix234:;
        }
        {
            void * t247 = gm_l0;
            gm_tl874t2atnlh3 t248 = gm_l1;
            gm_t88rcucg1b7uk t249 = gm_l2;
            gm_b16_8 t250 = gm_l3;
            gm_tmc91gv7n9c0i t251;
            gm_f_seq_of_ifahtjs((void *)&t251, (void *)NULL, cx, NULL, t247, t248, t249, t250);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t251))[0] != 0)) {
                gm_t1ec8br9ddkav t252;
                GM_ST(uint8_t, (uint8_t *)&t252 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t252 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t252 + 4, 0);
                ((uint8_t *)&t252)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t252 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t251) + 8)));
                F.ret = t252;
                goto gm_fret;
            }
            F.l20 = (*(gm_t2jeg3bg7sfqa *)(((uint8_t *)&t251) + 8));
        }
        {
            void * t253 = gm_l0;
            gm_tl874t2atnlh3 t254 = gm_l1;
            gm_t88rcucg1b7uk t255 = gm_l2;
            gm_b16_8 t256 = gm_l4;
            gm_tmc91gv7n9c0i t257;
            gm_f_seq_of_ifahtjs((void *)&t257, (void *)NULL, cx, NULL, t253, t254, t255, t256);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t257))[0] != 0)) {
                gm_t1ec8br9ddkav t258;
                GM_ST(uint8_t, (uint8_t *)&t258 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t258 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t258 + 4, 0);
                ((uint8_t *)&t258)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t258 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t257) + 8)));
                F.ret = t258;
                goto gm_fret;
            }
            F.l21 = (*(gm_t2jeg3bg7sfqa *)(((uint8_t *)&t257) + 8));
        }
        {
            uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
            uint8_t t261 = ((t260 == ((uint64_t)1ULL)));
            if (!t261) goto ie259;
            void * t262 = gm_l0;
            gm_t88rcucg1b7uk t263 = gm_l16;
            gm_t53at80pfdus3 t264;
            gm_f_kind_htv6093((void *)&t264, t262, GM_CONV(gm_csguankko4lmhd, t263));
            uint8_t t265;
            uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&t264) + 0));
            if (t268 != ((uint8_t)0ULL)) goto mn267;
            t265 = ((uint8_t)1);
            goto me266;
mn267:;
            t265 = ((uint8_t)0);
            goto me266;
mn269:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 320, 32);
me266:;
            if (!t265) goto ie259;
            {
                uint8_t t271 = 0;
                uint64_t t272 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 24) + 8);
                uint64_t t273 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 24) + 8);
                uint8_t t274 = ((t272 == t273));
                if (t274) {
                    gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 24));
                    gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
                    gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 24));
                    gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
                    uint8_t t279 = gm_f_bytes_equal_cksjlgj(t276, t278);
                    t271 = t279;
                } else t271 = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t271);
                goto gm_fdone;
            }
            goto ix259;
ie259:;
ix259:;
        }
        {
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint64_t t282 = gm_f_us_cuv01fp(t281);
                gm_l22 = t282;
            }
            {
                for (;;) {
                    uint64_t t284 = gm_l23;
                    uint64_t t285 = gm_l22;
                    uint8_t t286 = (t284 < t285);
                    if (!t286) goto lb283;
                    {
                        uint64_t t289 = gm_l23;
                        uint64_t t292 = GM_LD(uint64_t, ((uint8_t *)&t289));
                        gm_l24 = t292;
                        {
                            void * t295 = gm_l0;
                            gm_tl874t2atnlh3 t296 = gm_l1;
                            gm_t88rcucg1b7uk t297 = gm_l16;
                            uint64_t t298 = GM_LD(uintptr_t, (((uint8_t *)&F.l20) + 24) + 8);
                            void * t299 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l20) + 24)));
                            uint64_t t300 = gm_l24;
                            uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r302; bool o303 = __builtin_mul_overflow(t300, t301, &r302);
                            if (o303) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 56);
                            uint64_t t304 = r302;
                            uint64_t t305 = (uintptr_t)t304;
                            uint64_t t306 = gm_l24;
                            uint64_t r307; bool o308 = __builtin_add_overflow(t306, ((uint64_t)1ULL), &r307);
                            if (o308) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 322, 69);
                            uint64_t t309 = r307;
                            uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r311; bool o312 = __builtin_mul_overflow(t309, t310, &r311);
                            if (o312) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 68);
                            uint64_t t313 = r311;
                            uint64_t t314 = (uintptr_t)t313;
                            if (t305 > t314 || t314 > (uintptr_t)t298) gm_trap_slice(t305, t314, t298, gm_srcj10g653a00lt, 322, 48);
                            gm_b16_8 t315;
                            GM_ST(void *, (uint8_t *)&t315, (void *)(((uint8_t *)t299 + t305 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(t314 - t305));
                            uint64_t t316 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 24) + 8);
                            void * t317 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 24)));
                            uint64_t t318 = gm_l24;
                            uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r320; bool o321 = __builtin_mul_overflow(t318, t319, &r320);
                            if (o321) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 95);
                            uint64_t t322 = r320;
                            uint64_t t323 = (uintptr_t)t322;
                            uint64_t t324 = gm_l24;
                            uint64_t r325; bool o326 = __builtin_add_overflow(t324, ((uint64_t)1ULL), &r325);
                            if (o326) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 322, 108);
                            uint64_t t327 = r325;
                            uint64_t t328 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t r329; bool o330 = __builtin_mul_overflow(t327, t328, &r329);
                            if (o330) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 322, 107);
                            uint64_t t331 = r329;
                            uint64_t t332 = (uintptr_t)t331;
                            if (t323 > t332 || t332 > (uintptr_t)t316) gm_trap_slice(t323, t332, t316, gm_srcj10g653a00lt, 322, 87);
                            gm_b16_8 t333;
                            GM_ST(void *, (uint8_t *)&t333, (void *)(((uint8_t *)t317 + t323 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(t332 - t323));
                            gm_t1ec8br9ddkav t334;
                            gm_f_equal_e5vf82h((void *)&t334, (void *)NULL, cx, NULL, t295, t296, t297, t315, t333);
                            if (gm_unw.kind) {
                                goto gm_ll287;
                            }
                            if (((((uint8_t *)&t334))[0] != 0)) {
                                gm_t1ec8br9ddkav t335;
                                GM_ST(uint8_t, (uint8_t *)&t335 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t335 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t335 + 4, 0);
                                ((uint8_t *)&t335)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t335 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t334) + 8)));
                                F.ret = t335;
                                goto gm_fret;
                            }
                            uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&t334) + 8));
                            uint8_t t337 = (uint8_t)!t336;
                            if (!t337) goto ie294;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                                goto gm_fdone;
                            }
                            goto ix294;
ie294:;
ix294:;
                        }
                        goto me290;
mn291:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 321, 13);
me290:;
                    }
lc283:;
                    {
                        uint64_t t340 = gm_l23;
                        uint64_t r341; bool o342 = __builtin_add_overflow(t340, ((uint64_t)1ULL), &r341);
                        uint64_t t343 = r341;
                        gm_l23 = t343;
                    }
                }
gm_ll287:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb283; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc283; }
                }
                goto gm_lad;
lb283:;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
            goto gm_fdone;
        }
        goto me5;
mn209:;
ma14:;
        uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t345 != ((uint8_t)13ULL)) goto mn344;
        gm_t88rcucg1b7uk t346 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t4) + 8));
        gm_l25 = t346;
        {
            void * t349 = gm_l0;
            void * t350 = GM_LD(void *, (((uint8_t *)t349) + 8));
            gm_t88rcucg1b7uk t351 = gm_l25;
            uint8_t t352 = gm_f_nullable_pointer_7qfmn6o(t350, GM_CONV(gm_csguankko4lmhd, t351));
            if (!t352) goto ie348;
            {
                void * t354 = gm_l0;
                gm_b16_8 t355 = gm_l3;
                gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
                uint64_t t357 = gm_f_ptr_of_b77b4c9(t354, t356);
                void * t358 = gm_l0;
                gm_b16_8 t359 = gm_l4;
                gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
                uint64_t t361 = gm_f_ptr_of_b77b4c9(t358, t360);
                uint8_t t362 = ((t357 == t361));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t362);
                goto gm_fdone;
            }
            goto ix348;
ie348:;
ix348:;
        }
        {
            void * t363 = gm_l0;
            gm_t88rcucg1b7uk t364 = gm_l2;
            gm_b16_8 t365 = gm_l3;
            gm_b16_8 t366 = GM_LD(gm_b16_8, ((uint8_t *)&t365));
            uint8_t t367 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t363, t364, t366);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t367;
        }
        {
            void * t368 = gm_l0;
            gm_t88rcucg1b7uk t369 = gm_l2;
            gm_b16_8 t370 = gm_l4;
            gm_b16_8 t371 = GM_LD(gm_b16_8, ((uint8_t *)&t370));
            uint8_t t372 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t368, t369, t371);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t372;
        }
        {
            uint8_t t374 = gm_l26;
            uint8_t t375 = (uint8_t)!t374;
            if (!t375) goto ie373;
            uint8_t t376 = gm_l27;
            uint8_t t377 = (uint8_t)!t376;
            if (!t377) goto ie373;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                goto gm_fdone;
            }
            goto ix373;
ie373:;
ix373:;
        }
        {
            uint8_t t380 = gm_l26;
            if (!t380) goto ie379;
            uint8_t t381 = gm_l27;
            if (!t381) goto ie379;
            {
                void * t383 = gm_l0;
                gm_tl874t2atnlh3 t384 = gm_l1;
                gm_t88rcucg1b7uk t385 = gm_l25;
                void * t386 = gm_l0;
                gm_t88rcucg1b7uk t387 = gm_l2;
                gm_b16_8 t388 = gm_l3;
                gm_b16_8 t389 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t386, t387, t388);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t390 = gm_l0;
                gm_t88rcucg1b7uk t391 = gm_l2;
                gm_b16_8 t392 = gm_l4;
                gm_b16_8 t393 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t390, t391, t392);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t383, t384, t385, t389, t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix379;
ie379:;
ix379:;
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
            goto gm_fdone;
        }
        goto me5;
mn344:;
ma15:;
        uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t395 != ((uint8_t)14ULL)) goto mn394;
        gm_t88rcucg1b7uk t396 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 0));
        gm_l28 = t396;
        gm_t88rcucg1b7uk t397 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l29 = t397;
        {
            void * t399 = gm_l0;
            gm_t88rcucg1b7uk t400 = gm_l2;
            gm_b16_8 t401 = gm_l3;
            gm_b16_8 t402 = GM_LD(gm_b16_8, ((uint8_t *)&t401));
            uint8_t t403 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t399, t400, t402);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t403;
        }
        {
            uint8_t t405 = gm_l30;
            void * t406 = gm_l0;
            gm_t88rcucg1b7uk t407 = gm_l2;
            gm_b16_8 t408 = gm_l4;
            gm_b16_8 t409 = GM_LD(gm_b16_8, ((uint8_t *)&t408));
            uint8_t t410 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t406, t407, t409);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t411 = (!(t405 == t410));
            if (!t411) goto ie404;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix404;
ie404:;
ix404:;
        }
        {
            void * t413 = gm_l0;
            gm_tl874t2atnlh3 t414 = gm_l1;
            gm_t88rcucg1b7uk t415;
            uint8_t t417 = gm_l30;
            if (!t417) goto ie416;
            gm_t88rcucg1b7uk t419 = gm_l28;
            gm_t88rcucg1b7uk t420 = t419;
            t415 = t420;
            goto ix416;
ie416:;
            gm_t88rcucg1b7uk t422 = gm_l29;
            gm_t88rcucg1b7uk t423 = t422;
            t415 = t423;
ix416:;
            void * t424 = gm_l0;
            gm_t88rcucg1b7uk t425 = gm_l2;
            gm_b16_8 t426 = gm_l3;
            gm_b16_8 t427 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t424, t425, t426);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t428 = gm_l0;
            gm_t88rcucg1b7uk t429 = gm_l2;
            gm_b16_8 t430 = gm_l4;
            gm_b16_8 t431 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t428, t429, t430);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t413, t414, t415, t427, t431);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn394:;
ma16:;
        uint8_t t433 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t433 != ((uint8_t)17ULL)) goto mn432;
        {
            void * t436 = gm_l0;
            gm_t88rcucg1b7uk t437 = gm_l2;
            gm_tkn42u7na3t81 t438 = gm_f_bits_struct_ty_22ue20s(t436, GM_CONV(gm_csguankko4lmhd, t437));
            if (!((((uint8_t *)&t438))[4] != 0)) goto ie435;
            gm_t88rcucg1b7uk t439 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t438));
            gm_l31 = t439;
            {
                void * t441 = gm_l0;
                gm_tl874t2atnlh3 t442 = gm_l1;
                gm_t88rcucg1b7uk t443 = gm_l31;
                gm_b16_8 t444 = gm_l3;
                gm_b16_8 t445 = gm_l4;
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t441, t442, t443, t444, t445);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix435;
ie435:;
ix435:;
        }
        {
            void * t447 = gm_l0;
            gm_t88rcucg1b7uk t448 = gm_l2;
            gm_t57euh612ligr t449;
            gm_f_union_def_h8mvf4t((void *)&t449, t447, GM_CONV(gm_csguankko4lmhd, t448));
            if (!((((uint8_t *)&t449))[24] != 0)) goto ie446;
            gm_l32 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t449) + 0));
            {
                void * t451 = gm_l0;
                gm_t88rcucg1b7uk t452 = gm_l2;
                gm_b16_8 t453 = gm_l3;
                gm_b16_8 t454 = GM_LD(gm_b16_8, ((uint8_t *)&t453));
                gm_t9805qtonbsag t455 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t451, t452, t454);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t456;
                if (((((uint8_t *)&t455))[4] != 0)) {
                    uint32_t t457 = GM_LD(uint32_t, (((uint8_t *)&t455) + 0));
                    t456 = t457;
                } else {
                    void * t458 = gm_l0;
                    gm_tl874t2atnlh3 t459 = gm_l1;
                    gm_t88rcucg1b7uk t460 = gm_l2;
                    gm_b16_8 t461 = gm_l3;
                    gm_b16_8 t462 = GM_LD(gm_b16_8, ((uint8_t *)&t461));
                    gm_t76123gfet73g t463;
                    gm_f_invalid_tag_1gif354((void *)&t463, (void *)NULL, cx, NULL, t458, t459, t460, t462);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t463);
                    goto gm_fdone;
                }
                gm_l33 = t456;
            }
            {
                void * t464 = gm_l0;
                gm_t88rcucg1b7uk t465 = gm_l2;
                gm_b16_8 t466 = gm_l4;
                gm_b16_8 t467 = GM_LD(gm_b16_8, ((uint8_t *)&t466));
                gm_t9805qtonbsag t468 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t464, t465, t467);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t469;
                if (((((uint8_t *)&t468))[4] != 0)) {
                    uint32_t t470 = GM_LD(uint32_t, (((uint8_t *)&t468) + 0));
                    t469 = t470;
                } else {
                    void * t471 = gm_l0;
                    gm_tl874t2atnlh3 t472 = gm_l1;
                    gm_t88rcucg1b7uk t473 = gm_l2;
                    gm_b16_8 t474 = gm_l4;
                    gm_b16_8 t475 = GM_LD(gm_b16_8, ((uint8_t *)&t474));
                    gm_t76123gfet73g t476;
                    gm_f_invalid_tag_1gif354((void *)&t476, (void *)NULL, cx, NULL, t471, t472, t473, t475);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t476);
                    goto gm_fdone;
                }
                gm_l34 = t469;
            }
            {
                uint32_t t478 = gm_l33;
                uint32_t t479 = gm_l34;
                uint8_t t480 = (!(t478 == t479));
                if (!t480) goto ie477;
                {
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                    goto gm_fdone;
                }
                goto ix477;
ie477:;
ix477:;
            }
            {
                gm_b16_8 t482 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                gm_b16_8 t483 = t482;
                uint32_t t484 = gm_l33;
                uint64_t t485 = (uint64_t)t484;
                uint64_t t486 = gm_f_us_cuv01fp(t485);
                uint64_t t487 = (uintptr_t)t486;
                uint64_t t488 = GM_LD(uintptr_t, ((uint8_t *)&t483) + 8);
                if (t487 >= (uintptr_t)(t488)) gm_trap_index(t487, t488, gm_srcj10g653a00lt, 345, 23);
                gm_tkn42u7na3t81 t489 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t483)) + t487 * 72) + 16));
                gm_t88rcucg1b7uk t490;
                if (((((uint8_t *)&t489))[4] != 0)) {
                    gm_t88rcucg1b7uk t491 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t489) + 0));
                    t490 = t491;
                } else {
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                    goto gm_fdone;
                }
                gm_l35 = t490;
            }
            {
                void * t492 = gm_l0;
                gm_t88rcucg1b7uk t493 = gm_l2;
                uint64_t t494 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t492, t493);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t495 = gm_f_us_cuv01fp(t494);
                gm_l36 = t495;
            }
            {
                void * t496 = gm_l0;
                gm_t88rcucg1b7uk t497 = gm_l35;
                uint64_t t498 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t496, t497);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t499 = gm_f_us_cuv01fp(t498);
                gm_l37 = t499;
            }
            {
                void * t500 = gm_l0;
                gm_tl874t2atnlh3 t501 = gm_l1;
                gm_t88rcucg1b7uk t502 = gm_l35;
                uint64_t t503 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t504 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t505 = gm_l36;
                uint64_t t506 = (uintptr_t)t505;
                uint64_t t507 = gm_l36;
                uint64_t t508 = gm_l37;
                uint64_t r509; bool o510 = __builtin_add_overflow(t507, t508, &r509);
                if (o510) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 348, 49);
                uint64_t t511 = r509;
                uint64_t t512 = (uintptr_t)t511;
                if (t506 > t512 || t512 > (uintptr_t)t503) gm_trap_slice(t506, t512, t503, gm_srcj10g653a00lt, 348, 42);
                gm_b16_8 t513;
                GM_ST(void *, (uint8_t *)&t513, (void *)(((uint8_t *)t504 + t506 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t513 + 8, (uintptr_t)(t512 - t506));
                uint64_t t514 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t515 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t516 = gm_l36;
                uint64_t t517 = (uintptr_t)t516;
                uint64_t t518 = gm_l36;
                uint64_t t519 = gm_l37;
                uint64_t r520; bool o521 = __builtin_add_overflow(t518, t519, &r520);
                if (o521) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 348, 66);
                uint64_t t522 = r520;
                uint64_t t523 = (uintptr_t)t522;
                if (t517 > t523 || t523 > (uintptr_t)t514) gm_trap_slice(t517, t523, t514, gm_srcj10g653a00lt, 348, 59);
                gm_b16_8 t524;
                GM_ST(void *, (uint8_t *)&t524, (void *)(((uint8_t *)t515 + t517 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t524 + 8, (uintptr_t)(t523 - t517));
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t500, t501, t502, t513, t524);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix446;
ie446:;
ix446:;
        }
        {
            void * t525 = gm_l0;
            gm_t88rcucg1b7uk t526 = gm_l2;
            void * t527 = gm_f_adt_of_8k5vqud(t525, GM_CONV(gm_csguankko4lmhd, t526));
            void * t528;
            if ((GM_LD(void *, ((uint8_t *)&t527)) != 0)) {
                void * t529 = GM_LD(void *, (((uint8_t *)&t527) + 0));
                t528 = t529;
            } else {
                void * t530 = gm_l0;
                gm_tl874t2atnlh3 t531 = gm_l1;
                void * t532 = gm_l0;
                void * t533 = GM_LD(void *, (((uint8_t *)t532) + 8));
                gm_t88rcucg1b7uk t534 = gm_l2;
                gm_b16_8 t535 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t533, t534);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t536;
                GM_ST(gm_b16_8, ((uint8_t *)&t536 + 0), t535);
                gm_tabb710d9mq2e t537;
                gm_f_format_h8e7ps1((void *)&t537, (void *)NULL, cx, NULL, t536);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t537))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t537) + 8, gm_srcj10g653a00lt, 350, 56);
                gm_b16_8 t538 = GM_LD(gm_b16_8, (((uint8_t *)&t537) + 8));
                gm_b16_8 t539 = GM_LD(gm_b16_8, ((uint8_t *)&t538));
                gm_t76123gfet73g t540;
                gm_f_trap_qh84l9d((void *)&t540, (void *)NULL, cx, NULL, t530, t531, t539);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t540);
                goto gm_fdone;
            }
            gm_l38 = t528;
        }
        {
            void * t542 = gm_l38;
            uint8_t t543 = GM_LD(uint8_t, ((((uint8_t *)t542) + 24) + 0));
            if (t543 != ((uint8_t)0ULL)) goto ie541;
            gm_tpautr4br7h3l t544 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t542) + 24) + 8));
            F.l39 = t544;
            {
                {
                    gm_l41 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t547 = GM_LD(uintptr_t, (((uint8_t *)&F.l39) + 0) + 8);
                    gm_l40 = t547;
                }
                {
                    for (;;) {
                        uint64_t t549 = gm_l41;
                        uint64_t t550 = gm_l40;
                        uint8_t t551 = (t549 < t550);
                        if (!t551) goto lb548;
                        {
                            uint64_t t554 = gm_l41;
                            uint64_t t557 = GM_LD(uint64_t, ((uint8_t *)&t554));
                            gm_l42 = t557;
                            {
                                void * t559 = gm_l0;
                                gm_t88rcucg1b7uk t560 = gm_l2;
                                uint64_t t561 = gm_l42;
                                uint32_t t562 = (uint32_t)t561;
                                gm_tfbnciphf3261 t563 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t559, t560, t562);
                                if (gm_unw.kind) {
                                    goto gm_ll552;
                                }
                                gm_l43 = t563;
                            }
                            {
                                uint64_t t564 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                                uint64_t t565 = gm_f_us_cuv01fp(t564);
                                gm_l44 = t565;
                            }
                            {
                                void * t566 = gm_l0;
                                gm_t88rcucg1b7uk t567 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l43) + 0));
                                uint64_t t568 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t566, t567);
                                if (gm_unw.kind) {
                                    goto gm_ll552;
                                }
                                uint64_t t569 = gm_f_us_cuv01fp(t568);
                                gm_l45 = t569;
                            }
                            {
                                void * t571 = gm_l0;
                                gm_tl874t2atnlh3 t572 = gm_l1;
                                gm_t88rcucg1b7uk t573 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l43) + 0));
                                uint64_t t574 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t575 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t576 = gm_l44;
                                uint64_t t577 = (uintptr_t)t576;
                                uint64_t t578 = gm_l44;
                                uint64_t t579 = gm_l45;
                                uint64_t r580; bool o581 = __builtin_add_overflow(t578, t579, &r580);
                                if (o581) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 356, 56);
                                uint64_t t582 = r580;
                                uint64_t t583 = (uintptr_t)t582;
                                if (t577 > t583 || t583 > (uintptr_t)t574) gm_trap_slice(t577, t583, t574, gm_srcj10g653a00lt, 356, 49);
                                gm_b16_8 t584;
                                GM_ST(void *, (uint8_t *)&t584, (void *)(((uint8_t *)t575 + t577 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t584 + 8, (uintptr_t)(t583 - t577));
                                uint64_t t585 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                void * t586 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                                uint64_t t587 = gm_l44;
                                uint64_t t588 = (uintptr_t)t587;
                                uint64_t t589 = gm_l44;
                                uint64_t t590 = gm_l45;
                                uint64_t r591; bool o592 = __builtin_add_overflow(t589, t590, &r591);
                                if (o592) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 356, 73);
                                uint64_t t593 = r591;
                                uint64_t t594 = (uintptr_t)t593;
                                if (t588 > t594 || t594 > (uintptr_t)t585) gm_trap_slice(t588, t594, t585, gm_srcj10g653a00lt, 356, 66);
                                gm_b16_8 t595;
                                GM_ST(void *, (uint8_t *)&t595, (void *)(((uint8_t *)t586 + t588 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t595 + 8, (uintptr_t)(t594 - t588));
                                gm_t1ec8br9ddkav t596;
                                gm_f_equal_e5vf82h((void *)&t596, (void *)NULL, cx, NULL, t571, t572, t573, t584, t595);
                                if (gm_unw.kind) {
                                    goto gm_ll552;
                                }
                                if (((((uint8_t *)&t596))[0] != 0)) {
                                    gm_t1ec8br9ddkav t597;
                                    GM_ST(uint8_t, (uint8_t *)&t597 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t597 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t597 + 4, 0);
                                    ((uint8_t *)&t597)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t597 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t596) + 8)));
                                    F.ret = t597;
                                    goto gm_fret;
                                }
                                uint8_t t598 = GM_LD(uint8_t, (((uint8_t *)&t596) + 8));
                                uint8_t t599 = (uint8_t)!t598;
                                if (!t599) goto ie570;
                                {
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                                    goto gm_fdone;
                                }
                                goto ix570;
ie570:;
ix570:;
                            }
                            goto me555;
mn556:;
                            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 352, 13);
me555:;
                        }
lc548:;
                        {
                            uint64_t t602 = gm_l41;
                            uint64_t r603; bool o604 = __builtin_add_overflow(t602, ((uint64_t)1ULL), &r603);
                            uint64_t t605 = r603;
                            gm_l41 = t605;
                        }
                    }
gm_ll552:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb548; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc548; }
                    }
                    goto gm_lad;
lb548:;
                }
            }
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                goto gm_fdone;
            }
            goto ix541;
ie541:;
            {
                void * t607 = gm_l0;
                gm_tl874t2atnlh3 t608 = gm_l1;
                void * t609 = gm_l0;
                void * t610 = GM_LD(void *, (((uint8_t *)t609) + 8));
                gm_t88rcucg1b7uk t611 = gm_l2;
                gm_b16_8 t612 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t610, t611);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t613;
                GM_ST(gm_b16_8, ((uint8_t *)&t613 + 0), t612);
                gm_tabb710d9mq2e t614;
                gm_f_format_dmukbli((void *)&t614, (void *)NULL, cx, NULL, t613);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t614))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t614) + 8, gm_srcj10g653a00lt, 351, 62);
                gm_b16_8 t615 = GM_LD(gm_b16_8, (((uint8_t *)&t614) + 8));
                gm_b16_8 t616 = GM_LD(gm_b16_8, ((uint8_t *)&t615));
                gm_t76123gfet73g t617;
                gm_f_trap_qh84l9d((void *)&t617, (void *)NULL, cx, NULL, t607, t608, t616);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t617);
                goto gm_fdone;
            }
ix541:;
        }
        goto me5;
mn432:;
ma17:;
        void * t619 = gm_l0;
        gm_tl874t2atnlh3 t620 = gm_l1;
        void * t621 = gm_l0;
        void * t622 = GM_LD(void *, (((uint8_t *)t621) + 8));
        gm_t88rcucg1b7uk t623 = gm_l2;
        gm_b16_8 t624 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t622, t623);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t625;
        GM_ST(gm_b16_8, ((uint8_t *)&t625 + 0), t624);
        gm_tabb710d9mq2e t626;
        gm_f_format_dmukbli((void *)&t626, (void *)NULL, cx, NULL, t625);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t626))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t626) + 8, gm_srcj10g653a00lt, 360, 35);
        gm_b16_8 t627 = GM_LD(gm_b16_8, (((uint8_t *)&t626) + 8));
        gm_b16_8 t628 = GM_LD(gm_b16_8, ((uint8_t *)&t627));
        gm_t76123gfet73g t629;
        gm_f_trap_qh84l9d((void *)&t629, (void *)NULL, cx, NULL, t619, t620, t628);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t629);
        goto gm_fdone;
        goto me5;
mn618:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 289, 5);
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
inline __attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_invalid_tag_1gif354 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    gm_tcmkompbl09il gm_l4; /* u */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t57euh612ligr t4;
        gm_f_union_def_h8mvf4t((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_tcmkompbl09il t5;
        if (((((uint8_t *)&t4))[24] != 0)) {
            t5 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t4) + 0));
        } else {
            void * t6 = gm_l0;
            gm_tl874t2atnlh3 t7 = gm_l1;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s7ah1q5ah26rj));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(11));
            gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l4 = t5;
    }
    {
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = gm_l1;
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l2;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 0));
        gm_b16_8 t14 = gm_l3;
        gm_b16_8 t15 = gm_f_invalid_tag_text_f3l5hi7((void *)NULL, cx, NULL, t11, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t9, t10, t15);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_invalid_tag_text_f3l5hi7 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* tag */
    gm_b16_8 gm_l3; /* b */
    unsigned __int128 gm_l4; /* raw */
    gm_ttovhfee8mf28 gm_l5; /* info */
    gm_b16_8 gm_l6; /* text */
    GM_STACK_CHECK(&F, 622u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_b16_8 t4 = gm_l3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        void * t6 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t4)));
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l2;
        uint64_t t10 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t11 = gm_f_us_cuv01fp(t10);
        uint64_t t12 = (uintptr_t)t11;
        if (t7 > t12 || t12 > (uintptr_t)t5) gm_trap_slice(t7, t12, t5, gm_srcj10g653a00lt, 373, 27);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t6 + t7 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t7));
        unsigned __int128 t14 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t13);
        gm_l4 = t14;
    }
    {
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_tg5lql6itufk7 t17 = gm_f_int_info_ulcevdv(t15, GM_CONV(gm_csguankko4lmhd, t16));
        gm_ttovhfee8mf28 t18;
        if (((((uint8_t *)&t17))[8] != 0)) {
            gm_ttovhfee8mf28 t19 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t17) + 0));
            t18 = t19;
        } else {
            gm_ttovhfee8mf28 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t20 + 4), ((uint32_t)8ULL));
            t18 = t20;
        }
        gm_l5 = t18;
    }
    {
        gm_b16_8 t21;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (!t23) goto ie22;
        unsigned __int128 t25 = gm_l4;
        __int128 t26;
        memset(&t26, 0, sizeof t26); memcpy(&t26, ((uint8_t *)&t25), sizeof t26 < 16 ? sizeof t26 : 16);
        gm_tpl3q6e8s9qo1 t27;
        GM_ST(__int128, ((uint8_t *)&t27 + 0), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_9qvncot((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcj10g653a00lt, 375, 30);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_b16_8 t30 = t29;
        t21 = t30;
        goto ix22;
ie22:;
        unsigned __int128 t32 = gm_l4;
        gm_tsn2omssg6cpf t33;
        GM_ST(unsigned __int128, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_ra7pont((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcj10g653a00lt, 375, 73);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_b16_8 t36 = t35;
        t21 = t36;
ix22:;
        gm_l6 = t21;
    }
    {
        gm_t6j6smj7qoctu t37;
        gm_b16_8 t38 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t37 + 0, t38);
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 8));
        gm_t88rcucg1b7uk t41 = gm_l1;
        gm_b16_8 t42 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t37 + 16, t42);
        gm_tabb710d9mq2e t43;
        gm_f_format_mq7e94u((void *)&t43, (void *)NULL, cx, NULL, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcj10g653a00lt, 376, 12);
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        uint8_t t46 = 0;
        F.ret = t45;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_less_nbfe87l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_less_nbfe87l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* x */
    gm_b16_8 gm_l4; /* y */
    gm_ttovhfee8mf28 gm_l5; /* info */
    unsigned __int128 gm_l6; /* a */
    unsigned __int128 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l12; /* fa */
    gm_b16_8 gm_l13; /* fb */
    uint64_t gm_l16; /* k */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l20; /* xs */
    gm_b16_8 gm_l21; /* ys */
    gm_tcmkompbl09il gm_l22; /* u */
    gm_ttovhfee8mf28 gm_l23; /* info */
    uint64_t gm_l24; /* n */
    unsigned __int128 gm_l25; /* a */
    unsigned __int128 gm_l26; /* c */
    gm_t88rcucg1b7uk gm_l27; /* u */
    GM_STACK_CHECK(&F, 3030u);
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
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma6:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t19 != ((uint8_t)0ULL)) goto pa18;
        goto po17;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t21 != ((uint8_t)1ULL)) goto pa20;
        goto po17;
pa20:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t23 != ((uint8_t)2ULL)) goto pa22;
        goto po17;
pa22:;
        goto mn16;
po17:;
        {
            void * t25 = gm_l0;
            gm_t88rcucg1b7uk t26 = gm_l2;
            gm_tg5lql6itufk7 t27 = gm_f_int_info_ulcevdv(t25, GM_CONV(gm_csguankko4lmhd, t26));
            gm_ttovhfee8mf28 t28;
            if (((((uint8_t *)&t27))[8] != 0)) {
                gm_ttovhfee8mf28 t29 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t27) + 0));
                t28 = t29;
            } else {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            gm_l5 = t28;
        }
        {
            void * t30 = gm_l0;
            gm_ttovhfee8mf28 t31 = gm_l5;
            gm_b16_8 t32 = gm_l3;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            unsigned __int128 t34 = gm_f_rd_int_iuioodt(t30, t31, t33);
            gm_l6 = t34;
        }
        {
            void * t35 = gm_l0;
            gm_ttovhfee8mf28 t36 = gm_l5;
            gm_b16_8 t37 = gm_l4;
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            unsigned __int128 t39 = gm_f_rd_int_iuioodt(t35, t36, t38);
            gm_l7 = t39;
        }
        {
            gm_t1ec8br9ddkav t40;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (!t42) goto ie41;
            unsigned __int128 t44 = gm_l6;
            __int128 t45;
            memset(&t45, 0, sizeof t45); memcpy(&t45, ((uint8_t *)&t44), sizeof t45 < 16 ? sizeof t45 : 16);
            unsigned __int128 t46 = gm_l7;
            __int128 t47;
            memset(&t47, 0, sizeof t47); memcpy(&t47, ((uint8_t *)&t46), sizeof t47 < 16 ? sizeof t47 : 16);
            uint8_t t48 = (t45 < t47);
            uint8_t t49 = t48;
            gm_t1ec8br9ddkav t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t50 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
            ((uint8_t *)&t50)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t50 + 8), t49);
            t40 = t50;
            goto ix41;
ie41:;
            unsigned __int128 t52 = gm_l6;
            unsigned __int128 t53 = gm_l7;
            uint8_t t54 = (t52 < t53);
            uint8_t t55 = t54;
            gm_t1ec8br9ddkav t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t56 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t56 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t56 + 24, 0);
            ((uint8_t *)&t56)[0] = 0;
            GM_ST(uint8_t, ((uint8_t *)&t56 + 8), t55);
            t40 = t56;
ix41:;
            uint8_t t57 = ((((uint8_t *)&t40))[0] != 0);
            F.ret = t40;
            goto gm_fret;
        }
        goto me5;
mn16:;
ma7:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t59 != ((uint8_t)3ULL)) goto mn58;
        void * t60 = gm_l0;
        gm_t88rcucg1b7uk t61 = gm_l2;
        gm_b16_8 t62 = gm_l3;
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        double t64 = gm_f_get_float_grmhlm4(t60, GM_CONV(gm_csguankko4lmhd, t61), t63);
        void * t65 = gm_l0;
        gm_t88rcucg1b7uk t66 = gm_l2;
        gm_b16_8 t67 = gm_l4;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        double t69 = gm_f_get_float_grmhlm4(t65, GM_CONV(gm_csguankko4lmhd, t66), t68);
        uint8_t t70 = (t64 < t69);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t70);
        goto gm_fdone;
        goto me5;
mn58:;
ma8:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t72 != ((uint8_t)4ULL)) goto mn71;
        uint64_t t73 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t73 >= (uintptr_t)(t74)) gm_trap_index(t73, t74, gm_srcj10g653a00lt, 246, 25);
        uint8_t t75 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t73 * 1));
        uint64_t t76 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcj10g653a00lt, 246, 32);
        uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t76 * 1));
        uint8_t t79 = (t75 < t78);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t79);
        goto gm_fdone;
        goto me5;
mn71:;
ma9:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t83 != ((uint8_t)8ULL)) goto pa82;
        goto po81;
pa82:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t85 != ((uint8_t)9ULL)) goto pa84;
        goto po81;
pa84:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t87 != ((uint8_t)7ULL)) goto pa86;
        goto po81;
pa86:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t89 != ((uint8_t)16ULL)) goto pa88;
        goto po81;
pa88:;
        goto mn80;
po81:;
        void * t90 = gm_l0;
        gm_b16_8 t91 = gm_l3;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
        uint64_t t93 = gm_f_ptr_of_b77b4c9(t90, t92);
        void * t94 = gm_l0;
        gm_b16_8 t95 = gm_l4;
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        uint64_t t97 = gm_f_ptr_of_b77b4c9(t94, t96);
        uint8_t t98 = (t93 < t97);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t98);
        goto gm_fdone;
        goto me5;
mn80:;
ma10:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t100 != ((uint8_t)12ULL)) goto mn99;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)&t4) + 8) + 0));
        gm_l8 = t101;
        {
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                gm_l9 = t104;
            }
            {
                for (;;) {
                    uint64_t t106 = gm_l10;
                    uint64_t t107 = gm_l9;
                    uint8_t t108 = (t106 < t107);
                    if (!t108) goto lb105;
                    {
                        uint64_t t111 = gm_l10;
                        uint64_t t114 = GM_LD(uint64_t, ((uint8_t *)&t111));
                        gm_l11 = t114;
                        {
                            void * t116 = gm_l0;
                            gm_t88rcucg1b7uk t117 = gm_l2;
                            gm_b16_8 t118 = gm_l3;
                            uint64_t t119 = gm_l11;
                            uint32_t t120 = (uint32_t)t119;
                            gm_b16_8 t121 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t116, t117, t118, t120);
                            if (gm_unw.kind) {
                                goto gm_ll109;
                            }
                            gm_l12 = t121;
                        }
                        {
                            void * t122 = gm_l0;
                            gm_t88rcucg1b7uk t123 = gm_l2;
                            gm_b16_8 t124 = gm_l4;
                            uint64_t t125 = gm_l11;
                            uint32_t t126 = (uint32_t)t125;
                            gm_b16_8 t127 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t122, t123, t124, t126);
                            if (gm_unw.kind) {
                                goto gm_ll109;
                            }
                            gm_l13 = t127;
                        }
                        {
                            void * t129 = gm_l0;
                            gm_tl874t2atnlh3 t130 = gm_l1;
                            uint64_t t131 = gm_l11;
                            uint64_t t132 = (uintptr_t)t131;
                            uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            if (t132 >= (uintptr_t)(t133)) gm_trap_index(t132, t133, gm_srcj10g653a00lt, 252, 42);
                            gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t132 * 4));
                            gm_b16_8 t135 = gm_l12;
                            gm_b16_8 t136 = gm_l13;
                            gm_t1ec8br9ddkav t137;
                            gm_f_equal_e5vf82h((void *)&t137, (void *)NULL, cx, NULL, t129, t130, t134, t135, t136);
                            if (gm_unw.kind) {
                                goto gm_ll109;
                            }
                            if (((((uint8_t *)&t137))[0] != 0)) {
                                gm_t1ec8br9ddkav t138;
                                GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                                ((uint8_t *)&t138)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t138 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t137) + 8)));
                                F.ret = t138;
                                goto gm_fret;
                            }
                            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&t137) + 8));
                            uint8_t t140 = (uint8_t)!t139;
                            if (!t140) goto ie128;
                            {
                                void * t142 = gm_l0;
                                gm_tl874t2atnlh3 t143 = gm_l1;
                                uint64_t t144 = gm_l11;
                                uint64_t t145 = (uintptr_t)t144;
                                uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                if (t145 >= (uintptr_t)(t146)) gm_trap_index(t145, t146, gm_srcj10g653a00lt, 252, 82);
                                gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t145 * 4));
                                gm_b16_8 t148 = gm_l12;
                                gm_b16_8 t149 = gm_l13;
                                gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t142, t143, t147, t148, t149);
                                if (gm_unw.kind) {
                                    goto gm_ll109;
                                }
                                goto gm_fdone;
                            }
                            goto ix128;
ie128:;
ix128:;
                        }
                        goto me112;
mn113:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 249, 13);
me112:;
                    }
lc105:;
                    {
                        uint64_t t151 = gm_l10;
                        uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                        uint64_t t154 = r152;
                        gm_l10 = t154;
                    }
                }
gm_ll109:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb105; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc105; }
                }
                goto gm_lad;
lb105:;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
            goto gm_fdone;
        }
        goto me5;
mn99:;
ma11:;
        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t158 != ((uint8_t)11ULL)) goto pa157;
        goto po156;
pa157:;
        uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t160 != ((uint8_t)10ULL)) goto pa159;
        goto po156;
pa159:;
        goto mn155;
po156:;
        {
            void * t162 = gm_l0;
            gm_tl874t2atnlh3 t163 = gm_l1;
            gm_t88rcucg1b7uk t164 = gm_l2;
            gm_b16_8 t165 = gm_l3;
            gm_tmc91gv7n9c0i t166;
            gm_f_seq_of_ifahtjs((void *)&t166, (void *)NULL, cx, NULL, t162, t163, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t166))[0] != 0)) {
                gm_t1ec8br9ddkav t167;
                GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
                ((uint8_t *)&t167)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t167 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t166) + 8)));
                F.ret = t167;
                goto gm_fret;
            }
            F.l14 = (*(gm_t2jeg3bg7sfqa *)(((uint8_t *)&t166) + 8));
        }
        {
            void * t168 = gm_l0;
            gm_tl874t2atnlh3 t169 = gm_l1;
            gm_t88rcucg1b7uk t170 = gm_l2;
            gm_b16_8 t171 = gm_l4;
            gm_tmc91gv7n9c0i t172;
            gm_f_seq_of_ifahtjs((void *)&t172, (void *)NULL, cx, NULL, t168, t169, t170, t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t172))[0] != 0)) {
                gm_t1ec8br9ddkav t173;
                GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
                ((uint8_t *)&t173)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t173 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t172) + 8)));
                F.ret = t173;
                goto gm_fret;
            }
            F.l15 = (*(gm_t2jeg3bg7sfqa *)(((uint8_t *)&t172) + 8));
        }
        {
            uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 16));
            uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 16));
            uint64_t t176 = (t174 < t175 ? t174 : t175);
            gm_l16 = t176;
        }
        {
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                uint64_t t178 = gm_l16;
                uint64_t t179 = gm_f_us_cuv01fp(t178);
                gm_l17 = t179;
            }
            {
                for (;;) {
                    uint64_t t181 = gm_l18;
                    uint64_t t182 = gm_l17;
                    uint8_t t183 = (t181 < t182);
                    if (!t183) goto lb180;
                    {
                        uint64_t t186 = gm_l18;
                        uint64_t t189 = GM_LD(uint64_t, ((uint8_t *)&t186));
                        gm_l19 = t189;
                        {
                            uint64_t t191 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 24) + 8);
                            void * t192 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 24)));
                            uint64_t t193 = gm_l19;
                            uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r195; bool o196 = __builtin_mul_overflow(t193, t194, &r195);
                            if (o196) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 261, 31);
                            uint64_t t197 = r195;
                            uint64_t t198 = (uintptr_t)t197;
                            uint64_t t199 = gm_l19;
                            uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)1ULL), &r200);
                            if (o201) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 261, 44);
                            uint64_t t202 = r200;
                            uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r204; bool o205 = __builtin_mul_overflow(t202, t203, &r204);
                            if (o205) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 261, 43);
                            uint64_t t206 = r204;
                            uint64_t t207 = (uintptr_t)t206;
                            if (t198 > t207 || t207 > (uintptr_t)t191) gm_trap_slice(t198, t207, t191, gm_srcj10g653a00lt, 261, 23);
                            gm_b16_8 t208;
                            GM_ST(void *, (uint8_t *)&t208, (void *)(((uint8_t *)t192 + t198 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(t207 - t198));
                            gm_l20 = t208;
                        }
                        {
                            uint64_t t209 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 24) + 8);
                            void * t210 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 24)));
                            uint64_t t211 = gm_l19;
                            uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r213; bool o214 = __builtin_mul_overflow(t211, t212, &r213);
                            if (o214) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 262, 31);
                            uint64_t t215 = r213;
                            uint64_t t216 = (uintptr_t)t215;
                            uint64_t t217 = gm_l19;
                            uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                            if (o219) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 262, 44);
                            uint64_t t220 = r218;
                            uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
                            uint64_t r222; bool o223 = __builtin_mul_overflow(t220, t221, &r222);
                            if (o223) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 262, 43);
                            uint64_t t224 = r222;
                            uint64_t t225 = (uintptr_t)t224;
                            if (t216 > t225 || t225 > (uintptr_t)t209) gm_trap_slice(t216, t225, t209, gm_srcj10g653a00lt, 262, 23);
                            gm_b16_8 t226;
                            GM_ST(void *, (uint8_t *)&t226, (void *)(((uint8_t *)t210 + t216 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(t225 - t216));
                            gm_l21 = t226;
                        }
                        {
                            void * t228 = gm_l0;
                            gm_tl874t2atnlh3 t229 = gm_l1;
                            gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 0));
                            gm_b16_8 t231 = gm_l20;
                            gm_b16_8 t232 = gm_l21;
                            gm_t1ec8br9ddkav t233;
                            gm_f_equal_e5vf82h((void *)&t233, (void *)NULL, cx, NULL, t228, t229, t230, t231, t232);
                            if (gm_unw.kind) {
                                goto gm_ll184;
                            }
                            if (((((uint8_t *)&t233))[0] != 0)) {
                                gm_t1ec8br9ddkav t234;
                                GM_ST(uint8_t, (uint8_t *)&t234 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t234 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
                                ((uint8_t *)&t234)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t234 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t233) + 8)));
                                F.ret = t234;
                                goto gm_fret;
                            }
                            uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&t233) + 8));
                            uint8_t t236 = (uint8_t)!t235;
                            if (!t236) goto ie227;
                            {
                                void * t238 = gm_l0;
                                gm_tl874t2atnlh3 t239 = gm_l1;
                                gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 0));
                                gm_b16_8 t241 = gm_l20;
                                gm_b16_8 t242 = gm_l21;
                                gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t238, t239, t240, t241, t242);
                                if (gm_unw.kind) {
                                    goto gm_ll184;
                                }
                                goto gm_fdone;
                            }
                            goto ix227;
ie227:;
ix227:;
                        }
                        goto me187;
mn188:;
                        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 260, 13);
me187:;
                    }
lc180:;
                    {
                        uint64_t t244 = gm_l18;
                        uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                        uint64_t t247 = r245;
                        gm_l18 = t247;
                    }
                }
gm_ll184:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb180; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc180; }
                }
                goto gm_lad;
lb180:;
            }
        }
        {
            uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 16));
            uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 16));
            uint8_t t250 = (t248 < t249);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t250);
            goto gm_fdone;
        }
        goto me5;
mn155:;
ma12:;
        uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t252 != ((uint8_t)17ULL)) goto mn251;
        {
            void * t255 = gm_l0;
            gm_t88rcucg1b7uk t256 = gm_l2;
            gm_t57euh612ligr t257;
            gm_f_union_def_h8mvf4t((void *)&t257, t255, GM_CONV(gm_csguankko4lmhd, t256));
            if (!((((uint8_t *)&t257))[24] != 0)) goto ie254;
            gm_l22 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t257) + 0));
            {
                void * t259 = gm_l0;
                gm_t88rcucg1b7uk t260 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                gm_tg5lql6itufk7 t261 = gm_f_int_info_ulcevdv(t259, GM_CONV(gm_csguankko4lmhd, t260));
                gm_ttovhfee8mf28 t262;
                if (((((uint8_t *)&t261))[8] != 0)) {
                    gm_ttovhfee8mf28 t263 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t261) + 0));
                    t262 = t263;
                } else {
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                    goto gm_fdone;
                }
                gm_l23 = t262;
            }
            {
                void * t264 = gm_l0;
                gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                uint64_t t266 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t264, t265);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t267 = gm_f_us_cuv01fp(t266);
                gm_l24 = t267;
            }
            {
                void * t268 = gm_l0;
                gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t271 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t272 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t273 = gm_l24;
                uint64_t t274 = (uintptr_t)t273;
                if (t272 > t274 || t274 > (uintptr_t)t270) gm_trap_slice(t272, t274, t270, gm_srcj10g653a00lt, 271, 39);
                gm_b16_8 t275;
                GM_ST(void *, (uint8_t *)&t275, (void *)(((uint8_t *)t271 + t272 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(t274 - t272));
                gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
                unsigned __int128 t277 = gm_f_get_int_pg7p4gc(t268, GM_CONV(gm_csguankko4lmhd, t269), t276);
                gm_l25 = t277;
            }
            {
                void * t278 = gm_l0;
                gm_t88rcucg1b7uk t279 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                uint64_t t280 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t281 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t282 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t283 = gm_l24;
                uint64_t t284 = (uintptr_t)t283;
                if (t282 > t284 || t284 > (uintptr_t)t280) gm_trap_slice(t282, t284, t280, gm_srcj10g653a00lt, 272, 39);
                gm_b16_8 t285;
                GM_ST(void *, (uint8_t *)&t285, (void *)(((uint8_t *)t281 + t282 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(t284 - t282));
                gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
                unsigned __int128 t287 = gm_f_get_int_pg7p4gc(t278, GM_CONV(gm_csguankko4lmhd, t279), t286);
                gm_l26 = t287;
            }
            {
                gm_t1ec8br9ddkav t288;
                uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                if (!t290) goto ie289;
                unsigned __int128 t292 = gm_l25;
                __int128 t293;
                memset(&t293, 0, sizeof t293); memcpy(&t293, ((uint8_t *)&t292), sizeof t293 < 16 ? sizeof t293 : 16);
                unsigned __int128 t294 = gm_l26;
                __int128 t295;
                memset(&t295, 0, sizeof t295); memcpy(&t295, ((uint8_t *)&t294), sizeof t295 < 16 ? sizeof t295 : 16);
                uint8_t t296 = (t293 < t295);
                uint8_t t297 = t296;
                gm_t1ec8br9ddkav t298;
                GM_ST(uint8_t, (uint8_t *)&t298 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t298 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t298 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t298 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t298 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t298 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t298 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t298 + 24, 0);
                ((uint8_t *)&t298)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t298 + 8), t297);
                t288 = t298;
                goto ix289;
ie289:;
                unsigned __int128 t300 = gm_l25;
                unsigned __int128 t301 = gm_l26;
                uint8_t t302 = (t300 < t301);
                uint8_t t303 = t302;
                gm_t1ec8br9ddkav t304;
                GM_ST(uint8_t, (uint8_t *)&t304 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t304 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t304 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t304 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t304 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t304 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t304 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t304 + 24, 0);
                ((uint8_t *)&t304)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t304 + 8), t303);
                t288 = t304;
ix289:;
                uint8_t t305 = ((((uint8_t *)&t288))[0] != 0);
                F.ret = t288;
                goto gm_fret;
            }
            goto ix254;
ie254:;
ix254:;
        }
        {
            void * t307 = gm_l0;
            gm_t88rcucg1b7uk t308 = gm_l2;
            gm_tkn42u7na3t81 t309 = gm_f_bits_struct_ty_22ue20s(t307, GM_CONV(gm_csguankko4lmhd, t308));
            if (!((((uint8_t *)&t309))[4] != 0)) goto ie306;
            gm_t88rcucg1b7uk t310 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t309));
            gm_l27 = t310;
            {
                void * t312 = gm_l0;
                gm_tl874t2atnlh3 t313 = gm_l1;
                gm_t88rcucg1b7uk t314 = gm_l27;
                gm_b16_8 t315 = gm_l3;
                gm_b16_8 t316 = gm_l4;
                gm_f_less_nbfe87l((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t312, t313, t314, t315, t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix306;
ie306:;
ix306:;
        }
        {
            void * t317 = gm_l0;
            gm_tl874t2atnlh3 t318 = gm_l1;
            void * t319 = gm_l0;
            void * t320 = GM_LD(void *, (((uint8_t *)t319) + 8));
            gm_t88rcucg1b7uk t321 = gm_l2;
            gm_b16_8 t322 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t320, t321);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t323;
            GM_ST(gm_b16_8, ((uint8_t *)&t323 + 0), t322);
            gm_tabb710d9mq2e t324;
            gm_f_format_vqmfrgb((void *)&t324, (void *)NULL, cx, NULL, t323);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t324))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t324) + 8, gm_srcj10g653a00lt, 276, 34);
            gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&t324) + 8));
            gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&t325));
            gm_t76123gfet73g t327;
            gm_f_trap_qh84l9d((void *)&t327, (void *)NULL, cx, NULL, t317, t318, t326);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t327);
            goto gm_fdone;
        }
        goto me5;
mn251:;
ma13:;
        void * t329 = gm_l0;
        gm_tl874t2atnlh3 t330 = gm_l1;
        void * t331 = gm_l0;
        void * t332 = GM_LD(void *, (((uint8_t *)t331) + 8));
        gm_t88rcucg1b7uk t333 = gm_l2;
        gm_b16_8 t334 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t332, t333);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t335;
        GM_ST(gm_b16_8, ((uint8_t *)&t335 + 0), t334);
        gm_tabb710d9mq2e t336;
        gm_f_format_vqmfrgb((void *)&t336, (void *)NULL, cx, NULL, t335);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t336))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t336) + 8, gm_srcj10g653a00lt, 278, 35);
        gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&t336) + 8));
        gm_b16_8 t338 = GM_LD(gm_b16_8, ((uint8_t *)&t337));
        gm_t76123gfet73g t339;
        gm_f_trap_qh84l9d((void *)&t339, (void *)NULL, cx, NULL, t329, t330, t338);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t339);
        goto gm_fdone;
        goto me5;
mn328:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 238, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_sym_6olbepb F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
    t1 = t21;
    goto me2;
mn19:;
ma4:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t23 != ((uint8_t)1ULL)) goto mn22;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
    t1 = t24;
    goto me2;
mn22:;
ma5:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s6s96daqmhcs9));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(1));
    t1 = t27;
    goto me2;
mn25:;
ma6:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t29 != ((uint8_t)3ULL)) goto mn28;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_si62ejtp0vbct));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(2));
    t1 = t30;
    goto me2;
mn28:;
ma7:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)4ULL)) goto mn31;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(1));
    t1 = t33;
    goto me2;
mn31:;
ma8:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t35 != ((uint8_t)5ULL)) goto mn34;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_siamquc19uikr));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
    t1 = t36;
    goto me2;
mn34:;
ma9:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)6ULL)) goto mn37;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s9hi985r6f61m));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
    t1 = t39;
    goto me2;
mn37:;
ma10:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t41 != ((uint8_t)7ULL)) goto mn40;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_smerqm8g793l6));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
    t1 = t42;
    goto me2;
mn40:;
ma11:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)8ULL)) goto mn43;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s2u5posfkbgnq));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
    t1 = t45;
    goto me2;
mn43:;
ma12:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t47 != ((uint8_t)9ULL)) goto mn46;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sa831sfid0hf3));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(1));
    t1 = t48;
    goto me2;
mn46:;
ma13:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)10ULL)) goto mn49;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
    t1 = t51;
    goto me2;
mn49:;
ma14:;
    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t53 != ((uint8_t)11ULL)) goto mn52;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s4g3cajt15dmi));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(2));
    t1 = t54;
    goto me2;
mn52:;
ma15:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)12ULL)) goto mn55;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sm5be4659ulci));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
    t1 = t57;
    goto me2;
mn55:;
ma16:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t59 != ((uint8_t)13ULL)) goto mn58;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s02ru83e3fffc));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(2));
    t1 = t60;
    goto me2;
mn58:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 9, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_seq_of_ifahtjs F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    uint64_t gm_l4; /* n */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint64_t gm_l7; /* esz */
    gm_t2lqu8jsj511d gm_l8; /* sp */
    uint64_t gm_l9; /* total */
    gm_b16_8 gm_l11; /* data */
    GM_STACK_CHECK(&F, 974u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t12 != ((uint8_t)11ULL)) goto mn11;
        uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l4 = t13;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 8));
        gm_l5 = t14;
        gm_t2jeg3bg7sfqa t15;
        memset((uint8_t *)&t15 + 4, 0, 4);
        gm_t88rcucg1b7uk t16 = gm_l5;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t15 + 0, t16);
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l5;
        uint64_t t19 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t20 = gm_f_us_cuv01fp(t19);
        GM_ST(uint64_t, (uint8_t *)&t15 + 8, t20);
        uint64_t t21 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t15 + 16, t21);
        gm_b16_8 t22 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 24, t22);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 0;
        GM_ST(gm_t2jeg3bg7sfqa, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8, t15);
        goto gm_fdone;
        goto me5;
mn11:;
ma7:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t24 != ((uint8_t)10ULL)) goto mn23;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l6 = t25;
        {
            void * t27 = gm_l0;
            gm_t88rcucg1b7uk t28 = gm_l6;
            uint64_t t29 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t29;
        }
        {
            void * t30 = gm_l0;
            gm_b16_8 t31 = gm_l3;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_t2lqu8jsj511d t33 = gm_f_slice_parts_1se5hu4(t30, t32);
            gm_l8 = t33;
        }
        {
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint64_t t35 = gm_l7;
            uint64_t r36; bool o37 = __builtin_mul_overflow(t34, t35, &r36);
            uint64_t t38 = r36;
            gm_l9 = t38;
        }
        {
            uint64_t t40 = gm_l7;
            uint8_t t41 = (!(t40 == ((uint64_t)0ULL)));
            if (!t41) goto ie39;
            uint64_t t42 = gm_l9;
            uint64_t t43 = gm_l7;
            if (t43 == 0) gm_trap("division by zero", gm_srcj10g653a00lt, 227, 33);
            uint64_t t44 = (uint64_t)(t42 / t43);
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t46 = (!(t44 == t45));
            if (!t46) goto ie39;
            {
                void * t48 = gm_l0;
                gm_tl874t2atnlh3 t49 = gm_l1;
                gm_b16_8 t50;
                GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sqp26euqk4rkd));
                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(15));
                gm_t76123gfet73g t51;
                gm_f_trap_qh84l9d((void *)&t51, (void *)NULL, cx, NULL, t48, t49, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8, t51);
                goto gm_fdone;
            }
            goto ix39;
ie39:;
ix39:;
        }
        {
            uint64_t t53 = gm_l9;
            uint8_t t54 = ((t53 == ((uint64_t)0ULL)));
            if (!t54) goto ie52;
            {
                gm_t2jeg3bg7sfqa t56;
                memset((uint8_t *)&t56 + 4, 0, 4);
                gm_t88rcucg1b7uk t57 = gm_l6;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t56 + 0, t57);
                uint64_t t58 = gm_l7;
                uint64_t t59 = gm_f_us_cuv01fp(t58);
                GM_ST(uint64_t, (uint8_t *)&t56 + 8, t59);
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                GM_ST(uint64_t, (uint8_t *)&t56 + 16, t60);
                gm_b16_8 t61 = gm_f_empty_slice_ijtp9lo();
                GM_ST(gm_b16_8, (uint8_t *)&t56 + 24, t61);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 0;
                GM_ST(gm_t2jeg3bg7sfqa, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8, t56);
                goto gm_fdone;
            }
            goto ix52;
ie52:;
ix52:;
        }
        {
            void * t62 = gm_l0;
            void * t63 = (void *)(((uint8_t *)t62) + 184);
            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 0));
            uint64_t t65 = gm_l9;
            gm_t934k1j7u70ct t66;
            gm_f_read_u6r4vns((void *)&t66, t63, t64, t65, ((uint64_t)1ULL));
            gm_b16_8 t67;
            if (!((((uint8_t *)&t66))[0] != 0)) {
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                t67 = t68;
            } else {
                F.l10 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t66) + 8));
                void * t69 = gm_l0;
                gm_tl874t2atnlh3 t70 = gm_l1;
                gm_t76123gfet73g t71;
                gm_f_mem_trap_ecogko0((void *)&t71, (void *)NULL, cx, NULL, t69, t70, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l10)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8, t71);
                goto gm_fdone;
            }
            gm_l11 = t67;
        }
        {
            gm_t2jeg3bg7sfqa t72;
            memset((uint8_t *)&t72 + 4, 0, 4);
            gm_t88rcucg1b7uk t73 = gm_l6;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t72 + 0, t73);
            uint64_t t74 = gm_l7;
            uint64_t t75 = gm_f_us_cuv01fp(t74);
            GM_ST(uint64_t, (uint8_t *)&t72 + 8, t75);
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            GM_ST(uint64_t, (uint8_t *)&t72 + 16, t76);
            gm_b16_8 t77 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t72 + 24, t77);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 0;
            GM_ST(gm_t2jeg3bg7sfqa, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8, t72);
            goto gm_fdone;
        }
        goto me5;
mn23:;
ma8:;
        void * t79 = gm_l0;
        gm_tl874t2atnlh3 t80 = gm_l1;
        void * t81 = gm_l0;
        void * t82 = GM_LD(void *, (((uint8_t *)t81) + 8));
        gm_t88rcucg1b7uk t83 = gm_l2;
        gm_b16_8 t84 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t85;
        GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
        gm_tabb710d9mq2e t86;
        gm_f_format_b946o7j((void *)&t86, (void *)NULL, cx, NULL, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_srcj10g653a00lt, 232, 35);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        gm_t76123gfet73g t89;
        gm_f_trap_qh84l9d((void *)&t89, (void *)NULL, cx, NULL, t79, t80, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 40, 0);
        ((uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tmc91gv7n9c0i *)gm_out) + 8, t89);
        goto gm_fdone;
        goto me5;
mn78:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 221, 5);
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
inline __attribute__((aligned(4))) void gm_f_unary_sorkbs7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, void * a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_unary_sorkbs7 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    void * gm_l3; /* x */
    void * gm_l4; /* e */
    gm_b16_8 gm_l5; /* dst */
    uint64_t gm_l6; /* n */
    gm_t51b2en4qp02s gm_l7; /* xb */
    gm_t2lqu8jsj511d gm_l8; /* mk */
    gm_b16_8 gm_l9; /* v */
    GM_STACK_CHECK(&F, 764u);
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
        {
            void * t3 = gm_l3;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
            uint64_t t5 = gm_f_us_cuv01fp(t4);
            gm_l6 = t5;
        }
        {
            uint64_t t7 = gm_l6;
            uint8_t t8 = (t7 <= ((uint64_t)16ULL));
            if (!t8) goto ie6;
            {
            }
            {
                void * t10 = gm_l0;
                gm_tbl8il92l97ut t11 = gm_l1;
                void * t12 = gm_l3;
                void * t13 = ((uint8_t *)&gm_l7);
                uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t15 = gm_l6;
                uint64_t t16 = (uintptr_t)t15;
                if (t14 > t16 || t16 > (uintptr_t)16) gm_trap_slice(t14, t16, 16, gm_srcj10g653a00lt, 162, 24);
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + t14 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16 - t14));
                gm_tcfa6l7reoosn t18;
                gm_f_ev_25jfdar((void *)&t18, (void *)NULL, cx, NULL, t10, t11, t12, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t18))[0] != 0)) {
                    gm_tcfa6l7reoosn t19;
                    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                    ((uint8_t *)&t19)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t19 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t18) + 8)));
                    F.ret = t19;
                    goto gm_fret;
                }
                gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 8));
            }
            {
                void * t21 = gm_l0;
                void * t22 = gm_l4;
                gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 112));
                gm_t9qkh0rsooc8d t24 = gm_l2;
                void * t25 = gm_l3;
                gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t25) + 96));
                void * t27 = ((uint8_t *)&gm_l7);
                uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t29 = gm_l6;
                uint64_t t30 = (uintptr_t)t29;
                if (t28 > t30 || t30 > (uintptr_t)16) gm_trap_slice(t28, t30, 16, gm_srcj10g653a00lt, 163, 41);
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t27 + t28 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30 - t28));
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                gm_b16_8 t33 = gm_l5;
                gm_f_unop_qamfooj((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t21, t23, t24, t26, t32, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            void * t34 = gm_l0;
            gm_t2lqu8jsj511d t35 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t34));
            gm_l8 = t35;
        }
        {
            void * t36 = gm_l0;
            void * t37 = gm_l3;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 104));
            gm_b16_8 t39 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t36, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t39;
        }
        {
            void * t40 = gm_l0;
            gm_tbl8il92l97ut t41 = gm_l1;
            void * t42 = gm_l3;
            gm_b16_8 t43 = gm_l9;
            gm_tcfa6l7reoosn t44;
            gm_f_ev_25jfdar((void *)&t44, (void *)NULL, cx, NULL, t40, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t44))[0] != 0)) {
                gm_tcfa6l7reoosn t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                ((uint8_t *)&t45)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t45 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t44) + 8)));
                F.ret = t45;
                goto gm_fret;
            }
            gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 8));
        }
        {
            void * t47 = gm_l0;
            void * t48 = gm_l4;
            gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 112));
            gm_t9qkh0rsooc8d t50 = gm_l2;
            void * t51 = gm_l3;
            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t51) + 96));
            gm_b16_8 t53 = gm_l9;
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_b16_8 t55 = gm_l5;
            gm_tcfa6l7reoosn t56;
            gm_f_unop_qamfooj((void *)&t56, (void *)NULL, cx, NULL, t47, t49, t50, t52, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t56))[0] != 0)) {
                gm_tcfa6l7reoosn t57;
                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                ((uint8_t *)&t57)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t57 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t56) + 8)));
                F.ret = t57;
                goto gm_fret;
            }
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 8));
        }
        {
            void * t59 = gm_l0;
            gm_t2lqu8jsj511d t60 = gm_l8;
            gm_f_release_jn5kqdi(t59, GM_CONV(gm_csc7lc6jlkqkuq, t60));
        }
    }
    gm_b0_1 t61;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t61);
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
__attribute__((aligned(4))) void gm_f_unop_qamfooj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_unop_qamfooj F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* dst */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    uint64_t gm_l8; /* esz */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_ttovhfee8mf28 gm_l12; /* info */
    unsigned __int128 gm_l13; /* v */
    __int128 gm_l14; /* x */
    uint64_t gm_l15; /* n */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* esz */
    uint8_t gm_l18; /* is_mask */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* lane */
    gm_b16_8 gm_l23; /* ds */
    gm_t88rcucg1b7uk gm_l24; /* u */
    gm_ttovhfee8mf28 gm_l25; /* info */
    GM_STACK_CHECK(&F, 2565u);
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
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t8) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; default: goto ma7; }
ma4:;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t10 != ((uint8_t)2ULL)) goto mn9;
            {
                uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcj10g653a00lt, 107, 19);
                uint8_t *t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t12 * 1);
                uint8_t t15;
                uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcj10g653a00lt, 107, 31);
                uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 1));
                uint8_t t20 = ((t19 == ((uint8_t)0ULL)));
                if (!t20) goto ie16;
                uint8_t t22 = ((uint8_t)1ULL);
                t15 = t22;
                goto ix16;
ie16:;
                uint8_t t24 = ((uint8_t)0ULL);
                t15 = t24;
ix16:;
                GM_ST(uint8_t, t14, t15);
            }
            goto me3;
mn9:;
ma5:;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t26 != ((uint8_t)0ULL)) goto mn25;
            {
                void * t29 = gm_l0;
                gm_t88rcucg1b7uk t30 = gm_l3;
                gm_t53at80pfdus3 t31;
                gm_f_kind_htv6093((void *)&t31, t29, GM_CONV(gm_csguankko4lmhd, t30));
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                if (t32 != ((uint8_t)3ULL)) goto ie28;
                {
                    void * t34 = gm_l0;
                    gm_t88rcucg1b7uk t35 = gm_l3;
                    void * t36 = gm_l0;
                    gm_t88rcucg1b7uk t37 = gm_l3;
                    gm_b16_8 t38 = gm_l4;
                    double t39 = gm_f_get_float_grmhlm4(t36, GM_CONV(gm_csguankko4lmhd, t37), t38);
                    double t40 = -t39;
                    gm_b16_8 t41 = gm_l5;
                    gm_f_put_float_fiad72d(t34, GM_CONV(gm_csguankko4lmhd, t35), t40, t41);
                }
                {
                    gm_b0_1 t42;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t42);
                    goto gm_fdone;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t44 = gm_l0;
                gm_t88rcucg1b7uk t45 = gm_l3;
                gm_t53at80pfdus3 t46;
                gm_f_kind_htv6093((void *)&t46, t44, GM_CONV(gm_csguankko4lmhd, t45));
                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t46) + 0));
                if (t47 != ((uint8_t)11ULL)) goto ie43;
                uint64_t t48 = GM_LD(uint64_t, ((((uint8_t *)&t46) + 8) + 0));
                gm_l6 = t48;
                gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t46) + 8) + 8));
                gm_l7 = t49;
                {
                    void * t51 = gm_l0;
                    gm_t88rcucg1b7uk t52 = gm_l7;
                    uint64_t t53 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t54 = gm_f_us_cuv01fp(t53);
                    gm_l8 = t54;
                }
                {
                    {
                        gm_l10 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t56 = gm_l6;
                        uint64_t t57 = gm_f_us_cuv01fp(t56);
                        gm_l9 = t57;
                    }
                    {
                        for (;;) {
                            uint64_t t59 = gm_l10;
                            uint64_t t60 = gm_l9;
                            uint8_t t61 = (t59 < t60);
                            if (!t61) goto lb58;
                            {
                                uint64_t t64 = gm_l10;
                                uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&t64));
                                gm_l11 = t67;
                                {
                                    void * t69 = gm_l0;
                                    gm_tl874t2atnlh3 t70 = gm_l1;
                                    gm_t9qkh0rsooc8d t71 = gm_l2;
                                    gm_t88rcucg1b7uk t72 = gm_l7;
                                    uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                    void * t74 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                                    uint64_t t75 = gm_l11;
                                    uint64_t t76 = gm_l8;
                                    uint64_t r77; bool o78 = __builtin_mul_overflow(t75, t76, &r77);
                                    if (o78) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 66);
                                    uint64_t t79 = r77;
                                    uint64_t t80 = (uintptr_t)t79;
                                    uint64_t t81 = gm_l11;
                                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                                    if (o83) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 115, 76);
                                    uint64_t t84 = r82;
                                    uint64_t t85 = gm_l8;
                                    uint64_t r86; bool o87 = __builtin_mul_overflow(t84, t85, &r86);
                                    if (o87) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 75);
                                    uint64_t t88 = r86;
                                    uint64_t t89 = (uintptr_t)t88;
                                    if (t80 > t89 || t89 > (uintptr_t)t73) gm_trap_slice(t80, t89, t73, gm_srcj10g653a00lt, 115, 64);
                                    gm_b16_8 t90;
                                    GM_ST(void *, (uint8_t *)&t90, (void *)(((uint8_t *)t74 + t80 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(t89 - t80));
                                    uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                    void * t92 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                                    uint64_t t93 = gm_l11;
                                    uint64_t t94 = gm_l8;
                                    uint64_t r95; bool o96 = __builtin_mul_overflow(t93, t94, &r95);
                                    if (o96) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 95);
                                    uint64_t t97 = r95;
                                    uint64_t t98 = (uintptr_t)t97;
                                    uint64_t t99 = gm_l11;
                                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                                    if (o101) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 115, 105);
                                    uint64_t t102 = r100;
                                    uint64_t t103 = gm_l8;
                                    uint64_t r104; bool o105 = __builtin_mul_overflow(t102, t103, &r104);
                                    if (o105) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 115, 104);
                                    uint64_t t106 = r104;
                                    uint64_t t107 = (uintptr_t)t106;
                                    if (t98 > t107 || t107 > (uintptr_t)t91) gm_trap_slice(t98, t107, t91, gm_srcj10g653a00lt, 115, 91);
                                    gm_b16_8 t108;
                                    GM_ST(void *, (uint8_t *)&t108, (void *)(((uint8_t *)t92 + t98 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(t107 - t98));
                                    gm_tcfa6l7reoosn t109;
                                    gm_f_unop_qamfooj((void *)&t109, (void *)NULL, cx, NULL, t69, t70, t71, t72, t90, t108);
                                    if (gm_unw.kind) {
                                        goto gm_ll62;
                                    }
                                    if (((((uint8_t *)&t109))[0] != 0)) {
                                        gm_tcfa6l7reoosn t110;
                                        GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                                        ((uint8_t *)&t110)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t110 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t109) + 8)));
                                        F.ret = t110;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 8));
                                }
                                goto me65;
mn66:;
                                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 115, 17);
me65:;
                            }
lc58:;
                            {
                                uint64_t t113 = gm_l10;
                                uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                                uint64_t t116 = r114;
                                gm_l10 = t116;
                            }
                        }
gm_ll62:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb58; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc58; }
                        }
                        goto gm_lad;
lb58:;
                    }
                }
                {
                    gm_b0_1 t117;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t117);
                    goto gm_fdone;
                }
                goto ix43;
ie43:;
ix43:;
            }
            {
                void * t118 = gm_l0;
                gm_t88rcucg1b7uk t119 = gm_l3;
                gm_tg5lql6itufk7 t120 = gm_f_int_info_ulcevdv(t118, GM_CONV(gm_csguankko4lmhd, t119));
                gm_ttovhfee8mf28 t121;
                if (((((uint8_t *)&t120))[8] != 0)) {
                    gm_ttovhfee8mf28 t122 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t120) + 0));
                    t121 = t122;
                } else {
                    void * t123 = gm_l0;
                    gm_tl874t2atnlh3 t124 = gm_l1;
                    void * t125 = gm_l0;
                    void * t126 = GM_LD(void *, (((uint8_t *)t125) + 8));
                    gm_t88rcucg1b7uk t127 = gm_l3;
                    gm_b16_8 t128 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t126, t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t129;
                    GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
                    gm_tabb710d9mq2e t130;
                    gm_f_format_vno937p((void *)&t130, (void *)NULL, cx, NULL, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_srcj10g653a00lt, 118, 61);
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                    gm_t76123gfet73g t133;
                    gm_f_trap_qh84l9d((void *)&t133, (void *)NULL, cx, NULL, t123, t124, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t133);
                    goto gm_fdone;
                }
                gm_l12 = t121;
            }
            {
                void * t134 = gm_l0;
                gm_t88rcucg1b7uk t135 = gm_l3;
                gm_b16_8 t136 = gm_l4;
                unsigned __int128 t137 = gm_f_get_int_pg7p4gc(t134, GM_CONV(gm_csguankko4lmhd, t135), t136);
                gm_l13 = t137;
            }
            {
                uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                if (!t139) goto ie138;
                {
                    unsigned __int128 t141 = gm_l13;
                    __int128 t142;
                    memset(&t142, 0, sizeof t142); memcpy(&t142, ((uint8_t *)&t141), sizeof t142 < 16 ? sizeof t142 : 16);
                    gm_l14 = t142;
                }
                {
                    __int128 t144 = gm_l14;
                    uint32_t t145 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                    __int128 t146 = gm_f_int_lo_i8jbm6e(t145);
                    uint8_t t147 = ((t144 == t146));
                    if (!t147) goto ie143;
                    {
                        void * t149 = gm_l0;
                        gm_tl874t2atnlh3 t150 = gm_l1;
                        gm_b16_8 t151;
                        GM_ST(void *, (uint8_t *)&t151, (void *)(gm_skolmnfc0ccbd));
                        GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(1));
                        gm_tcfa6l7reoosn t152;
                        gm_f_checked_overflow_ma1bm1g((void *)&t152, (void *)NULL, cx, NULL, t149, t150, t151);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t152))[0] != 0)) {
                            gm_tcfa6l7reoosn t153;
                            GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
                            ((uint8_t *)&t153)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t153 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t152) + 8)));
                            F.ret = t153;
                            goto gm_fret;
                        }
                        gm_b0_1 t154 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 8));
                    }
                    {
                        void * t155 = gm_l0;
                        gm_b16_8 t156 = gm_l5;
                        unsigned __int128 t157 = gm_l13;
                        gm_f_put_uint_cddin99(t155, t156, t157);
                    }
                    {
                        gm_b0_1 t158;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t158);
                        goto gm_fdone;
                    }
                    goto ix143;
ie143:;
ix143:;
                }
                {
                    void * t159 = gm_l0;
                    gm_b16_8 t160 = gm_l5;
                    __int128 t161 = gm_l14;
                    if (t161 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcj10g653a00lt, 127, 34);
                    __int128 t162 = (__int128)((unsigned __int128)0 - (unsigned __int128)t161);
                    unsigned __int128 t163;
                    memset(&t163, 0, sizeof t163); memcpy(&t163, ((uint8_t *)&t162), sizeof t163 < 16 ? sizeof t163 : 16);
                    gm_f_put_uint_cddin99(t159, t160, t163);
                }
                goto ix138;
ie138:;
                unsigned __int128 t165 = gm_l13;
                uint8_t t166 = ((t165 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (!t166) goto ie164;
                {
                    gm_b16_8 t168 = gm_l5;
                    gm_f_fill_abmf5g5(t168, ((uint8_t)0ULL));
                }
                goto ix164;
ie164:;
                {
                    void * t170 = gm_l0;
                    gm_tl874t2atnlh3 t171 = gm_l1;
                    gm_b16_8 t172;
                    GM_ST(void *, (uint8_t *)&t172, (void *)(gm_skolmnfc0ccbd));
                    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(1));
                    gm_tcfa6l7reoosn t173;
                    gm_f_checked_overflow_ma1bm1g((void *)&t173, (void *)NULL, cx, NULL, t170, t171, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t173))[0] != 0)) {
                        gm_tcfa6l7reoosn t174;
                        GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                        ((uint8_t *)&t174)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t174 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t173) + 8)));
                        F.ret = t174;
                        goto gm_fret;
                    }
                    gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 8));
                }
                {
                    void * t176 = gm_l0;
                    gm_b16_8 t177 = gm_l5;
                    uint32_t t178 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                    unsigned __int128 t179 = gm_l13;
                    unsigned __int128 r180; bool o181 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t179, &r180);
                    unsigned __int128 t182 = r180;
                    unsigned __int128 t183 = gm_f_num_ext_348kmbu(((uint8_t)0), t178, t182);
                    gm_f_put_uint_cddin99(t176, t177, t183);
                }
ix164:;
ix138:;
            }
            goto me3;
mn25:;
ma6:;
            uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t185 != ((uint8_t)1ULL)) goto mn184;
            {
                void * t188 = gm_l0;
                gm_t88rcucg1b7uk t189 = gm_l3;
                gm_t53at80pfdus3 t190;
                gm_f_kind_htv6093((void *)&t190, t188, GM_CONV(gm_csguankko4lmhd, t189));
                uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&t190) + 0));
                if (t191 != ((uint8_t)11ULL)) goto ie187;
                uint64_t t192 = GM_LD(uint64_t, ((((uint8_t *)&t190) + 8) + 0));
                gm_l15 = t192;
                gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t190) + 8) + 8));
                gm_l16 = t193;
                {
                    void * t195 = gm_l0;
                    gm_t88rcucg1b7uk t196 = gm_l16;
                    uint64_t t197 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t195, t196);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t198 = gm_f_us_cuv01fp(t197);
                    gm_l17 = t198;
                }
                {
                    void * t199 = gm_l0;
                    gm_t88rcucg1b7uk t200 = gm_l16;
                    gm_t53at80pfdus3 t201;
                    gm_f_kind_htv6093((void *)&t201, t199, GM_CONV(gm_csguankko4lmhd, t200));
                    uint8_t t202 = ((GM_LD(uint8_t, ((uint8_t *)&t201) + 0) == ((uint8_t)4ULL)));
                    gm_l18 = t202;
                }
                {
                    {
                        gm_l20 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t204 = gm_l15;
                        uint64_t t205 = gm_f_us_cuv01fp(t204);
                        gm_l19 = t205;
                    }
                    {
                        for (;;) {
                            uint64_t t207 = gm_l20;
                            uint64_t t208 = gm_l19;
                            uint8_t t209 = (t207 < t208);
                            if (!t209) goto lb206;
                            {
                                uint64_t t212 = gm_l20;
                                uint64_t t215 = GM_LD(uint64_t, ((uint8_t *)&t212));
                                gm_l21 = t215;
                                {
                                    uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                    void * t218 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                                    uint64_t t219 = gm_l21;
                                    uint64_t t220 = gm_l17;
                                    uint64_t r221; bool o222 = __builtin_mul_overflow(t219, t220, &r221);
                                    if (o222) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 140, 31);
                                    uint64_t t223 = r221;
                                    uint64_t t224 = (uintptr_t)t223;
                                    uint64_t t225 = gm_l21;
                                    uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                                    if (o227) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 140, 41);
                                    uint64_t t228 = r226;
                                    uint64_t t229 = gm_l17;
                                    uint64_t r230; bool o231 = __builtin_mul_overflow(t228, t229, &r230);
                                    if (o231) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 140, 40);
                                    uint64_t t232 = r230;
                                    uint64_t t233 = (uintptr_t)t232;
                                    if (t224 > t233 || t233 > (uintptr_t)t217) gm_trap_slice(t224, t233, t217, gm_srcj10g653a00lt, 140, 29);
                                    gm_b16_8 t234;
                                    GM_ST(void *, (uint8_t *)&t234, (void *)(((uint8_t *)t218 + t224 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(t233 - t224));
                                    gm_l22 = t234;
                                }
                                {
                                    uint64_t t235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                    void * t236 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                                    uint64_t t237 = gm_l21;
                                    uint64_t t238 = gm_l17;
                                    uint64_t r239; bool o240 = __builtin_mul_overflow(t237, t238, &r239);
                                    if (o240) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 141, 31);
                                    uint64_t t241 = r239;
                                    uint64_t t242 = (uintptr_t)t241;
                                    uint64_t t243 = gm_l21;
                                    uint64_t r244; bool o245 = __builtin_add_overflow(t243, ((uint64_t)1ULL), &r244);
                                    if (o245) gm_trap("integer overflow in `+`", gm_srcj10g653a00lt, 141, 41);
                                    uint64_t t246 = r244;
                                    uint64_t t247 = gm_l17;
                                    uint64_t r248; bool o249 = __builtin_mul_overflow(t246, t247, &r248);
                                    if (o249) gm_trap("integer overflow in `*`", gm_srcj10g653a00lt, 141, 40);
                                    uint64_t t250 = r248;
                                    uint64_t t251 = (uintptr_t)t250;
                                    if (t242 > t251 || t251 > (uintptr_t)t235) gm_trap_slice(t242, t251, t235, gm_srcj10g653a00lt, 141, 27);
                                    gm_b16_8 t252;
                                    GM_ST(void *, (uint8_t *)&t252, (void *)(((uint8_t *)t236 + t242 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(t251 - t242));
                                    gm_l23 = t252;
                                }
                                {
                                    uint8_t t254 = gm_l18;
                                    if (!t254) goto ie253;
                                    {
                                        uint64_t t256 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                        if (t256 >= (uintptr_t)(t257)) gm_trap_index(t256, t257, gm_srcj10g653a00lt, 143, 25);
                                        uint8_t *t258 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t256 * 1);
                                        uint8_t t259;
                                        uint64_t t261 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                        if (t261 >= (uintptr_t)(t262)) gm_trap_index(t261, t262, gm_srcj10g653a00lt, 143, 36);
                                        uint8_t t263 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t261 * 1));
                                        uint8_t t264 = ((t263 == ((uint8_t)0ULL)));
                                        if (!t264) goto ie260;
                                        uint8_t t266 = ((uint8_t)1ULL);
                                        t259 = t266;
                                        goto ix260;
ie260:;
                                        uint8_t t268 = ((uint8_t)0ULL);
                                        t259 = t268;
ix260:;
                                        GM_ST(uint8_t, t258, t259);
                                    }
                                    goto ix253;
ie253:;
                                    {
                                        void * t270 = gm_l0;
                                        gm_tl874t2atnlh3 t271 = gm_l1;
                                        gm_t9qkh0rsooc8d t272 = gm_l2;
                                        gm_t88rcucg1b7uk t273 = gm_l16;
                                        gm_b16_8 t274 = gm_l22;
                                        gm_b16_8 t275 = gm_l23;
                                        gm_tcfa6l7reoosn t276;
                                        gm_f_unop_qamfooj((void *)&t276, (void *)NULL, cx, NULL, t270, t271, t272, t273, t274, t275);
                                        if (gm_unw.kind) {
                                            goto gm_ll210;
                                        }
                                        if (((((uint8_t *)&t276))[0] != 0)) {
                                            gm_tcfa6l7reoosn t277;
                                            GM_ST(uint8_t, (uint8_t *)&t277 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t277 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t277 + 4, 0);
                                            ((uint8_t *)&t277)[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t277 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t276) + 8)));
                                            F.ret = t277;
                                            goto gm_fret;
                                        }
                                        gm_b0_1 t278 = GM_LD(gm_b0_1, (((uint8_t *)&t276) + 8));
                                    }
ix253:;
                                }
                                goto me213;
mn214:;
                                gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 139, 17);
me213:;
                            }
lc206:;
                            {
                                uint64_t t280 = gm_l20;
                                uint64_t r281; bool o282 = __builtin_add_overflow(t280, ((uint64_t)1ULL), &r281);
                                uint64_t t283 = r281;
                                gm_l20 = t283;
                            }
                        }
gm_ll210:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb206; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc206; }
                        }
                        goto gm_lad;
lb206:;
                    }
                }
                {
                    gm_b0_1 t284;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t284);
                    goto gm_fdone;
                }
                goto ix187;
ie187:;
ix187:;
            }
            {
                void * t285 = gm_l0;
                gm_t88rcucg1b7uk t286 = gm_l3;
                gm_tkn42u7na3t81 t287 = gm_f_bits_struct_ty_22ue20s(t285, GM_CONV(gm_csguankko4lmhd, t286));
                gm_t88rcucg1b7uk t288;
                if (((((uint8_t *)&t287))[4] != 0)) {
                    gm_t88rcucg1b7uk t289 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t287) + 0));
                    t288 = t289;
                } else {
                    gm_t88rcucg1b7uk t290 = gm_l3;
                    t288 = t290;
                }
                gm_l24 = t288;
            }
            {
                void * t291 = gm_l0;
                gm_t88rcucg1b7uk t292 = gm_l24;
                gm_tg5lql6itufk7 t293 = gm_f_int_info_ulcevdv(t291, GM_CONV(gm_csguankko4lmhd, t292));
                gm_ttovhfee8mf28 t294;
                if (((((uint8_t *)&t293))[8] != 0)) {
                    gm_ttovhfee8mf28 t295 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t293) + 0));
                    t294 = t295;
                } else {
                    void * t296 = gm_l0;
                    gm_tl874t2atnlh3 t297 = gm_l1;
                    void * t298 = gm_l0;
                    void * t299 = GM_LD(void *, (((uint8_t *)t298) + 8));
                    gm_t88rcucg1b7uk t300 = gm_l24;
                    gm_b16_8 t301 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t299, t300);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t302;
                    GM_ST(gm_b16_8, ((uint8_t *)&t302 + 0), t301);
                    gm_tabb710d9mq2e t303;
                    gm_f_format_gd3p27d((void *)&t303, (void *)NULL, cx, NULL, t302);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 8, gm_srcj10g653a00lt, 151, 61);
                    gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&t303) + 8));
                    gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                    gm_t76123gfet73g t306;
                    gm_f_trap_qh84l9d((void *)&t306, (void *)NULL, cx, NULL, t296, t297, t305);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t306);
                    goto gm_fdone;
                }
                gm_l25 = t294;
            }
            {
                void * t307 = gm_l0;
                gm_b16_8 t308 = gm_l5;
                uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                uint32_t t310 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 4));
                void * t311 = gm_l0;
                gm_t88rcucg1b7uk t312 = gm_l24;
                gm_b16_8 t313 = gm_l4;
                unsigned __int128 t314 = gm_f_get_int_pg7p4gc(t311, GM_CONV(gm_csguankko4lmhd, t312), t313);
                unsigned __int128 t315 = (unsigned __int128)~t314;
                unsigned __int128 t316 = gm_f_num_ext_348kmbu(t309, t310, t315);
                gm_f_put_uint_cddin99(t307, t308, t316);
            }
            goto me3;
mn184:;
ma7:;
            gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 106, 5);
me3:;
        }
    }
    gm_b0_1 t317;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t317);
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

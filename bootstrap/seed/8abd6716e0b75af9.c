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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; gm_tl874t2atnlh3 f3; } gm_tgk5j0pob2pqa;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
typedef struct { gm_t5r2drn9pjqkt f0; uint8_t f1; uint8_t pe[7]; } gm_tm4iv6anh1b9a;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_b16_8 f0; gm_tv2uf4leil5hq f1; } gm_taiafe1jasg4m;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgk5j0pob2pqa) == 24 && _Alignof(gm_tgk5j0pob2pqa) == 4 && offsetof(gm_tgk5j0pob2pqa, f0) == 0 && offsetof(gm_tgk5j0pob2pqa, f1) == 4 && offsetof(gm_tgk5j0pob2pqa, f2) == 8 && offsetof(gm_tgk5j0pob2pqa, f3) == 12, "layout of `LabelBreak`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tm4iv6anh1b9a) == 32 && _Alignof(gm_tm4iv6anh1b9a) == 8 && offsetof(gm_tm4iv6anh1b9a, f0) == 0 && offsetof(gm_tm4iv6anh1b9a, f1) == 24, "layout of `?PendingBody`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t08olq97rscn3 f1; } gm_tp9ftj4p31ha1;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
typedef struct { gm_toceffb697p0u f0; uint8_t f1; uint8_t pe[3]; } gm_trj4m8pa99qp7;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { gm_b16_8 f0; gm_tjsp7bhdn2e8j f1; uint8_t p2[5]; void * f2; } gm_t4d1qrgh0vnq1;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_taiafe1jasg4m) == 80 && _Alignof(gm_taiafe1jasg4m) == 8 && offsetof(gm_taiafe1jasg4m, f0) == 0 && offsetof(gm_taiafe1jasg4m, f1) == 16, "layout of `(attrs: []Attribute, binding: Binding)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tp9ftj4p31ha1) == 40 && _Alignof(gm_tp9ftj4p31ha1) == 8 && offsetof(gm_tp9ftj4p31ha1, f0) == 0 && offsetof(gm_tp9ftj4p31ha1, f1) == 8, "layout of `(fn_id: u32, name: Ident)`");
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trj4m8pa99qp7) == 20 && _Alignof(gm_trj4m8pa99qp7) == 4 && offsetof(gm_trj4m8pa99qp7, f0) == 0 && offsetof(gm_trj4m8pa99qp7, f1) == 16, "layout of `?NeedsAt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_thdmr9qicg35r f2; uint8_t pe[4]; } gm_tti6qnvf9jg1p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_ttja7ii61nila;
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t4d1qrgh0vnq1) == 32 && _Alignof(gm_t4d1qrgh0vnq1) == 8 && offsetof(gm_t4d1qrgh0vnq1, f0) == 0 && offsetof(gm_t4d1qrgh0vnq1, f1) == 16 && offsetof(gm_t4d1qrgh0vnq1, f2) == 24, "layout of `(places: []Expr, op: AssignOp, value: ^Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
typedef struct { gm_ttja7ii61nila f0; uint8_t f1; uint8_t pe[7]; } gm_t9t5bnrf784qu;
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tti6qnvf9jg1p) == 72 && _Alignof(gm_tti6qnvf9jg1p) == 8 && offsetof(gm_tti6qnvf9jg1p, f0) == 0 && offsetof(gm_tti6qnvf9jg1p, f1) == 40 && offsetof(gm_tti6qnvf9jg1p, f2) == 56, "layout of `InferTable`");
_Static_assert(sizeof(gm_ttja7ii61nila) == 48 && _Alignof(gm_ttja7ii61nila) == 8 && offsetof(gm_ttja7ii61nila, f0) == 0 && offsetof(gm_ttja7ii61nila, f1) == 8, "layout of `IfElse`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { gm_t0d02142k4tu8 f0; void * f1; void * f2; } gm_t15nkjiutlvf3;
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { gm_b16_8 f0; gm_t9t5bnrf784qu f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t99g4dkn8rrfb;
_Static_assert(sizeof(gm_t9t5bnrf784qu) == 56 && _Alignof(gm_t9t5bnrf784qu) == 8 && offsetof(gm_t9t5bnrf784qu, f0) == 0 && offsetof(gm_t9t5bnrf784qu, f1) == 48, "layout of `?IfElse`");
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t15nkjiutlvf3) == 80 && _Alignof(gm_t15nkjiutlvf3) == 8 && offsetof(gm_t15nkjiutlvf3, f0) == 0 && offsetof(gm_t15nkjiutlvf3, f1) == 64 && offsetof(gm_t15nkjiutlvf3, f2) == 72, "layout of `(pat: Pattern, value: ^Expr, fallback: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_b16_8 v1; } f1; } gm_t4t568o7qnv02;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_t99g4dkn8rrfb) == 88 && _Alignof(gm_t99g4dkn8rrfb) == 8 && offsetof(gm_t99g4dkn8rrfb, f0) == 0 && offsetof(gm_t99g4dkn8rrfb, f1) == 16 && offsetof(gm_t99g4dkn8rrfb, f2) == 72, "layout of `IfExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_taiafe1jasg4m v0; gm_t4d1qrgh0vnq1 v1; void * v2; gm_tgtub0v94d5u5 v3; void * v4; gm_t43d6nbdfap6p v5; void * v6; gm_t15nkjiutlvf3 v7; gm_t79rbdv4rafe5 v8; } f1; } gm_tem8p0t66u1ce;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t f6; uint8_t p7[3]; uint64_t f7; gm_t9805qtonbsag f8; uint8_t f9; uint8_t f10; uint8_t p11[2]; gm_tkn42u7na3t81 f11; uint8_t p12[4]; gm_t403eokj7ifg9 f12; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; } gm_tjhr0bdm69ios;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
typedef struct { void * f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_tti6qnvf9jg1p f4; gm_t88rcucg1b7uk f5; uint8_t p6[4]; gm_tnpifv20irnph f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t p10[4]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_t9805qtonbsag f13; gm_trj4m8pa99qp7 f14; uint8_t p15[4]; gm_trqfvmjtt4sij f15; uint8_t f16; uint8_t p17[7]; gm_trqfvmjtt4sij f17; uint8_t f18; uint8_t p19[7]; uint64_t f19; gm_trqfvmjtt4sij f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; uint8_t f24; uint8_t p25[7]; gm_trqfvmjtt4sij f25; uint8_t f26; uint8_t p27[3]; gm_tkn42u7na3t81 f27; uint8_t p28[4]; gm_trqfvmjtt4sij f28; gm_t403eokj7ifg9 f29; gm_trqfvmjtt4sij f30; gm_trqfvmjtt4sij f31; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; uint8_t f38; uint8_t p39[7]; gm_trqfvmjtt4sij f39; uint8_t f40; uint8_t p41[7]; void * f41; } gm_tqgtfeqqnkjsa;
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
_Static_assert(sizeof(gm_t4t568o7qnv02) == 208 && _Alignof(gm_t4t568o7qnv02) == 8 && offsetof(gm_t4t568o7qnv02, f0) == 0 && offsetof(gm_t4t568o7qnv02, f1) == 8, "layout of `UntypedOr`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_tjhr0bdm69ios f0; uint8_t f1; uint8_t pe[7]; } gm_ta7fvse6v13pf;
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
_Static_assert(sizeof(gm_tem8p0t66u1ce) == 120 && _Alignof(gm_tem8p0t66u1ce) == 8 && offsetof(gm_tem8p0t66u1ce, f0) == 0 && offsetof(gm_tem8p0t66u1ce, f1) == 8, "layout of `StmtKind`");
typedef struct { gm_t930q5l38hdap f0; uint32_t f1; uint8_t pe[4]; } gm_tfj8gdjm726n4;
_Static_assert(sizeof(gm_tjhr0bdm69ios) == 384 && _Alignof(gm_tjhr0bdm69ios) == 8 && offsetof(gm_tjhr0bdm69ios, f0) == 0 && offsetof(gm_tjhr0bdm69ios, f1) == 40 && offsetof(gm_tjhr0bdm69ios, f2) == 48 && offsetof(gm_tjhr0bdm69ios, f3) == 64 && offsetof(gm_tjhr0bdm69ios, f4) == 104 && offsetof(gm_tjhr0bdm69ios, f5) == 144 && offsetof(gm_tjhr0bdm69ios, f6) == 148 && offsetof(gm_tjhr0bdm69ios, f7) == 152 && offsetof(gm_tjhr0bdm69ios, f8) == 160 && offsetof(gm_tjhr0bdm69ios, f9) == 168 && offsetof(gm_tjhr0bdm69ios, f10) == 169 && offsetof(gm_tjhr0bdm69ios, f11) == 172 && offsetof(gm_tjhr0bdm69ios, f12) == 184 && offsetof(gm_tjhr0bdm69ios, f13) == 304 && offsetof(gm_tjhr0bdm69ios, f14) == 344, "layout of `Frame`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_b16_8 f0; gm_t930q5l38hdap f1; } gm_tmhicb6huo2ra;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tem8p0t66u1ce f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to88ae1b67aev;
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tqgtfeqqnkjsa) == 1304 && _Alignof(gm_tqgtfeqqnkjsa) == 8 && offsetof(gm_tqgtfeqqnkjsa, f0) == 0 && offsetof(gm_tqgtfeqqnkjsa, f1) == 8 && offsetof(gm_tqgtfeqqnkjsa, f2) == 16 && offsetof(gm_tqgtfeqqnkjsa, f3) == 56 && offsetof(gm_tqgtfeqqnkjsa, f4) == 96 && offsetof(gm_tqgtfeqqnkjsa, f5) == 168 && offsetof(gm_tqgtfeqqnkjsa, f6) == 176 && offsetof(gm_tqgtfeqqnkjsa, f7) == 192 && offsetof(gm_tqgtfeqqnkjsa, f8) == 232 && offsetof(gm_tqgtfeqqnkjsa, f9) == 272 && offsetof(gm_tqgtfeqqnkjsa, f10) == 280 && offsetof(gm_tqgtfeqqnkjsa, f11) == 320 && offsetof(gm_tqgtfeqqnkjsa, f12) == 360 && offsetof(gm_tqgtfeqqnkjsa, f13) == 400 && offsetof(gm_tqgtfeqqnkjsa, f14) == 408 && offsetof(gm_tqgtfeqqnkjsa, f15) == 432 && offsetof(gm_tqgtfeqqnkjsa, f16) == 472 && offsetof(gm_tqgtfeqqnkjsa, f17) == 480 && offsetof(gm_tqgtfeqqnkjsa, f18) == 520 && offsetof(gm_tqgtfeqqnkjsa, f19) == 528 && offsetof(gm_tqgtfeqqnkjsa, f20) == 536 && offsetof(gm_tqgtfeqqnkjsa, f21) == 576 && offsetof(gm_tqgtfeqqnkjsa, f22) == 616 && offsetof(gm_tqgtfeqqnkjsa, f23) == 656 && offsetof(gm_tqgtfeqqnkjsa, f24) == 696 && offsetof(gm_tqgtfeqqnkjsa, f25) == 704 && offsetof(gm_tqgtfeqqnkjsa, f26) == 744 && offsetof(gm_tqgtfeqqnkjsa, f27) == 748 && offsetof(gm_tqgtfeqqnkjsa, f28) == 760 && offsetof(gm_tqgtfeqqnkjsa, f29) == 800 && offsetof(gm_tqgtfeqqnkjsa, f30) == 920 && offsetof(gm_tqgtfeqqnkjsa, f31) == 960 && offsetof(gm_tqgtfeqqnkjsa, f32) == 1000 && offsetof(gm_tqgtfeqqnkjsa, f33) == 1040 && offsetof(gm_tqgtfeqqnkjsa, f34) == 1080 && offsetof(gm_tqgtfeqqnkjsa, f35) == 1120 && offsetof(gm_tqgtfeqqnkjsa, f36) == 1160 && offsetof(gm_tqgtfeqqnkjsa, f37) == 1200 && offsetof(gm_tqgtfeqqnkjsa, f38) == 1240 && offsetof(gm_tqgtfeqqnkjsa, f39) == 1248 && offsetof(gm_tqgtfeqqnkjsa, f40) == 1288 && offsetof(gm_tqgtfeqqnkjsa, f41) == 1296, "layout of `FnCx`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_ta7fvse6v13pf) == 392 && _Alignof(gm_ta7fvse6v13pf) == 8 && offsetof(gm_ta7fvse6v13pf, f0) == 0 && offsetof(gm_ta7fvse6v13pf, f1) == 384, "layout of `?Frame`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
_Static_assert(sizeof(gm_tfj8gdjm726n4) == 216 && _Alignof(gm_tfj8gdjm726n4) == 8 && offsetof(gm_tfj8gdjm726n4, f0) == 0 && offsetof(gm_tfj8gdjm726n4, f1) == 208, "layout of `(binding: LocalBinding, depth: u32)`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tmhicb6huo2ra) == 224 && _Alignof(gm_tmhicb6huo2ra) == 8 && offsetof(gm_tmhicb6huo2ra, f0) == 0 && offsetof(gm_tmhicb6huo2ra, f1) == 16, "layout of `Binding`");
_Static_assert(sizeof(gm_to88ae1b67aev) == 144 && _Alignof(gm_to88ae1b67aev) == 8 && offsetof(gm_to88ae1b67aev, f0) == 0 && offsetof(gm_to88ae1b67aev, f1) == 8 && offsetof(gm_to88ae1b67aev, f2) == 128, "layout of `Stmt`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
typedef struct { gm_tfj8gdjm726n4 f0; uint8_t f1; uint8_t pe[7]; } gm_tsf6kp4ohnuqs;
_Static_assert(sizeof(gm_tsf6kp4ohnuqs) == 224 && _Alignof(gm_tsf6kp4ohnuqs) == 8 && offsetof(gm_tsf6kp4ohnuqs, f0) == 0 && offsetof(gm_tsf6kp4ohnuqs, f1) == 216, "layout of `?(binding: LocalBinding, depth: u32)`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_block_expected_bv4ukm2 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* stmts */
    gm_tnvnsr1bfvtbd l7; /* cause */
    gm_t4gut2c9fj1kt l13; /* d */
    gm_t0d02142k4tu8 l15; /* pat */
    gm_t79rbdv4rafe5 l19; /* rest */
    gm_trqfvmjtt4sij l20; /* fb_stmts */
    gm_t79rbdv4rafe5 l21; /* fb */
    gm_trqfvmjtt4sij l22; /* parts */
    gm_trqfvmjtt4sij l23; /* branches */
    void * ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_typed_e7svg7n {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_check_bodies_qrmq5ll {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* core */
    gm_b0_1 ret;
};
struct gm_fr_check_fn_body_kc8b374 {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_trqfvmjtt4sij l6; /* numeric */
    gm_td7pkamr64l0c l10; /* outer */
    gm_tqgtfeqqnkjsa l11; /* cx */
    gm_tp9ftj4p31ha1 l17; /* g */
    gm_trqfvmjtt4sij l18; /* params */
    gm_t5v0c8ia6a5mo l21; /* p */
    gm_b0_1 ret;
};
struct gm_fr_clear_vns7pn2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_cond_99ajlhk {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_tq5j0547p8452 l5; /* p */
    gm_t0d02142k4tu8 l8; /* pat */
    gm_b16_8 ret;
};
struct gm_fr_declare_3l5mhoj {
    gm_frame h;
    gm_t930q5l38hdap l2; /* b */
    gm_t0ialelq3ql1k l4; /* v */
    gm_ts78ca2amthgu l5; /* c */
    gm_ts78ca2amthgu l6; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_declare_temps_a7fc14v {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stmts */
    void * ret;
};
struct gm_fr_defer_stmt_hm15mru {
    gm_frame h;
    gm_tj44oivvogvtl l10; /* p */
    gm_tv0k2vg3054a6 l14; /* o */
    gm_trqfvmjtt4sij l16; /* stmts */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_depth_fr7pn6n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_early_fallback_error_4lq49mb {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3g747ka {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7athnlt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3p9jvl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fdfu2r0 {
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
struct gm_fr_empty_slice_jvoelso {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kgfehvr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lgqbbc4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_li9u7hg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lio7tu0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ojer7i1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oldiiq9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p5lo8ed_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tcmn71c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u0hn964 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vefkkhp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vhibpt8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_fn_vo5kel1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
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
struct gm_fr_exit_fn_kj6vbu8 {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* f */
    gm_tk9n3vlila9qg l2; /* out */
    gm_tk9n3vlila9qg ret;
};
struct gm_fr_expect_bool_cdo5g1q {
    gm_frame h;
    gm_t53at80pfdus3 l2; /* k */
    void * ret;
};
struct gm_fr_finish_block_body_4vol3tv {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* stmts */
    void * ret;
};
struct gm_fr_finish_m8t0599 {
    gm_frame h;
    gm_tl93vjpcesj48 l7; /* n */
    gm_b0_1 ret;
};
struct gm_fr_fn_body_37q3l3a {
    gm_frame h;
    gm_t79rbdv4rafe5 l4; /* b */
    void * ret;
};
struct gm_fr_fn_cx_hgvq15e {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* scopes */
    gm_trqfvmjtt4sij l5; /* depths */
    gm_tqgtfeqqnkjsa ret;
};
struct gm_fr_forget_label_breaks_vl4mn72 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_infer_table_6o0km8j {
    gm_frame h;
    gm_tti6qnvf9jg1p ret;
};
struct gm_fr_is_bool_ajhjgeg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_leave_module_scope_4hp5mc4 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0bg6tcp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0ouok33 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5tqpoea {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6dnse27 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7ltkg0u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_88khgsi {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_clrolmo {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_d0n30gc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_e6ie1fq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fs7kvfg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fu87ckm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g3vmkan_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lgereu6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rp32tbu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_t7903ef {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_taqq58r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tmk0paj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_frame_r6k3kmp {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_local_at_p388mm4 {
    gm_frame h;
    gm_tsoie1ppgdvcg ret;
};
struct gm_fr_lookup_local_6f38t7p {
    gm_frame h;
    gm_tqvu78rpi6q1h ret;
};
struct gm_fr_lookup_local_at_ntm4scs {
    gm_frame h;
    gm_tsf6kp4ohnuqs ret;
};
struct gm_fr_missing_return_s12s32d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_never_cause_p5ir1i2 {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_tnvnsr1bfvtbd ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_new_label_3207tcb {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_new_local_003h9es {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_note_capture_0p0af7r {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_pop_f3uajlg_2 {
    gm_frame h;
    gm_tjhr0bdm69ios l2; /* x */
    gm_ta7fvse6v13pf ret;
};
struct gm_fr_pop_lnq3sua {
    gm_frame h;
    gm_tm4iv6anh1b9a ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_scope_mu7j3lg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_temps_upm64pn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_save_frame_8libfct {
    gm_frame h;
    gm_tjhr0bdm69ios ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
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
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_stmt_4145gsn {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* binding */
    gm_tkacitepg5fk0 l6; /* a */
    gm_t43d6nbdfap6p l11; /* w */
    gm_t79rbdv4rafe5 l12; /* b */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_stmt_diverges_en1hv79 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stmt_expr_evs4ic8 {
    gm_frame h;
    gm_t79rbdv4rafe5 l3; /* b */
    void * ret;
};
struct gm_fr_stmt_never_cause_r2lf089 {
    gm_frame h;
    gm_tnvnsr1bfvtbd ret;
};
struct gm_fr_swap_frame_4j63sdr {
    gm_frame h;
    gm_tjhr0bdm69ios l1; /* f */
    gm_tjhr0bdm69ios l2; /* out */
    gm_tjhr0bdm69ios ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_truncate_eoppkeo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_try_untyped_8j8up35 {
    gm_frame h;
    gm_t930q5l38hdap l3; /* b */
    gm_t0ialelq3ql1k l4; /* v */
    gm_t5vbkdq81qfs7 l7; /* r */
    gm_t0ialelq3ql1k l8; /* v */
    gm_t0ialelq3ql1k l9; /* x */
    gm_t5vbkdq81qfs7 l14; /* a */
    gm_t5vbkdq81qfs7 l15; /* b */
    gm_t0ialelq3ql1k l16; /* x */
    gm_t0ialelq3ql1k l17; /* y */
    gm_t0ialelq3ql1k l18; /* v */
    gm_tkv1vap90vht2 ret;
};
struct gm_fr_uninit_message_u2c3peu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unscope_type_params_bofhhep {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_untyped_op_7an9o22 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_when_stmt_3pk7eon {
    gm_frame h;
    gm_t79rbdv4rafe5 l3; /* b */
    void * ret;
};
struct gm_fr_while_loop_86vcgbb {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_assign_a0848vq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tjsp7bhdn2e8j a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_binary_9sb7899(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binding_cond_v86td9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binding_igkhmn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_body_when_cond_2coms3p(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_cancel_stmt_pfi7e6b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bodies_qrmq5ll(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_effects_mrg0sal(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_body_kc8b374(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pending_handlers_o_sunakri(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_vns7pn2(void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_cond_99ajlhk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_declare_temps_a7fc14v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_defer_stmt_hm15mru(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
__attribute__((aligned(4))) void gm_f_early_fallback_error_4lq49mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3g747ka(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7athnlt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3p9jvl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fdfu2r0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lgqbbc4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_li9u7hg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lio7tu0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojer7i1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u0hn964(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vefkkhp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tk9n3vlila9qg gm_f_exit_fn_kj6vbu8_c(void * a0);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_expr_rk98p2r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finalize_k1g9q48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_finish_block_body_4vol3tv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_fn_body_37q3l3a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void * gm_f_for_loop_b6hjsbq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_05vut39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_07fj847(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9i45r57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_biupho2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cn0m7al(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ggjb732(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n7840ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p8e3d4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q32bjcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r1s7qdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s8qs6vn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_if_expr_tiak47t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void gm_f_infer_table_6o0km8j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0ouok33(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5tqpoea(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6dnse27(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7ltkg0u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_88khgsi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_clrolmo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_d0n30gc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fs7kvfg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fu87ckm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lgereu6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rp32tbu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t7903ef(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_taqq58r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tmk0paj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp_c(void * a0, gm_tjhr0bdm69ios a1);
__attribute__((aligned(4))) void gm_f_local_at_p388mm4(void *gm_out, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tsoie1ppgdvcg gm_f_local_at_p388mm4_c(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqvu78rpi6q1h gm_f_lookup_local_6f38t7p_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_lookup_local_at_ntm4scs(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tsf6kp4ohnuqs gm_f_lookup_local_at_ntm4scs_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_match_expr_npcoo60(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_missing_return_s12s32d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_negate_fohael7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_never_cause_p5ir1i2(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_never_cause_p5ir1i2_c(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ulodmu9(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_note_capture_0p0af7r(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_2(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_lnq3sua(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_00ljs05(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0ie7e7i(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_8h1r7s6_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_lng5b06(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nr0n00f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o022ckl(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ola4v1l(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s39fiuo(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_temps_upm64pn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_33kejrs(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_remove_te9updc(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_remove_v59hrnl(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_save_frame_8libfct(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_save_frame_8libfct_c(void * a0);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_4145gsn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79_c(void * a0, gm_trcmmd12cefb1 a1);
__attribute__((aligned(4))) void * gm_f_stmt_expr_evs4ic8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_never_cause_r2lf089(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_stmt_never_cause_r2lf089_c(void * a0, gm_trcmmd12cefb1 a1);
inline __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_swap_frame_4j63sdr_c(void * a0, gm_tjhr0bdm69ios a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_untyped_op_7an9o22(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_when_stmt_3pk7eon(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_while_loop_86vcgbb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_with_expr_vi78dtj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k2rd7nce5lv[40] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k8qpjmtnqct[40] __attribute__((aligned(8)));
extern const uint8_t gm_s069kf53uhd4o[45];
extern const uint8_t gm_s0rfpnv4c3k1l[38];
extern const uint8_t gm_s7b7j2m1f6fle[71];
extern const uint8_t gm_s846hbv2ad5li[17];
extern const uint8_t gm_s993c5b6eq922[54];
extern const uint8_t gm_sadg245agtuao[45];
extern const uint8_t gm_sb3sf0ef436fi[40];
extern const uint8_t gm_seiql9tkbkhim[52];
extern const uint8_t gm_ses9h0rg965i4[54];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_she92i1u1a4v5[30];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sjeqed5d3f5pb[26];
extern const uint8_t gm_sju4s3ndpnsge[107];
extern const uint8_t gm_sm9otrmuq5hp3[34];
extern const uint8_t gm_souep3pirutur[55];
extern const uint8_t gm_sp6l5tr58ip76[23];
extern const uint8_t gm_sqc0827akcmi8[40];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src276s8986o4r3[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sroje34o1kfta[38];
extern const uint8_t gm_ss6qrj79r47rb[70];
extern const uint8_t gm_sscfkpmei7e4o[29];
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
    GM_STACK_CHECK(&F, 116u);
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
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = gm_l2;
        void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_block_q09sn1p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    uint8_t t3 = gm_l2;
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    void * t5 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t1, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_clear_vns7pn2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_vns7pn2 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_depth_fr7pn6n F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 576) + 0) + 8);
    uint32_t t3 = (uint32_t)t2;
    F.ret = t3;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3g747ka(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3g747ka F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7athnlt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7athnlt F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3p9jvl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3p9jvl F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fdfu2r0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fdfu2r0 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jvoelso F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kgfehvr F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lgqbbc4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lgqbbc4 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_li9u7hg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_li9u7hg F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lio7tu0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lio7tu0 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojer7i1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ojer7i1 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oldiiq9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p5lo8ed_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p5lo8ed_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 384)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tcmn71c F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u0hn964(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u0hn964 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vefkkhp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vefkkhp F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vhibpt8 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_module_scope_011vo5a F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 0, (((uint8_t *)t2) + 1568), 40);
        void * t3 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 40, (((uint8_t *)t3) + 1608), 40);
    }
    {
        void * t4 = gm_l0;
        uint8_t *t5 = (((uint8_t *)t4) + 1568);
        gm_f_list_1jabh22((void *)&(*(gm_trqfvmjtt4sij *)t5), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        uint8_t *t7 = (((uint8_t *)t6) + 1608);
        gm_f_list_0bj26n7((void *)&(*(gm_trqfvmjtt4sij *)t7), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 80);
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
inline __attribute__((aligned(4))) void gm_f_infer_table_6o0km8j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_infer_table_6o0km8j F;
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 68, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_e6ie1fq((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
    gm_thdmr9qicg35r t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 0, t1);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 40, t2);
    GM_ST(gm_thdmr9qicg35r, (uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 56, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_bool_ajhjgeg F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
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
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)4ULL)));
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 139u);
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
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
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
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_module_scope_4hp5mc4 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 0));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 1568), t3);
    }
    {
        void * t4 = gm_l0;
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 40));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t4) + 1608), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bg6tcp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_kgfehvr();
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
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_0ouok33(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0ouok33 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_tcmn71c();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_5tqpoea(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5tqpoea F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vefkkhp();
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
inline __attribute__((aligned(4))) void gm_f_list_6dnse27(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6dnse27 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lgqbbc4();
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
inline __attribute__((aligned(4))) void gm_f_list_7ltkg0u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7ltkg0u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vhibpt8();
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
inline __attribute__((aligned(4))) void gm_f_list_88khgsi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_88khgsi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qm5tp1g();
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
inline __attribute__((aligned(4))) void gm_f_list_clrolmo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_clrolmo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_li9u7hg();
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
inline __attribute__((aligned(4))) void gm_f_list_d0n30gc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_d0n30gc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c3p9jvl();
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
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_e6ie1fq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oldiiq9();
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
inline __attribute__((aligned(4))) void gm_f_list_fs7kvfg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fs7kvfg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_7athnlt();
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
inline __attribute__((aligned(4))) void gm_f_list_fu87ckm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fu87ckm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3g747ka();
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
inline __attribute__((aligned(4))) void gm_f_list_g3vmkan_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g3vmkan_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_p5lo8ed_2();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_lgereu6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lgereu6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_u0hn964();
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
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p9s2t99 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6trg8r7();
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
inline __attribute__((aligned(4))) void gm_f_list_rp32tbu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rp32tbu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jvoelso();
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
inline __attribute__((aligned(4))) void gm_f_list_t7903ef(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t7903ef F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lio7tu0();
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
inline __attribute__((aligned(4))) void gm_f_list_taqq58r(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_taqq58r F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ojer7i1();
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
inline __attribute__((aligned(4))) void gm_f_list_tmk0paj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tmk0paj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fdfu2r0();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lmbdkhb();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lmbdkhb_2();
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
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t66j1im9ip128 t29 = gm_l2;
        gm_td8hfgklo8dil t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 36, 0);
        GM_ST(gm_t66j1im9ip128, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[32] = 1;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_trqfvmjtt4sij t29 = F.l2;
        gm_tqetrgo0q24b5 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[40] = 1;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_f3uajlg_2(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_f3uajlg_2 F;
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
            gm_ta7fvse6v13pf t8;
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
        memmove(&F.l2, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 384), sizeof F.l2);
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)t18) + 0) + 8);
        void * t20 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t18) + 0)));
        uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t22 = gm_l1;
        uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
        if (o24) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t25 = r23;
        uint64_t t26 = (uintptr_t)t25;
        if (t21 > t26 || t26 > (uintptr_t)t19) gm_trap_slice(t21, t26, t19, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t20 + t21 * 384)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t21));
        GM_ST(gm_b16_8, (((uint8_t *)t17) + 0), t27);
    }
    {
        gm_tjhr0bdm69ios t28 = F.l2;
        gm_ta7fvse6v13pf t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 385, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 386, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 388, 0);
        GM_ST(gm_tjhr0bdm69ios, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[384] = 1;
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_lnq3sua(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_lnq3sua F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t5r2drn9pjqkt gm_l2; /* x */
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
            gm_tm4iv6anh1b9a t8;
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
        gm_t5r2drn9pjqkt t17 = GM_LD(gm_t5r2drn9pjqkt, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 24));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t5r2drn9pjqkt t29 = gm_l2;
        gm_tm4iv6anh1b9a t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 28, 0);
        GM_ST(gm_t5r2drn9pjqkt, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[24] = 1;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_ruvva9t F;
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
            gm_te5gbhu3us3k4 t8;
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
        gm_t8enl5g7b22jb t17 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t8enl5g7b22jb t29 = F.l2;
        gm_te5gbhu3us3k4 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 52, 0);
        GM_ST(gm_t8enl5g7b22jb, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[48] = 1;
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
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 272u);
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
        gm_td8hfgklo8dil t14 = t13;
        if (!((((uint8_t *)&t14))[32] != 0)) goto ie10;
        gm_t66j1im9ip128 t15 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)&t14));
        gm_l1 = t15;
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_f_restore_scope_edqq1v8(t18, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 1488) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1608) + 0) + 8);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1568) + 0) + 8);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0, t2);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8, t4);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16, t6);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24, t8);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_swap_frame_4j63sdr F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_save_frame_8libfct((void *)&F.l2, t2);
    }
    {
        void * t3 = gm_l0;
        gm_f_load_frame_r6k3kmp(t3, &(*(gm_tjhr0bdm69ios *)((uint8_t *)a1)));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 384);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_eoppkeo F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 4)));
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
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
    gm_t88rcucg1b7uk t3 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unscope_type_params_bofhhep F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_restore_scope_edqq1v8(t2, &(*(gm_t66j1im9ip128 *)((uint8_t *)a1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 133u);
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
        gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_tl874t2atnlh3 t9 = gm_l1;
        void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t11);
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
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_block_expected_bv4ukm2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    gm_tkn42u7na3t81 gm_l3; /* expected */
    void * gm_l5; /* tail */
    uint8_t gm_l6; /* diverges */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    void * gm_l12; /* s */
    gm_tbgd14esf0jvi gm_l14; /* c */
    void * gm_l16; /* v */
    void * gm_l17; /* fallback */
    gm_tl874t2atnlh3 gm_l18; /* rest_span */
    void * gm_l24; /* lowered */
    uint64_t gm_l25; /* mark */
    void * gm_l26; /* ex */
    gm_trl2khmuphncd gm_l27; /* x */
    void * gm_l28; /* e */
    gm_tmn7n0n9raou5 gm_l29; /* kind */
    void * gm_l30; /* inner */
    gm_t88rcucg1b7uk gm_l31; /* fallback_ty */
    uint64_t gm_l32; /* k */
    gm_tl874t2atnlh3 gm_l33; /* ospan */
    void * gm_l34; /* e */
    void * gm_l35; /* ex */
    uint64_t gm_l36; /* mark */
    gm_trcmmd12cefb1 gm_l37; /* st */
    void * gm_l38; /* t */
    gm_t88rcucg1b7uk gm_l39; /* ty */
    GM_STACK_CHECK(&F, 3385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l5 = ((void *)0);
    }
    {
        gm_l6 = ((uint8_t)0);
    }
    {
        gm_tnvnsr1bfvtbd t4;
        memset(&t4, 0, sizeof t4);
        F.l7 = t4;
    }
    {
        void * t5 = gm_l1;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 8) + 8);
        gm_l8 = t6;
    }
    {
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            uint64_t t8 = gm_l8;
            gm_l9 = t8;
        }
        {
            for (;;) {
                uint64_t t10 = gm_l10;
                uint64_t t11 = gm_l9;
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l10;
                    uint64_t t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t16));
                    gm_l11 = t19;
                    {
                        uint64_t t21 = gm_l11;
                        uint64_t t22 = (uintptr_t)t21;
                        void * t23 = gm_l1;
                        uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 8) + 8);
                        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_src276s8986o4r3, 728, 15);
                        void * t25 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t23) + 8)) + t22 * 144);
                        gm_l12 = t25;
                    }
                    {
                        uint8_t t27 = gm_l6;
                        if (!t27) goto ie26;
                        {
                            void * t29 = gm_l12;
                            gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 128));
                            gm_b16_8 t31;
                            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s846hbv2ad5li));
                            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(16));
                            gm_f_error_s6hn871((void *)&F.l13, (void *)NULL, cx, NULL, t30, t31);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                        }
                        {
                            if (!((((uint8_t *)&F.l7))[32] != 0)) goto ie32;
                            gm_tbgd14esf0jvi t33 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)&F.l7));
                            gm_l14 = t33;
                            {
                                void * t35 = (void *)((uint8_t *)&F.l13);
                                gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l14) + 0));
                                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 16));
                                gm_f_add_label_50fj7vv(t35, GM_CONV(gm_cseahvg8v7p0vs, t36), t37);
                            }
                            goto ix32;
ie32:;
ix32:;
                        }
                        {
                            void * t38 = gm_l0;
                            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
                            gm_b0_1 t40 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t39, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l13)));
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                        }
                        {
                            goto lb9;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        void * t42 = gm_l12;
                        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                        if (t43 != ((uint8_t)7ULL)) goto ie41;
                        gm_t0d02142k4tu8 t44 = GM_LD(gm_t0d02142k4tu8, (((((uint8_t *)t42) + 8) + 8) + 0));
                        F.l15 = t44;
                        void * t45 = GM_LD(void *, (((((uint8_t *)t42) + 8) + 8) + 64));
                        gm_l16 = t45;
                        void * t46 = GM_LD(void *, (((((uint8_t *)t42) + 8) + 8) + 72));
                        gm_l17 = t46;
                        {
                            gm_tl874t2atnlh3 t48;
                            uint64_t t50 = gm_l11;
                            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                            if (o52) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 737, 29);
                            uint64_t t53 = r51;
                            uint64_t t54 = gm_l8;
                            uint8_t t55 = (t53 < t54);
                            if (!t55) goto ie49;
                            uint64_t t57 = gm_l11;
                            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                            if (o59) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 737, 49);
                            uint64_t t60 = r58;
                            uint64_t t61 = (uintptr_t)t60;
                            void * t62 = gm_l1;
                            uint64_t t63 = GM_LD(uintptr_t, (((uint8_t *)t62) + 8) + 8);
                            if (t61 >= (uintptr_t)(t63)) gm_trap_index(t61, t63, gm_src276s8986o4r3, 737, 41);
                            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t62) + 8)) + t61 * 144) + 128));
                            void * t65 = gm_l1;
                            uint32_t t66 = GM_LD(uint32_t, ((((uint8_t *)t65) + 24) + 0));
                            void * t67 = gm_l1;
                            uint32_t t68 = GM_LD(uint32_t, ((((uint8_t *)t67) + 24) + 8));
                            void * t69 = gm_l1;
                            uint32_t t70 = GM_LD(uint32_t, ((((uint8_t *)t69) + 24) + 8));
                            gm_tl874t2atnlh3 t71;
                            GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t66);
                            GM_ST(uint32_t, ((uint8_t *)&t71 + 4), t68);
                            GM_ST(uint32_t, ((uint8_t *)&t71 + 8), t70);
                            gm_tl874t2atnlh3 t72 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t64), GM_CONV(gm_cseahvg8v7p0vs, t71)));
                            gm_tl874t2atnlh3 t73 = t72;
                            t48 = t73;
                            goto ix49;
ie49:;
                            void * t75 = gm_l12;
                            uint32_t t76 = GM_LD(uint32_t, ((((uint8_t *)t75) + 128) + 0));
                            void * t77 = gm_l12;
                            uint32_t t78 = GM_LD(uint32_t, ((((uint8_t *)t77) + 128) + 8));
                            void * t79 = gm_l12;
                            uint32_t t80 = GM_LD(uint32_t, ((((uint8_t *)t79) + 128) + 8));
                            gm_tl874t2atnlh3 t81;
                            GM_ST(uint32_t, ((uint8_t *)&t81 + 0), t76);
                            GM_ST(uint32_t, ((uint8_t *)&t81 + 4), t78);
                            GM_ST(uint32_t, ((uint8_t *)&t81 + 8), t80);
                            gm_tl874t2atnlh3 t82 = t81;
                            t48 = t82;
ix49:;
                            gm_l18 = t48;
                        }
                        {
                            memset((uint8_t *)&F.l19 + 4, 0, 4);
                            memset((uint8_t *)&F.l19 + 36, 0, 4);
                            void * t83 = gm_l1;
                            uint32_t t84 = GM_LD(uint32_t, (((uint8_t *)t83) + 0));
                            GM_ST(uint32_t, (uint8_t *)&F.l19 + 0, t84);
                            void * t85 = gm_l1;
                            uint64_t t86 = GM_LD(uintptr_t, (((uint8_t *)t85) + 8) + 8);
                            void * t87 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t85) + 8)));
                            uint64_t t88 = gm_l11;
                            uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                            if (o90) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 738, 56);
                            uint64_t t91 = r89;
                            uint64_t t92 = (uintptr_t)t91;
                            if (t92 > (uintptr_t)t86 || (uintptr_t)t86 > (uintptr_t)t86) gm_trap_slice(t92, (uintptr_t)t86, t86, gm_src276s8986o4r3, 738, 48);
                            gm_b16_8 t93;
                            GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t87 + t92 * 144)));
                            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)((uintptr_t)t86 - t92));
                            GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 8, t93);
                            gm_tl874t2atnlh3 t94 = gm_l18;
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l19 + 24, t94);
                        }
                        {
                            gm_f_list_ir0h3tc((void *)&F.l20, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                        }
                        {
                            void * t95 = (void *)((uint8_t *)&F.l20);
                            gm_to88ae1b67aev t96;
                            memset((uint8_t *)&t96 + 4, 0, 4);
                            memset((uint8_t *)&t96 + 140, 0, 4);
                            void * t97 = gm_l12;
                            uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)t97) + 0));
                            GM_ST(uint32_t, (uint8_t *)&t96 + 0, t98);
                            void * t99 = gm_l17;
                            gm_tem8p0t66u1ce t100;
                            memset(&t100, 0, sizeof t100);
                            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)2ULL));
                            GM_ST(void *, ((uint8_t *)&t100 + 8), t99);
                            GM_ST(gm_tem8p0t66u1ce, (uint8_t *)&t96 + 8, t100);
                            void * t101 = gm_l17;
                            gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t101) + 72));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t96 + 128, t102);
                            gm_tftb1odc739t5 t103 = gm_f_push_d6c76ud(t95, &t96);
                            gm_tftb1odc739t5 t104 = t103;
                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_src276s8986o4r3, 740, 13);
                            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                        }
                        {
                            memset((uint8_t *)&F.l21 + 4, 0, 4);
                            memset((uint8_t *)&F.l21 + 36, 0, 4);
                            void * t106 = gm_l1;
                            uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)t106) + 0));
                            GM_ST(uint32_t, (uint8_t *)&F.l21 + 0, t107);
                            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&F.l21 + 8, t108);
                            void * t109 = gm_l17;
                            gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 72));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l21 + 24, t110);
                        }
                        {
                            gm_f_list_v5d1o9b((void *)&F.l22, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                        }
                        {
                            void * t111 = (void *)((uint8_t *)&F.l22);
                            gm_tuqaojgkqdcop t112;
                            memcpy((uint8_t *)&t112 + 0, ((uint8_t *)&F.l15), 64);
                            void * t113 = gm_l16;
                            GM_ST(void *, (uint8_t *)&t112 + 64, t113);
                            gm_tq5j0547p8452 t114;
                            GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_tuqaojgkqdcop, ((uint8_t *)&t114 + 8), t112);
                            gm_tftb1odc739t5 t115 = gm_f_push_eb7kbhp(t111, &t114);
                            gm_tftb1odc739t5 t116 = t115;
                            if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_src276s8986o4r3, 743, 13);
                            gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
                        }
                        {
                            gm_f_list_taqq58r((void *)&F.l23, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                        }
                        {
                            void * t118 = (void *)((uint8_t *)&F.l23);
                            gm_tcuumjkg5eni6 t119;
                            memset((uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t119 + 0)) + 28, 0, 4);
                            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t119 + 0)) + 0, t120);
                            gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l15) + 48));
                            void * t122 = gm_l16;
                            gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 72));
                            gm_tl874t2atnlh3 t124 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t121), GM_CONV(gm_cseahvg8v7p0vs, t123)));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t119 + 0)) + 16, t124);
                            memcpy((uint8_t *)&t119 + 32, ((uint8_t *)&F.l19), 40);
                            gm_tftb1odc739t5 t125 = gm_f_push_0ie7e7i(t118, &t119);
                            gm_tftb1odc739t5 t126 = t125;
                            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_src276s8986o4r3, 745, 13);
                            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                        }
                        {
                            gm_t99g4dkn8rrfb t128;
                            memset((uint8_t *)&t128 + 84, 0, 4);
                            gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t128 + 0, t129);
                            gm_t79rbdv4rafe5 t130 = F.l21;
                            gm_ttja7ii61nila t131;
                            GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t131 + 8), t130);
                            gm_t9t5bnrf784qu t132;
                            GM_ST(uint8_t, (uint8_t *)&t132 + 49, 0);
                            GM_ST(uint16_t, (uint8_t *)&t132 + 50, 0);
                            GM_ST(uint32_t, (uint8_t *)&t132 + 52, 0);
                            GM_ST(gm_ttja7ii61nila, ((uint8_t *)&t132), t131);
                            ((uint8_t *)&t132)[48] = 1;
                            GM_ST(gm_t9t5bnrf784qu, (uint8_t *)&t128 + 16, t132);
                            void * t133 = gm_l12;
                            gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t133) + 128));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t128 + 72, t134);
                            gm_tq7s98ihonkju t135 = gm_f_new_ulodmu9((void *)NULL, cx, NULL, &t128, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2rd7nce5lv)));
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_tq7s98ihonkju t136 = t135;
                            if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 8, gm_src276s8986o4r3, 746, 24);
                            void * t137 = GM_LD(void *, (((uint8_t *)&t136) + 8));
                            gm_l24 = t137;
                        }
                        {
                            void * t138 = gm_l0;
                            uint64_t t139 = GM_LD(uintptr_t, ((((uint8_t *)t138) + 1200) + 0) + 8);
                            gm_l25 = t139;
                        }
                        {
                            void * t140 = gm_l0;
                            void * t141 = gm_l24;
                            uint8_t t142 = gm_l2;
                            gm_tkn42u7na3t81 t143 = gm_l3;
                            void * t144 = gm_f_if_expr_tiak47t((void *)NULL, cx, NULL, t140, t141, t142, t143);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_l26 = t144;
                        }
                        {
                            uint8_t t146 = gm_l2;
                            uint8_t t147 = (uint8_t)!t146;
                            if (!t147) goto ie145;
                            {
                                void * t149 = gm_l0;
                                uint64_t t150 = gm_l25;
                                void * t151 = (void *)((uint8_t *)&F.l4);
                                gm_b0_1 t152 = gm_f_push_temps_upm64pn((void *)NULL, cx, NULL, t149, t150, t151);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                            }
                            goto ix145;
ie145:;
ix145:;
                        }
                        {
                            void * t153 = gm_l0;
                            void * t154 = GM_LD(void *, (((uint8_t *)t153) + 0));
                            void * t155 = (void *)(((uint8_t *)t154) + 8);
                            void * t156 = gm_l26;
                            gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t156) + 0));
                            gm_t53at80pfdus3 t158;
                            gm_f_kind_jcnt4hm((void *)&t158, t155, GM_CONV(gm_csguankko4lmhd, t157));
                            gm_t53at80pfdus3 t159 = t158;
                            uint8_t t160 = ((GM_LD(uint8_t, ((uint8_t *)&t159) + 0) == ((uint8_t)5ULL)));
                            gm_l6 = t160;
                        }
                        {
                            void * t162 = gm_l26;
                            uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t162) + 8) + 0));
                            if (t163 != ((uint8_t)47ULL)) goto ie161;
                            gm_trl2khmuphncd t164 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t162) + 8) + 8));
                            gm_l27 = t164;
                            {
                                void * t167 = GM_LD(void *, (((uint8_t *)&gm_l27) + 24));
                                void * t168 = t167;
                                if (!(GM_LD(void *, ((uint8_t *)&t168)) != 0)) goto ie166;
                                void * t169 = GM_LD(void *, ((uint8_t *)&t168));
                                gm_l28 = t169;
                                {
                                    gm_t88rcucg1b7uk t171;
                                    void * t173 = gm_l28;
                                    uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t173) + 8) + 0));
                                    if (t174 != ((uint8_t)32ULL)) goto ie172;
                                    gm_tmn7n0n9raou5 t175 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t173) + 8) + 8) + 0));
                                    gm_l29 = t175;
                                    void * t176 = GM_LD(void *, (((((uint8_t *)t173) + 8) + 8) + 8));
                                    gm_l30 = t176;
                                    uint8_t t177 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l29) + 0) == ((uint8_t)5ULL)));
                                    if (!t177) goto ie172;
                                    void * t179 = gm_l30;
                                    gm_t88rcucg1b7uk t180 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t179) + 0));
                                    gm_t88rcucg1b7uk t181 = t180;
                                    t171 = t181;
                                    goto ix172;
ie172:;
                                    void * t183 = gm_l28;
                                    gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t183) + 0));
                                    gm_t88rcucg1b7uk t185 = t184;
                                    t171 = t185;
ix172:;
                                    gm_l31 = t171;
                                }
                                {
                                    void * t187 = gm_l0;
                                    void * t188 = GM_LD(void *, (((uint8_t *)t187) + 0));
                                    void * t189 = (void *)(((uint8_t *)t188) + 8);
                                    gm_t88rcucg1b7uk t190 = gm_l31;
                                    gm_t53at80pfdus3 t191;
                                    gm_f_kind_jcnt4hm((void *)&t191, t189, GM_CONV(gm_csguankko4lmhd, t190));
                                    gm_t53at80pfdus3 t192 = t191;
                                    uint8_t t193 = (!(GM_LD(uint8_t, ((uint8_t *)&t192) + 0) == ((uint8_t)5ULL)));
                                    if (!t193) goto ie186;
                                    void * t194 = gm_l0;
                                    void * t195 = GM_LD(void *, (((uint8_t *)t194) + 0));
                                    void * t196 = (void *)(((uint8_t *)t195) + 8);
                                    void * t197 = gm_l28;
                                    gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t197) + 0));
                                    uint8_t t199 = gm_f_is_error_8k27gd1(t196, GM_CONV(gm_csguankko4lmhd, t198));
                                    uint8_t t200 = (uint8_t)!t199;
                                    if (!t200) goto ie186;
                                    {
                                        gm_l32 = ((uint64_t)0ULL);
                                    }
                                    {
                                        for (;;) {
                                            uint64_t t203 = gm_l32;
                                            void * t204 = gm_l0;
                                            uint64_t t205 = GM_LD(uintptr_t, ((((uint8_t *)t204) + 320) + 0) + 8);
                                            uint8_t t206 = (t203 < t205);
                                            if (!t206) goto lb202;
                                            {
                                                uint64_t t210 = gm_l32;
                                                uint64_t t211 = (uintptr_t)t210;
                                                void * t212 = gm_l0;
                                                uint64_t t213 = GM_LD(uintptr_t, ((((uint8_t *)t212) + 320) + 0) + 8);
                                                if (t211 >= (uintptr_t)(t213)) gm_trap_index(t211, t213, gm_src276s8986o4r3, 759, 60);
                                                uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t212) + 320) + 0)) + t211 * 48) + 0));
                                                if (t214 != ((uint8_t)0ULL)) goto ie209;
                                                gm_tl874t2atnlh3 t215 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t212) + 320) + 0)) + t211 * 48) + 8) + 4));
                                                gm_l33 = t215;
                                                gm_tl874t2atnlh3 t216 = gm_l33;
                                                void * t217 = gm_l17;
                                                gm_tl874t2atnlh3 t218 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t217) + 72));
                                                gm_tl874t2atnlh3 t219 = t216;
                                                gm_tl874t2atnlh3 t220 = t218;
                                                uint8_t t221 = (gm_eqff85ac10o3(((uint8_t *)&t219), ((uint8_t *)&t220)));
                                                if (!t221) goto ie209;
                                                {
                                                    {
                                                        void * t224 = gm_l0;
                                                        void * t225 = (void *)(((uint8_t *)t224) + 320);
                                                        uint64_t t226 = gm_l32;
                                                        gm_tpm220j5qhjke t227;
                                                        gm_f_remove_te9updc((void *)&t227, t225, t226);
                                                    }
                                                }
                                                goto ix209;
ie209:;
                                                {
                                                    uint64_t t229 = gm_l32;
                                                    uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                                                    if (o231) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 762, 33);
                                                    uint64_t t232 = r230;
                                                    gm_l32 = t232;
                                                }
ix209:;
                                            }
lc202:;
                                        }
lb202:;
                                    }
                                    {
                                        void * t233 = gm_l0;
                                        void * t234 = gm_l17;
                                        gm_tl874t2atnlh3 t235 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t234) + 72));
                                        gm_b16_8 t236;
                                        GM_ST(void *, (uint8_t *)&t236, (void *)(gm_sju4s3ndpnsge));
                                        GM_ST(uintptr_t, (uint8_t *)&t236 + 8, (uintptr_t)(106));
                                        gm_b0_1 t237 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t233, t235, t236);
                                        if (gm_unw.kind) {
                                            goto gm_ll13;
                                        }
                                    }
                                    {
                                        gm_l6 = ((uint8_t)1);
                                    }
                                    goto ix186;
ie186:;
ix186:;
                                }
                                goto ix166;
ie166:;
ix166:;
                            }
                            goto ix161;
ie161:;
ix161:;
                        }
                        {
                            uint8_t t239 = gm_l2;
                            if (!t239) goto ie238;
                            {
                                void * t241 = gm_l26;
                                gm_l5 = t241;
                            }
                            goto ix238;
ie238:;
                            {
                                void * t243 = (void *)((uint8_t *)&F.l4);
                                void * t244 = gm_l26;
                                gm_trcmmd12cefb1 t245;
                                GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t245 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t245 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t245 + 8), t244);
                                gm_tftb1odc739t5 t246 = gm_f_push_d6c76ud_2(t243, &t245);
                                gm_tftb1odc739t5 t247 = t246;
                                if (((((uint8_t *)&t247))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t247) + 1, gm_src276s8986o4r3, 770, 43);
                                gm_b0_1 t248 = GM_LD(gm_b0_1, (((uint8_t *)&t247) + 1));
                            }
ix238:;
                        }
                        {
                            goto lb9;
                        }
                        goto ix41;
ie41:;
ix41:;
                    }
                    {
                        uint64_t t250 = gm_l11;
                        uint64_t r251; bool o252 = __builtin_add_overflow(t250, ((uint64_t)1ULL), &r251);
                        if (o252) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 773, 12);
                        uint64_t t253 = r251;
                        uint64_t t254 = gm_l8;
                        uint8_t t255 = ((t253 == t254));
                        if (!t255) goto ie249;
                        uint8_t t256 = gm_l2;
                        if (!t256) goto ie249;
                        {
                            void * t259 = gm_l12;
                            uint8_t t260 = GM_LD(uint8_t, ((((uint8_t *)t259) + 8) + 0));
                            if (t260 != ((uint8_t)2ULL)) goto ie258;
                            void * t261 = GM_LD(void *, ((((uint8_t *)t259) + 8) + 8));
                            gm_l34 = t261;
                            {
                                void * t263 = gm_l0;
                                void * t264 = gm_l34;
                                gm_tkn42u7na3t81 t265 = gm_l3;
                                void * t266 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t263, t264, t265);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                                gm_l35 = t266;
                            }
                            {
                                void * t267 = gm_l0;
                                void * t268 = GM_LD(void *, (((uint8_t *)t267) + 0));
                                void * t269 = (void *)(((uint8_t *)t268) + 8);
                                void * t270 = gm_l35;
                                gm_t88rcucg1b7uk t271 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t270) + 0));
                                gm_t53at80pfdus3 t272;
                                gm_f_kind_jcnt4hm((void *)&t272, t269, GM_CONV(gm_csguankko4lmhd, t271));
                                gm_t53at80pfdus3 t273 = t272;
                                uint8_t t274 = ((GM_LD(uint8_t, ((uint8_t *)&t273) + 0) == ((uint8_t)5ULL)));
                                gm_l6 = t274;
                            }
                            {
                                void * t275 = gm_l35;
                                gm_l5 = t275;
                            }
                            {
                                goto lb9;
                            }
                            goto ix258;
ie258:;
ix258:;
                        }
                        goto ix249;
ie249:;
ix249:;
                    }
                    {
                        void * t276 = gm_l0;
                        uint64_t t277 = GM_LD(uintptr_t, ((((uint8_t *)t276) + 1200) + 0) + 8);
                        gm_l36 = t277;
                    }
                    {
                        void * t278 = gm_l0;
                        void * t279 = gm_l12;
                        gm_f_stmt_4145gsn((void *)&gm_l37, (void *)NULL, cx, NULL, t278, t279);
                        if (gm_unw.kind) {
                            goto gm_ll13;
                        }
                    }
                    {
                        void * t280 = gm_l0;
                        uint64_t t281 = gm_l36;
                        void * t282 = (void *)((uint8_t *)&F.l4);
                        gm_b0_1 t283 = gm_f_push_temps_upm64pn((void *)NULL, cx, NULL, t280, t281, t282);
                        if (gm_unw.kind) {
                            goto gm_ll13;
                        }
                    }
                    {
                        void * t284 = gm_l0;
                        void * t285 = GM_LD(void *, (((uint8_t *)t284) + 0));
                        uint8_t t286 = gm_f_stmt_diverges_en1hv79(t285, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l37)));
                        gm_l6 = t286;
                    }
                    {
                        uint8_t t288 = gm_l6;
                        if (!t288) goto ie287;
                        {
                            void * t290 = gm_l0;
                            void * t291 = GM_LD(void *, (((uint8_t *)t290) + 0));
                            gm_f_stmt_never_cause_r2lf089((void *)&(*(gm_tnvnsr1bfvtbd *)((uint8_t *)&F.l7)), t291, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l37)));
                        }
                        goto ix287;
ie287:;
ix287:;
                    }
                    {
                        void * t292 = (void *)((uint8_t *)&F.l4);
                        gm_tftb1odc739t5 t293 = gm_f_push_d6c76ud_2(t292, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l37)));
                        gm_tftb1odc739t5 t294 = t293;
                        if (((((uint8_t *)&t294))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t294) + 1, gm_src276s8986o4r3, 786, 9);
                        gm_b0_1 t295 = GM_LD(gm_b0_1, (((uint8_t *)&t294) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 727, 5);
me17:;
                }
lc9:;
                {
                    uint64_t t297 = gm_l10;
                    uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                    uint64_t t300 = r298;
                    gm_l10 = t300;
                }
            }
gm_ll13:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
            }
            goto gm_lad;
lb9:;
        }
    }
    {
        void * t302 = gm_l0;
        uint64_t t303 = GM_LD(uintptr_t, ((((uint8_t *)t302) + 1040) + 0) + 8);
        uint8_t t304 = (t303 > ((uint64_t)0ULL));
        if (!t304) goto ie301;
        {
            void * t306 = gm_l0;
            gm_b0_1 t307 = gm_f_check_pending_handlers_o_sunakri((void *)NULL, cx, NULL, t306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix301;
ie301:;
ix301:;
    }
    {
        void * t308 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t308);
    }
    {
        gm_t88rcucg1b7uk t309;
        void * t311 = gm_l5;
        void * t312 = t311;
        if (!(GM_LD(void *, ((uint8_t *)&t312)) != 0)) goto ie310;
        void * t313 = GM_LD(void *, ((uint8_t *)&t312));
        gm_l38 = t313;
        void * t315 = gm_l38;
        gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t315) + 0));
        gm_t88rcucg1b7uk t317 = t316;
        t309 = t317;
        goto ix310;
ie310:;
        gm_t88rcucg1b7uk t318;
        uint8_t t320 = gm_l6;
        if (!t320) goto ie319;
        void * t322 = gm_l0;
        void * t323 = GM_LD(void *, (((uint8_t *)t322) + 0));
        void * t324 = (void *)(((uint8_t *)t323) + 8);
        gm_t88rcucg1b7uk t325 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t324);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t326 = t325;
        t318 = t326;
        goto ix319;
ie319:;
        void * t328 = gm_l0;
        void * t329 = GM_LD(void *, (((uint8_t *)t328) + 0));
        void * t330 = (void *)(((uint8_t *)t329) + 8);
        gm_t88rcucg1b7uk t331 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t330);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t332 = t331;
        t318 = t332;
ix319:;
        t309 = t318;
ix310:;
        gm_l39 = t309;
    }
    {
        gm_t88rcucg1b7uk t333 = gm_l39;
        gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        void * t335 = gm_l5;
        void * t336 = gm_l1;
        gm_tl874t2atnlh3 t337 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t336) + 24));
        void * t338 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t333, t334, t335, t337);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t339 = 0;
        F.ret = t338;
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
extern __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
extern __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_block_typed_e7svg7n F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_b16_8 gm_l1; /* stmts */
    void * gm_l2; /* tail */
    gm_tl874t2atnlh3 gm_l3; /* span */
    GM_STACK_CHECK(&F, 261u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2 = gm_l0;
        gm_tgabaikr08mjg t3;
        memset((uint8_t *)&t3 + 36, 0, 4);
        gm_b16_8 t4 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        void * t5 = gm_l2;
        GM_ST(void *, (uint8_t *)&t3 + 16, t5);
        gm_tl874t2atnlh3 t6 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t3 + 24, t6);
        gm_tq7s98ihonkju t7 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t3, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8qpjmtnqct)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_src276s8986o4r3, 978, 30);
        void * t9 = GM_LD(void *, (((uint8_t *)&t8) + 8));
        gm_teti411eujvf4 t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t10 + 8), t9);
        gm_tl874t2atnlh3 t11 = gm_l3;
        void * t12 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t10, t11);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_bodies_qrmq5ll(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_bodies_qrmq5ll F;
    void * gm_l0; /* s */
    gm_t5r2drn9pjqkt gm_l2; /* body */
    gm_t5r2drn9pjqkt gm_l3; /* body */
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_0bg6tcp((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 424);
            gm_tm4iv6anh1b9a t5;
            gm_f_pop_lnq3sua((void *)&t5, t4);
            gm_tm4iv6anh1b9a t6 = t5;
            if (!((((uint8_t *)&t6))[24] != 0)) goto lb2;
            gm_t5r2drn9pjqkt t7 = GM_LD(gm_t5r2drn9pjqkt, ((uint8_t *)&t6));
            gm_l2 = t7;
            {
                uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t12 = (uintptr_t)t11;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
                uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 200) + 0) + 8);
                if (t12 >= (uintptr_t)(t15)) gm_trap_index(t12, t15, gm_src276s8986o4r3, 352, 12);
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 200) + 0)) + t12 * 184) + 112));
                if (!t16) goto ie10;
                {
                    void * t18 = (void *)((uint8_t *)&F.l1);
                    gm_tftb1odc739t5 t19 = gm_f_push_o022ckl(t18, &(*(gm_t5r2drn9pjqkt *)((uint8_t *)&gm_l2)));
                    gm_tftb1odc739t5 t20 = t19;
                    if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_src276s8986o4r3, 353, 13);
                    gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                }
                {
                    goto lc2;
                }
                goto ix10;
ie10:;
ix10:;
            }
            {
                void * t22 = gm_l0;
                uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                void * t25 = GM_LD(void *, (((uint8_t *)&gm_l2) + 16));
                gm_b0_1 t26 = gm_f_check_fn_body_kc8b374((void *)NULL, cx, NULL, t22, t23, t24, t25);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
lc2:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        for (;;) {
            void * t28 = (void *)((uint8_t *)&F.l1);
            gm_tm4iv6anh1b9a t29;
            gm_f_pop_lnq3sua((void *)&t29, t28);
            gm_tm4iv6anh1b9a t30 = t29;
            if (!((((uint8_t *)&t30))[24] != 0)) goto lb27;
            gm_t5r2drn9pjqkt t31 = GM_LD(gm_t5r2drn9pjqkt, ((uint8_t *)&t30));
            gm_l3 = t31;
            {
                void * t34 = gm_l0;
                void * t35 = (void *)(((uint8_t *)t34) + 424);
                gm_tftb1odc739t5 t36 = gm_f_push_o022ckl(t35, &(*(gm_t5r2drn9pjqkt *)((uint8_t *)&gm_l3)));
                gm_tftb1odc739t5 t37 = t36;
                if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src276s8986o4r3, 358, 32);
                gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
            }
lc27:;
        }
lb27:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_body_kc8b374(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, void * a3) {
    struct gm_fr_check_fn_body_kc8b374 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* fn_id */
    uint64_t gm_l2; /* module */
    void * gm_l3; /* lit */
    gm_t66j1im9ip128 gm_l5; /* scope_len */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* g */
    uint8_t gm_l12; /* no_needs */
    gm_b16_8 gm_l13; /* e */
    uint32_t gm_l14; /* hi */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint32_t gm_l22; /* id */
    void * gm_l23; /* body */
    GM_STACK_CHECK(&F, 1786u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src276s8986o4r3, 363, 12);
        gm_tqqhoivahirr0 t7 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136));
        F.l4 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_f_scope_type_params_2di4dm5((void *)&gm_l5, (void *)NULL, cx, NULL, t8, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 80));
            gm_l7 = t13;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l8;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l8;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t21 * 4));
                    uint32_t t24 = t23;
                    uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&t24));
                    gm_l9 = t27;
                    {
                        void * t30 = gm_l0;
                        uint32_t t31 = gm_l9;
                        gm_t9qkh0rsooc8d t32;
                        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)2ULL));
                        uint8_t t33 = gm_f_param_has_2r09oc7(t30, t31, t32);
                        if (!t33) goto ie29;
                        {
                            void * t35 = (void *)((uint8_t *)&F.l6);
                            uint32_t t36 = gm_l9;
                            gm_tftb1odc739t5 t37 = gm_f_push_im9d4ro(t35, t36);
                            gm_tftb1odc739t5 t38 = t37;
                            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src276s8986o4r3, 368, 35);
                            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 367, 5);
me25:;
                }
lc14:;
                {
                    uint64_t t41 = gm_l8;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l8 = t44;
                }
            }
lb14:;
        }
    }
    {
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 2120);
        uint32_t t47 = gm_l1;
        gm_tftb1odc739t5 t48 = gm_f_push_im9d4ro(t46, t47);
        gm_tftb1odc739t5 t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_src276s8986o4r3, 370, 5);
        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
    }
    {
    }
    {
        void * t55 = gm_l0;
        gm_f_enter_module_scope_011vo5a((void *)&F.l10, (void *)NULL, cx, NULL, t55);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
    }
    {
        void * t57 = gm_l0;
        uint64_t t58 = gm_l2;
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        gm_tnpifv20irnph t60 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 64));
        gm_f_fn_cx_hgvq15e((void *)&F.l11, (void *)NULL, cx, NULL, t57, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        uint32_t t61 = gm_l1;
        gm_t9805qtonbsag t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)&F.l11) + 400), t62);
    }
    {
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, ((((uint8_t *)&F.l11) + 96) + 40), t63);
    }
    {
        gm_l12 = ((uint8_t)0);
    }
    {
        void * t65 = gm_l3;
        if (!((((((uint8_t *)t65) + 0) + 32))[16] != 0)) goto ie64;
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((((uint8_t *)t65) + 0) + 32));
        gm_l13 = t66;
        {
            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
            uint8_t t69 = ((t68 == ((uint64_t)0ULL)));
            gm_l12 = t69;
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        uint8_t t71 = gm_l12;
        uint8_t t72 = (uint8_t)!t71;
        if (!t72) goto ie70;
        {
            void * t74 = gm_l3;
            uint32_t t75 = GM_LD(uint32_t, (((((uint8_t *)t74) + 0) + 56) + 8));
            gm_l14 = t75;
        }
        {
            void * t76 = gm_l3;
            uint32_t t77 = GM_LD(uint32_t, (((((uint8_t *)t76) + 0) + 56) + 0));
            uint32_t t78 = gm_l14;
            uint32_t t79 = gm_l14;
            gm_tl874t2atnlh3 t80;
            GM_ST(uint32_t, ((uint8_t *)&t80 + 0), t77);
            GM_ST(uint32_t, ((uint8_t *)&t80 + 4), t78);
            GM_ST(uint32_t, ((uint8_t *)&t80 + 8), t79);
            void * t81 = gm_l3;
            uint8_t t82;
            if (((((((uint8_t *)t81) + 0) + 32))[16] != 0)) goto mn84;
            t82 = ((uint8_t)0);
            goto me83;
mn84:;
            t82 = ((uint8_t)1);
            goto me83;
mn85:;
            gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 382, 102);
me83:;
            gm_toceffb697p0u t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t86 + 0), t80);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 12), t82);
            gm_trj4m8pa99qp7 t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 18, 0);
            GM_ST(gm_toceffb697p0u, ((uint8_t *)&t87), t86);
            ((uint8_t *)&t87)[16] = 1;
            GM_ST(gm_trj4m8pa99qp7, (((uint8_t *)&F.l11) + 408), t87);
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        {
            void * t89 = gm_l0;
            gm_b16_8 t90 = GM_LD(gm_b16_8, ((((uint8_t *)t89) + 2272) + 0));
            gm_l15 = t90;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t92 = gm_l16;
                uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t94 = (t92 < t93);
                if (!t94) goto lb91;
                {
                    uint64_t t97 = gm_l16;
                    uint64_t t98 = (uintptr_t)t97;
                    uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_tp9ftj4p31ha1 t102 = GM_LD(gm_tp9ftj4p31ha1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t98 * 40));
                    F.l17 = t102;
                    {
                        uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 0));
                        uint32_t t106 = gm_l1;
                        uint8_t t107 = ((t105 == t106));
                        if (!t107) goto ie104;
                        {
                            void * t109 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t110 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l17) + 8) + 0));
                            uint32_t t111 = gm_l1;
                            gm_t930q5l38hdap t112;
                            memset(&t112, 0, sizeof t112);
                            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)3ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t112 + 8), t111);
                            gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l17) + 8) + 16));
                            gm_b0_1 t114 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t109, t110, &t112, t113);
                            if (gm_unw.kind) {
                                goto gm_ll95;
                            }
                        }
                        goto ix104;
ie104:;
ix104:;
                    }
                    goto me100;
mn101:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 385, 5);
me100:;
                }
lc91:;
                {
                    uint64_t t116 = gm_l16;
                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                    uint64_t t119 = r117;
                    gm_l16 = t119;
                }
            }
gm_ll95:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb91; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc91; }
            }
            goto gm_ld1_2;
lb91:;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        {
            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
            gm_l19 = t121;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t123 = gm_l20;
                uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t125 = (t123 < t124);
                if (!t125) goto lb122;
                {
                    uint64_t t128 = gm_l20;
                    uint64_t t129 = (uintptr_t)t128;
                    uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_t5v0c8ia6a5mo t133 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t129 * 56));
                    F.l21 = t133;
                    {
                        uint8_t t136;
                        if ((((((uint8_t *)&F.l21) + 40))[4] != 0)) goto mn138;
                        t136 = ((uint8_t)0);
                        goto me137;
mn138:;
                        t136 = ((uint8_t)1);
                        goto me137;
mn139:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 390, 12);
me137:;
                        if (!t136) goto ie135;
                        {
                            goto lc122;
                        }
                        goto ix135;
ie135:;
ix135:;
                    }
                    {
                        void * t141 = (void *)((uint8_t *)&F.l11);
                        gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                        gm_tl874t2atnlh3 t144 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 20));
                        uint32_t t145 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t141, t142, t143, ((uint8_t)0), t144);
                        if (gm_unw.kind) {
                            goto gm_ll126;
                        }
                        gm_l22 = t145;
                    }
                    {
                        uint8_t t147 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l21) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t147) goto ie146;
                        {
                            void * t149 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                            uint32_t t151 = gm_l22;
                            gm_t930q5l38hdap t152;
                            memset(&t152, 0, sizeof t152);
                            GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t152 + 8), t151);
                            gm_tl874t2atnlh3 t153 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 20));
                            gm_b0_1 t154 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t149, t150, &t152, t153);
                            if (gm_unw.kind) {
                                goto gm_ll126;
                            }
                        }
                        goto ix146;
ie146:;
ix146:;
                    }
                    {
                        void * t155 = (void *)((uint8_t *)&F.l18);
                        uint32_t t156 = gm_l22;
                        gm_tftb1odc739t5 t157 = gm_f_push_im9d4ro(t155, t156);
                        gm_tftb1odc739t5 t158 = t157;
                        if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_src276s8986o4r3, 393, 9);
                        gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                    }
                    goto me131;
mn132:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 389, 5);
me131:;
                }
lc122:;
                {
                    uint64_t t161 = gm_l20;
                    uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                    uint64_t t164 = r162;
                    gm_l20 = t164;
                }
            }
gm_ll126:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb122; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc122; }
            }
            goto gm_ld1_2;
lb122:;
        }
    }
    {
        void * t165 = (void *)((uint8_t *)&F.l11);
        void * t166 = gm_l3;
        void * t167 = (void *)(((uint8_t *)t166) + 88);
        void * t168 = gm_f_fn_body_37q3l3a((void *)NULL, cx, NULL, t165, t167);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
        gm_l23 = t168;
    }
    {
        void * t169 = (void *)((uint8_t *)&F.l11);
        void * t170 = gm_l23;
        gm_b0_1 t171 = gm_f_finish_m8t0599((void *)NULL, cx, NULL, t169, t170);
        if (gm_unw.kind) {
            goto gm_ld1_2;
        }
    }
    {
        void * t172 = gm_l0;
        gm_f_unscope_type_params_bofhhep(t172, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l5)));
    }
    {
        uint32_t t173 = gm_l1;
        uint64_t t174 = (uint64_t)t173;
        uint64_t t175 = (uintptr_t)t174;
        void * t176 = gm_l0;
        uint64_t t177 = GM_LD(uintptr_t, ((((uint8_t *)t176) + 384) + 0) + 8);
        if (t175 >= (uintptr_t)(t177)) gm_trap_index(t175, t177, gm_src276s8986o4r3, 398, 5);
        memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 1, 0, 7);
        memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 57, 0, 7);
        gm_t9qkh0rsooc8d t178;
        GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)0ULL));
        gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        gm_b16_8 t180 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 16) + 0));
        void * t181 = gm_l23;
        gm_t9805qtonbsag t182;
        memset(&t182, 0, sizeof t182);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 0, t178);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 8, t179);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 24, t180);
        GM_ST(void *, (uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 40, t181);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 48, t182);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t176) + 384) + 0)) + t175 * 64)) + 56, ((uint8_t)0));
    }
    {
        void * t183 = gm_l0;
        gm_f_leave_module_scope_4hp5mc4(t183, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l10)));
    }
    {
        {
            void * t185 = gm_l0;
            void * t186 = (void *)(((uint8_t *)t185) + 2120);
            gm_t9805qtonbsag t187 = gm_f_pop_570g79n(t186);
        }
    }
    goto gm_ls188;
gm_ld1_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t189 = gm_l0;
        gm_f_leave_module_scope_4hp5mc4(t189, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l10)));
        gm_unw = gm_saved;
    }
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t191 = gm_l0;
            void * t192 = (void *)(((uint8_t *)t191) + 2120);
            gm_t9805qtonbsag t193 = gm_f_pop_570g79n(t192);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls188:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cond_99ajlhk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_cond_99ajlhk F;
    void * gm_l0; /* cx */
    void * gm_l1; /* c */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l6; /* e */
    void * gm_l7; /* ex */
    void * gm_l9; /* value */
    GM_STACK_CHECK(&F, 536u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_v5d1o9b_2((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
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
                    gm_tq5j0547p8452 t16 = GM_LD(gm_tq5j0547p8452, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 80));
                    F.l5 = t16;
                    {
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t20 != ((uint8_t)1ULL)) goto mn19;
                        void * t21 = GM_LD(void *, (((uint8_t *)&F.l5) + 8));
                        gm_l6 = t21;
                        {
                            void * t23 = gm_l0;
                            void * t24 = gm_l6;
                            void * t25 = gm_l0;
                            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                            void * t27 = (void *)(((uint8_t *)t26) + 8);
                            gm_t88rcucg1b7uk t28 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t27);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tkn42u7na3t81 t29;
                            GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t29), t28);
                            ((uint8_t *)&t29)[4] = 1;
                            void * t30 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t23, t24, t29);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l7 = t30;
                        }
                        {
                            void * t31 = (void *)((uint8_t *)&F.l2);
                            void * t32 = gm_l0;
                            void * t33 = gm_l7;
                            void * t34 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t32, t33);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tlr9dp305htm7 t35;
                            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0ULL));
                            GM_ST(void *, ((uint8_t *)&t35 + 8), t34);
                            gm_tftb1odc739t5 t36 = gm_f_push_eb7kbhp_2(t31, &t35);
                            gm_tftb1odc739t5 t37 = t36;
                            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src276s8986o4r3, 920, 17);
                            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                        }
                        goto me18;
mn19:;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t40 != ((uint8_t)0ULL)) goto mn39;
                        gm_t0d02142k4tu8 t41 = GM_LD(gm_t0d02142k4tu8, ((((uint8_t *)&F.l5) + 8) + 0));
                        F.l8 = t41;
                        void * t42 = GM_LD(void *, ((((uint8_t *)&F.l5) + 8) + 64));
                        gm_l9 = t42;
                        void * t43 = (void *)((uint8_t *)&F.l2);
                        void * t44 = gm_l0;
                        void * t45 = (void *)((uint8_t *)&F.l8);
                        void * t46 = gm_l9;
                        gm_tlr9dp305htm7 t47;
                        gm_f_binding_cond_v86td9t((void *)&t47, (void *)NULL, cx, NULL, t44, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t48 = gm_f_push_eb7kbhp_2(t43, &t47);
                        gm_tftb1odc739t5 t49 = t48;
                        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_src276s8986o4r3, 922, 34);
                        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                        goto me18;
mn39:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 917, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 916, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t52 = gm_l4;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    uint64_t t55 = r53;
                    gm_l4 = t55;
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
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_declare_3l5mhoj F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    gm_tl874t2atnlh3 gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 3396u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        uint8_t t3 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sfdbfrv46lihd, 1) == 0));
        if (!t3) goto ie2;
        {
            gm_b0_1 t5;
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_l1;
        gm_tqvu78rpi6q1h t10;
        gm_f_lookup_local_6f38t7p((void *)&t10, t8, t9);
        gm_tqvu78rpi6q1h t11 = t10;
        uint8_t t12;
        if (((((uint8_t *)&t11))[208] != 0)) goto mn14;
        t12 = ((uint8_t)0);
        goto me13;
mn14:;
        t12 = ((uint8_t)1);
        goto me13;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 501, 8);
me13:;
        if (!t12) goto ie7;
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l3;
            gm_b16_8 t19 = gm_l1;
            gm_tnpifv20irnph t20;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
            gm_tabb710d9mq2e t21;
            gm_f_format_q32bjcv((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_src276s8986o4r3, 502, 24);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_b0_1 t26 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t17, t18, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix7;
ie7:;
        uint8_t t28 = (!(GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)2ULL)));
        if (!t28) goto ie27;
        uint8_t t29;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t32 != ((uint8_t)5ULL)) goto mn31;
        t29 = ((uint8_t)0);
        goto me30;
mn31:;
        t29 = ((uint8_t)1);
        goto me30;
mn33:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 503, 35);
me30:;
        if (!t29) goto ie27;
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
        void * t36 = gm_l0;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 8));
        gm_b16_8 t38 = gm_l1;
        uint8_t t39 = gm_f_names_type_rjdgib9((void *)NULL, cx, NULL, t35, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t39) goto ie27;
        {
            void * t41 = gm_l0;
            gm_tl874t2atnlh3 t42 = gm_l3;
            gm_b16_8 t43 = gm_l1;
            gm_tnpifv20irnph t44;
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
            gm_tabb710d9mq2e t45;
            gm_f_format_p8e3d4v((void *)&t45, (void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_src276s8986o4r3, 504, 24);
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
            gm_b16_8 t48 = t47;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_b0_1 t50 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t41, t42, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix27;
ie27:;
        gm_b16_8 t52 = gm_l1;
        uint8_t t53 = gm_f_is_reserved_type_name_tuq4qsj(t52);
        if (!t53) goto ie51;
        {
            void * t55 = gm_l0;
            gm_tl874t2atnlh3 t56 = gm_l3;
            gm_b16_8 t57 = gm_l1;
            gm_tnpifv20irnph t58;
            GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
            gm_tabb710d9mq2e t59;
            gm_f_format_9i45r57((void *)&t59, (void *)NULL, cx, NULL, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t60 = t59;
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_src276s8986o4r3, 506, 24);
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
            gm_b16_8 t62 = t61;
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            gm_b0_1 t64 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t55, t56, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix51;
ie51:;
ix51:;
ix27:;
ix7:;
    }
    {
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t71) { case ((uint8_t)0ULL): goto ma68; case ((uint8_t)1ULL): goto ma66; case ((uint8_t)2ULL): goto ma69; case ((uint8_t)3ULL): goto ma69; case ((uint8_t)4ULL): goto ma67; case ((uint8_t)5ULL): goto ma69; default: goto ma69; }
ma66:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t73 != ((uint8_t)1ULL)) goto mn72;
        gm_t0ialelq3ql1k t74 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l2) + 8));
        F.l4 = t74;
        void * t75 = gm_l0;
        void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
        void * t77 = (void *)(((uint8_t *)t76) + 1568);
        gm_tb07pp6njg0bk t78;
        gm_b16_8 t79 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t79);
        gm_t0ialelq3ql1k t80 = F.l4;
        gm_t1hofbajv6dns t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t81 + 8), t80);
        GM_ST(gm_t1hofbajv6dns, (uint8_t *)&t78 + 16, t81);
        gm_tftb1odc739t5 t82 = gm_f_push_ola4v1l(t77, &t78);
        gm_tftb1odc739t5 t83 = t82;
        if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_src276s8986o4r3, 511, 24);
        gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
        goto me65;
mn72:;
ma67:;
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t86 != ((uint8_t)4ULL)) goto mn85;
        gm_ts78ca2amthgu t87 = GM_LD(gm_ts78ca2amthgu, (((uint8_t *)&F.l2) + 8));
        F.l5 = t87;
        void * t88 = gm_l0;
        void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
        void * t90 = (void *)(((uint8_t *)t89) + 1568);
        gm_tb07pp6njg0bk t91;
        gm_b16_8 t92 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t91 + 0, t92);
        {
            gm_ts78ca2amthgu t94 = F.l5;
            F.l6 = t94;
        }
        {
            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 0));
            gm_l7 = t95;
        }
        {
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
            gm_l8 = t96;
        }
        {
            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 24));
            gm_l9 = t97;
        }
        gm_ts78ca2amthgu t98;
        memset((uint8_t *)&t98 + 4, 0, 4);
        memset((uint8_t *)&t98 + 36, 0, 4);
        gm_t88rcucg1b7uk t99 = gm_l7;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t98 + 0, t99);
        gm_b16_8 t100 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t98 + 8, t100);
        gm_tl874t2atnlh3 t101 = gm_l9;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t98 + 24, t101);
        gm_ts78ca2amthgu t102 = t98;
        gm_t1hofbajv6dns t103;
        memset(&t103, 0, sizeof t103);
        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)1ULL));
        GM_ST(gm_ts78ca2amthgu, ((uint8_t *)&t103 + 8), t102);
        GM_ST(gm_t1hofbajv6dns, (uint8_t *)&t91 + 16, t103);
        gm_tftb1odc739t5 t104 = gm_f_push_ola4v1l(t90, &t91);
        gm_tftb1odc739t5 t105 = t104;
        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_src276s8986o4r3, 512, 22);
        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
        goto me65;
mn85:;
ma68:;
        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t108 != ((uint8_t)0ULL)) goto mn107;
        void * t109 = gm_l0;
        void * t110 = GM_LD(void *, (((uint8_t *)t109) + 0));
        void * t111 = (void *)(((uint8_t *)t110) + 1568);
        gm_tb07pp6njg0bk t112;
        gm_b16_8 t113 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t112 + 0, t113);
        gm_t1hofbajv6dns t114;
        memset(&t114, 0, sizeof t114);
        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t1hofbajv6dns, (uint8_t *)&t112 + 16, t114);
        gm_tftb1odc739t5 t115 = gm_f_push_ola4v1l(t111, &t112);
        gm_tftb1odc739t5 t116 = t115;
        if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_src276s8986o4r3, 513, 20);
        gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
        goto me65;
mn107:;
ma69:;
        goto me65;
mn118:;
ma70:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 510, 5);
me65:;
    }
    {
        void * t120 = gm_l0;
        uint64_t t121 = GM_LD(uintptr_t, ((((uint8_t *)t120) + 56) + 0) + 8);
        uint64_t r122; bool o123 = __builtin_sub_overflow(t121, ((uint64_t)1ULL), &r122);
        if (o123) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 516, 21);
        uint64_t t124 = r122;
        uint64_t t125 = (uintptr_t)t124;
        void * t126 = gm_l0;
        uint64_t t127 = GM_LD(uintptr_t, ((((uint8_t *)t126) + 56) + 0) + 8);
        if (t125 >= (uintptr_t)(t127)) gm_trap_index(t125, t127, gm_src276s8986o4r3, 516, 5);
        void * t128 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t126) + 56) + 0)) + t125 * 40);
        gm_tmhicb6huo2ra t129;
        gm_b16_8 t130 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t129 + 0, t130);
        memcpy((uint8_t *)&t129 + 16, ((uint8_t *)&F.l2), 208);
        gm_tftb1odc739t5 t131 = gm_f_push_nr0n00f(t128, &t129);
        gm_tftb1odc739t5 t132 = t131;
        if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_src276s8986o4r3, 516, 5);
        gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
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
inline __attribute__((aligned(4))) void * gm_f_declare_temps_a7fc14v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_declare_temps_a7fc14v F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* mark */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1200) + 0) + 8);
        uint64_t t5 = gm_l1;
        uint8_t t6 = (t4 <= t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l2;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = gm_l1;
        void * t12 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t13 = gm_f_push_temps_upm64pn((void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l2;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        void * t17 = gm_l2;
        void * t18 = gm_l2;
        gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 72));
        void * t20 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t15, t16, t17, t19);
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
__attribute__((aligned(4))) void gm_f_defer_stmt_hm15mru(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2) {
    struct gm_fr_defer_stmt_hm15mru F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* is_err */
    void * gm_l2; /* body */
    uint8_t gm_l3; /* was */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* performed_base */
    gm_trcmmd12cefb1 gm_l6; /* inner */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l11; /* exits */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l15; /* e */
    void * gm_l17; /* x */
    GM_STACK_CHECK(&F, 990u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 520));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 528));
        gm_l4 = t5;
    }
    {
        void * t6 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t6) + 520), ((uint8_t)1));
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 232) + 0) + 8);
        GM_ST(uint64_t, (((uint8_t *)t7) + 528), t9);
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 192) + 0) + 8);
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l0;
        gm_b0_1 t13 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l2;
        gm_f_stmt_4145gsn((void *)&gm_l6, (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t16 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t16);
    }
    {
        {
            uint64_t t18 = gm_l5;
            gm_l8 = t18;
        }
        {
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 192) + 0) + 8);
            gm_l7 = t20;
        }
        {
            for (;;) {
                uint64_t t22 = gm_l8;
                uint64_t t23 = gm_l7;
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l8;
                    uint64_t t28 = t27;
                    uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&t28));
                    gm_l9 = t31;
                    {
                        uint64_t t33 = gm_l9;
                        uint64_t t34 = (uintptr_t)t33;
                        void * t35 = gm_l0;
                        uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 192) + 0) + 8);
                        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_src276s8986o4r3, 840, 14);
                        gm_tj44oivvogvtl t37 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 192) + 0)) + t34 * 40));
                        F.l10 = t37;
                    }
                    {
                        gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 24));
                        void * t40 = gm_l0;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)t40) + 536) + 0));
                        gm_b16_8 t42 = t41;
                        uint8_t t43 = 0;
                        gm_tl874t2atnlh3 t44 = t39;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t42) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t42))) + i * 12, ((uint8_t *)&t44))) { t43 = 1; break; }
                        uint8_t t45 = (uint8_t)(t43);
                        if (!t45) goto ie38;
                        {
                            goto lc21;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    {
                        gm_l11 = ((uint8_t)0);
                    }
                    {
                        {
                            uint32_t t48 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 0) + 0));
                            uint64_t t49 = (uint64_t)t48;
                            uint64_t t50 = (uintptr_t)t49;
                            void * t51 = gm_l0;
                            void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
                            uint64_t t53 = GM_LD(uintptr_t, ((((uint8_t *)t52) + 1048) + 0) + 8);
                            if (t50 >= (uintptr_t)(t53)) gm_trap_index(t50, t53, gm_src276s8986o4r3, 843, 18);
                            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t52) + 1048) + 0)) + t50 * 56) + 40));
                            gm_l12 = t54;
                        }
                        {
                            gm_l13 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t56 = gm_l13;
                                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                uint8_t t58 = (t56 < t57);
                                if (!t58) goto lb55;
                                {
                                    uint64_t t61 = gm_l13;
                                    uint64_t t62 = (uintptr_t)t61;
                                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    gm_tv0k2vg3054a6 t66 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t62 * 48));
                                    F.l14 = t66;
                                    {
                                        uint8_t t69 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l14) + 16) + 0) == ((uint8_t)0ULL)));
                                        if (!t69) goto ie68;
                                        {
                                            gm_l11 = ((uint8_t)1);
                                        }
                                        goto ix68;
ie68:;
ix68:;
                                    }
                                    goto me64;
mn65:;
                                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 843, 9);
me64:;
                                }
lc55:;
                                {
                                    uint64_t t72 = gm_l13;
                                    uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                                    uint64_t t75 = r73;
                                    gm_l13 = t75;
                                }
                            }
lb55:;
                        }
                    }
                    {
                        uint8_t t77 = gm_l11;
                        if (!t77) goto ie76;
                        {
                            void * t79 = gm_l0;
                            gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 24));
                            void * t81 = gm_l0;
                            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
                            void * t83 = (void *)(((uint8_t *)t82) + 8);
                            void * t84 = (void *)(((uint8_t *)&F.l10) + 0);
                            gm_b16_8 t85 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t83, t84);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                            gm_tnpifv20irnph t86;
                            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
                            gm_tabb710d9mq2e t87;
                            gm_f_format_n7840ue((void *)&t87, (void *)NULL, cx, NULL, t86);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                            gm_tabb710d9mq2e t88 = t87;
                            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_src276s8986o4r3, 847, 30);
                            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
                            gm_b16_8 t90 = t89;
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                            gm_b0_1 t92 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t79, t80, t91);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                        }
                        goto ix76;
ie76:;
ix76:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 839, 5);
me29:;
                }
lc21:;
                {
                    uint64_t t94 = gm_l8;
                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                    uint64_t t97 = r95;
                    gm_l8 = t97;
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
        void * t98 = gm_l0;
        uint8_t t99 = gm_l3;
        GM_ST(uint8_t, (((uint8_t *)t98) + 520), t99);
    }
    {
        void * t100 = gm_l0;
        uint64_t t101 = gm_l4;
        GM_ST(uint64_t, (((uint8_t *)t100) + 528), t101);
    }
    {
        void * t102;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
        if (t105 != ((uint8_t)4ULL)) goto mn104;
        void * t106 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
        gm_l15 = t106;
        void * t107 = gm_l15;
        t102 = t107;
        goto me103;
mn104:;
        {
            gm_f_list_ir0h3tc_2((void *)&F.l16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t110 = (void *)((uint8_t *)&F.l16);
            gm_tftb1odc739t5 t111 = gm_f_push_d6c76ud_2(t110, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l6)));
            gm_tftb1odc739t5 t112 = t111;
            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 1, gm_src276s8986o4r3, 856, 13);
            gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
        }
        void * t114 = gm_l0;
        void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        void * t117 = gm_l2;
        gm_tl874t2atnlh3 t118 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t117) + 128));
        void * t119 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t115, t116, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t120 = t119;
        t102 = t120;
        goto me103;
mn108:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 852, 10);
me103:;
        gm_l17 = t102;
    }
    {
        uint8_t t121 = gm_l1;
        void * t122 = gm_l17;
        gm_tgtub0v94d5u5 t123;
        GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), t121);
        GM_ST(void *, ((uint8_t *)&t123 + 8), t122);
        gm_trcmmd12cefb1 t124;
        GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t124 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t124 + 8), t123);
        uint8_t t125 = 0;
        F.ret = t124;
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
extern __attribute__((aligned(4))) uint32_t gm_f_depth_fr7pn6n(void * a0);
inline __attribute__((aligned(4))) void gm_f_early_fallback_error_4lq49mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_early_fallback_error_4lq49mb F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* settled_at */
    gm_b16_8 gm_l4; /* t */
    GM_STACK_CHECK(&F, 368u);
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
        gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_b16_8 t6 = gm_l4;
        gm_tnpifv20irnph t7;
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
        gm_tabb710d9mq2e t8;
        gm_f_format_07fj847((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_src276s8986o4r3, 984, 27);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_b16_8 t11 = t10;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_f_error_s6hn871((void *)&F.l5, (void *)NULL, cx, NULL, t5, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l5);
        gm_tl874t2atnlh3 t14 = gm_l3;
        gm_b16_8 t15 = gm_l4;
        gm_tnpifv20irnph t16;
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
        gm_tabb710d9mq2e t17;
        gm_f_format_r1s7qdu((void *)&t17, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_src276s8986o4r3, 985, 29);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        gm_f_add_label_50fj7vv(t13, GM_CONV(gm_cseahvg8v7p0vs, t14), t21);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l5), 184);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3) {
    struct gm_fr_enter_fn_vo5kel1 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* fn_id */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_tnpifv20irnph gm_l3; /* declared */
    GM_STACK_CHECK(&F, 772u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 576);
        void * t4 = gm_l0;
        gm_tjhr0bdm69ios t5;
        gm_f_save_frame_8libfct((void *)&t5, t4);
        gm_tftb1odc739t5 t6 = gm_f_push_8h1r7s6_2(t3, &t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_src276s8986o4r3, 569, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        uint8_t *t10 = (((uint8_t *)t9) + 1200);
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)t10), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l0;
        uint8_t *t12 = (((uint8_t *)t11) + 1000);
        gm_f_list_084fcd6((void *)&(*(gm_trqfvmjtt4sij *)t12), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = gm_l0;
        gm_t403eokj7ifg9 t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(gm_t403eokj7ifg9, (((uint8_t *)t13) + 800), t14);
    }
    {
        void * t15 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t15) + 744), ((uint8_t)0));
    }
    {
        void * t16 = gm_l0;
        gm_tkn42u7na3t81 t17;
        memset(&t17, 0, sizeof t17);
        GM_ST(gm_tkn42u7na3t81, (((uint8_t *)t16) + 748), t17);
    }
    {
        void * t18 = gm_l0;
        uint8_t *t19 = (((uint8_t *)t18) + 16);
        gm_f_list_p9s2t99((void *)&(*(gm_trqfvmjtt4sij *)t19), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 168), t21);
    }
    {
        void * t22 = gm_l0;
        gm_tnpifv20irnph t23 = gm_l3;
        GM_ST(gm_tnpifv20irnph, (((uint8_t *)t22) + 176), t23);
    }
    {
        void * t24 = gm_l0;
        uint8_t *t25 = (((uint8_t *)t24) + 192);
        gm_f_list_7ltkg0u((void *)&(*(gm_trqfvmjtt4sij *)t25), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t26 = gm_l0;
        uint8_t *t27 = (((uint8_t *)t26) + 232);
        gm_f_list_lgereu6((void *)&(*(gm_trqfvmjtt4sij *)t27), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        GM_ST(uint32_t, (((uint8_t *)t28) + 272), ((uint32_t)0ULL));
    }
    {
        void * t29 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t29) + 520), ((uint8_t)0));
    }
    {
        void * t30 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t30) + 528), ((uint64_t)0ULL));
    }
    {
        void * t31 = gm_l0;
        uint32_t t32 = gm_l1;
        gm_t9805qtonbsag t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t33), t32);
        ((uint8_t *)&t33)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)t31) + 400), t33);
    }
    {
        void * t34 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t34) + 696), ((uint8_t)0));
    }
    {
        void * t35 = gm_l0;
        gm_b0_1 t36 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t35);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exit_fn_kj6vbu8 F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 1706u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t2);
    }
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 576);
        gm_ta7fvse6v13pf t5;
        gm_f_pop_f3uajlg_2((void *)&t5, t4);
        gm_ta7fvse6v13pf t6 = t5;
        gm_tjhr0bdm69ios t7;
        if (((((uint8_t *)&t6))[384] != 0)) {
            gm_tjhr0bdm69ios t8 = GM_LD(gm_tjhr0bdm69ios, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            memset((uint8_t *)&(*(gm_tk9n3vlila9qg *)gm_out) + 33, 0, 7);
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 16) + 0));
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 192) + 0));
            void * t13 = gm_l0;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 696));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tk9n3vlila9qg *)gm_out) + 0, t10);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tk9n3vlila9qg *)gm_out) + 16, t12);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tk9n3vlila9qg *)gm_out) + 32, t14);
            goto gm_fdone;
        }
        F.l1 = t7;
    }
    {
        memset((uint8_t *)&F.l2 + 33, 0, 7);
        void * t15 = gm_l0;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + 16) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 0, t16);
        void * t17 = gm_l0;
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)t17) + 192) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 16, t18);
        void * t19 = gm_l0;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t19) + 696));
        GM_ST(uint8_t, (uint8_t *)&F.l2 + 32, t20);
    }
    {
        void * t21 = gm_l0;
        gm_f_load_frame_r6k3kmp(t21, &(*(gm_tjhr0bdm69ios *)((uint8_t *)&F.l1)));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tk9n3vlila9qg gm_f_exit_fn_kj6vbu8_c(void * a0) { gm_tk9n3vlila9qg gm_r; gm_f_exit_fn_kj6vbu8(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expect_bool_cdo5g1q F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint8_t gm_l4; /* mask */
    gm_tlcm4lvu3g1a8 gm_l5; /* p */
    gm_b16_8 gm_l6; /* hint */
    GM_STACK_CHECK(&F, 567u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        void * t5 = gm_l1;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        gm_f_kind_jcnt4hm((void *)&F.l2, t4, GM_CONV(gm_csguankko4lmhd, t6));
    }
    {
        uint8_t t8 = (!(GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)4ULL)));
        if (!t8) goto ie7;
        uint8_t t9 = (!(GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)5ULL)));
        if (!t9) goto ie7;
        uint8_t t10 = (!(GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)26ULL)));
        if (!t10) goto ie7;
        {
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 96);
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            void * t16 = gm_l1;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
            gm_t88rcucg1b7uk t18 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t13, t15, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t18;
        }
        {
            gm_l4 = ((uint8_t)0);
        }
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            void * t22 = (void *)(((uint8_t *)t21) + 8);
            gm_t88rcucg1b7uk t23 = gm_l3;
            gm_tb7u19loo3j87 t24;
            gm_f_vector_parts_crrfopm((void *)&t24, t22, GM_CONV(gm_csguankko4lmhd, t23));
            gm_tb7u19loo3j87 t25 = t24;
            if (!((((uint8_t *)&t25))[16] != 0)) goto ie19;
            gm_tlcm4lvu3g1a8 t26 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t25));
            gm_l5 = t26;
            {
                void * t28 = gm_l0;
                void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                void * t30 = (void *)(((uint8_t *)t29) + 8);
                gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                uint8_t t32 = gm_f_is_bool_ajhjgeg(t30, GM_CONV(gm_csguankko4lmhd, t31));
                gm_l4 = t32;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            gm_b16_8 t33;
            uint8_t t35 = gm_l4;
            if (!t35) goto ie34;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s069kf53uhd4o));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(44));
            gm_b16_8 t38 = t37;
            t33 = t38;
            goto ix34;
ie34:;
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_she92i1u1a4v5));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(29));
            gm_b16_8 t41 = t40;
            t33 = t41;
ix34:;
            gm_l6 = t33;
        }
        {
            void * t42 = gm_l0;
            void * t43 = gm_l1;
            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
            gm_t6j6smj7qoctu t45;
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l3;
            gm_b16_8 t48 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 0, t48);
            gm_b16_8 t49 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 16, t49);
            gm_tabb710d9mq2e t50;
            gm_f_format_biupho2((void *)&t50, (void *)NULL, cx, NULL, &t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t51 = t50;
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src276s8986o4r3, 935, 26);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
            gm_b16_8 t53 = t52;
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t42, t44, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t56 = gm_l1;
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
__attribute__((aligned(4))) void * gm_f_finish_block_body_4vol3tv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_finish_block_body_4vol3tv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* block */
    void * gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l3; /* ret */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ok */
    gm_tl874t2atnlh3 gm_l6; /* span */
    void * gm_l7; /* success */
    GM_STACK_CHECK(&F, 776u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 168));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_l4 = t6;
    }
    {
        void * t8 = gm_l4;
        gm_t88rcucg1b7uk t9 = gm_l3;
        gm_t53at80pfdus3 t10;
        gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
        gm_t53at80pfdus3 t11 = t10;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t12 != ((uint8_t)14ULL)) goto ie7;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t11) + 8) + 4));
        gm_l5 = t13;
        void * t14 = gm_l4;
        gm_t88rcucg1b7uk t15 = gm_l5;
        uint8_t t16 = gm_f_is_unit_n83ttnb(t14, GM_CONV(gm_csguankko4lmhd, t15));
        if (!t16) goto ie7;
        void * t17 = gm_l4;
        void * t18 = gm_l1;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + 0));
        gm_t53at80pfdus3 t20;
        gm_f_kind_jcnt4hm((void *)&t20, t17, GM_CONV(gm_csguankko4lmhd, t19));
        gm_t53at80pfdus3 t21 = t20;
        uint8_t t22 = (!(GM_LD(uint8_t, ((uint8_t *)&t21) + 0) == ((uint8_t)5ULL)));
        if (!t22) goto ie7;
        {
            void * t24 = gm_l2;
            uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)t24) + 24) + 0));
            void * t26 = gm_l2;
            uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t26) + 24) + 8));
            void * t28 = gm_l2;
            uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)t28) + 24) + 8));
            gm_tl874t2atnlh3 t30;
            GM_ST(uint32_t, ((uint8_t *)&t30 + 0), t25);
            GM_ST(uint32_t, ((uint8_t *)&t30 + 4), t27);
            GM_ST(uint32_t, ((uint8_t *)&t30 + 8), t29);
            gm_l6 = t30;
        }
        {
            void * t31 = gm_l0;
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
            gm_tl874t2atnlh3 t34 = gm_l6;
            void * t35 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t36 = gm_l3;
            void * t37 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t31, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t37;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t38 = (void *)((uint8_t *)&F.l8);
            void * t39 = gm_l1;
            gm_trcmmd12cefb1 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
            gm_tftb1odc739t5 t41 = gm_f_push_d6c76ud_2(t38, &t40);
            gm_tftb1odc739t5 t42 = t41;
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_src276s8986o4r3, 689, 9);
            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
        }
        {
            gm_t88rcucg1b7uk t44 = gm_l3;
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            void * t46 = gm_l7;
            void * t47 = gm_l2;
            gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 24));
            void * t49 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t44, t45, t46, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t52 = gm_l4;
        gm_t88rcucg1b7uk t53 = gm_l3;
        uint8_t t54 = gm_f_is_unit_n83ttnb(t52, GM_CONV(gm_csguankko4lmhd, t53));
        uint8_t t55 = (uint8_t)!t54;
        if (!t55) goto ie51;
        void * t56 = gm_l4;
        void * t57 = gm_l1;
        gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t57) + 0));
        gm_t53at80pfdus3 t59;
        gm_f_kind_jcnt4hm((void *)&t59, t56, GM_CONV(gm_csguankko4lmhd, t58));
        gm_t53at80pfdus3 t60 = t59;
        uint8_t t61 = (!(GM_LD(uint8_t, ((uint8_t *)&t60) + 0) == ((uint8_t)5ULL)));
        if (!t61) goto ie51;
        {
            void * t63 = gm_l0;
            void * t64 = gm_l2;
            gm_t88rcucg1b7uk t65 = gm_l3;
            gm_b0_1 t66 = gm_f_missing_return_s12s32d((void *)NULL, cx, NULL, t63, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        void * t67 = gm_l1;
        uint8_t t68 = 0;
        F.ret = t67;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_finish_m8t0599 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l8; /* root */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    void * gm_l12; /* sig */
    void * gm_l13; /* def */
    GM_STACK_CHECK(&F, 927u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_b0_1 t4 = gm_f_finalize_k1g9q48((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l0;
        gm_b0_1 t6 = gm_f_check_effects_mrg0sal((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 16) + 0) + 8);
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
                    uint64_t t17 = t16;
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t17));
                    gm_l4 = t20;
                    {
                        uint64_t t22 = gm_l4;
                        uint64_t t23 = (uintptr_t)t22;
                        void * t24 = gm_l0;
                        uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 16) + 0) + 8);
                        if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_src276s8986o4r3, 407, 9);
                        uint8_t *t26 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 16) + 0)) + t23 * 40) + 16);
                        void * t27 = gm_l0;
                        void * t28 = (void *)(((uint8_t *)t27) + 96);
                        void * t29 = gm_l0;
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                        uint64_t t31 = gm_l4;
                        uint64_t t32 = (uintptr_t)t31;
                        void * t33 = gm_l0;
                        uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 16) + 0) + 8);
                        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_src276s8986o4r3, 407, 56);
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 16) + 0)) + t32 * 40) + 16));
                        gm_t88rcucg1b7uk t36 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t28, t30, t35);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        GM_ST(gm_t88rcucg1b7uk, t26, t36);
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 406, 5);
me18:;
                }
lc10:;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
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
        {
            void * t43 = gm_l0;
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t43) + 704) + 0));
            gm_l5 = t44;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t46 = gm_l6;
                uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t48 = (t46 < t47);
                if (!t48) goto lb45;
                {
                    uint64_t t51 = gm_l6;
                    uint64_t t52 = (uintptr_t)t51;
                    uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tl93vjpcesj48 t56 = GM_LD(gm_tl93vjpcesj48, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t52 * 48));
                    F.l7 = t56;
                    {
                        void * t59 = gm_l0;
                        if (!(((((uint8_t *)t59) + 400))[4] != 0)) goto ie58;
                        uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)t59) + 400));
                        gm_l8 = t60;
                        {
                            void * t62 = gm_l0;
                            void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
                            void * t64 = (void *)(((uint8_t *)t63) + 2736);
                            uint32_t t65 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 0));
                            uint32_t t66 = gm_l8;
                            gm_tftb1odc739t5 t67 = gm_f_put_33kejrs(t64, t65, t66);
                            gm_tftb1odc739t5 t68 = t67;
                            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_src276s8986o4r3, 410, 31);
                            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
                        }
                        goto ix58;
ie58:;
ix58:;
                    }
                    {
                        void * t70 = gm_l0;
                        void * t71 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
                        gm_b0_1 t72 = gm_f_fin_expr_rk98p2r((void *)NULL, cx, NULL, t70, t71);
                        if (gm_unw.kind) {
                            goto gm_ll49;
                        }
                    }
                    {
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t74 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 24) + 8);
                            gm_l9 = t74;
                        }
                        {
                            for (;;) {
                                uint64_t t76 = gm_l10;
                                uint64_t t77 = gm_l9;
                                uint8_t t78 = (t76 < t77);
                                if (!t78) goto lb75;
                                {
                                    uint64_t t81 = gm_l10;
                                    uint64_t t82 = t81;
                                    uint64_t t85 = GM_LD(uint64_t, ((uint8_t *)&t82));
                                    gm_l11 = t85;
                                    {
                                        uint64_t t87 = gm_l11;
                                        uint64_t t88 = (uintptr_t)t87;
                                        uint64_t t89 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 24) + 8);
                                        if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_src276s8986o4r3, 412, 36);
                                        uint8_t *t90 = (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 24)) + t88 * 40) + 16);
                                        void * t91 = gm_l0;
                                        void * t92 = (void *)(((uint8_t *)t91) + 96);
                                        void * t93 = gm_l0;
                                        void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
                                        uint64_t t95 = gm_l11;
                                        uint64_t t96 = (uintptr_t)t95;
                                        uint64_t t97 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 24) + 8);
                                        if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_src276s8986o4r3, 412, 76);
                                        gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 24)) + t96 * 40) + 16));
                                        gm_t88rcucg1b7uk t99 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t92, t94, t98);
                                        if (gm_unw.kind) {
                                            goto gm_ll79;
                                        }
                                        GM_ST(gm_t88rcucg1b7uk, t90, t99);
                                    }
                                    goto me83;
mn84:;
                                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 412, 9);
me83:;
                                }
lc75:;
                                {
                                    uint64_t t101 = gm_l10;
                                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                                    uint64_t t104 = r102;
                                    gm_l10 = t104;
                                }
                            }
gm_ll79:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
                            }
                            goto gm_ll49;
lb75:;
                        }
                    }
                    {
                        uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 0));
                        uint64_t t106 = (uint64_t)t105;
                        uint64_t t107 = (uintptr_t)t106;
                        void * t108 = gm_l0;
                        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
                        uint64_t t110 = GM_LD(uintptr_t, ((((uint8_t *)t109) + 344) + 0) + 8);
                        if (t107 >= (uintptr_t)(t110)) gm_trap_index(t107, t110, gm_src276s8986o4r3, 414, 17);
                        void * t111 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t109) + 344) + 0)) + t107 * 136);
                        gm_l12 = t111;
                    }
                    {
                        void * t112 = gm_l12;
                        uint8_t *t113 = (((uint8_t *)t112) + 56);
                        void * t114 = gm_l0;
                        void * t115 = (void *)(((uint8_t *)t114) + 96);
                        void * t116 = gm_l0;
                        void * t117 = GM_LD(void *, (((uint8_t *)t116) + 0));
                        void * t118 = gm_l12;
                        gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t118) + 56));
                        gm_t88rcucg1b7uk t120 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t115, t117, t119);
                        if (gm_unw.kind) {
                            goto gm_ll49;
                        }
                        GM_ST(gm_t88rcucg1b7uk, t113, t120);
                    }
                    {
                        uint32_t t121 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 0));
                        uint64_t t122 = (uint64_t)t121;
                        uint64_t t123 = (uintptr_t)t122;
                        void * t124 = gm_l0;
                        void * t125 = GM_LD(void *, (((uint8_t *)t124) + 0));
                        uint64_t t126 = GM_LD(uintptr_t, ((((uint8_t *)t125) + 384) + 0) + 8);
                        if (t123 >= (uintptr_t)(t126)) gm_trap_index(t123, t126, gm_src276s8986o4r3, 416, 17);
                        void * t127 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t125) + 384) + 0)) + t123 * 64);
                        gm_l13 = t127;
                    }
                    {
                        void * t128 = gm_l13;
                        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
                        GM_ST(gm_b16_8, (((uint8_t *)t128) + 8), t129);
                    }
                    {
                        void * t130 = gm_l13;
                        gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
                        GM_ST(gm_b16_8, (((uint8_t *)t130) + 24), t131);
                    }
                    {
                        void * t132 = gm_l13;
                        void * t133 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
                        GM_ST(void *, (((uint8_t *)t132) + 40), t133);
                    }
                    {
                        void * t134 = gm_l13;
                        GM_ST(uint8_t, (((uint8_t *)t134) + 56), ((uint8_t)1));
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 409, 5);
me54:;
                }
lc45:;
                {
                    uint64_t t136 = gm_l6;
                    uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                    uint64_t t139 = r137;
                    gm_l6 = t139;
                }
            }
gm_ll49:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb45; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc45; }
            }
            goto gm_lad;
lb45:;
        }
    }
    {
        void * t140 = gm_l0;
        void * t141 = (void *)(((uint8_t *)t140) + 704);
        gm_f_clear_vns7pn2(t141);
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
inline __attribute__((aligned(4))) void * gm_f_fn_body_37q3l3a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_body_37q3l3a F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    void * gm_l3; /* e */
    void * gm_l5; /* checked */
    GM_STACK_CHECK(&F, 259u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 168));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t8 != ((uint8_t)1ULL)) goto mn7;
        void * t9 = GM_LD(void *, (((uint8_t *)t4) + 8));
        gm_l3 = t9;
        void * t10 = gm_l0;
        void * t11 = gm_l0;
        void * t12 = gm_l3;
        gm_t88rcucg1b7uk t13 = gm_l2;
        gm_tkn42u7na3t81 t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[4] = 1;
        void * t15 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t11, t12, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t16 = gm_l2;
        void * t17 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t10, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t17;
        goto me6;
mn7:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t19 != ((uint8_t)0ULL)) goto mn18;
        gm_t79rbdv4rafe5 t20 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)t4) + 8));
        F.l4 = t20;
        void * t21 = gm_l0;
        void * t22 = gm_l0;
        void * t23 = (void *)((uint8_t *)&F.l4);
        void * t24 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t22, t23, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t25 = (void *)((uint8_t *)&F.l4);
        void * t26 = gm_f_finish_block_body_4vol3tv((void *)NULL, cx, NULL, t21, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t26;
        goto me6;
mn18:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 658, 16);
me6:;
        gm_l5 = t5;
    }
    {
        void * t27 = gm_l0;
        void * t28 = gm_l5;
        void * t29 = gm_f_declare_temps_a7fc14v((void *)NULL, cx, NULL, t27, ((uint64_t)0ULL), t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = 0;
        F.ret = t29;
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
inline __attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3) {
    struct gm_fr_fn_cx_hgvq15e F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_tnpifv20irnph gm_l3; /* declared */
    GM_STACK_CHECK(&F, 1324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_t7903ef((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l4);
        gm_trqfvmjtt4sij t3;
        gm_f_list_fs7kvfg((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t4 = gm_f_push_00ljs05(t2, &t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src276s8986o4r3, 280, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_f_list_go2qg0b((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l5);
        gm_tftb1odc739t5 t8 = gm_f_push_im9d4ro(t7, ((uint32_t)0ULL));
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_src276s8986o4r3, 282, 5);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 172, 0, 4);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 276, 0, 4);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 428, 0, 4);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 473, 0, 7);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 521, 0, 7);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 697, 0, 7);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 745, 0, 3);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 756, 0, 4);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1241, 0, 7);
        memset((uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1289, 0, 7);
        void * t11 = gm_l0;
        uint64_t t12 = gm_l1;
        gm_trqfvmjtt4sij t13;
        gm_f_list_p9s2t99((void *)&t13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t14 = F.l4;
        gm_tti6qnvf9jg1p t15;
        gm_f_infer_table_6o0km8j((void *)&t15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_tnpifv20irnph t17 = gm_l3;
        gm_trqfvmjtt4sij t18;
        gm_f_list_7ltkg0u((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t19;
        gm_f_list_lgereu6((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t20;
        gm_f_list_d0n30gc((void *)&t20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t21;
        gm_f_list_88khgsi((void *)&t21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t22;
        gm_f_list_7s4om10((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t23;
        memset(&t23, 0, sizeof t23);
        gm_trj4m8pa99qp7 t24;
        memset(&t24, 0, sizeof t24);
        gm_trqfvmjtt4sij t25;
        gm_f_list_5tqpoea((void *)&t25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t26;
        gm_f_list_0ouok33((void *)&t26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t27;
        gm_f_list_7s4om10((void *)&t27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t28;
        gm_f_list_g3vmkan_2((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t29 = F.l5;
        gm_trqfvmjtt4sij t30;
        gm_f_list_clrolmo((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t31;
        gm_f_list_rp32tbu((void *)&t31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t32;
        memset(&t32, 0, sizeof t32);
        gm_trqfvmjtt4sij t33;
        gm_f_list_go2qg0b((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t403eokj7ifg9 t34;
        memset(&t34, 0, sizeof t34);
        gm_trqfvmjtt4sij t35;
        gm_f_list_fu87ckm((void *)&t35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t36;
        gm_f_list_7s4om10((void *)&t36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t37;
        gm_f_list_084fcd6((void *)&t37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t38;
        gm_f_list_tmk0paj((void *)&t38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t39;
        gm_f_list_6dnse27((void *)&t39, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t40;
        gm_f_list_7ltkg0u((void *)&t40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t41;
        gm_f_list_ir0h3tc_2((void *)&t41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t42;
        gm_f_list_go2qg0b((void *)&t42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t43;
        gm_f_list_7s4om10((void *)&t43, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 0, t11);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 8, t12);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 16, t13);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 56, t14);
        GM_ST(gm_tti6qnvf9jg1p, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 96, t15);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 168, t16);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 176, t17);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 192, t18);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 232, t19);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 272, ((uint32_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 280, t20);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 320, t21);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 360, t22);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 400, t23);
        GM_ST(gm_trj4m8pa99qp7, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 408, t24);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 432, t25);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 472, ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 480, t26);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 520, ((uint8_t)0));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 528, ((uint64_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 536, t27);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 576, t28);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 616, t29);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 656, t30);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 696, ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 704, t31);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 744, ((uint8_t)0));
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 748, t32);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 760, t33);
        GM_ST(gm_t403eokj7ifg9, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 800, t34);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 920, t35);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 960, t36);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1000, t37);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1040, t38);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1080, t39);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1120, t40);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1160, t41);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1200, t42);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1240, ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1248, t43);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1288, ((uint8_t)0));
        GM_ST(void *, (uint8_t *)&(*(gm_tqgtfeqqnkjsa *)gm_out) + 1296, ((void *)0));
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
inline __attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_forget_label_breaks_vl4mn72 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* id */
    uint8_t gm_l2; /* found */
    uint64_t gm_l3; /* i */
    uint32_t gm_l4; /* depth */
    GM_STACK_CHECK_SHALLOW(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_depth_fr7pn6n(t2);
        gm_l4 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l3;
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 432) + 0) + 8);
            uint8_t t8 = (t5 < t7);
            if (!t8) goto lb4;
            {
                uint64_t t12 = gm_l3;
                uint64_t t13 = (uintptr_t)t12;
                void * t14 = gm_l0;
                uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 432) + 0) + 8);
                if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src276s8986o4r3, 961, 12);
                uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 432) + 0)) + t13 * 24) + 4));
                uint32_t t17 = gm_l1;
                uint8_t t18 = ((t16 == t17));
                if (!t18) goto ie11;
                uint64_t t19 = gm_l3;
                uint64_t t20 = (uintptr_t)t19;
                void * t21 = gm_l0;
                uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 432) + 0) + 8);
                if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_src276s8986o4r3, 961, 53);
                uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 432) + 0)) + t20 * 24) + 0));
                uint32_t t24 = gm_l4;
                uint8_t t25 = ((t23 == t24));
                if (!t25) goto ie11;
                {
                    gm_l2 = ((uint8_t)1);
                }
                {
                    {
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 432);
                        uint64_t t30 = gm_l3;
                        gm_tgk5j0pob2pqa t31;
                        gm_f_remove_v59hrnl((void *)&t31, t29, t30);
                    }
                }
                goto ix11;
ie11:;
                {
                    uint64_t t33 = gm_l3;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    if (o35) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 965, 13);
                    uint64_t t36 = r34;
                    gm_l3 = t36;
                }
ix11:;
            }
lc4:;
        }
lb4:;
    }
    {
        uint8_t t37 = gm_l2;
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_load_frame_r6k3kmp F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 499u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 0));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 16), t3);
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 40));
        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t4) + 168), t5);
    }
    {
        void * t6 = gm_l0;
        gm_tnpifv20irnph t7 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)a1) + 48));
        GM_ST(gm_tnpifv20irnph, (((uint8_t *)t6) + 176), t7);
    }
    {
        void * t8 = gm_l0;
        gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 64));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t8) + 192), t9);
    }
    {
        void * t10 = gm_l0;
        gm_trqfvmjtt4sij t11 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 104));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t10) + 232), t11);
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)a1) + 144));
        GM_ST(uint32_t, (((uint8_t *)t12) + 272), t13);
    }
    {
        void * t14 = gm_l0;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a1) + 148));
        GM_ST(uint8_t, (((uint8_t *)t14) + 520), t15);
    }
    {
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)a1) + 152));
        GM_ST(uint64_t, (((uint8_t *)t16) + 528), t17);
    }
    {
        void * t18 = gm_l0;
        gm_t9805qtonbsag t19 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)a1) + 160));
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)t18) + 400), t19);
    }
    {
        void * t20 = gm_l0;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a1) + 168));
        GM_ST(uint8_t, (((uint8_t *)t20) + 696), t21);
    }
    {
        void * t22 = gm_l0;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a1) + 169));
        GM_ST(uint8_t, (((uint8_t *)t22) + 744), t23);
    }
    {
        void * t24 = gm_l0;
        gm_tkn42u7na3t81 t25 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)a1) + 172));
        GM_ST(gm_tkn42u7na3t81, (((uint8_t *)t24) + 748), t25);
    }
    {
        void * t26 = gm_l0;
        gm_t403eokj7ifg9 t27 = GM_LD(gm_t403eokj7ifg9, (((uint8_t *)a1) + 184));
        GM_ST(gm_t403eokj7ifg9, (((uint8_t *)t26) + 800), t27);
    }
    {
        void * t28 = gm_l0;
        gm_trqfvmjtt4sij t29 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 304));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t28) + 1000), t29);
    }
    {
        void * t30 = gm_l0;
        gm_trqfvmjtt4sij t31 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 344));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t30) + 1200), t31);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_load_frame_r6k3kmp_c(void * a0, gm_tjhr0bdm69ios a1) { gm_f_load_frame_r6k3kmp(a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_local_at_p388mm4(void *gm_out, void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_at_p388mm4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* depth */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 215u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint32_t t5 = gm_f_depth_fr7pn6n(t4);
        uint8_t t6 = ((t3 == t5));
        if (!t6) goto ie2;
        {
            uint32_t t8 = gm_l2;
            uint64_t t9 = (uint64_t)t8;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 16) + 0) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_src276s8986o4r3, 555, 37);
            gm_tsoie1ppgdvcg t13 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 16) + 0)) + t10 * 40));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t15 = gm_l2;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        uint32_t t18 = gm_l1;
        uint64_t t19 = (uint64_t)t18;
        uint64_t t20 = (uintptr_t)t19;
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 576) + 0) + 8);
        if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_src276s8986o4r3, 556, 12);
        uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 576) + 0)) + t20 * 384) + 0) + 0) + 8);
        if (t17 >= (uintptr_t)(t23)) gm_trap_index(t17, t23, gm_src276s8986o4r3, 556, 12);
        gm_tsoie1ppgdvcg t24 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 576) + 0)) + t20 * 384) + 0) + 0)) + t17 * 40));
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tsoie1ppgdvcg gm_f_local_at_p388mm4_c(void * a0, uint32_t a1, uint32_t a2) { gm_tsoie1ppgdvcg gm_r; gm_f_local_at_p388mm4(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_local_6f38t7p F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* scope */
    uint64_t gm_l4; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 933u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 56) + 0) + 8);
        gm_l2 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 486, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t13 = gm_l2;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 56) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src276s8986o4r3, 487, 18);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 56) + 0)) + t14 * 40) + 0));
                gm_l3 = t17;
            }
            {
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l4 = t18;
            }
            {
                for (;;) {
                    uint64_t t20 = gm_l4;
                    uint8_t t21 = (t20 > ((uint64_t)0ULL));
                    if (!t21) goto lb19;
                    {
                        uint64_t t24 = gm_l4;
                        uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 490, 13);
                        uint64_t t27 = r25;
                        gm_l4 = t27;
                    }
                    {
                        uint64_t t29 = gm_l4;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_src276s8986o4r3, 491, 16);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 224) + 0));
                        gm_b16_8 t33 = gm_l1;
                        gm_b16_8 t34 = t32;
                        gm_b16_8 t35 = t33;
                        uint8_t t36 = (gm_eq0iqiubfm7e(((uint8_t *)&t34), ((uint8_t *)&t35)));
                        if (!t36) goto ie28;
                        {
                            uint64_t t38 = gm_l4;
                            uint64_t t39 = (uintptr_t)t38;
                            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_src276s8986o4r3, 491, 47);
                            gm_t930q5l38hdap t41 = GM_LD(gm_t930q5l38hdap, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t39 * 224) + 16));
                            gm_tqvu78rpi6q1h t42;
                            GM_ST(uint8_t, (uint8_t *)&t42 + 209, 0);
                            GM_ST(uint16_t, (uint8_t *)&t42 + 210, 0);
                            GM_ST(uint32_t, (uint8_t *)&t42 + 212, 0);
                            GM_ST(gm_t930q5l38hdap, ((uint8_t *)&t42), t41);
                            ((uint8_t *)&t42)[208] = 1;
                            uint8_t t43 = 0;
                            F.ret = t42;
                            goto gm_fret;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
lc19:;
                }
lb19:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_tqvu78rpi6q1h t44;
        memset(&t44, 0, sizeof t44);
        uint8_t t45 = 0;
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tqvu78rpi6q1h gm_f_lookup_local_6f38t7p_c(void * a0, gm_b16_8 a1) { gm_tqvu78rpi6q1h gm_r; gm_f_lookup_local_6f38t7p(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_lookup_local_at_ntm4scs(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_local_at_ntm4scs F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* scope */
    uint64_t gm_l4; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 993u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 56) + 0) + 8);
        gm_l2 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 535, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t13 = gm_l2;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 56) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src276s8986o4r3, 536, 18);
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 56) + 0)) + t14 * 40) + 0));
                gm_l3 = t17;
            }
            {
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l4 = t18;
            }
            {
                for (;;) {
                    uint64_t t20 = gm_l4;
                    uint8_t t21 = (t20 > ((uint64_t)0ULL));
                    if (!t21) goto lb19;
                    {
                        uint64_t t24 = gm_l4;
                        uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 539, 13);
                        uint64_t t27 = r25;
                        gm_l4 = t27;
                    }
                    {
                        uint64_t t29 = gm_l4;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_src276s8986o4r3, 540, 16);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 224) + 0));
                        gm_b16_8 t33 = gm_l1;
                        gm_b16_8 t34 = t32;
                        gm_b16_8 t35 = t33;
                        uint8_t t36 = (gm_eq0iqiubfm7e(((uint8_t *)&t34), ((uint8_t *)&t35)));
                        if (!t36) goto ie28;
                        {
                            gm_tfj8gdjm726n4 t38;
                            memset((uint8_t *)&t38 + 212, 0, 4);
                            uint64_t t39 = gm_l4;
                            uint64_t t40 = (uintptr_t)t39;
                            uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src276s8986o4r3, 540, 57);
                            memcpy((uint8_t *)&t38 + 0, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t40 * 224) + 16), 208);
                            uint64_t t42 = gm_l2;
                            uint64_t t43 = (uintptr_t)t42;
                            void * t44 = gm_l0;
                            uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 616) + 0) + 8);
                            if (t43 >= (uintptr_t)(t45)) gm_trap_index(t43, t45, gm_src276s8986o4r3, 540, 80);
                            uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t44) + 616) + 0)) + t43 * 4));
                            GM_ST(uint32_t, (uint8_t *)&t38 + 208, t46);
                            gm_tsf6kp4ohnuqs t47;
                            GM_ST(uint8_t, (uint8_t *)&t47 + 217, 0);
                            GM_ST(uint16_t, (uint8_t *)&t47 + 218, 0);
                            GM_ST(uint32_t, (uint8_t *)&t47 + 220, 0);
                            GM_ST(gm_tfj8gdjm726n4, ((uint8_t *)&t47), t38);
                            ((uint8_t *)&t47)[216] = 1;
                            uint8_t t48 = 0;
                            F.ret = t47;
                            goto gm_fret;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
lc19:;
                }
lb19:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_tsf6kp4ohnuqs t49;
        memset(&t49, 0, sizeof t49);
        uint8_t t50 = 0;
        F.ret = t49;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tsf6kp4ohnuqs gm_f_lookup_local_at_ntm4scs_c(void * a0, gm_b16_8 a1) { gm_tsf6kp4ohnuqs gm_r; gm_f_lookup_local_at_ntm4scs(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_missing_return_s12s32d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_missing_return_s12s32d F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    void * gm_l3; /* e */
    uint64_t gm_l4; /* k */
    gm_t88rcucg1b7uk gm_l5; /* ty */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 8) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t8 = gm_l1;
            uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 8) + 8);
            uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
            if (o11) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 699, 32);
            uint64_t t12 = r10;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l1;
            uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)t14) + 8) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src276s8986o4r3, 699, 24);
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t14) + 8)) + t13 * 144) + 8) + 0));
            if (t16 != ((uint8_t)2ULL)) goto ie7;
            void * t17 = GM_LD(void *, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t14) + 8)) + t13 * 144) + 8) + 8));
            gm_l3 = t17;
            void * t18 = gm_l3;
            uint8_t t19;
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            switch (t28) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma26; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma26; case ((uint8_t)13ULL): goto ma26; case ((uint8_t)14ULL): goto ma26; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma26; case ((uint8_t)27ULL): goto ma26; case ((uint8_t)28ULL): goto ma26; case ((uint8_t)29ULL): goto ma26; case ((uint8_t)30ULL): goto ma26; case ((uint8_t)31ULL): goto ma26; case ((uint8_t)32ULL): goto ma26; case ((uint8_t)33ULL): goto ma26; case ((uint8_t)34ULL): goto ma26; case ((uint8_t)35ULL): goto ma26; case ((uint8_t)36ULL): goto ma26; case ((uint8_t)37ULL): goto ma26; case ((uint8_t)38ULL): goto ma26; case ((uint8_t)39ULL): goto ma26; case ((uint8_t)40ULL): goto ma21; case ((uint8_t)41ULL): goto ma22; case ((uint8_t)42ULL): goto ma24; case ((uint8_t)43ULL): goto ma23; case ((uint8_t)44ULL): goto ma25; case ((uint8_t)45ULL): goto ma26; case ((uint8_t)46ULL): goto ma26; case ((uint8_t)47ULL): goto ma26; case ((uint8_t)48ULL): goto ma26; case ((uint8_t)49ULL): goto ma26; case ((uint8_t)50ULL): goto ma26; default: goto ma26; }
ma21:;
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t30 != ((uint8_t)40ULL)) goto mn29;
            t19 = ((uint8_t)0);
            goto me20;
mn29:;
ma22:;
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t32 != ((uint8_t)41ULL)) goto mn31;
            t19 = ((uint8_t)0);
            goto me20;
mn31:;
ma23:;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t34 != ((uint8_t)43ULL)) goto mn33;
            t19 = ((uint8_t)0);
            goto me20;
mn33:;
ma24:;
            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t36 != ((uint8_t)42ULL)) goto mn35;
            t19 = ((uint8_t)0);
            goto me20;
mn35:;
ma25:;
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t38 != ((uint8_t)44ULL)) goto mn37;
            t19 = ((uint8_t)0);
            goto me20;
mn37:;
ma26:;
            t19 = ((uint8_t)1);
            goto me20;
mn39:;
ma27:;
            gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 699, 58);
me20:;
            if (!t19) goto ie7;
            {
                void * t41 = gm_l0;
                uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 320) + 0) + 8);
                gm_l4 = t42;
            }
            {
                for (;;) {
                    uint64_t t44 = gm_l4;
                    uint8_t t45 = (t44 > ((uint64_t)0ULL));
                    if (!t45) goto lb43;
                    {
                        uint64_t t48 = gm_l4;
                        uint64_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint64_t)1ULL), &r49);
                        if (o50) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 702, 17);
                        uint64_t t51 = r49;
                        gm_l4 = t51;
                    }
                    {
                        uint64_t t53 = gm_l4;
                        uint64_t t54 = (uintptr_t)t53;
                        void * t55 = gm_l0;
                        uint64_t t56 = GM_LD(uintptr_t, ((((uint8_t *)t55) + 320) + 0) + 8);
                        if (t54 >= (uintptr_t)(t56)) gm_trap_index(t54, t56, gm_src276s8986o4r3, 703, 45);
                        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 320) + 0)) + t54 * 48) + 0));
                        if (t57 != ((uint8_t)0ULL)) goto ie52;
                        gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 320) + 0)) + t54 * 48) + 8) + 0));
                        gm_l5 = t58;
                        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 320) + 0)) + t54 * 48) + 8) + 4));
                        gm_l6 = t59;
                        {
                            gm_tl874t2atnlh3 t62 = gm_l6;
                            void * t63 = gm_l3;
                            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
                            gm_tl874t2atnlh3 t65 = t62;
                            gm_tl874t2atnlh3 t66 = t64;
                            uint8_t t67 = (!gm_eqff85ac10o3(((uint8_t *)&t65), ((uint8_t *)&t66)));
                            if (!t67) goto ie61;
                            {
                                goto lc43;
                            }
                            goto ix61;
ie61:;
ix61:;
                        }
                        {
                            void * t69 = gm_l0;
                            void * t70 = (void *)(((uint8_t *)t69) + 96);
                            void * t71 = gm_l0;
                            void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
                            gm_t88rcucg1b7uk t73 = gm_l5;
                            gm_t88rcucg1b7uk t74 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t70, t72, t73);
                            if (gm_unw.kind) {
                                goto gm_ll46;
                            }
                            gm_l7 = t74;
                        }
                        {
                            uint8_t t76 = 0;
                            void * t77 = gm_l0;
                            void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
                            void * t79 = (void *)(((uint8_t *)t78) + 8);
                            gm_t88rcucg1b7uk t80 = gm_l7;
                            uint8_t t81 = gm_f_is_unit_n83ttnb(t79, GM_CONV(gm_csguankko4lmhd, t80));
                            if (!t81) {
                                void * t82 = gm_l0;
                                void * t83 = GM_LD(void *, (((uint8_t *)t82) + 0));
                                void * t84 = (void *)(((uint8_t *)t83) + 8);
                                gm_t88rcucg1b7uk t85 = gm_l7;
                                gm_t53at80pfdus3 t86;
                                gm_f_kind_jcnt4hm((void *)&t86, t84, GM_CONV(gm_csguankko4lmhd, t85));
                                gm_t53at80pfdus3 t87 = t86;
                                uint8_t t88 = ((GM_LD(uint8_t, ((uint8_t *)&t87) + 0) == ((uint8_t)5ULL)));
                                t76 = t88;
                            } else t76 = 1;
                            if (!t76) goto ie75;
                            {
                                goto lc43;
                            }
                            goto ix75;
ie75:;
ix75:;
                        }
                        {
                            {
                                void * t91 = gm_l0;
                                void * t92 = (void *)(((uint8_t *)t91) + 320);
                                uint64_t t93 = gm_l4;
                                gm_tpm220j5qhjke t94;
                                gm_f_remove_te9updc((void *)&t94, t92, t93);
                            }
                        }
                        {
                            void * t95 = gm_l0;
                            void * t96 = gm_l3;
                            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
                            void * t98 = gm_l0;
                            gm_t88rcucg1b7uk t99 = gm_l2;
                            gm_b16_8 t100 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t98, t99);
                            if (gm_unw.kind) {
                                goto gm_ll46;
                            }
                            gm_tnpifv20irnph t101;
                            GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t100);
                            gm_tabb710d9mq2e t102;
                            gm_f_format_05vut39((void *)&t102, (void *)NULL, cx, NULL, t101);
                            if (gm_unw.kind) {
                                goto gm_ll46;
                            }
                            gm_tabb710d9mq2e t103 = t102;
                            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 8, gm_src276s8986o4r3, 708, 34);
                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
                            gm_b16_8 t105 = t104;
                            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                            gm_b0_1 t107 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t95, t97, t106);
                            if (gm_unw.kind) {
                                goto gm_ll46;
                            }
                        }
                        {
                            gm_b0_1 t108;
                            uint8_t t109 = 0;
                            F.ret = t108;
                            goto gm_fret;
                        }
                        goto ix52;
ie52:;
                        {
                            goto lc43;
                        }
ix52:;
                    }
lc43:;
                }
gm_ll46:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb43; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc43; }
                }
                goto gm_lad;
lb43:;
            }
            goto ix7;
ie7:;
ix7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t111 = gm_l0;
        void * t112 = gm_l1;
        uint32_t t113 = GM_LD(uint32_t, ((((uint8_t *)t112) + 24) + 0));
        void * t114 = gm_l1;
        uint32_t t115 = GM_LD(uint32_t, ((((uint8_t *)t114) + 24) + 8));
        void * t116 = gm_l1;
        uint32_t t117 = GM_LD(uint32_t, ((((uint8_t *)t116) + 24) + 8));
        gm_tl874t2atnlh3 t118;
        GM_ST(uint32_t, ((uint8_t *)&t118 + 0), t113);
        GM_ST(uint32_t, ((uint8_t *)&t118 + 4), t115);
        GM_ST(uint32_t, ((uint8_t *)&t118 + 8), t117);
        void * t119 = gm_l0;
        gm_t88rcucg1b7uk t120 = gm_l2;
        gm_b16_8 t121 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t119, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t122;
        GM_ST(gm_b16_8, ((uint8_t *)&t122 + 0), t121);
        gm_tabb710d9mq2e t123;
        gm_f_format_s8qs6vn((void *)&t123, (void *)NULL, cx, NULL, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t124 = t123;
        if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_src276s8986o4r3, 713, 76);
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
        gm_b16_8 t126 = t125;
        gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
        gm_b0_1 t128 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t111, t118, t127);
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
__attribute__((aligned(4))) void gm_f_never_cause_p5ir1i2(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_never_cause_p5ir1i2 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l3; /* b */
    void * gm_l4; /* t */
    gm_b16_8 gm_l5; /* why */
    GM_STACK_CHECK(&F, 845u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_b16_8 t3;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma18; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma18; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma18; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma18; case ((uint8_t)21ULL): goto ma18; case ((uint8_t)22ULL): goto ma18; case ((uint8_t)23ULL): goto ma18; case ((uint8_t)24ULL): goto ma18; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma18; case ((uint8_t)27ULL): goto ma18; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma18; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma18; case ((uint8_t)33ULL): goto ma18; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma18; case ((uint8_t)37ULL): goto ma18; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma18; case ((uint8_t)40ULL): goto ma18; case ((uint8_t)41ULL): goto ma18; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma18; case ((uint8_t)44ULL): goto ma15; case ((uint8_t)45ULL): goto ma18; case ((uint8_t)46ULL): goto ma17; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma13; case ((uint8_t)54ULL): goto ma14; case ((uint8_t)55ULL): goto ma18; case ((uint8_t)56ULL): goto ma18; case ((uint8_t)57ULL): goto ma18; case ((uint8_t)58ULL): goto ma16; default: goto ma18; }
ma5:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)48ULL)) goto mn21;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sqc0827akcmi8));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(39));
        t3 = t23;
        goto me4;
mn21:;
ma6:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)51ULL)) goto mn24;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sm9otrmuq5hp3));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(33));
        t3 = t26;
        goto me4;
mn24:;
ma7:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t28 != ((uint8_t)49ULL)) goto mn27;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sscfkpmei7e4o));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(28));
        t3 = t29;
        goto me4;
mn27:;
ma8:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)50ULL)) goto mn30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_ses9h0rg965i4));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(53));
        t3 = t32;
        goto me4;
mn30:;
ma9:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)52ULL)) goto mn33;
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sp6l5tr58ip76));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(22));
        t3 = t35;
        goto me4;
mn33:;
ma10:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)34ULL)) goto mn36;
        gm_tsth9qu1dv0g3 t38 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l2 = t38;
        gm_b16_8 t39;
        uint8_t t41;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t44 != ((uint8_t)1ULL)) goto mn43;
        t41 = ((uint8_t)1);
        goto me42;
mn43:;
        t41 = ((uint8_t)0);
        goto me42;
mn45:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 1016, 32);
me42:;
        if (!t41) goto ie40;
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s7b7j2m1f6fle));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(70));
        gm_b16_8 t48 = t47;
        t39 = t48;
        goto ix40;
ie40:;
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_seiql9tkbkhim));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(51));
        gm_b16_8 t51 = t50;
        t39 = t51;
ix40:;
        t3 = t39;
        goto me4;
mn36:;
ma11:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)47ULL)) goto mn52;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_souep3pirutur));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(54));
        t3 = t54;
        goto me4;
mn52:;
ma12:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t56 != ((uint8_t)42ULL)) goto mn55;
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sjeqed5d3f5pb));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(25));
        t3 = t57;
        goto me4;
mn55:;
ma13:;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t59 != ((uint8_t)53ULL)) goto mn58;
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sroje34o1kfta));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(37));
        t3 = t60;
        goto me4;
mn58:;
ma14:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t62 != ((uint8_t)54ULL)) goto mn61;
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s0rfpnv4c3k1l));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(37));
        t3 = t63;
        goto me4;
mn61:;
ma15:;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t65 != ((uint8_t)44ULL)) goto mn64;
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s993c5b6eq922));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(53));
        t3 = t66;
        goto me4;
mn64:;
ma16:;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t68 != ((uint8_t)58ULL)) goto mn67;
        gm_b16_8 t69;
        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sb3sf0ef436fi));
        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(39));
        t3 = t69;
        goto me4;
mn67:;
ma17:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t71 != ((uint8_t)46ULL)) goto mn70;
        void * t72 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t72;
        {
            void * t75 = gm_l3;
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 16));
            void * t77 = t76;
            if (!(GM_LD(void *, ((uint8_t *)&t77)) != 0)) goto ie74;
            void * t78 = GM_LD(void *, ((uint8_t *)&t77));
            gm_l4 = t78;
            void * t79 = gm_l0;
            void * t80 = (void *)(((uint8_t *)t79) + 8);
            void * t81 = gm_l4;
            gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t81) + 0));
            gm_t53at80pfdus3 t83;
            gm_f_kind_jcnt4hm((void *)&t83, t80, GM_CONV(gm_csguankko4lmhd, t82));
            gm_t53at80pfdus3 t84 = t83;
            uint8_t t85 = ((GM_LD(uint8_t, ((uint8_t *)&t84) + 0) == ((uint8_t)5ULL)));
            if (!t85) goto ie74;
            {
                void * t87 = gm_l0;
                void * t88 = gm_l4;
                gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t87, t88);
                goto gm_fdone;
            }
            goto ix74;
ie74:;
ix74:;
        }
        {
            void * t90 = gm_l3;
            uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)t90) + 0) + 8);
            uint8_t t92 = (t91 > ((uint64_t)0ULL));
            if (!t92) goto ie89;
            {
                void * t94 = gm_l0;
                void * t95 = gm_l3;
                uint64_t t96 = GM_LD(uintptr_t, (((uint8_t *)t95) + 0) + 8);
                uint64_t r97; bool o98 = __builtin_sub_overflow(t96, ((uint64_t)1ULL), &r97);
                if (o98) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 1025, 69);
                uint64_t t99 = r97;
                uint64_t t100 = (uintptr_t)t99;
                void * t101 = gm_l3;
                uint64_t t102 = GM_LD(uintptr_t, (((uint8_t *)t101) + 0) + 8);
                if (t100 >= (uintptr_t)(t102)) gm_trap_index(t100, t102, gm_src276s8986o4r3, 1025, 61);
                gm_f_stmt_never_cause_r2lf089((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t94, &(*(gm_trcmmd12cefb1 *)((uint8_t *)GM_LD(void *, (((uint8_t *)t101) + 0)) + t100 * 32)));
                goto gm_fdone;
            }
            goto ix89;
ie89:;
ix89:;
        }
        gm_tnvnsr1bfvtbd t103;
        memset(&t103, 0, sizeof t103);
        uint8_t t104 = 0;
        F.ret = t103;
        goto gm_fret;
        goto me4;
mn70:;
ma18:;
        gm_tnvnsr1bfvtbd t106;
        memset(&t106, 0, sizeof t106);
        uint8_t t107 = 0;
        F.ret = t106;
        goto gm_fret;
        goto me4;
mn105:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 1010, 19);
me4:;
        gm_l5 = t3;
    }
    {
        gm_tbgd14esf0jvi t108;
        memset((uint8_t *)&t108 + 12, 0, 4);
        void * t109 = gm_l1;
        gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t108 + 0, t110);
        gm_b16_8 t111 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t111);
        gm_tnvnsr1bfvtbd t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 36, 0);
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t112), t108);
        ((uint8_t *)&t112)[32] = 1;
        uint8_t t113 = 0;
        F.ret = t112;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_never_cause_p5ir1i2_c(void * a0, void * a1) { gm_tnvnsr1bfvtbd gm_r; gm_f_never_cause_p5ir1i2(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3) {
    struct gm_fr_new_label_3207tcb F;
    void * gm_l0; /* cx */
    gm_th2lqbo77pvsc gm_l1; /* name */
    uint8_t gm_l2; /* is_loop */
    gm_t1qr0p6kln6tf gm_l3; /* value */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 272));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 272));
        uint32_t r7; bool o8 = __builtin_add_overflow(t6, ((uint32_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 647, 5);
        uint32_t t9 = r7;
        GM_ST(uint32_t, (((uint8_t *)t4) + 272), t9);
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 232);
        gm_t8enl5g7b22jb t12;
        memset((uint8_t *)&t12 + 29, 0, 3);
        memset((uint8_t *)&t12 + 44, 0, 4);
        memcpy((uint8_t *)&t12 + 0, ((uint8_t *)&gm_l1), 24);
        uint32_t t13 = gm_l4;
        GM_ST(uint32_t, (uint8_t *)&t12 + 24, t13);
        uint8_t t14 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&t12 + 28, t14);
        gm_t1qr0p6kln6tf t15 = gm_l3;
        GM_ST(gm_t1qr0p6kln6tf, (uint8_t *)&t12 + 32, t15);
        gm_tftb1odc739t5 t16 = gm_f_push_lng5b06(t11, &t12);
        gm_tftb1odc739t5 t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_src276s8986o4r3, 648, 5);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
        uint32_t t19 = gm_l4;
        uint8_t t20 = 0;
        F.ret = t19;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_new_local_003h9es F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint8_t gm_l3; /* mutable */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 171u);
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
        void * t3 = (void *)(((uint8_t *)t2) + 16);
        gm_tsoie1ppgdvcg t4;
        memset((uint8_t *)&t4 + 21, 0, 3);
        memset((uint8_t *)&t4 + 36, 0, 4);
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_t88rcucg1b7uk t6 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t4 + 16, t6);
        uint8_t t7 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&t4 + 20, t7);
        gm_tl874t2atnlh3 t8 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 24, t8);
        gm_tftb1odc739t5 t9 = gm_f_push_am7bo0v(t3, &t4);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src276s8986o4r3, 430, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 16) + 0) + 8);
        uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_src276s8986o4r3, 431, 13);
        uint64_t t16 = r14;
        uint32_t t17 = (uint32_t)t16;
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_note_capture_0p0af7r(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_note_capture_0p0af7r F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* depth */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint32_t t5 = gm_f_depth_fr7pn6n(t4);
        uint8_t t6 = (t3 >= t5);
        if (!t6) goto ie2;
        {
            gm_b0_1 t8;
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
        GM_ST(uint8_t, (((uint8_t *)t10) + 696), ((uint8_t)1));
    }
    {
        {
            uint32_t t12 = gm_l1;
            uint64_t t13 = (uint64_t)t12;
            uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
            if (o15) gm_trap("integer overflow in `+`", gm_src276s8986o4r3, 563, 14);
            uint64_t t16 = r14;
            gm_l3 = t16;
        }
        {
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 576) + 0) + 8);
            gm_l2 = t18;
        }
        {
            for (;;) {
                uint64_t t20 = gm_l3;
                uint64_t t21 = gm_l2;
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t t26 = t25;
                    uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&t26));
                    gm_l4 = t29;
                    {
                        uint64_t t31 = gm_l4;
                        uint64_t t32 = (uintptr_t)t31;
                        void * t33 = gm_l0;
                        uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 576) + 0) + 8);
                        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_src276s8986o4r3, 563, 53);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 576) + 0)) + t32 * 384) + 168), ((uint8_t)1));
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 563, 5);
me27:;
                }
lc19:;
                {
                    uint64_t t36 = gm_l3;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l3 = t39;
                }
            }
lb19:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_push_scope_mu7j3lg F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 56);
        gm_trqfvmjtt4sij t4;
        gm_f_list_fs7kvfg((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t5 = gm_f_push_00ljs05(t3, &t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src276s8986o4r3, 520, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 616);
        void * t10 = gm_l0;
        uint32_t t11 = gm_f_depth_fr7pn6n(t10);
        gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t9, t11);
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_src276s8986o4r3, 521, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 656);
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        gm_t66j1im9ip128 t19;
        gm_f_scope_mark_i3b2mvo((void *)&t19, t18);
        gm_tftb1odc739t5 t20 = gm_f_push_s39fiuo(t16, &t19);
        gm_tftb1odc739t5 t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_src276s8986o4r3, 522, 5);
        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_temps_upm64pn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_push_temps_upm64pn F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* mark */
    void * gm_l2; /* stmts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* t */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1200) + 0) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 1200) + 0)));
            uint64_t t6 = gm_l1;
            uint64_t t7 = (uintptr_t)t6;
            if (t7 > (uintptr_t)t4 || (uintptr_t)t4 > (uintptr_t)t4) gm_trap_slice(t7, (uintptr_t)t4, t4, gm_src276s8986o4r3, 676, 14);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t7 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)t4 - t7));
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
                    uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 4));
                    uint32_t t19 = t18;
                    uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&t19));
                    gm_l5 = t22;
                    {
                        void * t24 = gm_l2;
                        uint32_t t25 = gm_l5;
                        gm_ttggq7ng4kp3p t26;
                        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t26 + 0), t25);
                        GM_ST(void *, ((uint8_t *)&t26 + 8), ((void *)0));
                        gm_trcmmd12cefb1 t27;
                        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t27 + 8), t26);
                        gm_tftb1odc739t5 t28 = gm_f_push_d6c76ud_2(t24, &t27);
                        gm_tftb1odc739t5 t29 = t28;
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_src276s8986o4r3, 676, 45);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 676, 5);
me20:;
                }
lc9:;
                {
                    uint64_t t32 = gm_l4;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l4 = t35;
                }
            }
lb9:;
        }
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 1200);
        uint64_t t38 = gm_l1;
        gm_f_truncate_eoppkeo(t37, t38);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_save_frame_8libfct(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_save_frame_8libfct F;
    void * gm_l0; /* cx */
    GM_STACK_CHECK_SHALLOW(&F, 499u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 44, 0, 4);
        memset((uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 149, 0, 3);
        memset((uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 170, 0, 2);
        memset((uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 180, 0, 4);
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 16));
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t4) + 168));
        void * t6 = gm_l0;
        gm_tnpifv20irnph t7 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t6) + 176));
        void * t8 = gm_l0;
        gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t8) + 192));
        void * t10 = gm_l0;
        gm_trqfvmjtt4sij t11 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t10) + 232));
        void * t12 = gm_l0;
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 272));
        void * t14 = gm_l0;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 520));
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 528));
        void * t18 = gm_l0;
        gm_t9805qtonbsag t19 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t18) + 400));
        void * t20 = gm_l0;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t20) + 696));
        void * t22 = gm_l0;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t22) + 744));
        void * t24 = gm_l0;
        gm_tkn42u7na3t81 t25 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)t24) + 748));
        void * t26 = gm_l0;
        gm_t403eokj7ifg9 t27 = GM_LD(gm_t403eokj7ifg9, (((uint8_t *)t26) + 800));
        void * t28 = gm_l0;
        gm_trqfvmjtt4sij t29 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t28) + 1000));
        void * t30 = gm_l0;
        gm_trqfvmjtt4sij t31 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t30) + 1200));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 0, t3);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 40, t5);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 48, t7);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 64, t9);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 104, t11);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 144, t13);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 148, t15);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 152, t17);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 160, t19);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 168, t21);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 169, t23);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 172, t25);
        GM_ST(gm_t403eokj7ifg9, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 184, t27);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 304, t29);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tjhr0bdm69ios *)gm_out) + 344, t31);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_save_frame_8libfct_c(void * a0) { gm_tjhr0bdm69ios gm_r; gm_f_save_frame_8libfct(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_stmt_4145gsn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_4145gsn F;
    void * gm_l0; /* cx */
    void * gm_l1; /* s */
    gm_b16_8 gm_l2; /* attrs */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* places */
    gm_tjsp7bhdn2e8j gm_l8; /* op */
    void * gm_l9; /* value */
    void * gm_l10; /* e */
    uint8_t gm_l13; /* is_err */
    void * gm_l14; /* body */
    void * gm_l15; /* w */
    void * gm_l16; /* k */
    void * gm_l17; /* value */
    GM_STACK_CHECK(&F, 1297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma8; default: goto ma13; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t17;
        gm_tv2uf4leil5hq t18 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t2) + 8) + 8) + 16));
        F.l3 = t18;
        {
            {
                gm_b16_8 t21 = gm_l2;
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
                        gm_tkacitepg5fk0 t33 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t29 * 72));
                        F.l6 = t33;
                        {
                            void * t35 = gm_l0;
                            gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 56));
                            gm_b16_8 t37 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 0) + 0));
                            gm_tnpifv20irnph t38;
                            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
                            gm_tabb710d9mq2e t39;
                            gm_f_format_ggjb732((void *)&t39, (void *)NULL, cx, NULL, t38);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            gm_tabb710d9mq2e t40 = t39;
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_src276s8986o4r3, 798, 34);
                            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
                            gm_b16_8 t42 = t41;
                            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                            gm_b0_1 t44 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t35, t36, t43);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                        }
                        goto me31;
mn32:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 797, 13);
me31:;
                    }
lc22:;
                    {
                        uint64_t t46 = gm_l5;
                        uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                        uint64_t t49 = r47;
                        gm_l5 = t49;
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
            void * t50 = gm_l0;
            void * t51 = (void *)((uint8_t *)&F.l3);
            gm_f_binding_igkhmn1((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn15:;
ma5:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)1ULL)) goto mn52;
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t54;
        gm_tjsp7bhdn2e8j t55 = GM_LD(gm_tjsp7bhdn2e8j, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l8 = t55;
        void * t56 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 24));
        gm_l9 = t56;
        void * t57 = gm_l0;
        gm_b16_8 t58 = gm_l7;
        gm_b16_8 t59 = t58;
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
        gm_tjsp7bhdn2e8j t61 = gm_l8;
        void * t62 = gm_l9;
        void * t63 = gm_l1;
        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 128));
        gm_f_assign_a0848vq((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t57, t60, t61, t62, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn52:;
ma6:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t66 != ((uint8_t)2ULL)) goto mn65;
        void * t67 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l10 = t67;
        void * t68 = gm_l0;
        void * t69 = gm_l10;
        void * t70 = gm_f_stmt_expr_evs4ic8((void *)NULL, cx, NULL, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t71 + 8), t70);
        uint8_t t72 = 0;
        F.ret = t71;
        goto gm_fret;
        goto me3;
mn65:;
ma7:;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t74 != ((uint8_t)5ULL)) goto mn73;
        gm_t43d6nbdfap6p t75 = GM_LD(gm_t43d6nbdfap6p, ((((uint8_t *)t2) + 8) + 8));
        F.l11 = t75;
        void * t76 = gm_l0;
        void * t77 = (void *)(((uint8_t *)&F.l11) + 40);
        void * t78 = (void *)(((uint8_t *)&F.l11) + 72);
        void * t79 = gm_l1;
        gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t79) + 128));
        void * t81 = gm_f_while_loop_86vcgbb((void *)NULL, cx, NULL, t76, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l11) + 0)), t77, t78, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t82 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t82 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t82 + 8), t81);
        uint8_t t83 = 0;
        F.ret = t82;
        goto gm_fret;
        goto me3;
mn73:;
ma8:;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t85 != ((uint8_t)8ULL)) goto mn84;
        gm_t79rbdv4rafe5 t86 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l12 = t86;
        void * t87 = gm_l0;
        void * t88 = (void *)((uint8_t *)&F.l12);
        void * t89 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t87, t88, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t90;
        GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t90 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t90 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t90 + 8), t89);
        uint8_t t91 = 0;
        F.ret = t90;
        goto gm_fret;
        goto me3;
mn84:;
ma9:;
        uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t93 != ((uint8_t)3ULL)) goto mn92;
        uint8_t t94 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l13 = t94;
        void * t95 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l14 = t95;
        void * t96 = gm_l0;
        uint8_t t97 = gm_l13;
        void * t98 = gm_l14;
        gm_f_defer_stmt_hm15mru((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t96, t97, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn92:;
ma10:;
        uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t100 != ((uint8_t)4ULL)) goto mn99;
        void * t101 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l15 = t101;
        void * t102 = gm_l0;
        void * t103 = gm_l15;
        void * t104 = gm_f_when_stmt_3pk7eon((void *)NULL, cx, NULL, t102, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t105;
        GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t105 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t105 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t105 + 8), t104);
        uint8_t t106 = 0;
        F.ret = t105;
        goto gm_fret;
        goto me3;
mn99:;
ma11:;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t108 != ((uint8_t)6ULL)) goto mn107;
        void * t109 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l16 = t109;
        void * t110 = gm_l0;
        void * t111 = gm_l16;
        void * t112 = gm_l1;
        gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t112) + 128));
        void * t114 = gm_f_cancel_stmt_pfi7e6b((void *)NULL, cx, NULL, t110, t111, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t115 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t115 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t115 + 8), t114);
        uint8_t t116 = 0;
        F.ret = t115;
        goto gm_fret;
        goto me3;
mn107:;
ma12:;
        uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t118 != ((uint8_t)7ULL)) goto mn117;
        void * t119 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 64));
        gm_l17 = t119;
        {
            void * t121 = gm_l0;
            void * t122 = gm_l1;
            gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 128));
            gm_b16_8 t124;
            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_ss6qrj79r47rb));
            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(69));
            gm_b0_1 t125 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t121, t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t126 = gm_l0;
            void * t127 = gm_l17;
            gm_tkn42u7na3t81 t128;
            memset(&t128, 0, sizeof t128);
            void * t129 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t130 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t130 + 8), t129);
            uint8_t t131 = 0;
            F.ret = t130;
            goto gm_fret;
        }
        goto me3;
mn117:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 795, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_diverges_en1hv79 F;
    void * gm_l0; /* s */
    gm_trcmmd12cefb1 gm_l1; /* st */
    void * gm_l2; /* e */
    void * gm_l3; /* x */
    void * gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma3; case ((uint8_t)5ULL): goto ma7; default: goto ma8; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t11 != ((uint8_t)4ULL)) goto mn10;
    void * t12 = GM_LD(void *, (((uint8_t *)a1) + 8));
    gm_l2 = t12;
    void * t13 = gm_l0;
    void * t14 = (void *)(((uint8_t *)t13) + 8);
    void * t15 = gm_l2;
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 0));
    gm_t53at80pfdus3 t17;
    gm_f_kind_jcnt4hm((void *)&t17, t14, GM_CONV(gm_csguankko4lmhd, t16));
    gm_t53at80pfdus3 t18 = t17;
    uint8_t t19 = ((GM_LD(uint8_t, ((uint8_t *)&t18) + 0) == ((uint8_t)5ULL)));
    t1 = t19;
    goto me2;
mn10:;
ma4:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t21 != ((uint8_t)0ULL)) goto mn20;
    if ((GM_LD(void *, ((((uint8_t *)a1) + 8) + 8)) != 0)) goto mn20;
    t1 = ((uint8_t)0);
    goto me2;
mn20:;
ma5:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t23 != ((uint8_t)0ULL)) goto mn22;
    if (!(GM_LD(void *, ((((uint8_t *)a1) + 8) + 8)) != 0)) goto mn22;
    void * t24 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
    gm_l3 = t24;
    void * t25 = gm_l0;
    void * t26 = (void *)(((uint8_t *)t25) + 8);
    void * t27 = gm_l3;
    gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
    gm_t53at80pfdus3 t29;
    gm_f_kind_jcnt4hm((void *)&t29, t26, GM_CONV(gm_csguankko4lmhd, t28));
    gm_t53at80pfdus3 t30 = t29;
    uint8_t t31 = ((GM_LD(uint8_t, ((uint8_t *)&t30) + 0) == ((uint8_t)5ULL)));
    t1 = t31;
    goto me2;
mn22:;
ma6:;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t33 != ((uint8_t)2ULL)) goto mn32;
    void * t34 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
    gm_l4 = t34;
    void * t35 = gm_l0;
    void * t36 = (void *)(((uint8_t *)t35) + 8);
    void * t37 = gm_l4;
    gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t37) + 0));
    gm_t53at80pfdus3 t39;
    gm_f_kind_jcnt4hm((void *)&t39, t36, GM_CONV(gm_csguankko4lmhd, t38));
    gm_t53at80pfdus3 t40 = t39;
    uint8_t t41 = ((GM_LD(uint8_t, ((uint8_t *)&t40) + 0) == ((uint8_t)5ULL)));
    t1 = t41;
    goto me2;
mn32:;
ma7:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t45 != ((uint8_t)1ULL)) goto pa44;
    goto po43;
pa44:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t47 != ((uint8_t)3ULL)) goto pa46;
    goto po43;
pa46:;
    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t49 != ((uint8_t)5ULL)) goto pa48;
    goto po43;
pa48:;
    goto mn42;
po43:;
    t1 = ((uint8_t)0);
    goto me2;
mn42:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 989, 56);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_stmt_diverges_en1hv79_c(void * a0, gm_trcmmd12cefb1 a1) { return gm_f_stmt_diverges_en1hv79(a0, &a1); }
__attribute__((aligned(4))) void * gm_f_stmt_expr_evs4ic8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_expr_evs4ic8 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* i */
    void * gm_l4; /* f */
    void * gm_l5; /* m */
    void * gm_l6; /* w */
    uint64_t gm_l7; /* errors */
    void * gm_l8; /* x */
    void * gm_l9; /* ex */
    GM_STACK_CHECK(&F, 597u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        void * t3;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma5; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; default: goto ma10; }
ma5:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)40ULL)) goto mn13;
        void * t15 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        void * t16 = gm_l0;
        void * t17 = gm_l2;
        gm_tkn42u7na3t81 t18;
        memset(&t18, 0, sizeof t18);
        void * t19 = gm_f_if_expr_tiak47t((void *)NULL, cx, NULL, t16, t17, ((uint8_t)0), t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t3 = t19;
        goto me4;
mn13:;
ma6:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)44ULL)) goto mn20;
        gm_t79rbdv4rafe5 t22 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l3 = t22;
        void * t23 = gm_l0;
        void * t24 = (void *)((uint8_t *)&F.l3);
        void * t25 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t23, t24, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t3 = t25;
        goto me4;
mn20:;
ma7:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)43ULL)) goto mn26;
        void * t28 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t28;
        void * t29 = gm_l0;
        void * t30 = gm_l4;
        void * t31 = gm_f_for_loop_b6hjsbq((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t3 = t31;
        goto me4;
mn26:;
ma8:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)41ULL)) goto mn32;
        void * t34 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t34;
        void * t35 = gm_l0;
        void * t36 = gm_l5;
        gm_tkn42u7na3t81 t37;
        memset(&t37, 0, sizeof t37);
        void * t38 = gm_f_match_expr_npcoo60((void *)NULL, cx, NULL, t35, t36, ((uint8_t)0), t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t3 = t38;
        goto me4;
mn32:;
ma9:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)42ULL)) goto mn39;
        void * t41 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t41;
        void * t42 = gm_l0;
        void * t43 = gm_l6;
        gm_tkn42u7na3t81 t44;
        memset(&t44, 0, sizeof t44);
        void * t45 = gm_f_with_expr_vi78dtj((void *)NULL, cx, NULL, t42, t43, ((uint8_t)0), t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t3 = t45;
        goto me4;
mn39:;
ma10:;
        {
            void * t48 = gm_l0;
            void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 8));
            uint64_t t52 = gm_f_error_count_omtt6qd(t51);
            gm_l7 = t52;
        }
        {
            void * t53 = gm_l0;
            void * t54 = gm_l1;
            gm_tkn42u7na3t81 t55;
            memset(&t55, 0, sizeof t55);
            void * t56 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t56;
        }
        {
            void * t58 = gm_l0;
            void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
            void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
            void * t61 = GM_LD(void *, (((uint8_t *)t60) + 8));
            uint64_t t62 = gm_f_error_count_omtt6qd(t61);
            uint64_t t63 = gm_l7;
            uint8_t t64 = (!(t62 == t63));
            if (!t64) goto ie57;
            {
                void * t66 = gm_l8;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix57;
ie57:;
ix57:;
        }
        void * t68 = gm_l8;
        void * t69 = t68;
        t3 = t69;
        goto me4;
mn46:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 865, 11);
me4:;
        gm_l9 = t3;
    }
    {
        void * t70 = gm_l0;
        void * t71 = (void *)(((uint8_t *)t70) + 320);
        void * t72 = gm_l9;
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + 0));
        void * t74 = gm_l1;
        gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t74) + 72));
        gm_t2i8ne5ibd7v4 t76;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76 + 0), t73);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t76 + 4), t75);
        gm_tpm220j5qhjke t77;
        GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t77 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t77 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t77 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)&t77 + 8), t76);
        gm_tftb1odc739t5 t78 = gm_f_push_7koe9lk(t71, &t77);
        gm_tftb1odc739t5 t79 = t78;
        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_src276s8986o4r3, 880, 5);
        gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
    }
    {
        void * t81 = gm_l9;
        uint8_t t82 = 0;
        F.ret = t81;
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
inline __attribute__((aligned(4))) void gm_f_stmt_never_cause_r2lf089(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_never_cause_r2lf089 F;
    void * gm_l0; /* s */
    gm_trcmmd12cefb1 gm_l1; /* st */
    void * gm_l2; /* e */
    void * gm_l3; /* x */
    void * gm_l4; /* v */
    GM_STACK_CHECK(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma3; case ((uint8_t)5ULL): goto ma7; default: goto ma8; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t11 != ((uint8_t)4ULL)) goto mn10;
        void * t12 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
        gm_l2 = t12;
        void * t13 = gm_l0;
        void * t14 = gm_l2;
        gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t13, t14);
        goto gm_fdone;
        goto me2;
mn10:;
ma4:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        if ((GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8)) != 0)) goto mn15;
        gm_tnvnsr1bfvtbd t17;
        memset(&t17, 0, sizeof t17);
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
        goto me2;
mn15:;
ma5:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t20 != ((uint8_t)0ULL)) goto mn19;
        if (!(GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8)) != 0)) goto mn19;
        void * t21 = GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8));
        gm_l3 = t21;
        void * t22 = gm_l0;
        void * t23 = gm_l3;
        gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t22, t23);
        goto gm_fdone;
        goto me2;
mn19:;
ma6:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t25 != ((uint8_t)2ULL)) goto mn24;
        void * t26 = GM_LD(void *, ((((uint8_t *)&gm_l1) + 8) + 8));
        gm_l4 = t26;
        void * t27 = gm_l0;
        void * t28 = gm_l4;
        gm_f_never_cause_p5ir1i2((void *)&(*(gm_tnvnsr1bfvtbd *)gm_out), t27, t28);
        goto gm_fdone;
        goto me2;
mn24:;
ma7:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t32 != ((uint8_t)1ULL)) goto pa31;
        goto po30;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t34 != ((uint8_t)3ULL)) goto pa33;
        goto po30;
pa33:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t36 != ((uint8_t)5ULL)) goto pa35;
        goto po30;
pa35:;
        goto mn29;
po30:;
        gm_tnvnsr1bfvtbd t37;
        memset(&t37, 0, sizeof t37);
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
        goto me2;
mn29:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 999, 5);
me2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tnvnsr1bfvtbd gm_f_stmt_never_cause_r2lf089_c(void * a0, gm_trcmmd12cefb1 a1) { gm_tnvnsr1bfvtbd gm_r; gm_f_stmt_never_cause_r2lf089(&gm_r, a0, &a1); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_swap_frame_4j63sdr(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tjhr0bdm69ios gm_f_swap_frame_4j63sdr_c(void * a0, gm_tjhr0bdm69ios a1) { gm_tjhr0bdm69ios gm_r; gm_f_swap_frame_4j63sdr(&gm_r, a0, &a1); return gm_r; }
extern __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_try_untyped_8j8up35 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    void * gm_l5; /* inner */
    void * gm_l6; /* operand */
    gm_b16_8 gm_l10; /* m */
    gm_ts6oqppjh0ba3 gm_l11; /* op */
    void * gm_l12; /* lhs */
    void * gm_l13; /* rhs */
    gm_b16_8 gm_l19; /* m */
    GM_STACK_CHECK(&F, 11029u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)6ULL)) goto mn11;
        gm_t08olq97rscn3 t13 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t13;
        {
            void * t15 = gm_l0;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tqvu78rpi6q1h t17;
            gm_f_lookup_local_6f38t7p((void *)&t17, t15, t16);
            gm_tqvu78rpi6q1h t18 = t17;
            gm_t930q5l38hdap t19;
            if (((((uint8_t *)&t18))[208] != 0)) {
                gm_t930q5l38hdap t20 = GM_LD(gm_t930q5l38hdap, (((uint8_t *)&t18) + 0));
                t19 = t20;
            } else {
                void * t21 = gm_l0;
                void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
                void * t23 = gm_l0;
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 8));
                void * t25 = gm_l1;
                gm_f_try_untyped_fj0h8i5((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t22, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            F.l3 = t19;
        }
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t27 != ((uint8_t)1ULL)) goto ie26;
            gm_t0ialelq3ql1k t28 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l3) + 8));
            F.l4 = t28;
            {
                gm_t0ialelq3ql1k t30 = F.l4;
                gm_t5vbkdq81qfs7 t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t31 + 8), t30);
                gm_tkv1vap90vht2 t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t32), t31);
                ((uint8_t *)&t32)[208] = 1;
                uint8_t t33 = 0;
                F.ret = t32;
                goto gm_fret;
            }
            goto ix26;
ie26:;
            {
                gm_tkv1vap90vht2 t35;
                memset(&t35, 0, sizeof t35);
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
ix26:;
        }
        goto me3;
mn11:;
ma5:;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t38 != ((uint8_t)9ULL)) goto mn37;
        void * t39 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t39;
        void * t40 = gm_l0;
        void * t41 = gm_l5;
        gm_f_try_untyped_8j8up35((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn37:;
ma6:;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t43 != ((uint8_t)12ULL)) goto mn42;
        uint8_t t44 = GM_LD(uint8_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
        if (t44 != ((uint8_t)0ULL)) goto mn42;
        void * t45 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t45;
        {
            void * t47 = gm_l0;
            void * t48 = gm_l6;
            gm_tkv1vap90vht2 t49;
            gm_f_try_untyped_8j8up35((void *)&t49, (void *)NULL, cx, NULL, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t50 = t49;
            gm_t5vbkdq81qfs7 t51;
            if (((((uint8_t *)&t50))[208] != 0)) {
                gm_t5vbkdq81qfs7 t52 = GM_LD(gm_t5vbkdq81qfs7, (((uint8_t *)&t50) + 0));
                t51 = t52;
            } else {
                gm_tkv1vap90vht2 t53;
                memset(&t53, 0, sizeof t53);
                uint8_t t54 = 0;
                F.ret = t53;
                goto gm_fret;
            }
            F.l7 = t51;
        }
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t56 != ((uint8_t)0ULL)) goto ie55;
            gm_t0ialelq3ql1k t57 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l7) + 8));
            F.l8 = t57;
            {
                void * t59 = (void *)((uint8_t *)&F.l8);
                gm_t4t568o7qnv02 t60;
                gm_f_negate_fohael7((void *)&t60, (void *)NULL, cx, NULL, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t4t568o7qnv02 t61 = t60;
                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
                if (t64 != ((uint8_t)0ULL)) goto mn63;
                gm_t0ialelq3ql1k t65 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t61) + 8));
                F.l9 = t65;
                gm_t0ialelq3ql1k t66 = F.l9;
                gm_t5vbkdq81qfs7 t67;
                GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t67 + 8), t66);
                gm_tkv1vap90vht2 t68;
                GM_ST(uint8_t, (uint8_t *)&t68 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t68 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t68 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t68), t67);
                ((uint8_t *)&t68)[208] = 1;
                uint8_t t69 = 0;
                F.ret = t68;
                goto gm_fret;
                goto me62;
mn63:;
                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
                if (t71 != ((uint8_t)1ULL)) goto mn70;
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
                gm_l10 = t72;
                {
                    void * t74 = gm_l0;
                    void * t75 = gm_l1;
                    gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
                    gm_b16_8 t77 = gm_l10;
                    gm_b0_1 t78 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t74, t76, t77);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_t5vbkdq81qfs7 t79;
                    memset(&t79, 0, sizeof t79);
                    GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)1ULL));
                    gm_tkv1vap90vht2 t80;
                    GM_ST(uint8_t, (uint8_t *)&t80 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t80 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t80 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t80), t79);
                    ((uint8_t *)&t80)[208] = 1;
                    uint8_t t81 = 0;
                    F.ret = t80;
                    goto gm_fret;
                }
                goto me62;
mn70:;
                gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 450, 13);
me62:;
            }
            goto ix55;
ie55:;
            {
                gm_t5vbkdq81qfs7 t83;
                memset(&t83, 0, sizeof t83);
                GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)1ULL));
                gm_tkv1vap90vht2 t84;
                GM_ST(uint8_t, (uint8_t *)&t84 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t84 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t84 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t84), t83);
                ((uint8_t *)&t84)[208] = 1;
                uint8_t t85 = 0;
                F.ret = t84;
                goto gm_fret;
            }
ix55:;
        }
        goto me3;
mn42:;
ma7:;
        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t87 != ((uint8_t)13ULL)) goto mn86;
        gm_ts6oqppjh0ba3 t88 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l11 = t88;
        void * t89 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l12 = t89;
        void * t90 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l13 = t90;
        {
            gm_ts6oqppjh0ba3 t93 = gm_l11;
            uint8_t t94 = gm_f_untyped_op_7an9o22(t93);
            uint8_t t95 = (uint8_t)!t94;
            if (!t95) goto ie92;
            {
                gm_tkv1vap90vht2 t97;
                memset(&t97, 0, sizeof t97);
                uint8_t t98 = 0;
                F.ret = t97;
                goto gm_fret;
            }
            goto ix92;
ie92:;
ix92:;
        }
        {
            void * t99 = gm_l0;
            void * t100 = gm_l12;
            gm_tkv1vap90vht2 t101;
            gm_f_try_untyped_8j8up35((void *)&t101, (void *)NULL, cx, NULL, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t102 = t101;
            gm_t5vbkdq81qfs7 t103;
            if (((((uint8_t *)&t102))[208] != 0)) {
                gm_t5vbkdq81qfs7 t104 = GM_LD(gm_t5vbkdq81qfs7, (((uint8_t *)&t102) + 0));
                t103 = t104;
            } else {
                gm_tkv1vap90vht2 t105;
                memset(&t105, 0, sizeof t105);
                uint8_t t106 = 0;
                F.ret = t105;
                goto gm_fret;
            }
            F.l14 = t103;
        }
        {
            void * t107 = gm_l0;
            void * t108 = gm_l13;
            gm_tkv1vap90vht2 t109;
            gm_f_try_untyped_8j8up35((void *)&t109, (void *)NULL, cx, NULL, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t110 = t109;
            gm_t5vbkdq81qfs7 t111;
            if (((((uint8_t *)&t110))[208] != 0)) {
                gm_t5vbkdq81qfs7 t112 = GM_LD(gm_t5vbkdq81qfs7, (((uint8_t *)&t110) + 0));
                t111 = t112;
            } else {
                gm_tkv1vap90vht2 t113;
                memset(&t113, 0, sizeof t113);
                uint8_t t114 = 0;
                F.ret = t113;
                goto gm_fret;
            }
            F.l15 = t111;
        }
        {
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
            if (t116 != ((uint8_t)0ULL)) goto ie115;
            gm_t0ialelq3ql1k t117 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l14) + 8));
            F.l16 = t117;
            {
                uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                if (t120 != ((uint8_t)0ULL)) goto ie119;
                gm_t0ialelq3ql1k t121 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l15) + 8));
                F.l17 = t121;
                {
                    gm_ts6oqppjh0ba3 t123 = gm_l11;
                    void * t124 = (void *)((uint8_t *)&F.l16);
                    void * t125 = (void *)((uint8_t *)&F.l17);
                    gm_t4t568o7qnv02 t126;
                    gm_f_binary_9sb7899((void *)&t126, (void *)NULL, cx, NULL, t123, t124, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t4t568o7qnv02 t127 = t126;
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t127) + 0));
                    if (t130 != ((uint8_t)0ULL)) goto mn129;
                    gm_t0ialelq3ql1k t131 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t127) + 8));
                    F.l18 = t131;
                    gm_t0ialelq3ql1k t132 = F.l18;
                    gm_t5vbkdq81qfs7 t133;
                    GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t133 + 8), t132);
                    gm_tkv1vap90vht2 t134;
                    GM_ST(uint8_t, (uint8_t *)&t134 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t134 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t134 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t134), t133);
                    ((uint8_t *)&t134)[208] = 1;
                    uint8_t t135 = 0;
                    F.ret = t134;
                    goto gm_fret;
                    goto me128;
mn129:;
                    uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&t127) + 0));
                    if (t137 != ((uint8_t)1ULL)) goto mn136;
                    gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&t127) + 8));
                    gm_l19 = t138;
                    {
                        void * t140 = gm_l0;
                        void * t141 = gm_l1;
                        gm_tl874t2atnlh3 t142 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t141) + 72));
                        gm_b16_8 t143 = gm_l19;
                        gm_b0_1 t144 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t140, t142, t143);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_t5vbkdq81qfs7 t145;
                        memset(&t145, 0, sizeof t145);
                        GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)1ULL));
                        gm_tkv1vap90vht2 t146;
                        GM_ST(uint8_t, (uint8_t *)&t146 + 209, 0);
                        GM_ST(uint16_t, (uint8_t *)&t146 + 210, 0);
                        GM_ST(uint32_t, (uint8_t *)&t146 + 212, 0);
                        GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t146), t145);
                        ((uint8_t *)&t146)[208] = 1;
                        uint8_t t147 = 0;
                        F.ret = t146;
                        goto gm_fret;
                    }
                    goto me128;
mn136:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 464, 13);
me128:;
                }
                goto ix119;
ie119:;
                {
                    gm_t5vbkdq81qfs7 t149;
                    memset(&t149, 0, sizeof t149);
                    GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)1ULL));
                    gm_tkv1vap90vht2 t150;
                    GM_ST(uint8_t, (uint8_t *)&t150 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t150 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t150 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t150), t149);
                    ((uint8_t *)&t150)[208] = 1;
                    uint8_t t151 = 0;
                    F.ret = t150;
                    goto gm_fret;
                }
ix119:;
            }
            goto ix115;
ie115:;
            {
                gm_t5vbkdq81qfs7 t153;
                memset(&t153, 0, sizeof t153);
                GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)1ULL));
                gm_tkv1vap90vht2 t154;
                GM_ST(uint8_t, (uint8_t *)&t154 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t154 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t154 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t154), t153);
                ((uint8_t *)&t154)[208] = 1;
                uint8_t t155 = 0;
                F.ret = t154;
                goto gm_fret;
            }
ix115:;
        }
        goto me3;
mn86:;
ma8:;
        void * t157 = gm_l0;
        void * t158 = GM_LD(void *, (((uint8_t *)t157) + 0));
        void * t159 = gm_l0;
        uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)t159) + 8));
        void * t161 = gm_l1;
        gm_f_try_untyped_fj0h8i5((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t158, t160, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn156:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 440, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_uninit_message_u2c3peu F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* hint */
    GM_STACK_CHECK(&F, 447u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t12 != ((uint8_t)11ULL)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 950, 16);
me10:;
        if (!t9) goto ie3;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sadg245agtuao));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(44));
        gm_b16_8 t16 = t15;
        t2 = t16;
        goto ix3;
ie3:;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(0));
        gm_b16_8 t19 = t18;
        t2 = t19;
ix3:;
        gm_l2 = t2;
    }
    {
        gm_t6j6smj7qoctu t20;
        void * t21 = gm_l0;
        gm_t88rcucg1b7uk t22 = gm_l1;
        gm_b16_8 t23 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t23);
        gm_b16_8 t24 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t24);
        gm_tabb710d9mq2e t25;
        gm_f_format_cn0m7al((void *)&t25, (void *)NULL, cx, NULL, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src276s8986o4r3, 951, 12);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        uint8_t t30 = 0;
        F.ret = t29;
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
extern __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
extern __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_untyped_op_7an9o22(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_untyped_op_7an9o22 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 31u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma3; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma3; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma4; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma4; case ((uint8_t)13ULL): goto ma4; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma4; case ((uint8_t)17ULL): goto ma4; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)0ULL)) goto pa10;
    gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t12;
    goto po9;
pa10:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t14 != ((uint8_t)1ULL)) goto pa13;
    gm_t9qkh0rsooc8d t15 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t15;
    goto po9;
pa13:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t18;
    goto po9;
pa16:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)3ULL)) goto pa19;
    gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t21;
    goto po9;
pa19:;
    goto mn8;
po9:;
    uint8_t t22 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
    t1 = t22;
    goto me2;
mn8:;
ma4:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)4ULL)) goto pa25;
    goto po24;
pa25:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)5ULL)) goto pa27;
    goto po24;
pa27:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)6ULL)) goto pa29;
    goto po24;
pa29:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)7ULL)) goto pa31;
    goto po24;
pa31:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)8ULL)) goto pa33;
    goto po24;
pa33:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t36 != ((uint8_t)9ULL)) goto pa35;
    goto po24;
pa35:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)16ULL)) goto pa37;
    goto po24;
pa37:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)17ULL)) goto pa39;
    goto po24;
pa39:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t42 != ((uint8_t)18ULL)) goto pa41;
    goto po24;
pa41:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)19ULL)) goto pa43;
    goto po24;
pa43:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t46 != ((uint8_t)12ULL)) goto pa45;
    goto po24;
pa45:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t48 != ((uint8_t)13ULL)) goto pa47;
    goto po24;
pa47:;
    goto mn23;
po24:;
    t1 = ((uint8_t)1);
    goto me2;
mn23:;
ma5:;
    t1 = ((uint8_t)0);
    goto me2;
mn49:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 477, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_when_stmt_3pk7eon(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_when_stmt_3pk7eon F;
    void * gm_l0; /* cx */
    void * gm_l1; /* w */
    uint8_t gm_l2; /* taken */
    void * gm_l4; /* inner */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
        void * t6 = gm_l1;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        gm_tqs5p4n2tq19v t8 = gm_f_body_when_cond_2coms3p((void *)NULL, cx, NULL, t3, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqs5p4n2tq19v t9 = t8;
        uint8_t t10;
        if (((((uint8_t *)&t9))[1] != 0)) {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = gm_l1;
            gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t14) + 104));
            void * t16 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t13, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l2 = t10;
    }
    {
        uint8_t t19 = gm_l2;
        if (!t19) goto ie18;
        {
            void * t21 = gm_l0;
            void * t22 = gm_l1;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            void * t24 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t21, t23, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t26 = gm_l1;
        if (!(((((uint8_t *)t26) + 48))[48] != 0)) goto mn28;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t26) + 48) + 0));
        if (t29 != ((uint8_t)0ULL)) goto mn28;
        gm_t79rbdv4rafe5 t30 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t26) + 48) + 8));
        F.l3 = t30;
        void * t31 = gm_l0;
        void * t32 = (void *)((uint8_t *)&F.l3);
        void * t33 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t31, t32, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
        goto me27;
mn28:;
        if (!(((((uint8_t *)t26) + 48))[48] != 0)) goto mn35;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t26) + 48) + 0));
        if (t36 != ((uint8_t)1ULL)) goto mn35;
        void * t37 = GM_LD(void *, ((((uint8_t *)t26) + 48) + 8));
        gm_l4 = t37;
        void * t38 = gm_l0;
        void * t39 = gm_l4;
        void * t40 = gm_f_when_stmt_3pk7eon((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = 0;
        F.ret = t40;
        goto gm_fret;
        goto me27;
mn35:;
        if ((((((uint8_t *)t26) + 48))[48] != 0)) goto mn42;
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        void * t45 = gm_l1;
        gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 104));
        void * t47 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t44, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t48 = 0;
        F.ret = t47;
        goto gm_fret;
        goto me27;
mn42:;
        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 821, 5);
me27:;
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
__attribute__((aligned(4))) void * gm_f_while_loop_86vcgbb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_while_loop_86vcgbb F;
    void * gm_l0; /* cx */
    void * gm_l2; /* cond */
    void * gm_l3; /* body */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_th2lqbo77pvsc gm_l5; /* name */
    gm_t08olq97rscn3 gm_l6; /* l */
    gm_b16_8 gm_l7; /* c */
    uint32_t gm_l8; /* id */
    void * gm_l9; /* b */
    uint8_t gm_l10; /* infinite */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_tlr9dp305htm7 gm_l13; /* part */
    void * gm_l14; /* x */
    uint8_t gm_l15; /* breaks */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    void * gm_l17; /* blk */
    GM_STACK_CHECK(&F, 1097u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t4;
        memset(&t4, 0, sizeof t4);
        gm_l5 = t4;
    }
    {
        if (!((((uint8_t *)&F.l1))[32] != 0)) goto ie5;
        gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l1));
        gm_l6 = t6;
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_t930q5l38hdap t10;
            memset(&t10, 0, sizeof t10);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
            gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 16));
            gm_b0_1 t12 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t8, t9, &t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_th2lqbo77pvsc t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[16] = 1;
            gm_l5 = t14;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l2;
        gm_b16_8 t17 = gm_f_cond_99ajlhk((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_t1qr0p6kln6tf t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
        uint32_t t20 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t18, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l5)), ((uint8_t)1), t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l3;
        void * t23 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t21, t22, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t23;
    }
    {
        {
            void * t25 = gm_l0;
            void * t26 = (void *)(((uint8_t *)t25) + 232);
            gm_te5gbhu3us3k4 t27;
            gm_f_pop_ruvva9t((void *)&t27, t26);
        }
    }
    {
        void * t28 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t28);
    }
    {
        gm_l10 = ((uint8_t)1);
    }
    {
        {
            gm_b16_8 t30 = gm_l7;
            gm_l11 = t30;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t32 = gm_l12;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l12;
                    uint64_t t38 = (uintptr_t)t37;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_tlr9dp305htm7 t42 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t38 * 24));
                    gm_l13 = t42;
                    {
                        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                        if (t46 != ((uint8_t)0ULL)) goto mn45;
                        void * t47 = GM_LD(void *, (((uint8_t *)&gm_l13) + 8));
                        gm_l14 = t47;
                        void * t49 = gm_l14;
                        uint8_t t50 = (!((GM_LD(uint8_t, (((uint8_t *)t49) + 8) + 0) == ((uint8_t)4ULL)) && ((GM_LD(uint8_t, ((((uint8_t *)t49) + 8) + 8)) != 0) == (((uint8_t)1) != 0))));
                        if (!t50) goto ie48;
                        {
                            gm_l10 = ((uint8_t)0);
                        }
                        goto ix48;
ie48:;
ix48:;
                        goto me44;
mn45:;
                        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                        if (t53 != ((uint8_t)1ULL)) goto mn52;
                        {
                            gm_l10 = ((uint8_t)0);
                        }
                        goto me44;
mn52:;
                        gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 902, 9);
me44:;
                    }
                    goto me40;
mn41:;
                    gm_trap("no match arm matched the scrutinee", gm_src276s8986o4r3, 901, 5);
me40:;
                }
lc31:;
                {
                    uint64_t t56 = gm_l12;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    uint64_t t59 = r57;
                    gm_l12 = t59;
                }
            }
lb31:;
        }
    }
    {
        void * t60 = gm_l0;
        uint32_t t61 = gm_l8;
        uint8_t t62 = gm_f_forget_label_breaks_vl4mn72(t60, t61);
        gm_l15 = t62;
    }
    {
        gm_t88rcucg1b7uk t63;
        uint8_t t65 = gm_l10;
        if (!t65) goto ie64;
        uint8_t t66 = gm_l15;
        uint8_t t67 = (uint8_t)!t66;
        if (!t67) goto ie64;
        void * t69 = gm_l0;
        void * t70 = GM_LD(void *, (((uint8_t *)t69) + 0));
        void * t71 = (void *)(((uint8_t *)t70) + 8);
        gm_t88rcucg1b7uk t72 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t73 = t72;
        t63 = t73;
        goto ix64;
ie64:;
        void * t75 = gm_l0;
        void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
        void * t77 = (void *)(((uint8_t *)t76) + 8);
        gm_t88rcucg1b7uk t78 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t79 = t78;
        t63 = t79;
ix64:;
        gm_l16 = t63;
    }
    {
        void * t81 = gm_l9;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t81) + 8) + 0));
        if (t82 != ((uint8_t)46ULL)) goto ie80;
        void * t83 = GM_LD(void *, ((((uint8_t *)t81) + 8) + 8));
        gm_l17 = t83;
        {
            gm_t88rcucg1b7uk t85 = gm_l16;
            gm_t710lsrq1m8gk t86;
            memset((uint8_t *)&t86 + 4, 0, 4);
            uint32_t t87 = gm_l8;
            GM_ST(uint32_t, (uint8_t *)&t86 + 0, t87);
            gm_b16_8 t88 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t86 + 8, t88);
            void * t89 = gm_l17;
            GM_ST(void *, (uint8_t *)&t86 + 24, t89);
            GM_ST(void *, (uint8_t *)&t86 + 32, ((void *)0));
            gm_teti411eujvf4 t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t90 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)48ULL));
            GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t90 + 8), t86);
            gm_tl874t2atnlh3 t91 = gm_l4;
            void * t92 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t85, &t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t93 = 0;
            F.ret = t92;
            goto gm_fret;
        }
        goto ix80;
ie80:;
        {
            void * t95 = gm_l9;
            uint8_t t96 = 0;
            F.ret = t95;
            goto gm_fret;
        }
ix80:;
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

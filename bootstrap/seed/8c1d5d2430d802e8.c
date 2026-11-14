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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
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
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
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
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
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
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t08olq97rscn3 f1; } gm_tp9ftj4p31ha1;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tp9ftj4p31ha1) == 40 && _Alignof(gm_tp9ftj4p31ha1) == 8 && offsetof(gm_tp9ftj4p31ha1, f0) == 0 && offsetof(gm_tp9ftj4p31ha1, f1) == 8, "layout of `(fn_id: u32, name: Ident)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_tddtjfggdat0d f0; uint8_t f1; uint8_t pe[7]; } gm_tt775pork2bs8;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tt775pork2bs8) == 56 && _Alignof(gm_tt775pork2bs8) == 8 && offsetof(gm_tt775pork2bs8, f0) == 0 && offsetof(gm_tt775pork2bs8, f1) == 48, "layout of `?FnTy`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_check_no_self_reference_4ra5lcu {
    gm_frame h;
    uint32_t l1; /* fn_id */
    uint8_t l5; /* refers */
    gm_tl93vjpcesj48 l8; /* n */
    gm_b0_1 ret;
};
struct gm_fr_effect_has_param_args_oketd14 {
    gm_frame h;
    gm_toa2vhe3l4hfr l4; /* a */
    uint8_t ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
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
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fn_3e7faf9 {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_b0_1 ret;
};
struct gm_fr_fn_literal_fiu78lb {
    gm_frame h;
    gm_t1s75i08qtptd l3; /* name */
    gm_tt775pork2bs8 l10; /* expected */
    gm_tddtjfggdat0d l13; /* f */
    gm_tddtjfggdat0d l14; /* f */
    gm_tf2orqcrphmek l17; /* p */
    gm_tddtjfggdat0d l21; /* f */
    gm_trqfvmjtt4sij l23; /* open_effects */
    gm_tddtjfggdat0d l25; /* f */
    gm_trqfvmjtt4sij l26; /* kept */
    gm_trqfvmjtt4sij l40; /* param_ids */
    gm_t5v0c8ia6a5mo l43; /* p */
    gm_tk9n3vlila9qg l47; /* frame */
    gm_tj44oivvogvtl l52; /* p */
    gm_trqfvmjtt4sij l60; /* all */
    gm_trqfvmjtt4sij l61; /* ptys */
    gm_t5v0c8ia6a5mo l64; /* p */
    gm_tddtjfggdat0d l66; /* f */
    gm_tddtjfggdat0d l67; /* fty */
    void * ret;
};
struct gm_fr_fn_meq413i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_infer_open_effect_47fpti4 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* found */
    gm_tj44oivvogvtl l9; /* p */
    gm_trqfvmjtt4sij l14; /* names */
    gm_trqfvmjtt4sij l18; /* all */
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_generic_jrl2kf9 {
    gm_frame h;
    gm_tf2orqcrphmek l3; /* p */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
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
struct gm_fr_literal_body_5lm01a5 {
    gm_frame h;
    gm_t79rbdv4rafe5 l6; /* b */
    void * ret;
};
struct gm_fr_literal_params_n5511im {
    gm_frame h;
    gm_tt775pork2bs8 l2; /* expected */
    gm_trqfvmjtt4sij l3; /* params */
    gm_tf2orqcrphmek l6; /* p */
    gm_tddtjfggdat0d l15; /* f */
    gm_tddtjfggdat0d l16; /* f */
    gm_b16_8 ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_nested_fn_binding_0cl95ol {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_nested_generic_fn_q4qqrgj {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_new_literal_fn_oqjp4lg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_recursive_without_ret_48l31rm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_reject_returned_closure_1ouocor {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_route_performed_pruu3np {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* own */
    gm_tj44oivvogvtl l8; /* p */
    gm_b16_8 ret;
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
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_visit_e3tpkkh {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_no_self_reference_4ra5lcu(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void * gm_f_declare_temps_a7fc14v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_effect_has_param_args_oketd14(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_finish_block_body_4vol3tv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_3e7faf9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_fn_literal_fiu78lb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, const void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_26002qo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_57u5svl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8c2oop8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9lchiud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hgrco1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vrv37j5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_infer_open_effect_47fpti4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, uint32_t a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_generic_jrl2kf9(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_literal_body_5lm01a5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint8_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_literal_params_n5511im(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_nested_fn_binding_0cl95ol(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_nested_generic_fn_q4qqrgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu2hcel(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_p9c61k8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_reject_returned_closure_1ouocor(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_route_performed_pruu3np(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3, uint8_t a4);
__attribute__((aligned(4))) uint8_t gm_f_set_contains_8v9ep9m(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_undeclared_message_b14iouu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2, void * a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_otp5qp6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s4b8airp3rh69[50];
extern const uint8_t gm_s93p2i934ld6p[5];
extern const uint8_t gm_sdsd71bjtv81c[98];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgv98i2v4c6ls[93];
extern const uint8_t gm_skfniirr0r2gc[63];
extern const uint8_t gm_sn1j4m1p1cnm1[83];
extern const uint8_t gm_southrlg4k72m[6];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srdq954rpedg3[93];
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_meq413i F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_visit_e3tpkkh *)((&F.h)->up))->l1));
        gm_f_visit_e3tpkkh(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6) {
    struct gm_fr_new_literal_fn_oqjp4lg F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint64_t gm_l3; /* module */
    gm_b16_8 gm_l4; /* params */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tnpifv20irnph gm_l6; /* effects */
    GM_STACK_CHECK(&F, 174u);
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
        gm_t9qkh0rsooc8d t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
        gm_b16_8 t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = gm_l2;
        uint64_t t6 = gm_l3;
        gm_b16_8 t7 = gm_l4;
        gm_t88rcucg1b7uk t8 = gm_l5;
        gm_tnpifv20irnph t9 = gm_l6;
        uint32_t t10 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t2, t3, t4, t5, t6, t7, t8, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_recursive_without_ret_48l31rm F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l2;
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_c8qq9n9((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcl43af23eher3, 280, 20);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_b0_1 t11 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t10);
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
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visit_e3tpkkh F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        gm_closure t2 = F.l1;
        void * t3 = gm_l0;
        gm_closure t4 = t2;
        gm_node *ccx5 = t4.cx;
        void * t6 = t4.code;
        gm_b0_1 t7;
        if (GM_PTR_TAGGED(t6)) {
            ((void (*)(void *))GM_PTR_UNTAG(t6))(t3);
        } else {
            t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t6)(t4.env, ccx5, NULL, t3);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = gm_l0;
        gm_closure t9 = ((gm_closure){(void *)gm_f_fn_meq413i, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t8, t9);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_no_self_reference_4ra5lcu(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_check_no_self_reference_4ra5lcu F;
    void * gm_l0; /* cx */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* name */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 434u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        F.l5 = ((uint8_t)0);
    }
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 704) + 0) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 704) + 0)));
            uint64_t t6 = gm_l2;
            uint64_t t7 = (uintptr_t)t6;
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 704) + 0) + 8);
            uint64_t t10 = (uintptr_t)t9;
            if (t7 > t10 || t10 > (uintptr_t)t4) gm_trap_slice(t7, t10, t4, gm_srcl43af23eher3, 287, 14);
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t5 + t7 * 48)));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t7));
            gm_l6 = t11;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l7;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l7;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tl93vjpcesj48 t23 = GM_LD(gm_tl93vjpcesj48, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t19 * 48));
                    F.l8 = t23;
                    {
                        void * t25 = GM_LD(void *, (((uint8_t *)&F.l8) + 40));
                        gm_closure t26 = ((gm_closure){(void *)gm_f_fn_3e7faf9, (void *)&F.h, cx});
                        gm_f_visit_e3tpkkh(t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 287, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t28 = gm_l7;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l7 = t31;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        uint8_t t33 = F.l5;
        if (!t33) goto ie32;
        {
            void * t35 = gm_l0;
            gm_tl874t2atnlh3 t36 = gm_l4;
            gm_b16_8 t37 = gm_l3;
            gm_tnpifv20irnph t38;
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
            gm_tabb710d9mq2e t39;
            gm_f_format_hgrco1l((void *)&t39, (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t40 = t39;
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcl43af23eher3, 296, 32);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_b16_8 t42 = t41;
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            gm_b0_1 t44 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t35, t36, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix32;
ie32:;
ix32:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_effect_has_param_args_oketd14(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_has_param_args_oketd14 F;
    void * gm_l0; /* tys */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t88rcucg1b7uk gm_l5; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l1;
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
                    gm_toa2vhe3l4hfr t16 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 40));
                    F.l4 = t16;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto ie18;
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                        gm_l5 = t20;
                        {
                            void * t23 = gm_l0;
                            gm_t88rcucg1b7uk t24 = gm_l5;
                            uint8_t t25 = gm_f_has_param_u9t0efs(t23, GM_CONV(gm_csguankko4lmhd, t24));
                            if (!t25) goto ie22;
                            {
                                uint8_t t27 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix22;
ie22:;
ix22:;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 243, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l3;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l3 = t32;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t33 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3e7faf9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_3e7faf9 F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* f */
    uint32_t gm_l3; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma4; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma5; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)26ULL)) goto mn9;
        uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t11;
        uint32_t t13 = gm_l1;
        uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l1));
        uint8_t t15 = ((t13 == t14));
        if (!t15) goto ie12;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l5), ((uint8_t)1));
        }
        goto ix12;
ie12:;
ix12:;
        goto me3;
mn9:;
ma5:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)34ULL)) goto mn17;
        gm_tsth9qu1dv0g3 t19 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l2 = t19;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t21 != ((uint8_t)0ULL)) goto ie20;
        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 8));
        gm_l3 = t22;
        uint32_t t23 = gm_l3;
        uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l1));
        uint8_t t25 = ((t23 == t24));
        if (!t25) goto ie20;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l5), ((uint8_t)1));
        }
        goto ix20;
ie20:;
ix20:;
        goto me3;
mn17:;
ma6:;
        goto me3;
mn27:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 289, 13);
me3:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_fn_literal_fiu78lb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, const void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_fn_literal_fiu78lb F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tkn42u7na3t81 gm_l6; /* target */
    gm_t88rcucg1b7uk gm_l7; /* t0 */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* inner */
    uint8_t gm_l11; /* wants_pointer */
    gm_t88rcucg1b7uk gm_l12; /* t */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l18; /* params */
    gm_tkn42u7na3t81 gm_l19; /* ret_known */
    void * gm_l20; /* r */
    gm_tnpifv20irnph gm_l22; /* effects */
    gm_b16_8 gm_l24; /* list_ */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tiilmfd16v3fc gm_l29; /* e */
    uint8_t gm_l30; /* iterator_arg */
    uint8_t gm_l31; /* infer_ret */
    uint8_t gm_l32; /* infer_block */
    gm_t88rcucg1b7uk gm_l33; /* ret */
    gm_t08olq97rscn3 gm_l34; /* n */
    gm_b16_8 gm_l35; /* fn_name */
    uint32_t gm_l36; /* fn_id */
    gm_t08olq97rscn3 gm_l37; /* n */
    uint8_t gm_l38; /* open */
    uint64_t gm_l39; /* nested_start */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint32_t gm_l44; /* id */
    void * gm_l45; /* body */
    gm_t88rcucg1b7uk gm_l46; /* final_ret */
    uint8_t gm_l48; /* yields */
    uint32_t gm_l49; /* y */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    gm_t88rcucg1b7uk gm_l53; /* ty */
    gm_b16_8 gm_l54; /* <tmp> */
    uint64_t gm_l55; /* <tmp> */
    uint32_t gm_l56; /* id */
    uint8_t gm_l57; /* undeclared_is_error */
    gm_b16_8 gm_l58; /* own */
    uint8_t gm_l59; /* carries_context */
    gm_b16_8 gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint8_t gm_l65; /* win64 */
    gm_t08olq97rscn3 gm_l68; /* n */
    uint8_t gm_l69; /* expects_fn */
    gm_t88rcucg1b7uk gm_l70; /* t */
    gm_t88rcucg1b7uk gm_l71; /* ty */
    GM_STACK_CHECK(&F, 5876u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        uint8_t t6;
        if (((((uint8_t *)&F.l3))[32] != 0)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 8, 8);
me7:;
        if (!t6) goto ie5;
        void * t10 = gm_l1;
        uint8_t t11 = gm_f_is_generic_jrl2kf9(t10);
        if (!t11) goto ie5;
        {
            void * t13 = gm_l0;
            void * t14 = gm_l1;
            gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t14) + 136));
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sgv98i2v4c6ls));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(92));
            gm_b0_1 t17 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t13, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            gm_tl874t2atnlh3 t20 = gm_l4;
            void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_tkn42u7na3t81 t23;
        memset(&t23, 0, sizeof t23);
        gm_l6 = t23;
    }
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie24;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l7 = t25;
        {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 96);
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            gm_t88rcucg1b7uk t31 = gm_l7;
            gm_t88rcucg1b7uk t32 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t28, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t32;
        }
        {
            void * t34 = gm_l5;
            gm_t88rcucg1b7uk t35 = gm_l8;
            gm_t53at80pfdus3 t36;
            gm_f_kind_jcnt4hm((void *)&t36, t34, GM_CONV(gm_csguankko4lmhd, t35));
            gm_t53at80pfdus3 t37 = t36;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t37) + 0));
            if (t38 != ((uint8_t)13ULL)) goto ie33;
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t37) + 8));
            gm_l9 = t39;
            void * t40 = gm_l5;
            gm_t88rcucg1b7uk t41 = gm_l9;
            gm_t53at80pfdus3 t42;
            gm_f_kind_jcnt4hm((void *)&t42, t40, GM_CONV(gm_csguankko4lmhd, t41));
            gm_t53at80pfdus3 t43 = t42;
            uint8_t t44;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
            if (t47 != ((uint8_t)16ULL)) goto mn46;
            t44 = ((uint8_t)1);
            goto me45;
mn46:;
            t44 = ((uint8_t)0);
            goto me45;
mn48:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 16, 48);
me45:;
            if (!t44) goto ie33;
            {
                gm_t88rcucg1b7uk t50 = gm_l9;
                gm_l8 = t50;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            gm_t88rcucg1b7uk t51 = gm_l8;
            gm_tkn42u7na3t81 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[4] = 1;
            gm_l6 = t52;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        gm_tt775pork2bs8 t53;
        memset(&t53, 0, sizeof t53);
        F.l10 = t53;
    }
    {
        gm_l11 = ((uint8_t)0);
    }
    {
        if (!((((uint8_t *)&gm_l6))[4] != 0)) goto ie54;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
        gm_l12 = t55;
        {
            void * t57 = gm_l5;
            gm_t88rcucg1b7uk t58 = gm_l12;
            gm_t53at80pfdus3 t59;
            gm_f_kind_jcnt4hm((void *)&t59, t57, GM_CONV(gm_csguankko4lmhd, t58));
            gm_t53at80pfdus3 t60 = t59;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
            switch (t66) { case ((uint8_t)0ULL): goto ma64; case ((uint8_t)1ULL): goto ma64; case ((uint8_t)2ULL): goto ma64; case ((uint8_t)3ULL): goto ma64; case ((uint8_t)4ULL): goto ma64; case ((uint8_t)5ULL): goto ma64; case ((uint8_t)6ULL): goto ma64; case ((uint8_t)7ULL): goto ma64; case ((uint8_t)8ULL): goto ma64; case ((uint8_t)9ULL): goto ma64; case ((uint8_t)10ULL): goto ma64; case ((uint8_t)11ULL): goto ma64; case ((uint8_t)12ULL): goto ma64; case ((uint8_t)13ULL): goto ma64; case ((uint8_t)14ULL): goto ma64; case ((uint8_t)15ULL): goto ma62; case ((uint8_t)16ULL): goto ma63; case ((uint8_t)17ULL): goto ma64; case ((uint8_t)18ULL): goto ma64; case ((uint8_t)19ULL): goto ma64; case ((uint8_t)20ULL): goto ma64; case ((uint8_t)21ULL): goto ma64; case ((uint8_t)22ULL): goto ma64; case ((uint8_t)23ULL): goto ma64; case ((uint8_t)24ULL): goto ma64; case ((uint8_t)25ULL): goto ma64; case ((uint8_t)26ULL): goto ma64; default: goto ma64; }
ma62:;
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
            if (t68 != ((uint8_t)15ULL)) goto mn67;
            gm_tddtjfggdat0d t69 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t60) + 8));
            F.l13 = t69;
            {
                gm_tddtjfggdat0d t71 = F.l13;
                gm_tt775pork2bs8 t72;
                GM_ST(uint8_t, (uint8_t *)&t72 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t72 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t72 + 52, 0);
                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t72), t71);
                ((uint8_t *)&t72)[48] = 1;
                F.l10 = t72;
            }
            goto me61;
mn67:;
ma63:;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
            if (t74 != ((uint8_t)16ULL)) goto mn73;
            gm_tddtjfggdat0d t75 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t60) + 8));
            F.l14 = t75;
            {
                gm_tddtjfggdat0d t77 = F.l14;
                gm_tt775pork2bs8 t78;
                GM_ST(uint8_t, (uint8_t *)&t78 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t78 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t78 + 52, 0);
                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t78), t77);
                ((uint8_t *)&t78)[48] = 1;
                F.l10 = t78;
            }
            {
                gm_l11 = ((uint8_t)1);
            }
            goto me61;
mn73:;
ma64:;
            goto me61;
mn79:;
ma65:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 22, 9);
me61:;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        {
            void * t82 = gm_l1;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((((uint8_t *)t82) + 0) + 0));
            gm_l15 = t83;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t85 = gm_l16;
                uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t87 = (t85 < t86);
                if (!t87) goto lb84;
                {
                    uint64_t t90 = gm_l16;
                    uint64_t t91 = (uintptr_t)t90;
                    uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_tf2orqcrphmek t95 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t91 * 56));
                    F.l17 = t95;
                    {
                        uint8_t t98 = 0;
                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 17));
                        if (!t99) {
                            void * t100 = GM_LD(void *, (((uint8_t *)&F.l17) + 32));
                            void * t101 = t100;
                            uint8_t t102;
                            if ((GM_LD(void *, ((uint8_t *)&t101)) != 0)) goto mn104;
                            t102 = ((uint8_t)0);
                            goto me103;
mn104:;
                            t102 = ((uint8_t)1);
                            goto me103;
mn105:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 32, 22);
me103:;
                            t98 = t102;
                        } else t98 = 1;
                        if (!t98) goto ie97;
                        {
                            void * t107 = gm_l0;
                            gm_tl874t2atnlh3 t108 = gm_l4;
                            gm_b16_8 t109;
                            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_s4b8airp3rh69));
                            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(49));
                            void * t110 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t107, t108, t109);
                            if (gm_unw.kind) {
                                goto gm_ll88;
                            }
                            uint8_t t111 = 0;
                            F.ret = t110;
                            goto gm_fret;
                        }
                        goto ix97;
ie97:;
ix97:;
                    }
                    goto me93;
mn94:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 31, 5);
me93:;
                }
lc84:;
                {
                    uint64_t t113 = gm_l16;
                    uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                    uint64_t t116 = r114;
                    gm_l16 = t116;
                }
            }
gm_ll88:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb84; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc84; }
            }
            goto gm_lad;
lb84:;
        }
    }
    {
        void * t117 = gm_l0;
        void * t118 = gm_l1;
        gm_b16_8 t119 = gm_f_literal_params_n5511im((void *)NULL, cx, NULL, t117, t118, &(*(gm_tt775pork2bs8 *)((uint8_t *)&F.l10)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t119;
    }
    {
        gm_tkn42u7na3t81 t120;
        memset(&t120, 0, sizeof t120);
        gm_l19 = t120;
    }
    {
        void * t122 = gm_l1;
        void * t123 = GM_LD(void *, ((((uint8_t *)t122) + 0) + 24));
        void * t124 = t123;
        if (!(GM_LD(void *, ((uint8_t *)&t124)) != 0)) goto ie121;
        void * t125 = GM_LD(void *, ((uint8_t *)&t124));
        gm_l20 = t125;
        {
            void * t127 = gm_l0;
            void * t128 = gm_l20;
            gm_t88rcucg1b7uk t129 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t130), t129);
            ((uint8_t *)&t130)[4] = 1;
            gm_l19 = t130;
        }
        goto ix121;
ie121:;
        if (!((((uint8_t *)&F.l10))[48] != 0)) goto ie131;
        gm_tddtjfggdat0d t132 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l10));
        F.l21 = t132;
        void * t133 = gm_l5;
        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
        uint8_t t135 = gm_f_has_param_u9t0efs(t133, GM_CONV(gm_csguankko4lmhd, t134));
        uint8_t t136 = (uint8_t)!t135;
        if (!t136) goto ie131;
        {
            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
            gm_tkn42u7na3t81 t139;
            GM_ST(uint8_t, (uint8_t *)&t139 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t139 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t139), t138);
            ((uint8_t *)&t139)[4] = 1;
            gm_l19 = t139;
        }
        goto ix131;
ie131:;
ix131:;
ix121:;
    }
    {
        gm_tnpifv20irnph t140 = gm_f_empty_effects_r0iv3pv();
        gm_l22 = t140;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t142 = gm_l1;
        if (!((((((uint8_t *)t142) + 0) + 32))[16] != 0)) goto ie141;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((((uint8_t *)t142) + 0) + 32));
        gm_l24 = t143;
        {
            void * t145 = gm_l0;
            void * t146 = GM_LD(void *, (((uint8_t *)t145) + 0));
            void * t147 = gm_l0;
            uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)t147) + 8));
            gm_b16_8 t149 = gm_l24;
            gm_b16_8 t150 = t149;
            gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
            gm_tnpifv20irnph t152 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t146, t148, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t152;
        }
        goto ix141;
ie141:;
        if (!((((uint8_t *)&F.l10))[48] != 0)) goto ie153;
        gm_tddtjfggdat0d t154 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l10));
        F.l25 = t154;
        {
            gm_f_list_084fcd6((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t157 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l25) + 24) + 0));
                gm_l27 = t157;
            }
            {
                gm_l28 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t159 = gm_l28;
                    uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t161 = (t159 < t160);
                    if (!t161) goto lb158;
                    {
                        uint64_t t164 = gm_l28;
                        uint64_t t165 = (uintptr_t)t164;
                        uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        gm_tiilmfd16v3fc t169 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t165 * 24));
                        gm_l29 = t169;
                        {
                            void * t172 = gm_l5;
                            void * t173 = (void *)((uint8_t *)&gm_l29);
                            uint8_t t174 = gm_f_effect_has_param_args_oketd14(t172, t173);
                            if (!t174) goto ie171;
                            {
                                void * t176 = (void *)((uint8_t *)&F.l23);
                                uint32_t t177 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 0));
                                gm_tftb1odc739t5 t178 = gm_f_push_im9d4ro(t176, t177);
                                gm_tftb1odc739t5 t179 = t178;
                                if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srcl43af23eher3, 51, 17);
                                gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
                            }
                            goto ix171;
ie171:;
                            {
                                void * t182 = (void *)((uint8_t *)&F.l26);
                                gm_tftb1odc739t5 t183 = gm_f_push_d1r4h19(t182, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l29)));
                                gm_tftb1odc739t5 t184 = t183;
                                if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_srcl43af23eher3, 53, 17);
                                gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
                            }
ix171:;
                        }
                        goto me167;
mn168:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 49, 9);
me167:;
                    }
lc158:;
                    {
                        uint64_t t187 = gm_l28;
                        uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                        uint64_t t190 = r188;
                        gm_l28 = t190;
                    }
                }
lb158:;
            }
        }
        {
            gm_tnpifv20irnph t191;
            uint64_t t193 = GM_LD(uintptr_t, (((uint8_t *)&F.l23) + 0) + 8);
            uint8_t t194 = ((t193 == ((uint64_t)0ULL)));
            if (!t194) goto ie192;
            gm_tnpifv20irnph t196 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l25) + 24));
            gm_tnpifv20irnph t197 = t196;
            t191 = t197;
            goto ix192;
ie192:;
            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
            gm_b16_8 t200 = t199;
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            gm_tnpifv20irnph t202 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t201);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t203 = t202;
            t191 = t203;
ix192:;
            gm_l22 = t191;
        }
        goto ix153;
ie153:;
ix153:;
ix141:;
    }
    {
        void * t204 = gm_l1;
        gm_tl874t2atnlh3 t205 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t204) + 136));
        void * t206 = gm_l0;
        gm_b16_8 t207 = GM_LD(gm_b16_8, ((((uint8_t *)t206) + 1248) + 0));
        gm_b16_8 t208 = t207;
        uint8_t t209 = 0;
        gm_tl874t2atnlh3 t210 = t205;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t208) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t208))) + i * 12, ((uint8_t *)&t210))) { t209 = 1; break; }
        uint8_t t211 = (uint8_t)(t209);
        gm_l30 = t211;
    }
    {
        uint8_t t212 = 0;
        uint8_t t213 = 0;
        uint8_t t214;
        if (((((uint8_t *)&gm_l19))[4] != 0)) goto mn216;
        t214 = ((uint8_t)1);
        goto me215;
mn216:;
        t214 = ((uint8_t)0);
        goto me215;
mn217:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 60, 19);
me215:;
        if (!t214) {
            uint8_t t218 = gm_l30;
            t213 = t218;
        } else t213 = 1;
        if (t213) {
            void * t219 = gm_l1;
            uint8_t t220;
            uint8_t t223 = GM_LD(uint8_t, ((((uint8_t *)t219) + 88) + 0));
            if (t223 != ((uint8_t)1ULL)) goto mn222;
            t220 = ((uint8_t)1);
            goto me221;
mn222:;
            t220 = ((uint8_t)0);
            goto me221;
mn224:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 60, 58);
me221:;
            t212 = t220;
        } else t212 = 0;
        gm_l31 = t212;
    }
    {
        uint8_t t225 = 0;
        uint8_t t226;
        if (((((uint8_t *)&gm_l19))[4] != 0)) goto mn228;
        t226 = ((uint8_t)1);
        goto me227;
mn228:;
        t226 = ((uint8_t)0);
        goto me227;
mn229:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 61, 20);
me227:;
        if (t226) {
            void * t230 = gm_l1;
            uint8_t t231;
            uint8_t t234 = GM_LD(uint8_t, ((((uint8_t *)t230) + 88) + 0));
            if (t234 != ((uint8_t)0ULL)) goto mn233;
            t231 = ((uint8_t)1);
            goto me232;
mn233:;
            t231 = ((uint8_t)0);
            goto me232;
mn235:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 61, 42);
me232:;
            t225 = t231;
        } else t225 = 0;
        gm_l32 = t225;
    }
    {
        gm_tkn42u7na3t81 t236 = gm_l19;
        gm_tkn42u7na3t81 t237 = t236;
        gm_t88rcucg1b7uk t238;
        if (((((uint8_t *)&t237))[4] != 0)) {
            gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t237) + 0));
            t238 = t239;
        } else {
            void * t240 = gm_l5;
            gm_t88rcucg1b7uk t241 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t240);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t238 = t241;
        }
        gm_l33 = t238;
    }
    {
        gm_b16_8 t242;
        if (!((((uint8_t *)&F.l3))[32] != 0)) goto ie243;
        gm_t08olq97rscn3 t244 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l3));
        gm_l34 = t244;
        gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 0));
        gm_b16_8 t247 = t246;
        t242 = t247;
        goto ix243;
ie243:;
        gm_b16_8 t249;
        GM_ST(void *, (uint8_t *)&t249, (void *)(gm_s93p2i934ld6p));
        GM_ST(uintptr_t, (uint8_t *)&t249 + 8, (uintptr_t)(4));
        gm_b16_8 t250 = t249;
        t242 = t250;
ix243:;
        gm_l35 = t242;
    }
    {
        void * t251 = gm_l0;
        void * t252 = GM_LD(void *, (((uint8_t *)t251) + 0));
        gm_b16_8 t253 = gm_l35;
        gm_tl874t2atnlh3 t254 = gm_l4;
        void * t255 = gm_l0;
        uint64_t t256 = GM_LD(uint64_t, (((uint8_t *)t255) + 8));
        gm_b16_8 t257 = gm_l18;
        gm_t88rcucg1b7uk t258 = gm_l33;
        gm_tnpifv20irnph t259 = gm_l22;
        uint32_t t260 = gm_f_new_literal_fn_oqjp4lg((void *)NULL, cx, NULL, t252, t253, t254, t256, t257, t258, t259);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l36 = t260;
    }
    {
        if (!((((uint8_t *)&F.l3))[32] != 0)) goto ie261;
        gm_t08olq97rscn3 t262 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l3));
        gm_l37 = t262;
        {
            void * t264 = gm_l0;
            gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 0));
            uint32_t t266 = gm_l36;
            gm_t930q5l38hdap t267;
            memset(&t267, 0, sizeof t267);
            GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)3ULL));
            GM_ST(uint32_t, ((uint8_t *)&t267 + 8), t266);
            gm_tl874t2atnlh3 t268 = gm_l4;
            gm_b0_1 t269 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t264, t265, &t267, t268);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix261;
ie261:;
ix261:;
    }
    {
        void * t270 = gm_l0;
        uint32_t t271 = gm_l36;
        gm_t88rcucg1b7uk t272 = gm_l33;
        gm_tnpifv20irnph t273 = gm_l22;
        gm_b0_1 t274 = gm_f_enter_fn_vo5kel1((void *)NULL, cx, NULL, t270, t271, t272, t273);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t275 = gm_l0;
        uint8_t t276 = gm_l32;
        GM_ST(uint8_t, (((uint8_t *)t275) + 744), t276);
    }
    {
        uint8_t t277 = 0;
        uint8_t t278;
        if (((((uint8_t *)&F.l3))[32] != 0)) goto mn280;
        t278 = ((uint8_t)0);
        goto me279;
mn280:;
        t278 = ((uint8_t)1);
        goto me279;
mn281:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 69, 13);
me279:;
        if (t278) {
            uint8_t t282 = 0;
            uint8_t t283 = gm_l31;
            if (!t283) {
                uint8_t t284 = gm_l32;
                t282 = t284;
            } else t282 = 1;
            t277 = t282;
        } else t277 = 0;
        gm_l38 = t277;
    }
    {
        uint8_t t286 = gm_l38;
        if (!t286) goto ie285;
        {
            void * t288 = gm_l0;
            void * t289 = (void *)(((uint8_t *)t288) + 760);
            uint32_t t290 = gm_l36;
            gm_tftb1odc739t5 t291 = gm_f_push_im9d4ro(t289, t290);
            gm_tftb1odc739t5 t292 = t291;
            if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 1, gm_srcl43af23eher3, 70, 15);
            gm_b0_1 t293 = GM_LD(gm_b0_1, (((uint8_t *)&t292) + 1));
        }
        goto ix285;
ie285:;
ix285:;
    }
    {
        void * t294 = gm_l0;
        uint64_t t295 = GM_LD(uintptr_t, ((((uint8_t *)t294) + 704) + 0) + 8);
        gm_l39 = t295;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t297 = gm_l18;
            gm_l41 = t297;
        }
        {
            gm_l42 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t299 = gm_l42;
                uint64_t t300 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                uint8_t t301 = (t299 < t300);
                if (!t301) goto lb298;
                {
                    uint64_t t304 = gm_l42;
                    uint64_t t305 = (uintptr_t)t304;
                    uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                    gm_t5v0c8ia6a5mo t309 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t305 * 56));
                    F.l43 = t309;
                    {
                        void * t311 = gm_l0;
                        gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                        gm_t88rcucg1b7uk t313 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l43) + 16));
                        gm_tl874t2atnlh3 t314 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l43) + 20));
                        uint32_t t315 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t311, t312, t313, ((uint8_t)0), t314);
                        if (gm_unw.kind) {
                            goto gm_ll302;
                        }
                        gm_l44 = t315;
                    }
                    {
                        uint8_t t317 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l43) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&F.l43) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t317) goto ie316;
                        {
                            void * t319 = gm_l0;
                            gm_b16_8 t320 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                            uint32_t t321 = gm_l44;
                            gm_t930q5l38hdap t322;
                            memset(&t322, 0, sizeof t322);
                            GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t322 + 8), t321);
                            gm_tl874t2atnlh3 t323 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l43) + 20));
                            gm_b0_1 t324 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t319, t320, &t322, t323);
                            if (gm_unw.kind) {
                                goto gm_ll302;
                            }
                        }
                        goto ix316;
ie316:;
ix316:;
                    }
                    {
                        void * t325 = (void *)((uint8_t *)&F.l40);
                        uint32_t t326 = gm_l44;
                        gm_tftb1odc739t5 t327 = gm_f_push_im9d4ro(t325, t326);
                        gm_tftb1odc739t5 t328 = t327;
                        if (((((uint8_t *)&t328))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t328) + 1, gm_srcl43af23eher3, 78, 9);
                        gm_b0_1 t329 = GM_LD(gm_b0_1, (((uint8_t *)&t328) + 1));
                    }
                    goto me307;
mn308:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 75, 5);
me307:;
                }
lc298:;
                {
                    uint64_t t331 = gm_l42;
                    uint64_t r332; bool o333 = __builtin_add_overflow(t331, ((uint64_t)1ULL), &r332);
                    uint64_t t334 = r332;
                    gm_l42 = t334;
                }
            }
gm_ll302:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb298; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc298; }
            }
            goto gm_lad;
lb298:;
        }
    }
    {
        void * t335 = gm_l0;
        void * t336 = gm_l0;
        void * t337 = gm_l1;
        void * t338 = (void *)(((uint8_t *)t337) + 88);
        gm_t88rcucg1b7uk t339 = gm_l33;
        uint8_t t340 = gm_l31;
        void * t341 = gm_f_literal_body_5lm01a5((void *)NULL, cx, NULL, t336, t338, t339, t340);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t342 = gm_f_declare_temps_a7fc14v((void *)NULL, cx, NULL, t335, ((uint64_t)0ULL), t341);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l45 = t342;
    }
    {
        uint8_t t344 = gm_l38;
        if (!t344) goto ie343;
        {
            {
                void * t347 = gm_l0;
                void * t348 = (void *)(((uint8_t *)t347) + 760);
                gm_t9805qtonbsag t349 = gm_f_pop_570g79n(t348);
            }
        }
        goto ix343;
ie343:;
ix343:;
    }
    {
        void * t350 = gm_l0;
        gm_t88rcucg1b7uk t351 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t350) + 168));
        gm_l46 = t351;
    }
    {
        void * t352 = gm_l0;
        void * t353 = gm_l1;
        gm_t88rcucg1b7uk t354 = gm_l46;
        gm_b0_1 t355 = gm_f_reject_returned_closure_1ouocor((void *)NULL, cx, NULL, t352, t353, t354);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t356 = gm_l0;
        gm_f_exit_fn_kj6vbu8((void *)&F.l47, t356);
    }
    {
        uint8_t t358 = gm_l30;
        if (!t358) goto ie357;
        {
            gm_l48 = ((uint8_t)0);
        }
        {
            void * t361 = gm_l0;
            void * t362 = GM_LD(void *, (((uint8_t *)t361) + 0));
            if (!(((((uint8_t *)t362) + 1956))[4] != 0)) goto ie360;
            uint32_t t363 = GM_LD(uint32_t, (((uint8_t *)t362) + 1956));
            gm_l49 = t363;
            {
                {
                    gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 16));
                    gm_l50 = t366;
                }
                {
                    gm_l51 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t368 = gm_l51;
                        uint64_t t369 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                        uint8_t t370 = (t368 < t369);
                        if (!t370) goto lb367;
                        {
                            uint64_t t373 = gm_l51;
                            uint64_t t374 = (uintptr_t)t373;
                            uint64_t t375 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                            gm_tj44oivvogvtl t378 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t374 * 40));
                            F.l52 = t378;
                            {
                                uint32_t t381 = GM_LD(uint32_t, ((((uint8_t *)&F.l52) + 0) + 0));
                                uint32_t t382 = gm_l49;
                                uint8_t t383 = ((t381 == t382));
                                if (!t383) goto ie380;
                                {
                                    gm_l48 = ((uint8_t)1);
                                }
                                goto ix380;
ie380:;
ix380:;
                            }
                            goto me376;
mn377:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 88, 13);
me376:;
                        }
lc367:;
                        {
                            uint64_t t386 = gm_l51;
                            uint64_t r387; bool o388 = __builtin_add_overflow(t386, ((uint64_t)1ULL), &r387);
                            uint64_t t389 = r387;
                            gm_l51 = t389;
                        }
                    }
lb367:;
                }
            }
            goto ix360;
ie360:;
ix360:;
        }
        {
            uint8_t t391 = gm_l48;
            uint8_t t392 = (uint8_t)!t391;
            if (!t392) goto ie390;
            {
                void * t394 = gm_l0;
                void * t395 = (void *)(((uint8_t *)t394) + 96);
                void * t396 = gm_l0;
                void * t397 = GM_LD(void *, (((uint8_t *)t396) + 0));
                gm_t88rcucg1b7uk t398 = gm_l46;
                gm_t88rcucg1b7uk t399 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t395, t397, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l53 = t399;
            }
            {
                void * t401 = gm_l5;
                gm_t88rcucg1b7uk t402 = gm_l53;
                uint8_t t403 = gm_f_is_error_8k27gd1(t401, GM_CONV(gm_csguankko4lmhd, t402));
                uint8_t t404 = (uint8_t)!t403;
                if (!t404) goto ie400;
                {
                    void * t406 = gm_l0;
                    gm_tl874t2atnlh3 t407 = gm_l4;
                    void * t408 = gm_l0;
                    gm_t88rcucg1b7uk t409 = gm_l53;
                    gm_b16_8 t410 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t408, t409);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t411;
                    GM_ST(gm_b16_8, ((uint8_t *)&t411 + 0), t410);
                    gm_tabb710d9mq2e t412;
                    gm_f_format_8c2oop8((void *)&t412, (void *)NULL, cx, NULL, t411);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t413 = t412;
                    if (((((uint8_t *)&t413))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t413) + 8, gm_srcl43af23eher3, 95, 32);
                    gm_b16_8 t414 = GM_LD(gm_b16_8, (((uint8_t *)&t413) + 8));
                    gm_b16_8 t415 = t414;
                    gm_b16_8 t416 = GM_LD(gm_b16_8, ((uint8_t *)&t415));
                    gm_b0_1 t417 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t406, t407, t416);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix400;
ie400:;
ix400:;
            }
            {
                void * t418 = gm_l0;
                void * t419 = GM_LD(void *, (((uint8_t *)t418) + 0));
                gm_tl874t2atnlh3 t420 = gm_l4;
                void * t421 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t419, t420);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t422 = 0;
                F.ret = t421;
                goto gm_fret;
            }
            goto ix390;
ie390:;
ix390:;
        }
        goto ix357;
ie357:;
ix357:;
    }
    {
        {
            gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
            gm_l54 = t424;
        }
        {
            gm_l55 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t426 = gm_l55;
                uint64_t t427 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                uint8_t t428 = (t426 < t427);
                if (!t428) goto lb425;
                {
                    uint64_t t431 = gm_l55;
                    uint64_t t432 = (uintptr_t)t431;
                    uint64_t t433 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                    uint32_t t434 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l54)) + t432 * 4));
                    uint32_t t435 = t434;
                    uint32_t t438 = GM_LD(uint32_t, ((uint8_t *)&t435));
                    gm_l56 = t438;
                    {
                        void * t441 = gm_l0;
                        uint32_t t442 = gm_l56;
                        gm_b16_8 t443 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 16));
                        gm_b16_8 t444 = t443;
                        gm_b16_8 t445 = GM_LD(gm_b16_8, ((uint8_t *)&t444));
                        void * t446 = (void *)((uint8_t *)&gm_l22);
                        uint32_t t447 = gm_l36;
                        gm_tl874t2atnlh3 t448 = gm_l4;
                        uint8_t t449 = gm_f_infer_open_effect_47fpti4((void *)NULL, cx, NULL, t441, t442, t445, t446, t447, t448);
                        if (gm_unw.kind) {
                            goto gm_ll429;
                        }
                        uint8_t t450 = (uint8_t)!t449;
                        if (!t450) goto ie440;
                        {
                            void * t452 = gm_l0;
                            void * t453 = GM_LD(void *, (((uint8_t *)t452) + 0));
                            gm_tl874t2atnlh3 t454 = gm_l4;
                            void * t455 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t453, t454);
                            if (gm_unw.kind) {
                                goto gm_ll429;
                            }
                            uint8_t t456 = 0;
                            F.ret = t455;
                            goto gm_fret;
                        }
                        goto ix440;
ie440:;
ix440:;
                    }
                    goto me436;
mn437:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 100, 5);
me436:;
                }
lc425:;
                {
                    uint64_t t458 = gm_l55;
                    uint64_t r459; bool o460 = __builtin_add_overflow(t458, ((uint64_t)1ULL), &r459);
                    uint64_t t461 = r459;
                    gm_l55 = t461;
                }
            }
gm_ll429:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb425; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc425; }
            }
            goto gm_lad;
lb425:;
        }
    }
    {
        uint8_t t463 = 0;
        uint8_t t464 = gm_l31;
        if (!t464) {
            uint8_t t465 = gm_l32;
            t463 = t465;
        } else t463 = 1;
        if (!t463) goto ie462;
        {
            uint32_t t467 = gm_l36;
            uint64_t t468 = (uint64_t)t467;
            uint64_t t469 = (uintptr_t)t468;
            void * t470 = gm_l0;
            void * t471 = GM_LD(void *, (((uint8_t *)t470) + 0));
            uint64_t t472 = GM_LD(uintptr_t, ((((uint8_t *)t471) + 344) + 0) + 8);
            if (t469 >= (uintptr_t)(t472)) gm_trap_index(t469, t472, gm_srcl43af23eher3, 103, 35);
            gm_t88rcucg1b7uk t473 = gm_l46;
            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t471) + 344) + 0)) + t469 * 136) + 56), t473);
        }
        goto ix462;
ie462:;
ix462:;
    }
    {
        uint8_t t474 = 0;
        uint8_t t475;
        if (((((uint8_t *)&F.l3))[32] != 0)) goto mn477;
        t475 = ((uint8_t)0);
        goto me476;
mn477:;
        t475 = ((uint8_t)1);
        goto me476;
mn478:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 104, 28);
me476:;
        if (t475) {
            uint8_t t479 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
            uint8_t t480 = (uint8_t)!t479;
            t474 = t480;
        } else t474 = 0;
        gm_l57 = t474;
    }
    {
        void * t481 = gm_l0;
        gm_b16_8 t482 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 16));
        gm_b16_8 t483 = t482;
        gm_b16_8 t484 = GM_LD(gm_b16_8, ((uint8_t *)&t483));
        void * t485 = (void *)((uint8_t *)&gm_l22);
        uint8_t t486 = 0;
        uint8_t t487 = gm_l11;
        if (t487) {
            uint8_t t488;
            if (((((uint8_t *)&F.l3))[32] != 0)) goto mn490;
            t488 = ((uint8_t)1);
            goto me489;
mn490:;
            t488 = ((uint8_t)0);
            goto me489;
mn491:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 105, 76);
me489:;
            t486 = t488;
        } else t486 = 0;
        uint8_t t492 = gm_l57;
        gm_b16_8 t493 = gm_f_route_performed_pruu3np((void *)NULL, cx, NULL, t481, t484, t485, t486, t492);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l58 = t493;
    }
    {
        uint64_t t494 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
        uint8_t t495 = (t494 > ((uint64_t)0ULL));
        gm_l59 = t495;
    }
    {
        uint8_t t497 = gm_l59;
        if (!t497) goto ie496;
        {
            gm_f_list_084fcd6((void *)&F.l60, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t499 = (void *)((uint8_t *)&F.l60);
            gm_b16_8 t500 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 0));
            gm_tftb1odc739t5 t501 = gm_f_extend_74ac4hb(t499, t500);
            gm_tftb1odc739t5 t502 = t501;
            if (((((uint8_t *)&t502))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t502) + 1, gm_srcl43af23eher3, 111, 9);
            gm_b0_1 t503 = GM_LD(gm_b0_1, (((uint8_t *)&t502) + 1));
        }
        {
            void * t504 = (void *)((uint8_t *)&F.l60);
            gm_b16_8 t505 = gm_l58;
            gm_b16_8 t506 = t505;
            gm_b16_8 t507 = GM_LD(gm_b16_8, ((uint8_t *)&t506));
            gm_tftb1odc739t5 t508 = gm_f_extend_74ac4hb(t504, t507);
            gm_tftb1odc739t5 t509 = t508;
            if (((((uint8_t *)&t509))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t509) + 1, gm_srcl43af23eher3, 112, 9);
            gm_b0_1 t510 = GM_LD(gm_b0_1, (((uint8_t *)&t509) + 1));
        }
        {
            uint32_t t511 = gm_l36;
            uint64_t t512 = (uint64_t)t511;
            uint64_t t513 = (uintptr_t)t512;
            void * t514 = gm_l0;
            void * t515 = GM_LD(void *, (((uint8_t *)t514) + 0));
            uint64_t t516 = GM_LD(uintptr_t, ((((uint8_t *)t515) + 344) + 0) + 8);
            if (t513 >= (uintptr_t)(t516)) gm_trap_index(t513, t516, gm_srcl43af23eher3, 113, 9);
            uint8_t *t517 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t515) + 344) + 0)) + t513 * 136) + 64);
            gm_b16_8 t518 = GM_LD(gm_b16_8, (((uint8_t *)&F.l60) + 0));
            gm_b16_8 t519 = t518;
            gm_b16_8 t520 = GM_LD(gm_b16_8, ((uint8_t *)&t519));
            gm_tnpifv20irnph t521 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t520);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_tnpifv20irnph, t517, t521);
        }
        goto ix496;
ie496:;
ix496:;
    }
    {
        uint8_t t523 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
        if (!t523) goto ie522;
        {
            uint32_t t525 = gm_l36;
            uint64_t t526 = (uint64_t)t525;
            uint64_t t527 = (uintptr_t)t526;
            void * t528 = gm_l0;
            void * t529 = GM_LD(void *, (((uint8_t *)t528) + 0));
            uint64_t t530 = GM_LD(uintptr_t, ((((uint8_t *)t529) + 384) + 0) + 8);
            if (t527 >= (uintptr_t)(t530)) gm_trap_index(t527, t530, gm_srcl43af23eher3, 115, 25);
            void * t531 = gm_l0;
            gm_t9805qtonbsag t532 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t531) + 400));
            GM_ST(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t529) + 384) + 0)) + t527 * 64) + 48), t532);
        }
        goto ix522;
ie522:;
ix522:;
    }
    {
        void * t533 = gm_l0;
        void * t534 = (void *)(((uint8_t *)t533) + 704);
        gm_tl93vjpcesj48 t535;
        memset((uint8_t *)&t535 + 4, 0, 4);
        uint32_t t536 = gm_l36;
        GM_ST(uint32_t, (uint8_t *)&t535 + 0, t536);
        gm_b16_8 t537 = GM_LD(gm_b16_8, (((uint8_t *)&F.l40) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t535 + 8, t537);
        gm_b16_8 t538 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t535 + 24, t538);
        void * t539 = gm_l45;
        GM_ST(void *, (uint8_t *)&t535 + 40, t539);
        gm_tftb1odc739t5 t540 = gm_f_push_jd7smj6(t534, &t535);
        gm_tftb1odc739t5 t541 = t540;
        if (((((uint8_t *)&t541))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t541) + 1, gm_srcl43af23eher3, 116, 5);
        gm_b0_1 t542 = GM_LD(gm_b0_1, (((uint8_t *)&t541) + 1));
    }
    {
        gm_f_list_nof7vfj((void *)&F.l61, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t544 = gm_l18;
            gm_l62 = t544;
        }
        {
            gm_l63 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t546 = gm_l63;
                uint64_t t547 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                uint8_t t548 = (t546 < t547);
                if (!t548) goto lb545;
                {
                    uint64_t t551 = gm_l63;
                    uint64_t t552 = (uintptr_t)t551;
                    uint64_t t553 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                    gm_t5v0c8ia6a5mo t556 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l62)) + t552 * 56));
                    F.l64 = t556;
                    {
                        void * t558 = (void *)((uint8_t *)&F.l61);
                        gm_t88rcucg1b7uk t559 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l64) + 16));
                        gm_tftb1odc739t5 t560 = gm_f_push_k3l9nic(t558, GM_CONV(gm_csguankko4lmhd, t559));
                        gm_tftb1odc739t5 t561 = t560;
                        if (((((uint8_t *)&t561))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t561) + 1, gm_srcl43af23eher3, 118, 23);
                        gm_b0_1 t562 = GM_LD(gm_b0_1, (((uint8_t *)&t561) + 1));
                    }
                    goto me554;
mn555:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 118, 5);
me554:;
                }
lc545:;
                {
                    uint64_t t564 = gm_l63;
                    uint64_t r565; bool o566 = __builtin_add_overflow(t564, ((uint64_t)1ULL), &r565);
                    uint64_t t567 = r565;
                    gm_l63 = t567;
                }
            }
lb545:;
        }
    }
    {
        gm_l65 = ((uint8_t)0);
    }
    {
        if (!((((uint8_t *)&F.l10))[48] != 0)) goto ie568;
        gm_tddtjfggdat0d t569 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l10));
        F.l66 = t569;
        uint8_t t570 = gm_l11;
        if (!t570) goto ie568;
        uint8_t t571 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
        uint8_t t572 = (uint8_t)!t571;
        if (!t572) goto ie568;
        {
            uint8_t t574 = GM_LD(uint8_t, (((uint8_t *)&F.l66) + 40));
            gm_l65 = t574;
        }
        goto ix568;
ie568:;
ix568:;
    }
    {
        uint8_t t576 = gm_l65;
        if (!t576) goto ie575;
        {
            void * t579 = gm_l0;
            void * t580 = GM_LD(void *, (((uint8_t *)t579) + 0));
            void * t581 = (void *)((uint8_t *)&gm_l22);
            uint8_t t582 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t580, t581);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t582) goto ie578;
            {
                uint32_t t584 = gm_l36;
                uint64_t t585 = (uint64_t)t584;
                uint64_t t586 = (uintptr_t)t585;
                void * t587 = gm_l0;
                void * t588 = GM_LD(void *, (((uint8_t *)t587) + 0));
                uint64_t t589 = GM_LD(uintptr_t, ((((uint8_t *)t588) + 344) + 0) + 8);
                if (t586 >= (uintptr_t)(t589)) gm_trap_index(t586, t589, gm_srcl43af23eher3, 124, 13);
                GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t588) + 344) + 0)) + t586 * 136) + 128), ((uint8_t)1));
            }
            goto ix578;
ie578:;
            {
                void * t591 = gm_l0;
                gm_tl874t2atnlh3 t592 = gm_l4;
                gm_b16_8 t593;
                GM_ST(void *, (uint8_t *)&t593, (void *)(gm_srdq954rpedg3));
                GM_ST(uintptr_t, (uint8_t *)&t593 + 8, (uintptr_t)(92));
                gm_b0_1 t594 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t591, t592, t593);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix578:;
        }
        goto ix575;
ie575:;
ix575:;
    }
    {
        memset((uint8_t *)&F.l67 + 20, 0, 4);
        memset((uint8_t *)&F.l67 + 41, 0, 7);
        gm_b16_8 t595 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
        gm_b16_8 t596 = t595;
        gm_b16_8 t597 = GM_LD(gm_b16_8, ((uint8_t *)&t596));
        GM_ST(gm_b16_8, (uint8_t *)&F.l67 + 0, t597);
        gm_t88rcucg1b7uk t598 = gm_l46;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l67 + 16, t598);
        gm_tnpifv20irnph t599 = gm_l22;
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&F.l67 + 24, t599);
        uint8_t t600 = gm_l65;
        GM_ST(uint8_t, (uint8_t *)&F.l67 + 40, t600);
    }
    {
        uint8_t t602 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
        if (!t602) goto ie601;
        {
            uint8_t t605 = gm_l11;
            if (!t605) goto ie604;
            {
                void * t607 = gm_l0;
                gm_tl874t2atnlh3 t608 = gm_l4;
                gm_b16_8 t609;
                GM_ST(void *, (uint8_t *)&t609, (void *)(gm_sn1j4m1p1cnm1));
                GM_ST(uintptr_t, (uint8_t *)&t609 + 8, (uintptr_t)(82));
                gm_b0_1 t610 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t607, t608, t609);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t611 = gm_l0;
                void * t612 = GM_LD(void *, (((uint8_t *)t611) + 0));
                gm_tl874t2atnlh3 t613 = gm_l4;
                void * t614 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t612, t613);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t615 = 0;
                F.ret = t614;
                goto gm_fret;
            }
            goto ix604;
ie604:;
ix604:;
        }
        {
            if (!((((uint8_t *)&F.l3))[32] != 0)) goto ie616;
            gm_t08olq97rscn3 t617 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l3));
            gm_l68 = t617;
            {
                void * t619 = gm_l0;
                uint32_t t620 = gm_l36;
                uint64_t t621 = gm_l39;
                gm_b16_8 t622 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 0));
                gm_tl874t2atnlh3 t623 = gm_l4;
                gm_b0_1 t624 = gm_f_check_no_self_reference_4ra5lcu((void *)NULL, cx, NULL, t619, t620, t621, t622, t623);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix616;
ie616:;
ix616:;
        }
        {
            void * t625 = gm_l5;
            gm_tddtjfggdat0d t626 = F.l67;
            gm_t53at80pfdus3 t627;
            GM_ST(uint8_t, (uint8_t *)&t627 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t627 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t627 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t627 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t627 + 8), t626);
            gm_t88rcucg1b7uk t628 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t625, &t627);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t629 = gm_l36;
            gm_teti411eujvf4 t630;
            memset(&t630, 0, sizeof t630);
            GM_ST(uint8_t, ((uint8_t *)&t630 + 0), ((uint8_t)27ULL));
            GM_ST(uint32_t, ((uint8_t *)&t630 + 8), t629);
            gm_tl874t2atnlh3 t631 = gm_l4;
            void * t632 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t628, &t630, t631);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t633 = 0;
            F.ret = t632;
            goto gm_fret;
        }
        goto ix601;
ie601:;
ix601:;
    }
    {
        gm_l69 = ((uint8_t)0);
    }
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie634;
        gm_t88rcucg1b7uk t635 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l70 = t635;
        void * t636 = gm_l5;
        gm_t88rcucg1b7uk t637 = gm_l70;
        gm_t53at80pfdus3 t638;
        gm_f_kind_jcnt4hm((void *)&t638, t636, GM_CONV(gm_csguankko4lmhd, t637));
        gm_t53at80pfdus3 t639 = t638;
        uint8_t t640;
        uint8_t t643 = GM_LD(uint8_t, (((uint8_t *)&t639) + 0));
        if (t643 != ((uint8_t)15ULL)) goto mn642;
        t640 = ((uint8_t)1);
        goto me641;
mn642:;
        t640 = ((uint8_t)0);
        goto me641;
mn644:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 139, 24);
me641:;
        if (!t640) goto ie634;
        {
            gm_l69 = ((uint8_t)1);
        }
        goto ix634;
ie634:;
ix634:;
    }
    {
        gm_t88rcucg1b7uk t646;
        uint8_t t648 = 0;
        uint8_t t649 = gm_l59;
        if (!t649) {
            uint8_t t650 = gm_l69;
            t648 = t650;
        } else t648 = 1;
        if (!t648) goto ie647;
        void * t652 = gm_l5;
        gm_tddtjfggdat0d t653 = F.l67;
        gm_t53at80pfdus3 t654;
        GM_ST(uint8_t, (uint8_t *)&t654 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t654 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t654 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t654 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t654 + 8), t653);
        gm_t88rcucg1b7uk t655 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t652, &t654);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t656 = t655;
        t646 = t656;
        goto ix647;
ie647:;
        void * t658 = gm_l5;
        gm_tddtjfggdat0d t659 = F.l67;
        gm_t53at80pfdus3 t660;
        GM_ST(uint8_t, (uint8_t *)&t660 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t660 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t660 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t660 + 0), ((uint8_t)16ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t660 + 8), t659);
        gm_t88rcucg1b7uk t661 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t658, &t660);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t662 = t661;
        t646 = t662;
ix647:;
        gm_l71 = t646;
    }
    {
        gm_t88rcucg1b7uk t663 = gm_l71;
        uint32_t t664 = gm_l36;
        gm_teti411eujvf4 t665;
        memset(&t665, 0, sizeof t665);
        GM_ST(uint8_t, ((uint8_t *)&t665 + 0), ((uint8_t)26ULL));
        GM_ST(uint32_t, ((uint8_t *)&t665 + 8), t664);
        gm_tl874t2atnlh3 t666 = gm_l4;
        void * t667 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t663, &t665, t666);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t668 = 0;
        F.ret = t667;
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
__attribute__((aligned(4))) uint8_t gm_f_infer_open_effect_47fpti4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, uint32_t a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_infer_open_effect_47fpti4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* performed */
    void * gm_l3; /* effects */
    uint32_t gm_l4; /* fn_id */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint8_t gm_l10; /* seen */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_tiilmfd16v3fc gm_l13; /* x */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tiilmfd16v3fc gm_l17; /* e */
    GM_STACK_CHECK(&F, 1190u);
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
        gm_f_list_084fcd6((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l7 = t3;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l8;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l8;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_tj44oivvogvtl t15 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t11 * 40));
                    F.l9 = t15;
                    {
                        uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)&F.l9) + 0) + 0));
                        uint32_t t19 = gm_l1;
                        uint8_t t20 = (!(t18 == t19));
                        if (!t20) goto ie17;
                        {
                            goto lc4;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        gm_l10 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_l11 = t23;
                        }
                        {
                            gm_l12 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t25 = gm_l12;
                                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                uint8_t t27 = (t25 < t26);
                                if (!t27) goto lb24;
                                {
                                    uint64_t t30 = gm_l12;
                                    uint64_t t31 = (uintptr_t)t30;
                                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    gm_tiilmfd16v3fc t35 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t31 * 24));
                                    gm_l13 = t35;
                                    {
                                        void * t38 = (void *)((uint8_t *)&gm_l13);
                                        void * t39 = (void *)(((uint8_t *)&F.l9) + 0);
                                        int32_t t40 = gm_f_cmp_ref_8ntrqll(t38, t39);
                                        uint8_t t41 = ((t40 == ((int32_t)0LL)));
                                        if (!t41) goto ie37;
                                        {
                                            gm_l10 = ((uint8_t)1);
                                        }
                                        goto ix37;
ie37:;
ix37:;
                                    }
                                    goto me33;
mn34:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 258, 9);
me33:;
                                }
lc24:;
                                {
                                    uint64_t t44 = gm_l12;
                                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                                    uint64_t t47 = r45;
                                    gm_l12 = t47;
                                }
                            }
lb24:;
                        }
                    }
                    {
                        uint8_t t49 = gm_l10;
                        uint8_t t50 = (uint8_t)!t49;
                        if (!t50) goto ie48;
                        {
                            void * t52 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t53 = gm_f_push_d1r4h19(t52, &(*(gm_tiilmfd16v3fc *)(((uint8_t *)&F.l9) + 0)));
                            gm_tftb1odc739t5 t54 = t53;
                            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcl43af23eher3, 261, 23);
                            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        }
                        goto ix48;
ie48:;
ix48:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 255, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t57 = gm_l8;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l8 = t60;
                }
            }
lb4:;
        }
    }
    {
        uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t63 = (t62 > ((uint64_t)1ULL));
        if (!t63) goto ie61;
        {
            gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_l15 = t66;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t68 = gm_l16;
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t70 = (t68 < t69);
                    if (!t70) goto lb67;
                    {
                        uint64_t t73 = gm_l16;
                        uint64_t t74 = (uintptr_t)t73;
                        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_tiilmfd16v3fc t78 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t74 * 24));
                        gm_l17 = t78;
                        {
                            void * t80 = (void *)((uint8_t *)&F.l14);
                            void * t81 = gm_l0;
                            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
                            void * t83 = (void *)(((uint8_t *)t82) + 8);
                            void * t84 = (void *)((uint8_t *)&gm_l17);
                            gm_b16_8 t85 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t83, t84);
                            if (gm_unw.kind) {
                                goto gm_ll71;
                            }
                            gm_tnpifv20irnph t86;
                            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
                            gm_tabb710d9mq2e t87;
                            gm_f_format_h4jcodi((void *)&t87, (void *)NULL, cx, NULL, t86);
                            if (gm_unw.kind) {
                                goto gm_ll71;
                            }
                            gm_tabb710d9mq2e t88 = t87;
                            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_srcl43af23eher3, 265, 43);
                            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
                            gm_b16_8 t90 = t89;
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                            gm_tftb1odc739t5 t92 = gm_f_push_3ef746b(t80, t91);
                            gm_tftb1odc739t5 t93 = t92;
                            if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srcl43af23eher3, 265, 32);
                            gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                        }
                        goto me76;
mn77:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 265, 9);
me76:;
                    }
lc67:;
                    {
                        uint64_t t96 = gm_l16;
                        uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                        uint64_t t99 = r97;
                        gm_l16 = t99;
                    }
                }
gm_ll71:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb67; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc67; }
                }
                goto gm_lad;
lb67:;
            }
        }
        {
            void * t100 = gm_l0;
            gm_tl874t2atnlh3 t101 = gm_l5;
            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t103 = t102;
            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_southrlg4k72m));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(5));
            gm_tabb710d9mq2e t106;
            gm_f_join_sfknljb((void *)&t106, (void *)NULL, cx, NULL, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t107 = t106;
            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srcl43af23eher3, 266, 128);
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
            gm_tnpifv20irnph t109;
            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
            gm_tabb710d9mq2e t110;
            gm_f_format_57u5svl((void *)&t110, (void *)NULL, cx, NULL, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t111 = t110;
            if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcl43af23eher3, 266, 24);
            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
            gm_b16_8 t113 = t112;
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            gm_b0_1 t115 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t100, t101, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t116 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        uint64_t t118 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t119 = ((t118 == ((uint64_t)1ULL)));
        if (!t119) goto ie117;
        {
            gm_f_list_084fcd6((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t121 = (void *)((uint8_t *)&F.l18);
            void * t122 = gm_l3;
            gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)t122) + 0));
            gm_tftb1odc739t5 t124 = gm_f_extend_74ac4hb(t121, t123);
            gm_tftb1odc739t5 t125 = t124;
            if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcl43af23eher3, 271, 9);
            gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
        }
        {
            void * t127 = (void *)((uint8_t *)&F.l18);
            uint64_t t128 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t129 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_srcl43af23eher3, 272, 18);
            gm_tftb1odc739t5 t130 = gm_f_push_d1r4h19(t127, &(*(gm_tiilmfd16v3fc *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t128 * 24)));
            gm_tftb1odc739t5 t131 = t130;
            if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 1, gm_srcl43af23eher3, 272, 9);
            gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 1));
        }
        {
            void * t133 = gm_l3;
            uint8_t *t134 = ((uint8_t *)t133);
            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            gm_b16_8 t136 = t135;
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
            gm_tnpifv20irnph t138 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_tnpifv20irnph, t134, t138);
        }
        {
            uint32_t t139 = gm_l4;
            uint64_t t140 = (uint64_t)t139;
            uint64_t t141 = (uintptr_t)t140;
            void * t142 = gm_l0;
            void * t143 = GM_LD(void *, (((uint8_t *)t142) + 0));
            uint64_t t144 = GM_LD(uintptr_t, ((((uint8_t *)t143) + 344) + 0) + 8);
            if (t141 >= (uintptr_t)(t144)) gm_trap_index(t141, t144, gm_srcl43af23eher3, 274, 9);
            void * t145 = gm_l3;
            gm_tnpifv20irnph t146 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)t145));
            GM_ST(gm_tnpifv20irnph, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t143) + 344) + 0)) + t141 * 136) + 64), t146);
        }
        goto ix117;
ie117:;
ix117:;
    }
    {
        uint8_t t147 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_generic_jrl2kf9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_generic_jrl2kf9 F;
    void * gm_l0; /* lit */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    void * gm_l4; /* t */
    gm_t08olq97rscn3 gm_l5; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 72) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 0) + 0));
            gm_l1 = t10;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l2;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l2;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tf2orqcrphmek t22 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 56));
                    F.l3 = t22;
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
                        if (!t25) goto ie24;
                        {
                            uint8_t t27 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    {
                        void * t29 = GM_LD(void *, (((uint8_t *)&F.l3) + 24));
                        void * t30 = t29;
                        if (!(GM_LD(void *, ((uint8_t *)&t30)) != 0)) goto ie28;
                        void * t31 = GM_LD(void *, ((uint8_t *)&t30));
                        gm_l4 = t31;
                        void * t32 = gm_l4;
                        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                        if (t33 != ((uint8_t)6ULL)) goto ie28;
                        gm_t08olq97rscn3 t34 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t32) + 8) + 8));
                        gm_l5 = t34;
                        uint8_t t35 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l5) + 0)), gm_s2737i1iq57nf, 4) == 0));
                        if (!t35) goto ie28;
                        {
                            uint8_t t37 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 149, 5);
me20:;
                }
lc11:;
                {
                    uint64_t t39 = gm_l2;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l2 = t42;
                }
            }
lb11:;
        }
    }
    {
        uint8_t t43 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_literal_body_5lm01a5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint8_t a3) {
    struct gm_fr_literal_body_5lm01a5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    uint8_t gm_l3; /* infer_ret */
    void * gm_l4; /* e */
    void * gm_l5; /* b */
    void * gm_l7; /* block */
    GM_STACK_CHECK(&F, 388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t5 != ((uint8_t)1ULL)) goto mn4;
        void * t6 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l4 = t6;
        {
            uint8_t t9 = gm_l3;
            if (!t9) goto ie8;
            {
                void * t11 = gm_l0;
                void * t12 = gm_l4;
                gm_tkn42u7na3t81 t13;
                memset(&t13, 0, sizeof t13);
                void * t14 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t11, t12, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t14;
            }
            {
                void * t15 = gm_l0;
                void * t16 = gm_l5;
                gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
                GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 168), t17);
            }
            {
                void * t18 = gm_l5;
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            goto ix8;
ie8:;
ix8:;
        }
        {
            void * t20 = gm_l0;
            void * t21 = gm_l0;
            void * t22 = gm_l4;
            gm_t88rcucg1b7uk t23 = gm_l2;
            gm_tkn42u7na3t81 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[4] = 1;
            void * t25 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t21, t22, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t26 = gm_l2;
            void * t27 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t20, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto me3;
mn4:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t30 != ((uint8_t)0ULL)) goto mn29;
        gm_t79rbdv4rafe5 t31 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)t2) + 8));
        F.l6 = t31;
        {
            void * t33 = gm_l0;
            void * t34 = (void *)((uint8_t *)&F.l6);
            void * t35 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t33, t34, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t35;
        }
        {
            void * t37 = gm_l0;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + 744));
            if (!t38) goto ie36;
            {
                void * t40 = gm_l0;
                uint8_t *t41 = (((uint8_t *)t40) + 168);
                void * t42 = gm_l0;
                gm_tkn42u7na3t81 t43 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)t42) + 748));
                gm_tkn42u7na3t81 t44 = t43;
                gm_t88rcucg1b7uk t45;
                if (((((uint8_t *)&t44))[4] != 0)) {
                    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t44) + 0));
                    t45 = t46;
                } else {
                    void * t47 = gm_l0;
                    void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                    void * t49 = (void *)(((uint8_t *)t48) + 8);
                    gm_t88rcucg1b7uk t50 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t45 = t50;
                }
                GM_ST(gm_t88rcucg1b7uk, t41, t45);
            }
            {
                void * t51 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t51) + 744), ((uint8_t)0));
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            void * t52 = gm_l0;
            void * t53 = gm_l7;
            void * t54 = (void *)((uint8_t *)&F.l6);
            void * t55 = gm_f_finish_block_body_4vol3tv((void *)NULL, cx, NULL, t52, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto me3;
mn29:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 187, 5);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_literal_params_n5511im(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_literal_params_n5511im F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tkn42u7na3t81 gm_l7; /* annotated */
    void * gm_l8; /* t */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t08olq97rscn3 gm_l11; /* n */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* ty */
    gm_t88rcucg1b7uk gm_l14; /* t */
    GM_STACK_CHECK(&F, 1184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        gm_f_list_af74s0q((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 0));
            gm_l4 = t4;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l5;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tf2orqcrphmek t16 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 56));
                    F.l6 = t16;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 16));
                        if (!t19) goto ie18;
                        {
                            void * t21 = gm_l0;
                            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 40));
                            gm_b16_8 t23;
                            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_skfniirr0r2gc));
                            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(62));
                            gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t21, t22, t23);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        gm_tkn42u7na3t81 t25;
                        memset(&t25, 0, sizeof t25);
                        gm_l7 = t25;
                    }
                    {
                        void * t27 = GM_LD(void *, (((uint8_t *)&F.l6) + 24));
                        void * t28 = t27;
                        if (!(GM_LD(void *, ((uint8_t *)&t28)) != 0)) goto ie26;
                        void * t29 = GM_LD(void *, ((uint8_t *)&t28));
                        gm_l8 = t29;
                        {
                            void * t31 = gm_l0;
                            void * t32 = gm_l8;
                            gm_t88rcucg1b7uk t33 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t31, t32);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tkn42u7na3t81 t34;
                            GM_ST(uint8_t, (uint8_t *)&t34 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t34 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34), t33);
                            ((uint8_t *)&t34)[4] = 1;
                            gm_l7 = t34;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    {
                        {
                            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_l9 = t36;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t38 = gm_l10;
                                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t40 = (t38 < t39);
                                if (!t40) goto lb37;
                                {
                                    uint64_t t43 = gm_l10;
                                    uint64_t t44 = (uintptr_t)t43;
                                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_t08olq97rscn3 t48 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t44 * 32));
                                    gm_l11 = t48;
                                    {
                                        uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                        gm_l12 = t50;
                                    }
                                    {
                                        void * t51 = gm_l0;
                                        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
                                        void * t53 = (void *)(((uint8_t *)t52) + 8);
                                        gm_t88rcucg1b7uk t54 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t53);
                                        if (gm_unw.kind) {
                                            goto gm_ll41;
                                        }
                                        gm_l13 = t54;
                                    }
                                    {
                                        if (!((((uint8_t *)&gm_l7))[4] != 0)) goto ie55;
                                        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l7));
                                        gm_l14 = t56;
                                        {
                                            gm_t88rcucg1b7uk t58 = gm_l14;
                                            gm_l13 = t58;
                                        }
                                        goto ix55;
ie55:;
                                        if (!((((uint8_t *)&F.l2))[48] != 0)) goto ie59;
                                        gm_tddtjfggdat0d t60 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l2));
                                        F.l15 = t60;
                                        uint64_t t61 = gm_l12;
                                        uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                        uint8_t t63 = (t61 < t62);
                                        if (!t63) goto ie59;
                                        void * t64 = gm_l0;
                                        void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
                                        void * t66 = (void *)(((uint8_t *)t65) + 8);
                                        uint64_t t67 = gm_l12;
                                        uint64_t t68 = (uintptr_t)t67;
                                        uint64_t t69 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                        if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcl43af23eher3, 169, 85);
                                        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t68 * 4));
                                        uint8_t t71 = gm_f_has_param_u9t0efs(t66, GM_CONV(gm_csguankko4lmhd, t70));
                                        uint8_t t72 = (uint8_t)!t71;
                                        if (!t72) goto ie59;
                                        {
                                            uint64_t t74 = gm_l12;
                                            uint64_t t75 = (uintptr_t)t74;
                                            uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                                            if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcl43af23eher3, 172, 22);
                                            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t75 * 4));
                                            gm_l13 = t77;
                                        }
                                        goto ix59;
ie59:;
                                        {
                                            void * t79 = gm_l0;
                                            gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l11) + 16));
                                            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                                            gm_tnpifv20irnph t82;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t81);
                                            gm_tabb710d9mq2e t83;
                                            gm_f_format_vrv37j5((void *)&t83, (void *)NULL, cx, NULL, t82);
                                            if (gm_unw.kind) {
                                                goto gm_ll41;
                                            }
                                            gm_tabb710d9mq2e t84 = t83;
                                            if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 8, gm_srcl43af23eher3, 174, 34);
                                            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&t84) + 8));
                                            gm_b16_8 t86 = t85;
                                            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                                            gm_b0_1 t88 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t79, t80, t87);
                                            if (gm_unw.kind) {
                                                goto gm_ll41;
                                            }
                                        }
ix59:;
ix55:;
                                    }
                                    {
                                        void * t89 = (void *)((uint8_t *)&F.l3);
                                        gm_t5v0c8ia6a5mo t90;
                                        memset((uint8_t *)&t90 + 49, 0, 7);
                                        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t90 + 0, t91);
                                        gm_t88rcucg1b7uk t92 = gm_l13;
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t90 + 16, t92);
                                        gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l11) + 16));
                                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t90 + 20, t93);
                                        GM_ST(void *, (uint8_t *)&t90 + 32, ((void *)0));
                                        gm_t9805qtonbsag t94;
                                        memset(&t94, 0, sizeof t94);
                                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t90 + 40, t94);
                                        GM_ST(uint8_t, (uint8_t *)&t90 + 48, ((uint8_t)0));
                                        gm_tftb1odc739t5 t95 = gm_f_push_o8u01sh(t89, &t90);
                                        gm_tftb1odc739t5 t96 = t95;
                                        if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_srcl43af23eher3, 176, 13);
                                        gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
                                    }
                                    goto me46;
mn47:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 164, 9);
me46:;
                                }
lc37:;
                                {
                                    uint64_t t99 = gm_l10;
                                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                                    uint64_t t102 = r100;
                                    gm_l10 = t102;
                                }
                            }
gm_ll41:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
                            }
                            goto gm_ll9;
lb37:;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 160, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t104 = gm_l5;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l5 = t107;
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
        if (!((((uint8_t *)&F.l2))[48] != 0)) goto ie108;
        gm_tddtjfggdat0d t109 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l2));
        F.l16 = t109;
        uint64_t t110 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        uint64_t t111 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t112 = (!(t110 == t111));
        if (!t112) goto ie108;
        {
            void * t114 = gm_l0;
            void * t115 = gm_l1;
            gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t115) + 0) + 56));
            uint64_t t117 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
            uint64_t t118 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            gm_t2lqu8jsj511d t119;
            GM_ST(uint64_t, ((uint8_t *)&t119 + 0), t117);
            GM_ST(uint64_t, ((uint8_t *)&t119 + 8), t118);
            gm_tabb710d9mq2e t120;
            gm_f_format_9lchiud((void *)&t120, (void *)NULL, cx, NULL, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t121 = t120;
            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 8, gm_srcl43af23eher3, 180, 32);
            gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t121) + 8));
            gm_b16_8 t123 = t122;
            gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
            gm_b0_1 t125 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t114, t116, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix108;
ie108:;
ix108:;
    }
    {
        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t127 = 0;
        F.ret = t126;
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
__attribute__((aligned(4))) void gm_f_nested_fn_binding_0cl95ol(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_nested_fn_binding_0cl95ol F;
    void * gm_l0; /* cx */
    gm_t08olq97rscn3 gm_l1; /* name */
    void * gm_l2; /* lit */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* v */
    uint32_t gm_l5; /* id */
    void * gm_l6; /* scope */
    uint64_t gm_l7; /* j */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 959u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l2;
        uint8_t t4 = gm_f_is_generic_jrl2kf9(t3);
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            void * t7 = gm_l2;
            gm_tl874t2atnlh3 t8 = gm_l3;
            gm_f_nested_generic_fn_q4qqrgj((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t6, &(*(gm_t08olq97rscn3 *)((uint8_t *)a1)), t7, t8);
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
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        gm_tkn42u7na3t81 t11;
        memset(&t11, 0, sizeof t11);
        gm_t08olq97rscn3 t12 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
        gm_t1s75i08qtptd t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 36, 0);
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[32] = 1;
        gm_tl874t2atnlh3 t14 = gm_l3;
        void * t15 = gm_f_fn_literal_fiu78lb((void *)NULL, cx, NULL, t9, t10, t11, &t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t15;
    }
    {
        void * t17 = gm_l4;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
        if (t18 != ((uint8_t)27ULL)) goto ie16;
        {
            void * t20 = gm_l0;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
            void * t22 = gm_l4;
            gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + 0));
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            uint32_t t25 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t20, t21, t23, ((uint8_t)0), t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t25;
        }
        {
            void * t26 = gm_l0;
            uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 56) + 0) + 8);
            uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
            if (o29) gm_trap("integer overflow in `-`", gm_srcl43af23eher3, 319, 35);
            uint64_t t30 = r28;
            uint64_t t31 = (uintptr_t)t30;
            void * t32 = gm_l0;
            uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 56) + 0) + 8);
            if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_srcl43af23eher3, 319, 19);
            void * t34 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 56) + 0)) + t31 * 40);
            gm_l6 = t34;
        }
        {
            void * t35 = gm_l6;
            uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
            gm_l7 = t36;
        }
        {
            for (;;) {
                uint64_t t38 = gm_l7;
                uint8_t t39 = (t38 > ((uint64_t)0ULL));
                if (!t39) goto lb37;
                {
                    uint64_t t42 = gm_l7;
                    uint64_t r43; bool o44 = __builtin_sub_overflow(t42, ((uint64_t)1ULL), &r43);
                    if (o44) gm_trap("integer overflow in `-`", gm_srcl43af23eher3, 322, 13);
                    uint64_t t45 = r43;
                    gm_l7 = t45;
                }
                {
                    uint64_t t47 = gm_l7;
                    uint64_t t48 = (uintptr_t)t47;
                    void * t49 = gm_l6;
                    uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)t49) + 0) + 8);
                    if (t48 >= (uintptr_t)(t50)) gm_trap_index(t48, t50, gm_srcl43af23eher3, 323, 16);
                    gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t49) + 0)) + t48 * 224) + 0));
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
                    gm_b16_8 t53 = t51;
                    gm_b16_8 t54 = t52;
                    uint8_t t55 = (gm_eq0iqiubfm7e(((uint8_t *)&t53), ((uint8_t *)&t54)));
                    if (!t55) goto ie46;
                    {
                        uint64_t t57 = gm_l7;
                        uint64_t t58 = (uintptr_t)t57;
                        void * t59 = gm_l6;
                        uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)t59) + 0) + 8);
                        if (t58 >= (uintptr_t)(t60)) gm_trap_index(t58, t60, gm_srcl43af23eher3, 324, 17);
                        uint32_t t61 = gm_l5;
                        gm_t930q5l38hdap t62;
                        memset(&t62, 0, sizeof t62);
                        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t62 + 8), t61);
                        GM_ST(gm_t930q5l38hdap, (((uint8_t *)GM_LD(void *, (((uint8_t *)t59) + 0)) + t58 * 224) + 16), t62);
                    }
                    {
                        goto lb37;
                    }
                    goto ix46;
ie46:;
ix46:;
                }
lc37:;
            }
lb37:;
        }
        {
            uint32_t t63 = gm_l5;
            void * t64 = gm_l4;
            gm_ttggq7ng4kp3p t65;
            GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t65 + 0), t63);
            GM_ST(void *, ((uint8_t *)&t65 + 8), t64);
            gm_trcmmd12cefb1 t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t66 + 8), t65);
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t69 = gm_l4;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t70 != ((uint8_t)26ULL)) goto ie68;
        uint32_t t71 = GM_LD(uint32_t, ((((uint8_t *)t69) + 8) + 8));
        gm_l8 = t71;
        {
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
            void * t75 = (void *)(((uint8_t *)t74) + 1608);
            gm_tommf4j9upomc t76;
            memset((uint8_t *)&t76 + 20, 0, 4);
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t76 + 0, t77);
            uint32_t t78 = gm_l8;
            GM_ST(uint32_t, (uint8_t *)&t76 + 16, t78);
            gm_tftb1odc739t5 t79 = gm_f_push_p9c61k8(t75, &t76);
            gm_tftb1odc739t5 t80 = t79;
            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcl43af23eher3, 330, 30);
            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        void * t82 = gm_l0;
        void * t83 = GM_LD(void *, (((uint8_t *)t82) + 0));
        gm_tl874t2atnlh3 t84 = gm_l3;
        void * t85 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t83, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t86;
        GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t86 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t86 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t86 + 8), t85);
        uint8_t t87 = 0;
        F.ret = t86;
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
inline __attribute__((aligned(4))) void gm_f_nested_generic_fn_q4qqrgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_nested_generic_fn_q4qqrgj F;
    void * gm_l0; /* cx */
    gm_t08olq97rscn3 gm_l1; /* name */
    void * gm_l2; /* lit */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK(&F, 914u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 8));
        void * t7 = gm_l2;
        gm_t6qooom8qa2do t8;
        gm_f_declare_fn_6piolnr((void *)&t8, (void *)NULL, cx, NULL, t4, t6, &(*(gm_t08olq97rscn3 *)((uint8_t *)a1)), t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t9 = t8;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t10 != ((uint8_t)5ULL)) goto ie2;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&t9) + 8));
        gm_l4 = t11;
        {
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = (void *)(((uint8_t *)t14) + 2272);
            gm_tp9ftj4p31ha1 t16;
            memset((uint8_t *)&t16 + 4, 0, 4);
            uint32_t t17 = gm_l4;
            GM_ST(uint32_t, (uint8_t *)&t16 + 0, t17);
            memcpy((uint8_t *)&t16 + 8, ((uint8_t *)a1), 32);
            gm_tftb1odc739t5 t18 = gm_f_push_bu2hcel(t15, &t16);
            gm_tftb1odc739t5 t19 = t18;
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srcl43af23eher3, 305, 9);
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
        }
        {
            void * t21 = gm_l0;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
            uint32_t t23 = gm_l4;
            gm_t930q5l38hdap t24;
            memset(&t24, 0, sizeof t24);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)3ULL));
            GM_ST(uint32_t, ((uint8_t *)&t24 + 8), t23);
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_b0_1 t26 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t21, t22, &t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        gm_tl874t2atnlh3 t29 = gm_l3;
        void * t30 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t31 + 8), t30);
        uint8_t t32 = 0;
        F.ret = t31;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_reject_returned_closure_1ouocor(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_reject_returned_closure_1ouocor F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_t88rcucg1b7uk gm_l2; /* final_ret */
    gm_tl874t2atnlh3 gm_l3; /* at */
    void * gm_l4; /* r */
    void * gm_l5; /* e */
    GM_STACK_CHECK(&F, 372u);
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
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 96);
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        gm_t88rcucg1b7uk t10 = gm_l2;
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t7, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t12;
        gm_f_kind_jcnt4hm((void *)&t12, t5, GM_CONV(gm_csguankko4lmhd, t11));
        gm_t53at80pfdus3 t13 = t12;
        uint8_t t14;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t17 != ((uint8_t)15ULL)) goto mn16;
        t14 = ((uint8_t)0);
        goto me15;
mn16:;
        t14 = ((uint8_t)1);
        goto me15;
mn18:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 210, 8);
me15:;
        if (!t14) goto ie2;
        {
            gm_b0_1 t20;
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l1;
        gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t22) + 0) + 56));
        gm_l3 = t23;
    }
    {
        void * t25 = gm_l1;
        void * t26 = GM_LD(void *, ((((uint8_t *)t25) + 0) + 24));
        void * t27 = t26;
        if (!(GM_LD(void *, ((uint8_t *)&t27)) != 0)) goto ie24;
        void * t28 = GM_LD(void *, ((uint8_t *)&t27));
        gm_l4 = t28;
        {
            void * t30 = gm_l4;
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
            gm_l3 = t31;
        }
        goto ix24;
ie24:;
        void * t33 = gm_l1;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t33) + 88) + 0));
        if (t34 != ((uint8_t)1ULL)) goto ie32;
        void * t35 = GM_LD(void *, ((((uint8_t *)t33) + 88) + 8));
        gm_l5 = t35;
        {
            void * t37 = gm_l5;
            gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
            gm_l3 = t38;
        }
        goto ix32;
ie32:;
ix32:;
ix24:;
    }
    {
        void * t39 = gm_l0;
        gm_tl874t2atnlh3 t40 = gm_l3;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sdsd71bjtv81c));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(97));
        gm_b0_1 t42 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t39, t40, t41);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_route_performed_pruu3np(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3, uint8_t a4) {
    struct gm_fr_route_performed_pruu3np F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* performed */
    void * gm_l2; /* effects */
    uint8_t gm_l3; /* pointer_literal */
    uint8_t gm_l4; /* undeclared_is_error */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 571u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_f_list_084fcd6((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l6 = t3;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tj44oivvogvtl t15 = GM_LD(gm_tj44oivvogvtl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t11 * 40));
                    F.l8 = t15;
                    {
                        void * t18 = gm_l2;
                        void * t19 = (void *)(((uint8_t *)&F.l8) + 0);
                        uint8_t t20 = gm_f_set_contains_8v9ep9m(t18, t19);
                        if (!t20) goto ie17;
                        {
                            goto lc4;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        uint8_t t23 = gm_l3;
                        if (!t23) goto ie22;
                        {
                            void * t25 = gm_l0;
                            gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 24));
                            void * t27 = gm_l0;
                            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                            void * t29 = (void *)(((uint8_t *)t28) + 8);
                            void * t30 = (void *)(((uint8_t *)&F.l8) + 0);
                            gm_b16_8 t31 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t29, t30);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tnpifv20irnph t32;
                            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
                            gm_tabb710d9mq2e t33;
                            gm_f_format_26002qo((void *)&t33, (void *)NULL, cx, NULL, t32);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tabb710d9mq2e t34 = t33;
                            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcl43af23eher3, 228, 30);
                            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                            gm_b16_8 t36 = t35;
                            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                            gm_b0_1 t38 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t25, t26, t37);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            goto lc4;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    {
                        void * t39 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t40 = gm_f_push_d1r4h19(t39, &(*(gm_tiilmfd16v3fc *)(((uint8_t *)&F.l8) + 0)));
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcl43af23eher3, 231, 9);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                    }
                    {
                        uint8_t t44 = gm_l4;
                        if (!t44) goto ie43;
                        {
                            void * t46 = gm_l0;
                            gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 24));
                            void * t48 = gm_l0;
                            void * t49 = (void *)(((uint8_t *)&F.l8) + 0);
                            gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 24));
                            void * t51 = gm_l2;
                            gm_th2lqbo77pvsc t52;
                            gm_f_undeclared_message_b14iouu((void *)&t52, (void *)NULL, cx, NULL, t48, t49, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_th2lqbo77pvsc t53 = t52;
                            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 0));
                            gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t46, t47, t54);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix43;
ie43:;
                        {
                            void * t57 = gm_l0;
                            void * t58 = (void *)(((uint8_t *)t57) + 192);
                            gm_tftb1odc739t5 t59 = gm_f_push_3onuaru(t58, &(*(gm_tj44oivvogvtl *)((uint8_t *)&F.l8)));
                            gm_tftb1odc739t5 t60 = t59;
                            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcl43af23eher3, 235, 13);
                            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
                        }
ix43:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 225, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t63 = gm_l7;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                    uint64_t t66 = r64;
                    gm_l7 = t66;
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
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
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

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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
struct gm_fr_for_body_hb9k0f9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_jvjn1fs {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
gm_t62ebbi2i5rkd gm_dtu8v6n5i2qd_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_t4q2b5ibsrdik a2);
gm_b0_1 gm_dtu8v6n5i2qd_1(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_t62ebbi2i5rkd gm_dtu8v6n5i2qd_10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
void gm_dtu8v6n5i2qd_11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_b0_1 gm_dtu8v6n5i2qd_12(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
void gm_dtu8v6n5i2qd_13(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_14(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, uint64_t a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_15(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_16(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_17(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_18(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_19(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_20(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint32_t a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_21(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, int64_t a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_3(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtu8v6n5i2qd_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, int64_t a2, gm_t9qkh0rsooc8d a3);
void gm_dtu8v6n5i2qd_5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, gm_b16_8 a2);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_ttln0d0hd70en gm_dtu8v6n5i2qd_9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1b63vnj(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hb9k0f9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_t3rkkf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
extern uint8_t gm_c08htgempbt[7] __attribute__((aligned(1)));
extern uint8_t gm_c0qkheasoov[17] __attribute__((aligned(1)));
extern uint8_t gm_c137q3t6vnc[9] __attribute__((aligned(1)));
extern uint8_t gm_c19g30clc28[5] __attribute__((aligned(1)));
extern uint8_t gm_c1cb3lujqna[801] __attribute__((aligned(1)));
extern uint8_t gm_c1se330bih6[13] __attribute__((aligned(1)));
extern uint8_t gm_c2mocn1cpdr[4] __attribute__((aligned(1)));
extern uint8_t gm_c34nm3e2f7b[3] __attribute__((aligned(1)));
extern uint8_t gm_c387ahiebcp[7] __attribute__((aligned(1)));
extern uint8_t gm_c4lell1pi73[17] __attribute__((aligned(1)));
extern uint8_t gm_c5655r183jp[24] __attribute__((aligned(1)));
extern uint8_t gm_c8a5fuunssu[14] __attribute__((aligned(1)));
extern uint8_t gm_c8fctfks9t5[15] __attribute__((aligned(1)));
extern uint8_t gm_c935g5d7vau[4] __attribute__((aligned(1)));
extern uint8_t gm_c94f17fblj5[1] __attribute__((aligned(1)));
extern uint8_t gm_c956v7aird0[8] __attribute__((aligned(8)));
extern uint8_t gm_c97f5362n6p[8] __attribute__((aligned(1)));
extern uint8_t gm_c9n447mn3q9[5] __attribute__((aligned(1)));
extern uint8_t gm_c9rkig62cpc[30] __attribute__((aligned(1)));
extern uint8_t gm_ca95asi83lb[10] __attribute__((aligned(1)));
extern uint8_t gm_ca9ikdfm7p4[4] __attribute__((aligned(4)));
extern uint8_t gm_cab7j216uqg[12] __attribute__((aligned(1)));
extern uint8_t gm_cagddvvpjek[10] __attribute__((aligned(1)));
extern uint8_t gm_cak4bln8pr2[10] __attribute__((aligned(1)));
extern uint8_t gm_caqt8iiohcr[12] __attribute__((aligned(1)));
extern uint8_t gm_cb1h8mol9po[16] __attribute__((aligned(8)));
extern uint8_t gm_cb4pf8mh5iq[1] __attribute__((aligned(8)));
extern uint8_t gm_cbqhferh238[12] __attribute__((aligned(1)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cdhp3orov5g[5] __attribute__((aligned(1)));
extern uint8_t gm_cdu58t719v0[6] __attribute__((aligned(1)));
extern uint8_t gm_cepf08kh92d[97] __attribute__((aligned(1)));
extern uint8_t gm_cfea4sug7gd[8] __attribute__((aligned(8)));
extern uint8_t gm_cfeq13tqi9u[10] __attribute__((aligned(1)));
extern uint8_t gm_cfhpe9hto1b[14] __attribute__((aligned(1)));
extern uint8_t gm_cfiqcgtjhen[416] __attribute__((aligned(8)));
extern uint8_t gm_cggqgl3b8fs[3] __attribute__((aligned(1)));
extern uint8_t gm_cgnn9n3q0cc[4] __attribute__((aligned(1)));
extern uint8_t gm_ch37hhcaakv[12] __attribute__((aligned(1)));
extern uint8_t gm_cim1n3o1mct[8] __attribute__((aligned(1)));
extern uint8_t gm_cjdu4hivd9h[16] __attribute__((aligned(8)));
extern uint8_t gm_ckh5e429gka[8] __attribute__((aligned(8)));
extern uint8_t gm_cl4aag70hg2[15] __attribute__((aligned(1)));
extern uint8_t gm_cmd4bh5vti6[11] __attribute__((aligned(1)));
extern uint8_t gm_cmdcrpgn3bo[6] __attribute__((aligned(1)));
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_cml190pi1go[15] __attribute__((aligned(1)));
extern uint8_t gm_cmo2h50nb4b[12] __attribute__((aligned(1)));
extern uint8_t gm_co1jgvp4ags[39] __attribute__((aligned(1)));
extern uint8_t gm_cpb6bb227bn[11] __attribute__((aligned(1)));
extern uint8_t gm_cqb1jv05g6h[6] __attribute__((aligned(1)));
extern uint8_t gm_cqppk8qnrpu[17] __attribute__((aligned(1)));
extern uint8_t gm_cr046dlv3ko[4] __attribute__((aligned(1)));
extern uint8_t gm_ct2qmh8qddo[7] __attribute__((aligned(1)));
extern uint8_t gm_ctfv30s4j48[11] __attribute__((aligned(1)));
extern uint8_t gm_ctnl7besb79[13] __attribute__((aligned(1)));
extern uint8_t gm_ctru94s97vo[4] __attribute__((aligned(1)));
extern uint8_t gm_cuncmusjbv3[10] __attribute__((aligned(1)));
extern void *const gm_hdu8v6n5i2qd[];
extern void *const gm_hl8alodt65ii[];
extern void *const gm_hlj1in77f861[];
extern void *const gm_hlk5mm1461td[];
extern uint8_t gm_k0kae0be6jj[40] __attribute__((aligned(8)));
extern uint8_t gm_k26b4lfjb04[40] __attribute__((aligned(8)));
extern uint8_t gm_k6tu981lmau[40] __attribute__((aligned(8)));
extern uint8_t gm_k9nqfo1idrl[40] __attribute__((aligned(8)));
extern uint8_t gm_kecscp3is96[40] __attribute__((aligned(8)));
extern uint8_t gm_kefd26qglb8[40] __attribute__((aligned(8)));
extern uint8_t gm_kequv58cbtg[40] __attribute__((aligned(8)));
extern uint8_t gm_khouror17c2[40] __attribute__((aligned(8)));
extern uint8_t gm_ki0a6rpk4hh[40] __attribute__((aligned(8)));
extern uint8_t gm_kif5rfegp72[40] __attribute__((aligned(8)));
extern uint8_t gm_kjn96jdce98[40] __attribute__((aligned(8)));
extern uint8_t gm_kmgr5pk3qpo[40] __attribute__((aligned(8)));
extern uint8_t gm_kp1liaipvo1[40] __attribute__((aligned(8)));
extern uint8_t gm_kp3nl0er6il[40] __attribute__((aligned(8)));
extern uint8_t gm_ks1bcpoe2h2[40] __attribute__((aligned(8)));
extern uint8_t gm_ktv7kfnnmgp[40] __attribute__((aligned(8)));
extern uint8_t gm_kvk9mrlml9b[40] __attribute__((aligned(8)));
extern const uint8_t gm_s03itq0fotubb[14];
extern const uint8_t gm_s05fn63o3te82[13];
extern const uint8_t gm_s064htvgs8b59[69];
extern const uint8_t gm_s069kf53uhd4o[45];
extern const uint8_t gm_s0flic1r29j4i[14];
extern const uint8_t gm_s0iioal6odpu7[11];
extern const uint8_t gm_s0iql3kt61auq[41];
extern const uint8_t gm_s0k8sea1jhldg[21];
extern const uint8_t gm_s0l81imssm78p[7];
extern const uint8_t gm_s0r850bpi2rnm[14];
extern const uint8_t gm_s0rcpa4ucqf07[103];
extern const uint8_t gm_s0s4ef50964r9[3];
extern const uint8_t gm_s0uum3co6vapt[23];
extern const uint8_t gm_s107olobe2ou0[28];
extern const uint8_t gm_s129bqtq598qj[54];
extern const uint8_t gm_s15g40fmv51gv[59];
extern const uint8_t gm_s17uhrtdo7u30[2];
extern const uint8_t gm_s1a1820qk5dro[23];
extern const uint8_t gm_s1amcoqteaeio[38];
extern const uint8_t gm_s1djo7fpeeqnt[11];
extern const uint8_t gm_s1e7k26kdaoj3[31];
extern const uint8_t gm_s1h9rpfuhvh29[60];
extern const uint8_t gm_s1mud7cvvuuon[4];
extern const uint8_t gm_s1n20vngt0mkv[3];
extern const uint8_t gm_s1rpc945q7l9o[15];
extern const uint8_t gm_s1utcdes4r8ku[7];
extern const uint8_t gm_s205esok2u8pk[4];
extern const uint8_t gm_s21284p79u3jq[14];
extern const uint8_t gm_s22mk3qhbqci2[11];
extern const uint8_t gm_s23siujajsn8e[111];
extern const uint8_t gm_s26di78e9g1gn[41];
extern const uint8_t gm_s2791bst0j7pi[41];
extern const uint8_t gm_s28ijm46j20ds[8];
extern const uint8_t gm_s29cvp3di8ou1[4];
extern const uint8_t gm_s29t9tfffm60i[41];
extern const uint8_t gm_s2cqd5egpbgbs[41];
extern const uint8_t gm_s2d9sjnr88ud8[16];
extern const uint8_t gm_s2e48s7fhhseb[17];
extern const uint8_t gm_s2fp7iapio2lo[11];
extern const uint8_t gm_s2k1l0sh4mlmv[10];
extern const uint8_t gm_s2mb2caup8rnh[49];
extern const uint8_t gm_s2ot4uojp2s87[7];
extern const uint8_t gm_s2ph2ad6lffbq[17];
extern const uint8_t gm_s2rbapbbig1pe[19];
extern const uint8_t gm_s2uimijqjekbd[67];
extern const uint8_t gm_s2uqljf8sumcn[11];
extern const uint8_t gm_s34o1igjpenij[26];
extern const uint8_t gm_s3ap820v1eq4f[9];
extern const uint8_t gm_s3b32la11650d[51];
extern const uint8_t gm_s3bddqij01brp[83];
extern const uint8_t gm_s3bli9pbcj81a[12];
extern const uint8_t gm_s3brf0f9sa1j7[26];
extern const uint8_t gm_s3ceboq671qr5[39];
extern const uint8_t gm_s3hqs40g2kokp[9];
extern const uint8_t gm_s3inajboct11s[8];
extern const uint8_t gm_s3ks2qlmjeh74[11];
extern const uint8_t gm_s3mvfcnrk9f21[48];
extern const uint8_t gm_s3qd7ldqb9526[29];
extern const uint8_t gm_s3qnfqte7c8r2[35];
extern const uint8_t gm_s3t3r41giacns[14];
extern const uint8_t gm_s3udkpu8iv0h2[75];
extern const uint8_t gm_s41sdeqq056ae[9];
extern const uint8_t gm_s42bvs5l5hcla[4];
extern const uint8_t gm_s43jqu37ghtoq[19];
extern const uint8_t gm_s43sk7g92lovh[13];
extern const uint8_t gm_s4873mb1qbjh2[5];
extern const uint8_t gm_s489g6hodd4uq[44];
extern const uint8_t gm_s48iukd74s6c6[22];
extern const uint8_t gm_s4bdmssec9aiu[4];
extern const uint8_t gm_s4d81vdcmt94n[14];
extern const uint8_t gm_s4gh6uvc8i8um[24];
extern const uint8_t gm_s4lt93qhapike[4];
extern const uint8_t gm_s4m36jv5rcfat[13];
extern const uint8_t gm_s4niue72anpss[37];
extern const uint8_t gm_s4otfbu8ml1qq[7];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4rr6a2j9pjhb[35];
extern const uint8_t gm_s4uditudns916[3];
extern const uint8_t gm_s520nu4qcaerc[6];
extern const uint8_t gm_s565fgm0tsngr[15];
extern const uint8_t gm_s58rgrgmho9sa[68];
extern const uint8_t gm_s5k56vqvt8t7d[12];
extern const uint8_t gm_s5lk0utb9h01t[3];
extern const uint8_t gm_s5mu2shl2bmft[14];
extern const uint8_t gm_s5om28rj8ddf9[28];
extern const uint8_t gm_s5oredgotak9m[58];
extern const uint8_t gm_s5pq4t98troao[34];
extern const uint8_t gm_s5t5jm6ask6ip[15];
extern const uint8_t gm_s5tee1epg2e5a[24];
extern const uint8_t gm_s601j8au4fcin[6];
extern const uint8_t gm_s6033vr1c63cl[17];
extern const uint8_t gm_s615gqen9ca7q[16];
extern const uint8_t gm_s62maodcvu607[20];
extern const uint8_t gm_s658vrmfgnecg[28];
extern const uint8_t gm_s69ahc8sth76o[41];
extern const uint8_t gm_s6cqii80as1c2[41];
extern const uint8_t gm_s6f0l2e1iha9i[8];
extern const uint8_t gm_s6gohqbjroq8g[13];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6i2cgc2057sp[5];
extern const uint8_t gm_s6ibuqseh9tt7[152];
extern const uint8_t gm_s6ipt60dh9o1j[11];
extern const uint8_t gm_s6lcmor84nt1l[4];
extern const uint8_t gm_s6lddausbiitu[9];
extern const uint8_t gm_s6nt01jkbgi6h[52];
extern const uint8_t gm_s6ocl6n3lno8m[8];
extern const uint8_t gm_s6tp3e68vgt6g[13];
extern const uint8_t gm_s7278rr8ss873[19];
extern const uint8_t gm_s7cn098jq5gvg[9];
extern const uint8_t gm_s7dobq9tho3k8[96];
extern const uint8_t gm_s7gn66u5sfpv0[41];
extern const uint8_t gm_s7kc4hmkruq4r[11];
extern const uint8_t gm_s7spn02nsqs4u[18];
extern const uint8_t gm_s7ta8sbflqphb[28];
extern const uint8_t gm_s7vj61tk4g1rp[14];
extern const uint8_t gm_s7vt7hgga3uq7[7];
extern const uint8_t gm_s805anh9k3rr5[7];
extern const uint8_t gm_s80ddhqa1o9i9[7];
extern const uint8_t gm_s80tqs0e3u7de[11];
extern const uint8_t gm_s81f5igr577he[14];
extern const uint8_t gm_s84lh6to0er2m[19];
extern const uint8_t gm_s85kbq4rs5fa7[83];
extern const uint8_t gm_s86fb5obo4kb8[154];
extern const uint8_t gm_s8afn1h3asffl[32];
extern const uint8_t gm_s8ea9885mu6jv[28];
extern const uint8_t gm_s8ei36h3rj566[14];
extern const uint8_t gm_s8j9ber7fuaks[20];
extern const uint8_t gm_s8o1jnchmsg7i[7];
extern const uint8_t gm_s8oq9c4aac3sc[52];
extern const uint8_t gm_s8us8sihs6eh7[74];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9abd2e9it4et[67];
extern const uint8_t gm_s9caepulv0516[9];
extern const uint8_t gm_s9i4dp3hfg864[18];
extern const uint8_t gm_sa23gppmgki7j[11];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_sa2tquttrsgo4[17];
extern const uint8_t gm_sa5v7oq7un6c6[84];
extern const uint8_t gm_sa7tr9aaspmun[5];
extern const uint8_t gm_sa8p4rum5g6ig[116];
extern const uint8_t gm_sa9dtqeprt7kf[75];
extern const uint8_t gm_sabc7m3rug04j[3];
extern const uint8_t gm_sac01n562d8ve[59];
extern const uint8_t gm_sadscuj628cl7[8];
extern const uint8_t gm_saimr6ebj0d4t[12];
extern const uint8_t gm_sakmuvnjjp217[10];
extern const uint8_t gm_salbpgh5m2h5m[15];
extern const uint8_t gm_samq5nkk38rku[17];
extern const uint8_t gm_sap9oatgj29c2[13];
extern const uint8_t gm_sas87vff32eaf[43];
extern const uint8_t gm_satkhe9qra7p5[13];
extern const uint8_t gm_sb0fu128u868p[27];
extern const uint8_t gm_sb36bkunl16pp[22];
extern const uint8_t gm_sb371d9627jdq[5];
extern const uint8_t gm_sb6c4qgs6rpfi[8];
extern const uint8_t gm_sb8us6li0fvm5[42];
extern const uint8_t gm_sb9fu6ufbt9g1[17];
extern const uint8_t gm_sbapuchngtvak[28];
extern const uint8_t gm_sbd6ket0ql5ab[9];
extern const uint8_t gm_sbgl5dgur8udg[7];
extern const uint8_t gm_sbgqae2tm3q08[39];
extern const uint8_t gm_sbim95dtkmqmv[49];
extern const uint8_t gm_sbk48vc8h32c1[9];
extern const uint8_t gm_sbn0999b7p1ph[83];
extern const uint8_t gm_sc06resjcdhg4[19];
extern const uint8_t gm_sc12loraoaq3m[7];
extern const uint8_t gm_sc1vrnob3chsu[41];
extern const uint8_t gm_sc3p6dp1skc87[8];
extern const uint8_t gm_sc5aaou69jljp[10];
extern const uint8_t gm_sc71n5mgucblh[50];
extern const uint8_t gm_sc9goq69s6n9d[37];
extern const uint8_t gm_scb853lmgr58k[6];
extern const uint8_t gm_sce79e7st00ad[4];
extern const uint8_t gm_scfppf8naer38[21];
extern const uint8_t gm_sch7ep6ht4utr[58];
extern const uint8_t gm_schn9bp2k5kqo[47];
extern const uint8_t gm_scj8qp34gs3m8[58];
extern const uint8_t gm_scogrgnqo3tpu[22];
extern const uint8_t gm_scsqaooh7eo7l[153];
extern const uint8_t gm_sct0vr0h329vl[43];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd323o3qr3mfd[64];
extern const uint8_t gm_sd5g4r2t5gcbk[25];
extern const uint8_t gm_sd6g049s1ldbu[2];
extern const uint8_t gm_sd97d6kmlmb3h[30];
extern const uint8_t gm_sda1bf43v6985[10];
extern const uint8_t gm_sdad15q200tmk[4];
extern const uint8_t gm_sdbocdqbukr6k[77];
extern const uint8_t gm_sddnsndqgnosl[31];
extern const uint8_t gm_sdjk9c005ec9t[13];
extern const uint8_t gm_sdle7podd8151[45];
extern const uint8_t gm_sdq6l7oddjqfe[12];
extern const uint8_t gm_sdsrtcf8ng48d[6];
extern const uint8_t gm_sdt9q53qgnu87[7];
extern const uint8_t gm_sdtdaesvk7dfl[8];
extern const uint8_t gm_sdtthp48kia81[5];
extern const uint8_t gm_se0kk0u6b53ko[6];
extern const uint8_t gm_se1apliihhr7c[9];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sech7am46drki[16];
extern const uint8_t gm_sed64s2n0ri16[52];
extern const uint8_t gm_sehvo63gl3eu0[26];
extern const uint8_t gm_seidpmiq51gtp[60];
extern const uint8_t gm_sekmbu83qkg9c[44];
extern const uint8_t gm_selnb2co583fc[6];
extern const uint8_t gm_senb8fal0j05l[15];
extern const uint8_t gm_serd0h8povggs[12];
extern const uint8_t gm_sf1hl3n20st3g[37];
extern const uint8_t gm_sf1ljria5bdei[8];
extern const uint8_t gm_sf1qp7r4ktsd1[11];
extern const uint8_t gm_sf3irv2ans6de[57];
extern const uint8_t gm_sf42bd5sk6816[26];
extern const uint8_t gm_sf9ha6blerd0b[88];
extern const uint8_t gm_sfde9lsgge1ld[26];
extern const uint8_t gm_sffbik0ast0lg[53];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfkmkcagsnqnb[7];
extern const uint8_t gm_sfn9k9vgl9ged[28];
extern const uint8_t gm_sfrdbcc53tuv9[18];
extern const uint8_t gm_sftkp1r7uetfv[10];
extern const uint8_t gm_sfuh3dki1la7r[32];
extern const uint8_t gm_sg344a2p7u6i6[6];
extern const uint8_t gm_sg4kirhst3a6p[15];
extern const uint8_t gm_sg7i6m1frjk5n[63];
extern const uint8_t gm_sgcuckp03jlhp[23];
extern const uint8_t gm_sghautilepqio[13];
extern const uint8_t gm_sgkfsokc46mg5[34];
extern const uint8_t gm_sgks95jf6fs73[21];
extern const uint8_t gm_sgme8q1t414rd[19];
extern const uint8_t gm_sgmhc3klfmn46[5];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sgpn27p7jhj9p[12];
extern const uint8_t gm_sgpp0p44ddknc[76];
extern const uint8_t gm_sgtriqnjufjbf[10];
extern const uint8_t gm_sguqrpr7akp41[28];
extern const uint8_t gm_sgvhjfacilshn[9];
extern const uint8_t gm_sh01rb1u2jpfs[19];
extern const uint8_t gm_sh09ul4fvjk00[36];
extern const uint8_t gm_sh0fsro8kchj4[52];
extern const uint8_t gm_shb5dhf7pftn9[17];
extern const uint8_t gm_shfae2hkjlb47[15];
extern const uint8_t gm_shnvun4juqpma[9];
extern const uint8_t gm_shqseub127akh[47];
extern const uint8_t gm_shr2dul5unlgn[16];
extern const uint8_t gm_shsv7bl83q04d[9];
extern const uint8_t gm_shug2jbeta627[41];
extern const uint8_t gm_si3qm04ntebij[6];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_si6c601mt9p4v[220];
extern const uint8_t gm_sie9phnhh4ub6[11];
extern const uint8_t gm_siefj6nst22k5[41];
extern const uint8_t gm_sigi809bnb15c[41];
extern const uint8_t gm_sihr7qmh227og[26];
extern const uint8_t gm_siic7p394dov6[13];
extern const uint8_t gm_sij6bc8bh95o1[59];
extern const uint8_t gm_sit8u7uppaik9[7];
extern const uint8_t gm_sitcmrrnvoedc[55];
extern const uint8_t gm_sive7ce5krqer[18];
extern const uint8_t gm_sj19m7hg2ca80[11];
extern const uint8_t gm_sj3m78jf2dftr[24];
extern const uint8_t gm_sj4n6nvsgq2jn[43];
extern const uint8_t gm_sj4smhrnkurr1[24];
extern const uint8_t gm_sj98l5dehulso[8];
extern const uint8_t gm_sjaeqf6f3s51a[45];
extern const uint8_t gm_sjc9ao9066c7n[33];
extern const uint8_t gm_sjcg5brfqm3a2[8];
extern const uint8_t gm_sjef1iqti8f5h[5];
extern const uint8_t gm_sjfcbh429vmot[9];
extern const uint8_t gm_sjfpr08jc3ep1[23];
extern const uint8_t gm_sji60fgbl9iul[41];
extern const uint8_t gm_sjjig60ja37ka[3];
extern const uint8_t gm_sjl4446pn0dup[3];
extern const uint8_t gm_sjm2j40p6489d[9];
extern const uint8_t gm_sjss9mvc5fcod[9];
extern const uint8_t gm_sjtiiac2ha9i1[8];
extern const uint8_t gm_sk1d0v1onojc1[8];
extern const uint8_t gm_sk4cv563hhed7[2];
extern const uint8_t gm_sk5htm2k25gsj[16];
extern const uint8_t gm_sk8csn7us23m3[9];
extern const uint8_t gm_sk8it0s509dbd[38];
extern const uint8_t gm_sk9dt92p27k0o[53];
extern const uint8_t gm_skbsro97j503c[7];
extern const uint8_t gm_skdhvc36gfkb0[47];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_ske39klqfe71k[82];
extern const uint8_t gm_ske6qr3raj7r9[24];
extern const uint8_t gm_skf0mm42l0tsk[17];
extern const uint8_t gm_skhu4ngu6nt5n[15];
extern const uint8_t gm_skitebsmtce1i[34];
extern const uint8_t gm_skki6c17jgbtj[30];
extern const uint8_t gm_skou19srual26[10];
extern const uint8_t gm_skrn366c6rv49[41];
extern const uint8_t gm_sksgudmhhp60q[14];
extern const uint8_t gm_sl0kpeuro9al4[23];
extern const uint8_t gm_sl1tolq1dm391[10];
extern const uint8_t gm_sl325guoc37g3[23];
extern const uint8_t gm_sl6fvbluu7spr[26];
extern const uint8_t gm_sla6dgu7p35mv[31];
extern const uint8_t gm_slb3ondne09t1[28];
extern const uint8_t gm_sldehiepn7rta[6];
extern const uint8_t gm_slfk6iuc3d2pa[14];
extern const uint8_t gm_slfnv05vuf0b8[22];
extern const uint8_t gm_slk5snj328gko[45];
extern const uint8_t gm_sloe55935mobb[8];
extern const uint8_t gm_slqq08j0fro21[31];
extern const uint8_t gm_slrfv83pestqp[31];
extern const uint8_t gm_sluddd1oa2j8v[54];
extern const uint8_t gm_sm3dios51ece1[10];
extern const uint8_t gm_sm4s7blimj76n[41];
extern const uint8_t gm_sm5s4k1c62cb1[18];
extern const uint8_t gm_sm7oa350rtqnm[12];
extern const uint8_t gm_sm7qvedi3emcj[41];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smap40unho18s[19];
extern const uint8_t gm_smbkn5defd9ht[48];
extern const uint8_t gm_smdvilsvvv2u5[33];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_smkijcjdhajsg[5];
extern const uint8_t gm_smo8ffg4jq2d6[45];
extern const uint8_t gm_smorejbk97kqm[65];
extern const uint8_t gm_smrmke0a8tkf0[120];
extern const uint8_t gm_smuec3qfsdh82[10];
extern const uint8_t gm_sn1seggk13bop[4];
extern const uint8_t gm_snbque26o7lbi[7];
extern const uint8_t gm_snd30h4tliigb[12];
extern const uint8_t gm_snduiu5un9hfb[41];
extern const uint8_t gm_snh06kr7sd4ji[41];
extern const uint8_t gm_sni6hcs91b3qh[13];
extern const uint8_t gm_snikqaq1mfe0u[29];
extern const uint8_t gm_snil57vrv77lr[12];
extern const uint8_t gm_sniofsn8cqk74[10];
extern const uint8_t gm_snkdp6opmsb8i[42];
extern const uint8_t gm_snnagva49avpd[8];
extern const uint8_t gm_snqe7tsf40ume[138];
extern const uint8_t gm_snupav63sairp[12];
extern const uint8_t gm_snvisme5hp379[7];
extern const uint8_t gm_so0m8vf92uemo[66];
extern const uint8_t gm_so5q4maooc958[35];
extern const uint8_t gm_so7oqi7vob9ku[17];
extern const uint8_t gm_so97uarpe8rtg[9];
extern const uint8_t gm_so9b3j0thioda[78];
extern const uint8_t gm_sobcm12sogdng[5];
extern const uint8_t gm_sockrjemj2if8[61];
extern const uint8_t gm_soduqpin16t7d[6];
extern const uint8_t gm_soe998lc1ukpn[11];
extern const uint8_t gm_sogkbe8nre26a[17];
extern const uint8_t gm_sohcj0rggg07e[51];
extern const uint8_t gm_sonht3t87vai7[9];
extern const uint8_t gm_soofuqhnovv5o[5];
extern const uint8_t gm_sopjc7gjg0pgu[32];
extern const uint8_t gm_soqr0md31pcm7[38];
extern const uint8_t gm_souosds7adra9[36];
extern const uint8_t gm_sp0l171g2m7ho[25];
extern const uint8_t gm_sp5mk8jqu8dn0[70];
extern const uint8_t gm_sp6s13hdc6c0b[4];
extern const uint8_t gm_sp7351fukevmv[29];
extern const uint8_t gm_sp7me12frc01v[22];
extern const uint8_t gm_spbhhmdbp335u[4];
extern const uint8_t gm_spckols8oluq7[15];
extern const uint8_t gm_spcl9lgniu1he[18];
extern const uint8_t gm_spgsforhruitc[30];
extern const uint8_t gm_spi5fq66572ic[20];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_spica4gaklfma[27];
extern const uint8_t gm_spit8mdn0781q[26];
extern const uint8_t gm_splq0kkmd3hj2[31];
extern const uint8_t gm_spm7tm8a050b3[7];
extern const uint8_t gm_spnad0ot4gagr[10];
extern const uint8_t gm_spr274eleu2lj[4];
extern const uint8_t gm_spskhsf5tnedj[10];
extern const uint8_t gm_spspe7tb8bsr3[8];
extern const uint8_t gm_sq800qhfmpfk7[39];
extern const uint8_t gm_sq9i3vfk7nqs8[10];
extern const uint8_t gm_sqa0b48i8mvk7[6];
extern const uint8_t gm_sqf5m57l1ua5d[27];
extern const uint8_t gm_sqfsba89a30n8[26];
extern const uint8_t gm_sqjomq37io03j[151];
extern const uint8_t gm_sql1qe620spbq[23];
extern const uint8_t gm_sqlpqagi4dpk1[38];
extern const uint8_t gm_sqmifqg6egoj0[41];
extern const uint8_t gm_sqp9p8tc2dil3[8];
extern const uint8_t gm_sqpev88g6capg[9];
extern const uint8_t gm_squ601q9vv2so[33];
extern const uint8_t gm_sr0sh5asl4po7[22];
extern const uint8_t gm_sr244s00qj01d[3];
extern const uint8_t gm_sr3lr7c9psgf2[12];
extern const uint8_t gm_sr5mr0r3jci3n[12];
extern const uint8_t gm_sr99v3u3nhpad[81];
extern const uint8_t gm_srajaqomjf1d3[19];
extern const uint8_t gm_src5778gpfoh9[9];
extern const char gm_src7afujsggtlje[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcejinh96g1530[];
extern const char gm_srcg82mujgottmf[];
extern const char gm_srcgr7vbgb5cqmg[];
extern const char gm_srclfnv05vuf0b8[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcm0juv8ecdls4[];
extern const char gm_srcn3jjc71a2bv3[];
extern const char gm_srcoimgd0jo62fr[];
extern const char gm_srcqr4ch56kv6bv[];
extern const uint8_t gm_srcs6slmq8ak7[15];
extern const char gm_srcsloagb2nrd8g[];
extern const uint8_t gm_sre31tu0qddge[24];
extern const uint8_t gm_srgr2i687arao[75];
extern const uint8_t gm_srh29phg8b30r[4];
extern const uint8_t gm_srjh1vh9n4las[87];
extern const uint8_t gm_srkqhsgbb6hhs[41];
extern const uint8_t gm_srn9e8kdj18it[33];
extern const uint8_t gm_srne5d503fcjg[5];
extern const uint8_t gm_srof610t1er1p[26];
extern const uint8_t gm_sropo1941pvlt[16];
extern const uint8_t gm_srs84t3n56p1c[8];
extern const uint8_t gm_sru5s20jprgc9[12];
extern const uint8_t gm_srundlmqvm06p[60];
extern const uint8_t gm_ss0g40l133d39[13];
extern const uint8_t gm_ss3tu1f38tkcj[6];
extern const uint8_t gm_ss8448efddk3m[32];
extern const uint8_t gm_ssbg0bg772d67[5];
extern const uint8_t gm_ssg5cub3gqpc7[13];
extern const uint8_t gm_ssi2113jtjul7[41];
extern const uint8_t gm_ssnsddtjt3gpa[35];
extern const uint8_t gm_sso3tuh819o23[9];
extern const uint8_t gm_ssop4efhh9upu[41];
extern const uint8_t gm_ssq1no648r8mj[63];
extern const uint8_t gm_sss3cg4gsncei[12];
extern const uint8_t gm_ssuuud2f0uns5[9];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_st2tqp657i86j[8];
extern const uint8_t gm_st88sqv2u9dqh[8];
extern const uint8_t gm_st984in2akmmc[43];
extern const uint8_t gm_st9dp4tkd7f7l[13];
extern const uint8_t gm_st9g0aublffth[18];
extern const uint8_t gm_sta9bvnin7dlr[36];
extern const uint8_t gm_stbuagagnsakl[37];
extern const uint8_t gm_stdehenbgfc4h[13];
extern const uint8_t gm_ste5bc2m7dvjm[12];
extern const uint8_t gm_stivl0smu8eq0[9];
extern const uint8_t gm_stl47boisb31o[11];
extern const uint8_t gm_stm8v8ngvll3q[16];
extern const uint8_t gm_stnlf4pvbut1c[3];
extern const uint8_t gm_stntm4cqqrhf7[7];
extern const uint8_t gm_stpkldc5dmf71[33];
extern const uint8_t gm_su18s5052fc6v[15];
extern const uint8_t gm_su4aje74nkg3o[51];
extern const uint8_t gm_su6il7ugmnq2g[48];
extern const uint8_t gm_su6oav9vpqcnu[17];
extern const uint8_t gm_sub8b9nf0ms2i[104];
extern const uint8_t gm_sub9rknupld7d[52];
extern const uint8_t gm_sucsarpa3psmp[55];
extern const uint8_t gm_sud6d85d454o1[42];
extern const uint8_t gm_sue9kqmk98bh9[34];
extern const uint8_t gm_sui7l1ojsejig[13];
extern const uint8_t gm_sukutl224cgkm[8];
extern const uint8_t gm_sum9vj65rg4jb[4];
extern const uint8_t gm_sup9af88eqfm1[25];
extern const uint8_t gm_suv2tallru6a1[2];
extern const uint8_t gm_sv19dj08f7d6b[45];
extern const uint8_t gm_sv4cnc01vnmra[11];
extern const uint8_t gm_sv9jtudu9cu4k[41];
extern const uint8_t gm_svbe9btc37i4e[13];
extern const uint8_t gm_svgqk0opgf2oj[60];
extern const uint8_t gm_svgsom6hbqtcn[40];
extern const uint8_t gm_svlu840bnmh08[10];
extern const uint8_t gm_svm0rfaqk3b2q[4];
extern const uint8_t gm_svn0tfk5ehe5n[48];
extern const uint8_t gm_svo0v47tsnb8b[14];
extern const uint8_t gm_svo7nu0hjqbeg[11];
extern const uint8_t gm_svqbt6tnehohi[14];
extern const uint8_t gm_svqdrhms9i231[7];
extern const gm_variant gm_tyehhdtpeti1_v[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hb9k0f9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_hb9k0f9 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_for_body_jvjn1fs *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcbq9k9kvdokbf, 139, 42);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 139, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
const char gm_src7afujsggtlje[] = "compiler/interp/code.gm";
const char gm_srcd5arorrf8jkg[] = "compiler/cgen/gen.gm";
const char gm_srcejinh96g1530[] = "compiler/lsp/transport.gm";
const char gm_srcg82mujgottmf[] = "compiler/check/assign.gm";
const char gm_srcgr7vbgb5cqmg[] = "compiler/testing/coverage.gm";
const char gm_srclfnv05vuf0b8[] = "compiler/check/seq.gm";
const char gm_srclv3pj5es0f8o[] = "compiler/bigint/bigint.gm";
const char gm_srcm0juv8ecdls4[] = "compiler/ide/info.gm";
const char gm_srcn3jjc71a2bv3[] = "compiler/interp/sys.gm";
const char gm_srcoimgd0jo62fr[] = "compiler/check/iterators.gm";
const char gm_srcqr4ch56kv6bv[] = "compiler/driver/native.gm";
const char gm_srcsloagb2nrd8g[] = "compiler/check/escape.gm";
const gm_variant gm_tyehhdtpeti1_v[] = {{"Checked", 0, NULL}, {"Wrap", 1, NULL}, {"Sat", 2, NULL}, {"Opt", 3, NULL}};
const uint8_t gm_s03itq0fotubb[14] = "` has no body";
const uint8_t gm_s05fn63o3te82[13] = "Type aliases";
const uint8_t gm_s064htvgs8b59[69] = "internal error: pointer arithmetic by a value that is not an integer";
const uint8_t gm_s069kf53uhd4o[45] = "; a mask is tested with `.all()` or `.any()`";
const uint8_t gm_s0flic1r29j4i[14] = "parse_fn_expr";
const uint8_t gm_s0iioal6odpu7[11] = "block : {}";
const uint8_t gm_s0iql3kt61auq[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\244\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s0k8sea1jhldg[21] = "pow by a non-integer";
const uint8_t gm_s0l81imssm78p[7] = "places";
const uint8_t gm_s0r850bpi2rnm[14] = "goto gm_fret;";
const uint8_t gm_s0rcpa4ucqf07[103] = "the address of a local, such as a loop variable or a pattern binding, kept where it outlives the local";
const uint8_t gm_s0s4ef50964r9[3] = "+|";
const uint8_t gm_s0uum3co6vapt[23] = "`{}` has no field `{}`";
const uint8_t gm_s107olobe2ou0[28] = "#define GM_RUN_EXPORT {}\012{}";
const uint8_t gm_s129bqtq598qj[54] = "`{}` is declared by `{}`: import it and write `{}.{}`";
const uint8_t gm_s15g40fmv51gv[59] = "a pointer offset must be a `usize` or an `isize`, not `{}`";
const uint8_t gm_s17uhrtdo7u30[2] = "'";
const uint8_t gm_s1a1820qk5dro[23] = "        gm_format(&b, ";
const uint8_t gm_s1amcoqteaeio[38] = "{} passed, {} failed, {} filtered out";
const uint8_t gm_s1djo7fpeeqnt[11] = "{} modules";
const uint8_t gm_s1e7k26kdaoj3[31] = "no C compiler for the target `";
const uint8_t gm_s1h9rpfuhvh29[60] = "a handler literal is second-class and cannot be {} (\302\24711.6)";
const uint8_t gm_s1mud7cvvuuon[4] = "ctz";
const uint8_t gm_s1n20vngt0mkv[3] = "Le";
const uint8_t gm_s1rpc945q7l9o[15] = "end_frame_slow";
const uint8_t gm_s1utcdes4r8ku[7] = ")1 << ";
const uint8_t gm_s205esok2u8pk[4] = "env";
const uint8_t gm_s21284p79u3jq[14] = "write `ok(x)`";
const uint8_t gm_s22mk3qhbqci2[11] = "signatures";
const uint8_t gm_s23siujajsn8e[111] = "`try` does not convert between optionals and results; give the absent value an error: `try (x else !.Missing)`";
const uint8_t gm_s26di78e9g1gn[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000'\000\000\000,\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s2791bst0j7pi[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\213\000\000\000\035\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s28ijm46j20ds[8] = "SignBit";
const uint8_t gm_s29cvp3di8ou1[4] = "/..";
const uint8_t gm_s29t9tfffm60i[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000A\001\000\000\022\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s2cqd5egpbgbs[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\214\001\000\000K\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s2d9sjnr88ud8[16] = "->fn = (void *)";
const uint8_t gm_s2e48s7fhhseb[17] = "gm_proc_wait_for";
const uint8_t gm_s2fp7iapio2lo[11] = "write `is`";
const uint8_t gm_s2k1l0sh4mlmv[10] = "soa_block";
const uint8_t gm_s2mb2caup8rnh[49] = "` needs a union type, but the expected type is `";
const uint8_t gm_s2ot4uojp2s87[7] = "int {}";
const uint8_t gm_s2ph2ad6lffbq[17] = "GM_ROUNDEVEN({})";
const uint8_t gm_s2rbapbbig1pe[19] = "(GM_LD(uintptr_t, ";
const uint8_t gm_s2uimijqjekbd[67] = "a variable whose value reaches a function that uses a new constant";
const uint8_t gm_s2uqljf8sumcn[11] = "FloatWiden";
const uint8_t gm_s34o1igjpenij[26] = "`not not x`, which is `x`";
const uint8_t gm_s3ap820v1eq4f[9] = "offsetof";
const uint8_t gm_s3b32la11650d[51] = "empty identifier segment in pre-release identifier";
const uint8_t gm_s3bddqij01brp[83] = "`{}` contains the second-class type `{}`, so it cannot be a type argument (\302\24716.1)";
const uint8_t gm_s3bli9pbcj81a[12] = "expect_true";
const uint8_t gm_s3brf0f9sa1j7[26] = "` is a local name where `";
const uint8_t gm_s3ceboq671qr5[39] = "this name matches only a present value";
const uint8_t gm_s3hqs40g2kokp[9] = "sign_bit";
const uint8_t gm_s3inajboct11s[8] = "defined";
const uint8_t gm_s3ks2qlmjeh74[11] = " *)gm_out)";
const uint8_t gm_s3mvfcnrk9f21[48] = "internal error: a `with \342\200\246 on` is active twice";
const uint8_t gm_s3qd7ldqb9526[29] = "Is a directory (os error 21)";
const uint8_t gm_s3qnfqte7c8r2[35] = "and a type after the variable name";
const uint8_t gm_s3t3r41giacns[14] = "GM_LD({}, {})";
const uint8_t gm_s3udkpu8iv0h2[75] = "this returns {} the {} `{}`, which dies when the function returns (\302\24715.1)";
const uint8_t gm_s41sdeqq056ae[9] = "checkout";
const uint8_t gm_s42bvs5l5hcla[4] = " (`";
const uint8_t gm_s43jqu37ghtoq[19] = "this error lifting";
const uint8_t gm_s43sk7g92lovh[13] = "identifier `";
const uint8_t gm_s4873mb1qbjh2[5] = ".exe";
const uint8_t gm_s489g6hodd4uq[44] = "` does not apply to a function pointer type";
const uint8_t gm_s48iukd74s6c6[22] = "static const uint8_t ";
const uint8_t gm_s4bdmssec9aiu[4] = "And";
const uint8_t gm_s4d81vdcmt94n[14] = "((uint8_t *)a";
const uint8_t gm_s4gh6uvc8i8um[24] = "this placeholder needs ";
const uint8_t gm_s4lt93qhapike[4] = "Len";
const uint8_t gm_s4m36jv5rcfat[13] = "gm_with_go(&";
const uint8_t gm_s4niue72anpss[37] = "`_` cannot name an argument (\302\24711.3)";
const uint8_t gm_s4otfbu8ml1qq[7] = ", loc)";
const uint8_t gm_s4pru1na1k33s[3] = "{}";
const uint8_t gm_s4rr6a2j9pjhb[35] = ": use debug, checked, safe or fast";
const uint8_t gm_s4uditudns916[3] = ":h";
const uint8_t gm_s520nu4qcaerc[6] = "fiber";
const uint8_t gm_s565fgm0tsngr[15] = "__builtin_sqrt";
const uint8_t gm_s58rgrgmho9sa[68] = "a `#soa` array's length is a constant, not a compile-time parameter";
const uint8_t gm_s5k56vqvt8t7d[12] = "--target={}";
const uint8_t gm_s5lk0utb9h01t[3] = "fb";
const uint8_t gm_s5mu2shl2bmft[14] = "float literal";
const uint8_t gm_s5om28rj8ddf9[28] = "after the clause parameters";
const uint8_t gm_s5oredgotak9m[58] = "`sha256` of dependency `{}` must be 64 hexadecimal digits";
const uint8_t gm_s5pq4t98troao[34] = "unnecessary parentheses around {}";
const uint8_t gm_s5t5jm6ask6ip[15] = "{}({}, {}, {})";
const uint8_t gm_s5tee1epg2e5a[24] = "`build` must be a table";
const uint8_t gm_s601j8au4fcin[6] = "parse";
const uint8_t gm_s6033vr1c63cl[17] = "gm_set_cur_fiber";
const uint8_t gm_s615gqen9ca7q[16] = "core.bare=false";
const uint8_t gm_s62maodcvu607[20] = "expected `,` or `]`";
const uint8_t gm_s658vrmfgnecg[28] = "`@{}` needs a type argument";
const uint8_t gm_s69ahc8sth76o[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000L\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s6cqii80as1c2[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\343\000\000\000\031\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s6f0l2e1iha9i[8] = "fs_open";
const uint8_t gm_s6gohqbjroq8g[13] = " backtick(s)";
const uint8_t gm_s6groq37ram3h[6] = "usize";
const uint8_t gm_s6i2cgc2057sp[5] = "soa ";
const uint8_t gm_s6ibuqseh9tt7[152] = "a handler literal cannot be a top-level declaration; bind it inside a function, or declare a handler for a type with `handler E for T { \342\200\246 }` (\302\24712.6)";
const uint8_t gm_s6ipt60dh9o1j[11] = "AtomicSwap";
const uint8_t gm_s6lcmor84nt1l[4] = "`_`";
const uint8_t gm_s6lddausbiitu[9] = "lanes_ne";
const uint8_t gm_s6nt01jkbgi6h[52] = "the executable to write (default: the input's name)";
const uint8_t gm_s6ocl6n3lno8m[8] = "] != 0)";
const uint8_t gm_s6tp3e68vgt6g[13] = ") for union ";
const uint8_t gm_s7278rr8ss873[19] = "GAMMA_UNIT_OBJECTS";
const uint8_t gm_s7cn098jq5gvg[9] = "--output";
const uint8_t gm_s7dobq9tho3k8[96] = "gamma.lock needs to be updated, but --locked was given; run `gamma fetch` and commit gamma.lock";
const uint8_t gm_s7gn66u5sfpv0[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\226\001\000\000\015\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s7kc4hmkruq4r[11] = "gm_dir_put";
const uint8_t gm_s7spn02nsqs4u[18] = "{} does not exist";
const uint8_t gm_s7ta8sbflqphb[28] = "`, found a complex constant";
const uint8_t gm_s7vj61tk4g1rp[14] = ": not every `";
const uint8_t gm_s7vt7hgga3uq7[7] = ") & ((";
const uint8_t gm_s805anh9k3rr5[7] = ", hi: ";
const uint8_t gm_s80ddhqa1o9i9[7] = "spread";
const uint8_t gm_s80tqs0e3u7de[11] = "` (\302\24721.3)";
const uint8_t gm_s81f5igr577he[14] = "else capture ";
const uint8_t gm_s84lh6to0er2m[19] = "assertion failed: ";
const uint8_t gm_s85kbq4rs5fa7[83] = "this is nested more than {} levels deep, the limit of this implementation (\302\24723.5)";
const uint8_t gm_s86fb5obo4kb8[154] = "`{}` is declared inside a function and cannot refer to a type or value parameter of that function; it would be a different type in every instance (\302\2473.5)";
const uint8_t gm_s8afn1h3asffl[32] = "this comparison at compile time";
const uint8_t gm_s8ea9885mu6jv[28] = "{}(cx->env, cx->hctx, cx{})";
const uint8_t gm_s8ei36h3rj566[14] = "</code></h1>\012";
const uint8_t gm_s8j9ber7fuaks[20] = "invalid multi-line ";
const uint8_t gm_s8o1jnchmsg7i[7] = "#vec [";
const uint8_t gm_s8oq9c4aac3sc[52] = "a vector has a power of two lanes, at least 2, not ";
const uint8_t gm_s8us8sihs6eh7[74] = "invalid suffix `{}` for an imaginary literal; expected j, j16, j32 or j64";
const uint8_t gm_s929lennfd3g6[5] = "bool";
const uint8_t gm_s9abd2e9it4et[67] = "`{}` names a type; a local may not have the name of a type (\302\2474.6)";
const uint8_t gm_s9caepulv0516[9] = ",\"text\":";
const uint8_t gm_s9i4dp3hfg864[18] = "<temporary array>";
const uint8_t gm_sa23gppmgki7j[11] = ".size = {}";
const uint8_t gm_sa2h9vonan0ma[4] = "url";
const uint8_t gm_sa2tquttrsgo4[17] = "\000\000\000\000\000\000\000\0009\000\000\000\000\000\000\000";
const uint8_t gm_sa5v7oq7un6c6[84] = "the archive is not a gzip-compressed tar file (only .tar.gz and .tgz are supported)";
const uint8_t gm_sa7tr9aaspmun[5] = "help";
const uint8_t gm_sa8p4rum5g6ig[116] = "this condition always succeeds: `_` matches a missing value too; bind a name (`v := \342\200\246`) or test `\342\200\246 is not null`";
const uint8_t gm_sa9dtqeprt7kf[75] = "` for a binary or octal literal; only decimal and hexadecimal floats exist";
const uint8_t gm_sabc7m3rug04j[3] = "-\077";
const uint8_t gm_sac01n562d8ve[59] = "internal error: `{}` called with {} arguments, expected {}";
const uint8_t gm_sadscuj628cl7[8] = "vector ";
const uint8_t gm_saimr6ebj0d4t[12] = "MemoryOrder";
const uint8_t gm_sakmuvnjjp217[10] = "FutexWait";
const uint8_t gm_salbpgh5m2h5m[15] = ".env, .hctx = ";
const uint8_t gm_samq5nkk38rku[17] = "\000\000\000\000\000\000\000\000!\003\000\000\000\000\000\000";
const uint8_t gm_sap9oatgj29c2[13] = "],\"message\":";
const uint8_t gm_sas87vff32eaf[43] = "` shifts integer lanes, not the lanes of `";
const uint8_t gm_satkhe9qra7p5[13] = "{} ({}*)({})";
const uint8_t gm_sb0fu128u868p[27] = "double free of heap block ";
const uint8_t gm_sb36bkunl16pp[22] = "the archive contains ";
const uint8_t gm_sb371d9627jdq[5] = "auto";
const uint8_t gm_sb6c4qgs6rpfi[8] = "` names";
const uint8_t gm_sb8us6li0fvm5[42] = "`\077` unwraps an optional, but this is `{}`";
const uint8_t gm_sb9fu6ufbt9g1[17] = "\012    gamma test ";
const uint8_t gm_sbapuchngtvak[28] = "variant `{}` has no payload";
const uint8_t gm_sbd6ket0ql5ab[9] = "<global>";
const uint8_t gm_sbgl5dgur8udg[7] = "module";
const uint8_t gm_sbgqae2tm3q08[39] = "{}{}GM_RUN_EXPORT(gm_xt{}); return {};";
const uint8_t gm_sbim95dtkmqmv[49] = "`#bits` needs an unsigned integer type, not `{}`";
const uint8_t gm_sbk48vc8h32c1[9] = "--target";
const uint8_t gm_sbn0999b7p1ph[83] = "`on` gives a stack to fibers, but none of these handlers handles a `ctl` operation";
const uint8_t gm_sc06resjcdhg4[19] = "the returned value";
const uint8_t gm_sc12loraoaq3m[7] = " says ";
const uint8_t gm_sc1vrnob3chsu[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\016\002\000\000\031\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_sc3p6dp1skc87[8] = "{}.used";
const uint8_t gm_sc5aaou69jljp[10] = "gm_c{}_{}";
const uint8_t gm_sc71n5mgucblh[50] = "expected `]` to close the array length, found `,`";
const uint8_t gm_sc9goq69s6n9d[37] = "while (!__atomic_compare_exchange_n(";
const uint8_t gm_scb853lmgr58k[6] = "Floor";
const uint8_t gm_sce79e7st00ad[4] = "</h";
const uint8_t gm_scfppf8naer38[21] = " has no dependency `";
const uint8_t gm_sch7ep6ht4utr[58] = "`{}` is not a target triple (such as `aarch64-linux-gnu`)";
const uint8_t gm_schn9bp2k5kqo[47] = " a;\012    GM_ST(void *, (uint8_t *)&a, (void *)(";
const uint8_t gm_scj8qp34gs3m8[58] = "a program has at most one `#trap_hook` function (\302\2473.7.1)";
const uint8_t gm_scogrgnqo3tpu[22] = "allocation alignment ";
const uint8_t gm_scsqaooh7eo7l[153] = "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sct0vr0h329vl[43] = "internal error: an unknown global variable";
const uint8_t gm_scvhpklutvpjg[4] = "not";
const uint8_t gm_sd323o3qr3mfd[64] = "` is made by slicing an array or by `.make(n)`, not constructed";
const uint8_t gm_sd5g4r2t5gcbk[25] = "`sha256` of dependency `";
const uint8_t gm_sd6g049s1ldbu[2] = "\377";
const uint8_t gm_sd97d6kmlmb3h[30] = "`with \342\200\246 on` at compile time";
const uint8_t gm_sda1bf43v6985[10] = "    if (!";
const uint8_t gm_sdad15q200tmk[4] = "-dM";
const uint8_t gm_sdbocdqbukr6k[77] = "`f16` needs a C compiler with `_Float16`, which this target's compiler lacks";
const uint8_t gm_sddnsndqgnosl[31] = "` is not a valid memory order ";
const uint8_t gm_sdjk9c005ec9t[13] = "cannot read ";
const uint8_t gm_sdle7podd8151[45] = " an integer, float, `bool` or pointer, not `";
const uint8_t gm_sdq6l7oddjqfe[12] = "AtomicStore";
const uint8_t gm_sdsrtcf8ng48d[6] = " \342\206\222 ";
const uint8_t gm_sdt9q53qgnu87[7] = " = \342\200\246";
const uint8_t gm_sdtdaesvk7dfl[8] = "{}.{}.c";
const uint8_t gm_sdtthp48kia81[5] = "uint";
const uint8_t gm_se0kk0u6b53ko[6] = " = 1;";
const uint8_t gm_se1apliihhr7c[9] = "\000\004\000\000\000\000\000\000";
const uint8_t gm_seccpktgmrevt[2] = "#";
const uint8_t gm_sech7am46drki[16] = "` is not a type";
const uint8_t gm_sed64s2n0ri16[52] = "this loop's value is `{}`, so `break` needs a value";
const uint8_t gm_sehvo63gl3eu0[26] = " && memcmp(GM_LD(void *, ";
const uint8_t gm_seidpmiq51gtp[60] = "the `{}` declaration `{}` mentions the {} type `{}` (\302\2473.3)";
const uint8_t gm_sekmbu83qkg9c[44] = "    (void)r;\012    gm_flush();\012    return 0;\012";
const uint8_t gm_selnb2co583fc[6] = ") && ";
const uint8_t gm_senb8fal0j05l[15] = "gm_scratch_top";
const uint8_t gm_serd0h8povggs[12] = "</td></tr>\012";
const uint8_t gm_sf1hl3n20st3g[37] = "slices have no method `.{}` (\302\24721.3)";
const uint8_t gm_sf1ljria5bdei[8] = "{}{}\012{}";
const uint8_t gm_sf1qp7r4ktsd1[11] = "reduce_max";
const uint8_t gm_sf3irv2ans6de[57] = "a type parameter needs a type: `T: type` or `N :: usize`";
const uint8_t gm_sf42bd5sk6816[26] = "(__builtin_convertvector(";
const uint8_t gm_sf9ha6blerd0b[88] = "run <n> tests at a time (default 1; 0: one per processor;\012the output is still in order)";
const uint8_t gm_sfde9lsgge1ld[26] = "compiler/interp/format.gm";
const uint8_t gm_sffbik0ast0lg[53] = "targets {}-byte pointers; only 4 and 8 are supported";
const uint8_t gm_sfi2ha42r1nr2[2] = "@";
const uint8_t gm_sfkmkcagsnqnb[7] = "params";
const uint8_t gm_sfn9k9vgl9ged[28] = "` has no variant carrying `";
const uint8_t gm_sfrdbcc53tuv9[18] = "` has lanes 0 to ";
const uint8_t gm_sftkp1r7uetfv[10] = "-lshell32";
const uint8_t gm_sfuh3dki1la7r[32] = "and a type after the field name";
const uint8_t gm_sg344a2p7u6i6[6] = "</td>";
const uint8_t gm_sg4kirhst3a6p[15] = "gm_trap_unwrap";
const uint8_t gm_sg7i6m1frjk5n[63] = "`#link_name` applies to a member of a `foreign` block (\302\24719.2)";
const uint8_t gm_sgcuckp03jlhp[23] = "expected a struct type";
const uint8_t gm_sghautilepqio[13] = "for-each-ref";
const uint8_t gm_sgkfsokc46mg5[34] = "cannot infer the union type of `.";
const uint8_t gm_sgks95jf6fs73[21] = "` does not satisfy `";
const uint8_t gm_sgme8q1t414rd[19] = "{}({}, {}, {}, {})";
const uint8_t gm_sgmhc3klfmn46[5] = "int ";
const uint8_t gm_sgpdmvc9s0d2p[3] = "is";
const uint8_t gm_sgpn27p7jhj9p[12] = "(uint8_t){}";
const uint8_t gm_sgpp0p44ddknc[76] = "` cannot be spread: its length depends on a compile-time parameter (\302\24711.4)";
const uint8_t gm_sgtriqnjufjbf[10] = "FsSetMode";
const uint8_t gm_sguqrpr7akp41[28] = "type `{}` has no element {}";
const uint8_t gm_sgvhjfacilshn[9] = ", zero: ";
const uint8_t gm_sh01rb1u2jpfs[19] = "fma on a non-float";
const uint8_t gm_sh09ul4fvjk00[36] = "a package entry needs a string `{}`";
const uint8_t gm_sh0fsro8kchj4[52] = "no version tag of {} satisfies the requirements: {}";
const uint8_t gm_shb5dhf7pftn9[17] = "import cycle: {}";
const uint8_t gm_shfae2hkjlb47[15] = "invalid mode `";
const uint8_t gm_shnvun4juqpma[9] = "\200\360\372\002\000\000\000\000";
const uint8_t gm_shqseub127akh[47] = "`---` needs a plain-data tuple type annotation";
const uint8_t gm_shr2dul5unlgn[16] = "static uint8_t ";
const uint8_t gm_shsv7bl83q04d[9] = "[]const ";
const uint8_t gm_shug2jbeta627[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\314\001\000\000\031\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_si3qm04ntebij[6] = "Win64";
const uint8_t gm_si4j8sh2qika1[3] = "//";
const uint8_t gm_si6c601mt9p4v[220] = "a name in the position of an optional or a result below the top of a pattern matches only a present or success value (\302\2479.3); add an arm for the other case (such as `.V(null)`), or write `\077{}` to bind the value as it is";
const uint8_t gm_sie9phnhh4ub6[11] = "{} error{}";
const uint8_t gm_siefj6nst22k5[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000!\001\000\000\034\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sigi809bnb15c[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\261\002\000\000\014\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sihr7qmh227og[26] = "#define GM_RUN_MAIN {}\012{}";
const uint8_t gm_siic7p394dov6[13] = "(no message)";
const uint8_t gm_sij6bc8bh95o1[59] = "internal error: a `ctl` operation of a `with` without `on`";
const uint8_t gm_sit8u7uppaik9[7] = "FsStat";
const uint8_t gm_sitcmrrnvoedc[55] = "invalid {} name `{}`: it is the name of a builtin type";
const uint8_t gm_sive7ce5krqer[18] = " values are given";
const uint8_t gm_sj19m7hg2ca80[11] = "; B.col = ";
const uint8_t gm_sj3m78jf2dftr[24] = "textDocument/references";
const uint8_t gm_sj4n6nvsgq2jn[43] = "`trap` takes one optional message argument";
const uint8_t gm_sj4smhrnkurr1[24] = "gm_asyncify_stop_rewind";
const uint8_t gm_sj98l5dehulso[8] = "gm_pdir";
const uint8_t gm_sjaeqf6f3s51a[45] = "`--older-than` needs a number of days, not `";
const uint8_t gm_sjc9ao9066c7n[33] = "invalid digit `{}` in {} literal";
const uint8_t gm_sjcg5brfqm3a2[8] = "gm_wdir";
const uint8_t gm_sjef1iqti8f5h[5] = "html";
const uint8_t gm_sjfcbh429vmot[9] = "NetClose";
const uint8_t gm_sjfpr08jc3ep1[23] = "return;\012gm_fprop:\012    ";
const uint8_t gm_sji60fgbl9iul[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000$\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sjjig60ja37ka[3] = "-%";
const uint8_t gm_sjl4446pn0dup[3] = "&F";
const uint8_t gm_sjm2j40p6489d[9] = "{} :: {}";
const uint8_t gm_sjss9mvc5fcod[9] = "variable";
const uint8_t gm_sjtiiac2ha9i1[8] = "Structs";
const uint8_t gm_sk1d0v1onojc1[8] = "fs_sync";
const uint8_t gm_sk4cv563hhed7[2] = "g";
const uint8_t gm_sk5htm2k25gsj[16] = "missing command";
const uint8_t gm_sk8csn7us23m3[9] = "fnformat";
const uint8_t gm_sk8it0s509dbd[38] = "(__builtin_convertvector({}, {}) & 1)";
const uint8_t gm_sk9dt92p27k0o[53] = "every alternative of an arm must bind the same names";
const uint8_t gm_skbsro97j503c[7] = "x86_64";
const uint8_t gm_skdhvc36gfkb0[47] = "`from_tag` is defined only for enums, not `{}`";
const uint8_t gm_skdpq4t6o94o2[25] = "__attribute__((ms_abi)) ";
const uint8_t gm_ske39klqfe71k[82] = "the directory name `{}` gives the package name `{}`: {}; choose one with `--name`";
const uint8_t gm_ske6qr3raj7r9[24] = "` needs a type argument";
const uint8_t gm_skf0mm42l0tsk[17] = "inst->with_frame";
const uint8_t gm_skhu4ngu6nt5n[15] = "gm_veh_install";
const uint8_t gm_skitebsmtce1i[34] = "` has a variant with a payload (`";
const uint8_t gm_skki6c17jgbtj[30] = "`mode` in {} must be a string";
const uint8_t gm_skou19srual26[10] = "--version";
const uint8_t gm_skrn366c6rv49[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\0008\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sksgudmhhp60q[14] = "fiber_builtin";
const uint8_t gm_sl0kpeuro9al4[23] = "(cx->env, cx->hctx, cx";
const uint8_t gm_sl1tolq1dm391[10] = "Functions";
const uint8_t gm_sl325guoc37g3[23] = " must be true or false";
const uint8_t gm_sl6fvbluu7spr[26] = "GM_LD(uintptr_t, {} + {})";
const uint8_t gm_sla6dgu7p35mv[31] = "`not` takes a `bool`, not `{}`";
const uint8_t gm_slb3ondne09t1[28] = ");\012    __builtin_trap();\012}\012";
const uint8_t gm_sldehiepn7rta[6] = "{}\302\265s";
const uint8_t gm_slfk6iuc3d2pa[14] = "cannot fetch ";
const uint8_t gm_slfnv05vuf0b8[22] = "compiler/check/seq.gm";
const uint8_t gm_slk5snj328gko[45] = "internal error: native operation {} of `Env`";
const uint8_t gm_sloe55935mobb[8] = "#soa {}";
const uint8_t gm_slqq08j0fro21[31] = "checksum mismatch for package ";
const uint8_t gm_slrfv83pestqp[31] = "more than one input file given";
const uint8_t gm_sluddd1oa2j8v[54] = "destructuring needs a tuple, but this is `{}` (\302\2474.5)";
const uint8_t gm_sm3dios51ece1[10] = "MaskCount";
const uint8_t gm_sm4s7blimj76n[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\021\001\000\0006\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sm5s4k1c62cb1[18] = "gm_altstack_keyed";
const uint8_t gm_sm7oa350rtqnm[12] = "{}inline {}";
const uint8_t gm_sm7qvedi3emcj[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\322\003\000\000\036\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sm8obe455p2d9[3] = "=>";
const uint8_t gm_sm9e81djuncnu[4] = "f32";
const uint8_t gm_smap40unho18s[19] = "static GM_LONG_FN ";
const uint8_t gm_smbkn5defd9ht[48] = "`null` matches only optionals, but this is `{}`";
const uint8_t gm_smdvilsvvv2u5[33] = "expected a newline or `;` after ";
const uint8_t gm_smhvmmegaur89[7] = "{}({})";
const uint8_t gm_smkijcjdhajsg[5] = "enum";
const uint8_t gm_smo8ffg4jq2d6[45] = " is a constant, not a compile-time parameter";
const uint8_t gm_smorejbk97kqm[65] = "`{}` is indexed by its enum, so it is sliced only whole: `a[..]`";
const uint8_t gm_smrmke0a8tkf0[120] = "unexpected character `{}` (U+{:04X}); non-ASCII characters are only allowed in comments, strings and character literals";
const uint8_t gm_smuec3qfsdh82[10] = "ReduceMax";
const uint8_t gm_sn1seggk13bop[4] = "u))";
const uint8_t gm_snbque26o7lbi[7] = "local ";
const uint8_t gm_snd30h4tliigb[12] = "0x7fffffffu";
const uint8_t gm_snduiu5un9hfb[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000*\000\000\000\027\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_snh06kr7sd4ji[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000w\000\000\000\031\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sni6hcs91b3qh[13] = "{}.tmp-{}.{}";
const uint8_t gm_snikqaq1mfe0u[29] = "` is supplied by two spreads";
const uint8_t gm_snil57vrv77lr[12] = "OpenOptions";
const uint8_t gm_sniofsn8cqk74[10] = ", width: ";
const uint8_t gm_snkdp6opmsb8i[42] = "`, so the error cannot be lifted (\302\24710.6)";
const uint8_t gm_snnagva49avpd[8] = "LDFLAGS";
const uint8_t gm_snqe7tsf40ume[138] = "Safe builds compile with the Fil-C compiler (https://fil-c.org), but `filcc` is neither on PATH nor in /opt/fil/bin; install it or set CC";
const uint8_t gm_snupav63sairp[12] = " values are";
const uint8_t gm_snvisme5hp379[7] = "locked";
const uint8_t gm_so0m8vf92uemo[66] = "`@static_assert(cond, \"message\")` takes a condition and a message";
const uint8_t gm_so5q4maooc958[35] = "does not define __SIZEOF_POINTER__";
const uint8_t gm_so7oqi7vob9ku[17] = "</body>\012</html>\012";
const uint8_t gm_so97uarpe8rtg[9] = "Listener";
const uint8_t gm_so9b3j0thioda[78] = "a private top-level function, type, constant or global that nothing refers to";
const uint8_t gm_sobcm12sogdng[5] = "cond";
const uint8_t gm_sockrjemj2if8[61] = ".pushsection \".debug_gdb_scripts\", \"MS\",%progbits,1\012.byte 4\012";
const uint8_t gm_soduqpin16t7d[6] = "decls";
const uint8_t gm_soe998lc1ukpn[11] = "gm_fs_read";
const uint8_t gm_sogkbe8nre26a[17] = "unknown option `";
const uint8_t gm_sohcj0rggg07e[51] = "`try` needs an optional or a result, but this is `";
const uint8_t gm_sonht3t87vai7[9] = "__mips64";
const uint8_t gm_soofuqhnovv5o[5] = "warn";
const uint8_t gm_sopjc7gjg0pgu[32] = "the archive contains the path `";
const uint8_t gm_soqr0md31pcm7[38] = "`: it is part of the standard library";
const uint8_t gm_souosds7adra9[36] = "{{ {} gm_r = F.ret; {}return {}; }}";
const uint8_t gm_sp0l171g2m7ho[25] = "compiler/interp/fiber.gm";
const uint8_t gm_sp5mk8jqu8dn0[70] = "this default depends on itself; the cycle cannot be evaluated (\302\2473.4)";
const uint8_t gm_sp6s13hdc6c0b[4] = "Raw";
const uint8_t gm_sp7351fukevmv[29] = "(uint8_t)__builtin_isinf({})";
const uint8_t gm_sp7me12frc01v[22] = "too many sub-patterns";
const uint8_t gm_spbhhmdbp335u[4] = "; }";
const uint8_t gm_spckols8oluq7[15] = "({} + {} * {})";
const uint8_t gm_spcl9lgniu1he[18] = " cannot be hashed";
const uint8_t gm_spgsforhruitc[30] = "str: count of an empty needle";
const uint8_t gm_spi5fq66572ic[20] = "__builtin_trunc({})";
const uint8_t gm_spi8n2atkg603[17] = "assertion failed";
const uint8_t gm_spica4gaklfma[27] = "`@{}` takes {} argument(s)";
const uint8_t gm_spit8mdn0781q[26] = "compiler/interp/interp.gm";
const uint8_t gm_splq0kkmd3hj2[31] = "`--jobs` needs a number, not `";
const uint8_t gm_spm7tm8a050b3[7] = "result";
const uint8_t gm_spnad0ot4gagr[10] = " (\302\24711.6)";
const uint8_t gm_spr274eleu2lj[4] = "Add";
const uint8_t gm_spskhsf5tnedj[10] = "NetAccept";
const uint8_t gm_spspe7tb8bsr3[8] = "Regular";
const uint8_t gm_sq800qhfmpfk7[39] = ";\012    const uint8_t *pa = a, *pb = b;\012";
const uint8_t gm_sq9i3vfk7nqs8[10] = "source = ";
const uint8_t gm_sqa0b48i8mvk7[6] = "Slice";
const uint8_t gm_sqf5m57l1ua5d[27] = "the template writes value ";
const uint8_t gm_sqfsba89a30n8[26] = "the C compiler failed on ";
const uint8_t gm_sqjomq37io03j[151] = "` is declared inside a function and cannot refer to a type or value parameter of that function; it would be a different type in every instance (\302\2473.5)";
const uint8_t gm_sql1qe620spbq[23] = "))GM_PTR_UNTAG(code))(";
const uint8_t gm_sqlpqagi4dpk1[38] = "{} has no tag `{}` (required by `{}`)";
const uint8_t gm_sqmifqg6egoj0[41] = "`{}` of dependency `{}` must be a string";
const uint8_t gm_sqp9p8tc2dil3[8] = "reserve";
const uint8_t gm_sqpev88g6capg[9] = "variadic";
const uint8_t gm_squ601q9vv2so[33] = "the {} `{}` is not written in {}";
const uint8_t gm_sr0sh5asl4po7[22] = "{} {}, {}, {} ({} {})";
const uint8_t gm_sr244s00qj01d[3] = ")`";
const uint8_t gm_sr3lr7c9psgf2[12] = "gm_run_main";
const uint8_t gm_sr5mr0r3jci3n[12] = "thread_main";
const uint8_t gm_sr99v3u3nhpad[81] = "usage: gamma fmt [--check] <file or directory>... (`-` filters stdin to stdout)\012";
const uint8_t gm_srajaqomjf1d3[19] = "{} is matched here";
const uint8_t gm_src5778gpfoh9[9] = "HeapFree";
const uint8_t gm_srcs6slmq8ak7[15] = "` is a valid `";
const uint8_t gm_sre31tu0qddge[24] = "`, which may have moved";
const uint8_t gm_srgr2i687arao[75] = "internal error: `else` on a value that is neither an optional nor a result";
const uint8_t gm_srh29phg8b30r[4] = "\357\273\277";
const uint8_t gm_srjh1vh9n4las[87] = "a clause of a `fn` operation can only produce a value; it cannot `break` or `continue`";
const uint8_t gm_srkqhsgbb6hhs[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000X\002\000\000\021\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_srn9e8kdj18it[33] = "GIT_ALTERNATE_OBJECT_DIRECTORIES";
const uint8_t gm_srne5d503fcjg[5] = "try ";
const uint8_t gm_srof610t1er1p[26] = "((gm_hval){(void *)&F.h, ";
const uint8_t gm_sropo1941pvlt[16] = "kind = GM_K_INT";
const uint8_t gm_srs84t3n56p1c[8] = "<arg{}>";
const uint8_t gm_sru5s20jprgc9[12] = "coverage-{}";
const uint8_t gm_srundlmqvm06p[60] = "internal error: a template parameter without a string value";
const uint8_t gm_ss0g40l133d39[13] = "); i++) if (";
const uint8_t gm_ss3tu1f38tkcj[6] = "bool ";
const uint8_t gm_ss8448efddk3m[32] = "((gm_hval){{(void *)&F.h, {}}})";
const uint8_t gm_ssbg0bg772d67[5] = "char";
const uint8_t gm_ssg5cub3gqpc7[13] = "value-param ";
const uint8_t gm_ssi2113jtjul7[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\202\000\000\0002\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_ssnsddtjt3gpa[35] = ")` would convert from its own type";
const uint8_t gm_sso3tuh819o23[9] = "fs_lstat";
const uint8_t gm_ssop4efhh9upu[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000/\001\000\000\014\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_ssq1no648r8mj[63] = "}`: expected `[[fill]align][+][#][0][width][.precision][kind]`";
const uint8_t gm_sss3cg4gsncei[12] = "gm_net_send";
const uint8_t gm_ssuuud2f0uns5[9] = ", kind: ";
const uint8_t gm_st2dnokm1h0jd[5] = "cont";
const uint8_t gm_st2tqp657i86j[8] = "box_mut";
const uint8_t gm_st88sqv2u9dqh[8] = "get_ptr";
const uint8_t gm_st984in2akmmc[43] = "`{}` cannot appear in a deferred statement";
const uint8_t gm_st9dp4tkd7f7l[13] = "expected `:`";
const uint8_t gm_st9g0aublffth[18] = "unsigned __int128";
const uint8_t gm_sta9bvnin7dlr[36] = "defaults of fields of generic types";
const uint8_t gm_stbuagagnsakl[37] = "((((unsigned __int128)1) << {}) - 1)";
const uint8_t gm_stdehenbgfc4h[13] = "isIncomplete";
const uint8_t gm_ste5bc2m7dvjm[12] = "()[]{},;\"'`";
const uint8_t gm_stivl0smu8eq0[9] = "IntWiden";
const uint8_t gm_stl47boisb31o[11] = "net_accept";
const uint8_t gm_stm8v8ngvll3q[16] = "write-deferred ";
const uint8_t gm_stnlf4pvbut1c[3] = "!=";
const uint8_t gm_stntm4cqqrhf7[7] = "_slice";
const uint8_t gm_stpkldc5dmf71[33] = "cannot negate a boolean constant";
const uint8_t gm_su18s5052fc6v[15] = "gm_format_view";
const uint8_t gm_su4aje74nkg3o[51] = "` is a reserved type name and cannot be redeclared";
const uint8_t gm_su6il7ugmnq2g[48] = "missing argument for the parameter `{}` of `{}`";
const uint8_t gm_su6oav9vpqcnu[17] = "invalid free of ";
const uint8_t gm_sub8b9nf0ms2i[104] = "`@fiber` needs a function pointer `^fn(a: A) -> never needs \342\200\246` with the C calling convention, found `";
const uint8_t gm_sub9rknupld7d[52] = "the integer {} is not exactly representable as `{}`";
const uint8_t gm_sucsarpa3psmp[55] = "        gm_buf_push(&b, \"\\n\", 1);\012        gm_flush();\012";
const uint8_t gm_sud6d85d454o1[42] = "function literal parameters must be named";
const uint8_t gm_sue9kqmk98bh9[34] = "missing argument: the {} takes {}";
const uint8_t gm_sui7l1ojsejig[13] = "gm_susp_link";
const uint8_t gm_sukutl224cgkm[8] = "addr-of";
const uint8_t gm_sum9vj65rg4jb[4] = "fn(";
const uint8_t gm_sup9af88eqfm1[25] = "`subdir` of dependency `";
const uint8_t gm_suv2tallru6a1[2] = "A";
const uint8_t gm_sv19dj08f7d6b[45] = "{}({}, 0xcbf29ce484222325ULL ^ (uint64_t){})";
const uint8_t gm_sv4cnc01vnmra[11] = "gm_cover_n";
const uint8_t gm_sv9jtudu9cu4k[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\026\001\000\000\033\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_svbe9btc37i4e[13] = "No such file";
const uint8_t gm_svgqk0opgf2oj[60] = ") == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, \"";
const uint8_t gm_svgsom6hbqtcn[40] = "    return GM_RUN_MAIN(gm_entry0, cx);\012";
const uint8_t gm_svlu840bnmh08[10] = "GM_COVER(";
const uint8_t gm_svm0rfaqk3b2q[4] = "..=";
const uint8_t gm_svn0tfk5ehe5n[48] = "`return` is not allowed in a deferred statement";
const uint8_t gm_svo0v47tsnb8b[14] = "` needs `url`";
const uint8_t gm_svo7nu0hjqbeg[11] = "gm_fmt_seq";
const uint8_t gm_svqbt6tnehohi[14] = " placeholders";
const uint8_t gm_svqdrhms9i231[7] = "effect";
uint8_t gm_c08htgempbt[7] __attribute__((aligned(1)));
uint8_t gm_c0qkheasoov[17] __attribute__((aligned(1)));
uint8_t gm_c137q3t6vnc[9] __attribute__((aligned(1)));
uint8_t gm_c19g30clc28[5] __attribute__((aligned(1)));
uint8_t gm_c1cb3lujqna[801] __attribute__((aligned(1)));
uint8_t gm_c1se330bih6[13] __attribute__((aligned(1)));
uint8_t gm_c2mocn1cpdr[4] __attribute__((aligned(1)));
uint8_t gm_c34nm3e2f7b[3] __attribute__((aligned(1)));
uint8_t gm_c387ahiebcp[7] __attribute__((aligned(1)));
uint8_t gm_c4lell1pi73[17] __attribute__((aligned(1)));
uint8_t gm_c5655r183jp[24] __attribute__((aligned(1)));
uint8_t gm_c8a5fuunssu[14] __attribute__((aligned(1)));
uint8_t gm_c8fctfks9t5[15] __attribute__((aligned(1)));
uint8_t gm_c935g5d7vau[4] __attribute__((aligned(1)));
uint8_t gm_c94f17fblj5[1] __attribute__((aligned(1)));
uint8_t gm_c956v7aird0[8] __attribute__((aligned(8)));
uint8_t gm_c97f5362n6p[8] __attribute__((aligned(1)));
uint8_t gm_c9n447mn3q9[5] __attribute__((aligned(1)));
uint8_t gm_c9rkig62cpc[30] __attribute__((aligned(1)));
uint8_t gm_ca95asi83lb[10] __attribute__((aligned(1)));
uint8_t gm_ca9ikdfm7p4[4] __attribute__((aligned(4)));
uint8_t gm_cab7j216uqg[12] __attribute__((aligned(1)));
uint8_t gm_cagddvvpjek[10] __attribute__((aligned(1)));
uint8_t gm_cak4bln8pr2[10] __attribute__((aligned(1)));
uint8_t gm_caqt8iiohcr[12] __attribute__((aligned(1)));
uint8_t gm_cb1h8mol9po[16] __attribute__((aligned(8)));
uint8_t gm_cb4pf8mh5iq[1] __attribute__((aligned(8)));
uint8_t gm_cbqhferh238[12] __attribute__((aligned(1)));
uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
uint8_t gm_cdhp3orov5g[5] __attribute__((aligned(1)));
uint8_t gm_cdu58t719v0[6] __attribute__((aligned(1)));
uint8_t gm_cepf08kh92d[97] __attribute__((aligned(1)));
uint8_t gm_cfea4sug7gd[8] __attribute__((aligned(8)));
uint8_t gm_cfeq13tqi9u[10] __attribute__((aligned(1)));
uint8_t gm_cfhpe9hto1b[14] __attribute__((aligned(1)));
uint8_t gm_cfiqcgtjhen[416] __attribute__((aligned(8)));
uint8_t gm_cggqgl3b8fs[3] __attribute__((aligned(1)));
uint8_t gm_cgnn9n3q0cc[4] __attribute__((aligned(1)));
uint8_t gm_ch37hhcaakv[12] __attribute__((aligned(1)));
uint8_t gm_cim1n3o1mct[8] __attribute__((aligned(1)));
uint8_t gm_cjdu4hivd9h[16] __attribute__((aligned(8)));
uint8_t gm_ckh5e429gka[8] __attribute__((aligned(8)));
uint8_t gm_cl4aag70hg2[15] __attribute__((aligned(1)));
uint8_t gm_cmd4bh5vti6[11] __attribute__((aligned(1)));
uint8_t gm_cmdcrpgn3bo[6] __attribute__((aligned(1)));
uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
uint8_t gm_cml190pi1go[15] __attribute__((aligned(1)));
uint8_t gm_cmo2h50nb4b[12] __attribute__((aligned(1)));
uint8_t gm_co1jgvp4ags[39] __attribute__((aligned(1)));
uint8_t gm_cpb6bb227bn[11] __attribute__((aligned(1)));
uint8_t gm_cqb1jv05g6h[6] __attribute__((aligned(1)));
uint8_t gm_cqppk8qnrpu[17] __attribute__((aligned(1)));
uint8_t gm_cr046dlv3ko[4] __attribute__((aligned(1)));
uint8_t gm_ct2qmh8qddo[7] __attribute__((aligned(1)));
uint8_t gm_ctfv30s4j48[11] __attribute__((aligned(1)));
uint8_t gm_ctnl7besb79[13] __attribute__((aligned(1)));
uint8_t gm_ctru94s97vo[4] __attribute__((aligned(1)));
uint8_t gm_cuncmusjbv3[10] __attribute__((aligned(1)));
uint8_t gm_k0kae0be6jj[40] __attribute__((aligned(8)));
uint8_t gm_k26b4lfjb04[40] __attribute__((aligned(8)));
uint8_t gm_k6tu981lmau[40] __attribute__((aligned(8)));
uint8_t gm_k9nqfo1idrl[40] __attribute__((aligned(8)));
uint8_t gm_kecscp3is96[40] __attribute__((aligned(8)));
uint8_t gm_kefd26qglb8[40] __attribute__((aligned(8)));
uint8_t gm_kequv58cbtg[40] __attribute__((aligned(8)));
uint8_t gm_khouror17c2[40] __attribute__((aligned(8)));
uint8_t gm_ki0a6rpk4hh[40] __attribute__((aligned(8)));
uint8_t gm_kif5rfegp72[40] __attribute__((aligned(8)));
uint8_t gm_kjn96jdce98[40] __attribute__((aligned(8)));
uint8_t gm_kmgr5pk3qpo[40] __attribute__((aligned(8)));
uint8_t gm_kp1liaipvo1[40] __attribute__((aligned(8)));
uint8_t gm_kp3nl0er6il[40] __attribute__((aligned(8)));
uint8_t gm_ks1bcpoe2h2[40] __attribute__((aligned(8)));
uint8_t gm_ktv7kfnnmgp[40] __attribute__((aligned(8)));
uint8_t gm_kvk9mrlml9b[40] __attribute__((aligned(8)));
void *const gm_hdu8v6n5i2qd[] = {(void *)gm_dtu8v6n5i2qd_0, (void *)gm_dtu8v6n5i2qd_1, (void *)gm_dtu8v6n5i2qd_2, (void *)gm_dtu8v6n5i2qd_3, (void *)gm_dtu8v6n5i2qd_4, (void *)gm_dtu8v6n5i2qd_5, (void *)gm_dtu8v6n5i2qd_6, (void *)gm_dtu8v6n5i2qd_7, (void *)gm_dtu8v6n5i2qd_8, (void *)gm_dtu8v6n5i2qd_9, (void *)gm_dtu8v6n5i2qd_10, (void *)gm_dtu8v6n5i2qd_11, (void *)gm_dtu8v6n5i2qd_12, (void *)gm_dtu8v6n5i2qd_13, (void *)gm_dtu8v6n5i2qd_14, (void *)gm_dtu8v6n5i2qd_15, (void *)gm_dtu8v6n5i2qd_16, (void *)gm_dtu8v6n5i2qd_17, (void *)gm_dtu8v6n5i2qd_18, (void *)gm_dtu8v6n5i2qd_19, (void *)gm_dtu8v6n5i2qd_20, (void *)gm_dtu8v6n5i2qd_21};
void *const gm_hl8alodt65ii[] = {(void *)gm_f_for_body_1b63vnj};
void *const gm_hlj1in77f861[] = {(void *)gm_f_for_body_hb9k0f9};
void *const gm_hlk5mm1461td[] = {(void *)gm_f_for_body_t3rkkf2};

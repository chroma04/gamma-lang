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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; } gm_t33jkjo5anlut;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t33jkjo5anlut) == 24 && _Alignof(gm_t33jkjo5anlut) == 4 && offsetof(gm_t33jkjo5anlut, f0) == 0 && offsetof(gm_t33jkjo5anlut, f1) == 4 && offsetof(gm_t33jkjo5anlut, f2) == 8 && offsetof(gm_t33jkjo5anlut, f3) == 12 && offsetof(gm_t33jkjo5anlut, f4) == 16 && offsetof(gm_t33jkjo5anlut, f5) == 20, "layout of `(u32, u32, u32, u32, u32, u32)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_tl874t2atnlh3 f3; gm_tl874t2atnlh3 f4; gm_b16_8 f5; } gm_t80lhgi01aeei;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_tl874t2atnlh3 f0; gm_t9805qtonbsag f1; } gm_tdo2c70b3n0vq;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
typedef struct { gm_trqfvmjtt4sij f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_trqfvmjtt4sij f5; gm_t4c7ipjj9s3fp f6; gm_b16_8 f7; gm_b16_8 f8; } gm_t6vagfvc3bffs;
_Static_assert(sizeof(gm_t80lhgi01aeei) == 80 && _Alignof(gm_t80lhgi01aeei) == 8 && offsetof(gm_t80lhgi01aeei, f0) == 0 && offsetof(gm_t80lhgi01aeei, f1) == 16 && offsetof(gm_t80lhgi01aeei, f2) == 24 && offsetof(gm_t80lhgi01aeei, f3) == 40 && offsetof(gm_t80lhgi01aeei, f4) == 52 && offsetof(gm_t80lhgi01aeei, f5) == 64, "layout of `DocSymbol`");
_Static_assert(sizeof(gm_tdo2c70b3n0vq) == 20 && _Alignof(gm_tdo2c70b3n0vq) == 4 && offsetof(gm_tdo2c70b3n0vq, f0) == 0 && offsetof(gm_tdo2c70b3n0vq, f1) == 12, "layout of `(span: Span, info: TypeInfo)`");
typedef struct { gm_t2p74ki8sl4an f0; uint8_t f1; uint8_t pe[3]; } gm_tdt98rnrk0psp;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
typedef struct { gm_tdo2c70b3n0vq f0; uint8_t f1; uint8_t pe[3]; } gm_tlsv15umjjhme;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint32_t f0; uint32_t f1; gm_toceffb697p0u f2; } gm_tu6ladb6432hk;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
_Static_assert(sizeof(gm_t6vagfvc3bffs) == 360 && _Alignof(gm_t6vagfvc3bffs) == 8 && offsetof(gm_t6vagfvc3bffs, f0) == 0 && offsetof(gm_t6vagfvc3bffs, f1) == 40 && offsetof(gm_t6vagfvc3bffs, f2) == 96 && offsetof(gm_t6vagfvc3bffs, f3) == 136 && offsetof(gm_t6vagfvc3bffs, f4) == 176 && offsetof(gm_t6vagfvc3bffs, f5) == 232 && offsetof(gm_t6vagfvc3bffs, f6) == 272 && offsetof(gm_t6vagfvc3bffs, f7) == 328 && offsetof(gm_t6vagfvc3bffs, f8) == 344, "layout of `IdeIndex`");
typedef struct { gm_t6ok5tvcjc200 f0; uint8_t f1; uint8_t pe[7]; } gm_t80ma80d0n3ke;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_t6ok5tvcjc200 f1; } gm_t8scu1osgcra5;
_Static_assert(sizeof(gm_tdt98rnrk0psp) == 28 && _Alignof(gm_tdt98rnrk0psp) == 4 && offsetof(gm_tdt98rnrk0psp, f0) == 0 && offsetof(gm_tdt98rnrk0psp, f1) == 24, "layout of `?Ref`");
typedef struct { gm_tl5cuhjfnc4cr f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tfgiovingnnea;
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tlsv15umjjhme) == 24 && _Alignof(gm_tlsv15umjjhme) == 4 && offsetof(gm_tlsv15umjjhme, f0) == 0 && offsetof(gm_tlsv15umjjhme, f1) == 20, "layout of `?(span: Span, info: TypeInfo)`");
_Static_assert(sizeof(gm_tu6ladb6432hk) == 24 && _Alignof(gm_tu6ladb6432hk) == 4 && offsetof(gm_tu6ladb6432hk, f0) == 0 && offsetof(gm_tu6ladb6432hk, f1) == 4 && offsetof(gm_tu6ladb6432hk, f2) == 8, "layout of `Narrowest`");
_Static_assert(sizeof(gm_t80ma80d0n3ke) == 48 && _Alignof(gm_t80ma80d0n3ke) == 8 && offsetof(gm_t80ma80d0n3ke, f0) == 0 && offsetof(gm_t80ma80d0n3ke, f1) == 40, "layout of `?Hover`");
_Static_assert(sizeof(gm_t8scu1osgcra5) == 56 && _Alignof(gm_t8scu1osgcra5) == 8 && offsetof(gm_t8scu1osgcra5, f0) == 0 && offsetof(gm_t8scu1osgcra5, f1) == 16, "layout of `HoverResult`");
typedef struct { gm_t8scu1osgcra5 f0; uint8_t f1; uint8_t pe[7]; } gm_teijtv56moffm;
_Static_assert(sizeof(gm_tfgiovingnnea) == 88 && _Alignof(gm_tfgiovingnnea) == 8 && offsetof(gm_tfgiovingnnea, f0) == 0 && offsetof(gm_tfgiovingnnea, f1) == 64 && offsetof(gm_tfgiovingnnea, f2) == 76 && offsetof(gm_tfgiovingnnea, f3) == 80, "layout of `LocalScope`");
_Static_assert(sizeof(gm_teijtv56moffm) == 64 && _Alignof(gm_teijtv56moffm) == 8 && offsetof(gm_teijtv56moffm, f0) == 0 && offsetof(gm_teijtv56moffm, f1) == 56, "layout of `?HoverResult`");
struct gm_fr_consider_8v0icu0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_consider_symbols_ds19ph7 {
    gm_frame h;
    gm_t80lhgi01aeei l4; /* s */
    gm_b0_1 ret;
};
struct gm_fr_declared_at_31b77n7 {
    gm_frame h;
    gm_tu6ladb6432hk l3; /* n */
    gm_closure l4; /* consider */
    gm_tfgiovingnnea l10; /* sc */
    gm_toceffb697p0u ret;
    gm_b0_1 w2;
};
struct gm_fr_definition_at_9jj9uuu {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_finish_gv1a795 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_40fde41 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_contains_fesi73m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_enqel4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_3en4m89 {
    gm_frame h;
    gm_t8scu1osgcra5 l0; /* <item> */
    gm_t8scu1osgcra5 l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_g1u7f51 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tprkpoe {
    gm_frame h;
    gm_t8scu1osgcra5 l0; /* <item> */
    gm_t8scu1osgcra5 l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_hover_at_19okc4g {
    gm_frame h;
    gm_teijtv56moffm l3; /* by_use */
    gm_t6ok5tvcjc200 l10; /* h */
    gm_teijtv56moffm l11; /* by_def */
    gm_tu6ladb6432hk l12; /* def */
    gm_teijtv56moffm l14; /* named */
    gm_t8scu1osgcra5 l15; /* a */
    gm_t8scu1osgcra5 l16; /* b */
    gm_tlsv15umjjhme l17; /* typed */
    gm_tu6ladb6432hk l18; /* ty */
    gm_t8scu1osgcra5 l20; /* n */
    gm_teijtv56moffm ret;
    gm_b0_1 w1;
    gm_b0_1 w2;
};
struct gm_fr_index_nki2g2t {
    gm_frame h;
    gm_t6vagfvc3bffs ret;
};
struct gm_fr_locals_at_2n5h0d6 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tfgiovingnnea l6; /* s */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_members_at_do67lp0 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_narrowest_around_10pkumk {
    gm_frame h;
    gm_tu6ladb6432hk ret;
};
struct gm_fr_offer_lap4ju5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_outline_of_01eujq2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_uses_of_nalefva {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_width_spp7n92 {
    gm_frame h;
    uint32_t ret;
};
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_consider_8v0icu0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0);
__attribute__((aligned(4))) void gm_f_consider_symbols_ds19ph7(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_declared_at_31b77n7(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint64_t gm_f_dedup_ogel4tf(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_dedup_thticse(gm_b16_8 a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_definition_at_9jj9uuu(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6ld1tun(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4_3(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_e4miqcm_si7fq37(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_e4miqcm_sjd4qkf(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_lr25p57_sipsive(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_finish_gv1a795(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_fn_40fde41(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_contains_fesi73m(gm_cseahvg8v7p0vs a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint8_t gm_f_fn_enqel4h(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3en4m89(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g1u7f51(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tprkpoe(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_get_2ciof6m(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_get_7o29gjt(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_hash_map_apl3mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_glsl7u2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_lc3d6ha(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hover_at_19okc4g(void *gm_out, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_teijtv56moffm gm_f_hover_at_19okc4g_c(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_index_nki2g2t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_bpm3e45(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_h7regd0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_umenr17(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_locals_at_2n5h0d6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_members_at_do67lp0(void *gm_out, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_members_at_do67lp0_c(void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_narrowest_around_10pkumk(void *gm_out, uint32_t a0, uint32_t a1);
__attribute__((aligned(4))) gm_tu6ladb6432hk gm_f_narrowest_around_10pkumk_c(uint32_t a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_offer_lap4ju5(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_outline_of_01eujq2(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sige3eu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_sort_by_0sodivd(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_sort_by_5e667o9(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_truncate_cvbnnli(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_truncate_qtpovqr(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_uses_of_nalefva(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) uint32_t gm_f_width_spp7n92(gm_cseahvg8v7p0vs a0);
bool gm_lt9v0qhqsmrd(const uint8_t *a, const uint8_t *b);
bool gm_ltiv63rof2vl(const uint8_t *a, const uint8_t *b);
extern void *const gm_hli2tfvgok6p[];
extern void *const gm_hlpd8uohstag[];
extern void *const gm_hlpn5rfhnj54[];
extern const char gm_srck9pqpjjfjkmq[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_consider_8v0icu0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0) {
    struct gm_fr_consider_8v0icu0 F;
    gm_tl874t2atnlh3 gm_l0; /* s */
    GM_STACK_CHECK(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            {
                void * t8 = (void *)((uint8_t *)&((struct gm_fr_declared_at_31b77n7 *)((&F.h)->up))->l3);
                gm_tl874t2atnlh3 t9 = gm_l0;
                uint8_t t10 = gm_f_offer_lap4ju5(t8, GM_CONV(gm_cseahvg8v7p0vs, t9));
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
inline __attribute__((aligned(4))) void gm_f_consider_symbols_ds19ph7(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_consider_symbols_ds19ph7 F;
    gm_b16_8 gm_l0; /* syms */
    gm_closure gm_l1; /* consider */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
                    gm_t80lhgi01aeei t15 = GM_LD(gm_t80lhgi01aeei, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 80));
                    F.l4 = t15;
                    {
                        gm_closure t17 = gm_l1;
                        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 52));
                        gm_closure t19 = t17;
                        gm_node *ccx20 = t19.cx;
                        void * t21 = t19.code;
                        gm_b0_1 t22;
                        if (GM_PTR_TAGGED(t21)) {
                            ((void (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t21))(GM_CONV(gm_cseahvg8v7p0vs, t18));
                        } else {
                            t22 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t21)(t19.env, ccx20, NULL, t18);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 64));
                        gm_b16_8 t24 = t23;
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                        gm_closure t26 = gm_l1;
                        gm_f_consider_symbols_ds19ph7(t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 239, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
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
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_declared_at_31b77n7(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declared_at_31b77n7 F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t2p74ki8sl4an gm_l7; /* r */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_tiilmfd16v3fc gm_l14; /* o */
    GM_STACK_CHECK(&F, 861u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint32_t t3 = gm_l2;
        gm_f_narrowest_around_10pkumk((void *)&F.l3, t2, t3);
    }
    {
        gm_closure t4 = ((gm_closure){(void *)gm_f_consider_8v0icu0, (void *)&F.h, cx});
        F.l4 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t6) + 0) + 0));
            gm_l5 = t7;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l6;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l6;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t2p74ki8sl4an t19 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t15 * 24));
                    gm_l7 = t19;
                    {
                        gm_closure t21 = F.l4;
                        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 12));
                        gm_closure t23 = t21;
                        gm_node *ccx24 = t23.cx;
                        void * t25 = t23.code;
                        gm_b0_1 t26;
                        if (GM_PTR_TAGGED(t25)) {
                            ((void (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t25))(GM_CONV(gm_cseahvg8v7p0vs, t22));
                        } else {
                            t26 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t25)(t23.env, ccx24, NULL, t22);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 229, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t28 = gm_l6;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l6 = t31;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
        }
    }
    {
        {
            void * t33 = gm_l0;
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t33) + 136) + 0));
            gm_l8 = t34;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t36 = gm_l9;
                uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t38 = (t36 < t37);
                if (!t38) goto lb35;
                {
                    uint64_t t41 = gm_l9;
                    uint64_t t42 = (uintptr_t)t41;
                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tfgiovingnnea t46 = GM_LD(gm_tfgiovingnnea, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t42 * 88));
                    F.l10 = t46;
                    {
                        gm_closure t48 = F.l4;
                        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 64));
                        gm_closure t50 = t48;
                        gm_node *ccx51 = t50.cx;
                        void * t52 = t50.code;
                        gm_b0_1 t53;
                        if (GM_PTR_TAGGED(t52)) {
                            ((void (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t52))(GM_CONV(gm_cseahvg8v7p0vs, t49));
                        } else {
                            t53 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t52)(t50.env, ccx51, NULL, t49);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                    }
                    goto me44;
mn45:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 230, 5);
me44:;
                }
lc35:;
                {
                    uint64_t t55 = gm_l9;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l9 = t58;
                }
            }
gm_ll39:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
            }
            goto gm_lad;
lb35:;
        }
    }
    {
        {
            void * t60 = gm_l0;
            void * t61 = (void *)(((uint8_t *)t60) + 40);
            gm_l11 = t61;
        }
        gm_hval t62 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn63 = {.eff = 1957790874, .kind = GM_LIT, .clauses = gm_hlpn5rfhnj54, .env = t62.env, .hctx = t62.cx, .with_frame = &F.h, .with_label = 2, .result = (void *)&F.w2, .fw = NULL, .next = cx};
        void * t66 = gm_l11;
        gm_b0_1 t67 = gm_f_entries_e4miqcm_si7fq37((void *)NULL, (&wn63), NULL, t66);
        if (gm_unw.kind) {
            goto gm_ll65;
        }
        F.w2 = t67;
        goto gm_ls68;
gm_ll65:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 2) { gm_unw.kind = GM_NONE; goto we64; }
        goto gm_lad;
gm_ls68:;
we64:;
        gm_b0_1 t69 = F.w2;
        gm_b0_1 t70 = t69;
    }
    {
        {
            void * t72 = gm_l0;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((((uint8_t *)t72) + 96) + 0));
            gm_l12 = t73;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t75 = gm_l13;
                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t77 = (t75 < t76);
                if (!t77) goto lb74;
                {
                    uint64_t t80 = gm_l13;
                    uint64_t t81 = (uintptr_t)t80;
                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_tiilmfd16v3fc t85 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t81 * 24));
                    gm_l14 = t85;
                    {
                        uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 0));
                        uint32_t t89 = gm_l1;
                        uint8_t t90 = ((t88 == t89));
                        if (!t90) goto ie87;
                        {
                            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                            gm_b16_8 t93 = t92;
                            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                            gm_closure t95 = F.l4;
                            gm_f_consider_symbols_ds19ph7(t94, t95);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                        }
                        goto ix87;
ie87:;
ix87:;
                    }
                    goto me83;
mn84:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 232, 5);
me83:;
                }
lc74:;
                {
                    uint64_t t97 = gm_l13;
                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                    uint64_t t100 = r98;
                    gm_l13 = t100;
                }
            }
gm_ll78:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
            }
            goto gm_lad;
lb74:;
        }
    }
    {
        gm_toceffb697p0u t101 = GM_LD(gm_toceffb697p0u, (((uint8_t *)&F.l3) + 8));
        uint8_t t102 = 0;
        F.ret = t101;
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_definition_at_9jj9uuu(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_definition_at_9jj9uuu F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_tdt98rnrk0psp gm_l3; /* best */
    gm_tu6ladb6432hk gm_l4; /* use */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t2p74ki8sl4an gm_l7; /* r */
    gm_t2p74ki8sl4an gm_l8; /* b */
    GM_STACK_CHECK(&F, 472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tdt98rnrk0psp t2;
        memset(&t2, 0, sizeof t2);
        gm_l3 = t2;
    }
    {
        uint32_t t3 = gm_l1;
        uint32_t t4 = gm_l2;
        gm_f_narrowest_around_10pkumk((void *)&gm_l4, t3, t4);
    }
    {
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t6) + 0) + 0));
            gm_l5 = t7;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l6;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l6;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t2p74ki8sl4an t19 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t15 * 24));
                    gm_l7 = t19;
                    {
                        void * t22 = (void *)((uint8_t *)&gm_l4);
                        gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 0));
                        uint8_t t24 = gm_f_offer_lap4ju5(t22, GM_CONV(gm_cseahvg8v7p0vs, t23));
                        if (!t24) goto ie21;
                        {
                            gm_t2p74ki8sl4an t26 = gm_l7;
                            gm_tdt98rnrk0psp t27;
                            GM_ST(uint8_t, (uint8_t *)&t27 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t27 + 26, 0);
                            GM_ST(gm_t2p74ki8sl4an, ((uint8_t *)&t27), t26);
                            ((uint8_t *)&t27)[24] = 1;
                            gm_l3 = t27;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 215, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t29 = gm_l6;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l6 = t32;
                }
            }
lb8:;
        }
    }
    {
        if (!((((uint8_t *)&gm_l3))[24] != 0)) goto ie33;
        gm_t2p74ki8sl4an t34 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)&gm_l3));
        gm_l8 = t34;
        {
            gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 12));
            gm_toceffb697p0u t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t37), t36);
            ((uint8_t *)&t37)[12] = 1;
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t39 = gm_l0;
        uint32_t t40 = gm_l1;
        uint32_t t41 = gm_l2;
        gm_toceffb697p0u t42 = gm_f_declared_at_31b77n7(t39, t40, t41);
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_finish_gv1a795(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_finish_gv1a795 F;
    void * gm_l0; /* ix */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 0) + 0));
        gm_closure t4 = ((gm_closure){GM_PTR_TAG(gm_f_fn_enqel4h), NULL, cx});
        gm_f_sort_by_0sodivd(t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 0);
        void * t7 = gm_l0;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 0) + 0));
        uint64_t t9 = gm_f_dedup_thticse(t8);
        gm_f_truncate_cvbnnli(t6, t9);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_40fde41(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_40fde41 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 4));
    void * t5 = gm_l0;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 8));
    gm_tl874t2atnlh3 t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 8), t6);
    void * t8 = gm_l1;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 0));
    void * t10 = gm_l1;
    uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)t10) + 4));
    void * t12 = gm_l1;
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 8));
    gm_tl874t2atnlh3 t14;
    GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t9);
    GM_ST(uint32_t, ((uint8_t *)&t14 + 4), t11);
    GM_ST(uint32_t, ((uint8_t *)&t14 + 8), t13);
    gm_tl874t2atnlh3 t15 = t7;
    gm_tl874t2atnlh3 t16 = t14;
    uint8_t t17 = (uint8_t)gm_lt9v0qhqsmrd(((uint8_t *)&t15), ((uint8_t *)&t16));
    F.ret = t17;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_contains_fesi73m(gm_cseahvg8v7p0vs a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_contains_fesi73m F;
    gm_tl874t2atnlh3 gm_l0; /* s */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
    uint32_t t4 = gm_l1;
    uint8_t t5 = ((t3 == t4));
    if (t5) {
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        uint32_t t7 = gm_l2;
        uint8_t t8 = (t6 <= t7);
        t2 = t8;
    } else t2 = 0;
    if (t2) {
        uint32_t t9 = gm_l2;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t11 = (t9 <= t10);
        t1 = t11;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_enqel4h(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_enqel4h F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 259u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t33jkjo5anlut t1;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((((uint8_t *)t2) + 0) + 0));
    GM_ST(uint32_t, (uint8_t *)&t1 + 0, t3);
    void * t4 = gm_l0;
    uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)t4) + 0) + 4));
    GM_ST(uint32_t, (uint8_t *)&t1 + 4, t5);
    void * t6 = gm_l0;
    uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)t6) + 0) + 8));
    GM_ST(uint32_t, (uint8_t *)&t1 + 8, t7);
    void * t8 = gm_l0;
    uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)t8) + 12) + 0));
    GM_ST(uint32_t, (uint8_t *)&t1 + 12, t9);
    void * t10 = gm_l0;
    uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)t10) + 12) + 4));
    GM_ST(uint32_t, (uint8_t *)&t1 + 16, t11);
    void * t12 = gm_l0;
    uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t12) + 12) + 8));
    GM_ST(uint32_t, (uint8_t *)&t1 + 20, t13);
    gm_t33jkjo5anlut t14;
    void * t15 = gm_l1;
    uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)t15) + 0) + 0));
    GM_ST(uint32_t, (uint8_t *)&t14 + 0, t16);
    void * t17 = gm_l1;
    uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)t17) + 0) + 4));
    GM_ST(uint32_t, (uint8_t *)&t14 + 4, t18);
    void * t19 = gm_l1;
    uint32_t t20 = GM_LD(uint32_t, ((((uint8_t *)t19) + 0) + 8));
    GM_ST(uint32_t, (uint8_t *)&t14 + 8, t20);
    void * t21 = gm_l1;
    uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t21) + 12) + 0));
    GM_ST(uint32_t, (uint8_t *)&t14 + 12, t22);
    void * t23 = gm_l1;
    uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t23) + 12) + 4));
    GM_ST(uint32_t, (uint8_t *)&t14 + 16, t24);
    void * t25 = gm_l1;
    uint32_t t26 = GM_LD(uint32_t, ((((uint8_t *)t25) + 12) + 8));
    GM_ST(uint32_t, (uint8_t *)&t14 + 20, t26);
    gm_t33jkjo5anlut t27 = t1;
    gm_t33jkjo5anlut t28 = t14;
    uint8_t t29 = (uint8_t)gm_ltiv63rof2vl(((uint8_t *)&t27), ((uint8_t *)&t28));
    F.ret = t29;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3en4m89(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_3en4m89 F;
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t8scu1osgcra5 t3 = GM_LD(gm_t8scu1osgcra5, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        gm_closure t5 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_declared_at_31b77n7 *)((&F.h)->up))->l4));
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 0));
        gm_closure t7 = t5;
        gm_node *ccx8 = t7.cx;
        void * t9 = t7.code;
        gm_b0_1 t10;
        if (GM_PTR_TAGGED(t9)) {
            ((void (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t9))(GM_CONV(gm_cseahvg8v7p0vs, t6));
        } else {
            t10 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t9)(t7.env, ccx8, NULL, t6);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 231, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g1u7f51(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_g1u7f51 F;
    gm_tdo2c70b3n0vq gm_l0; /* <item> */
    gm_tdo2c70b3n0vq gm_l1; /* kv */
    GM_STACK_CHECK(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tdo2c70b3n0vq t3 = GM_LD(gm_tdo2c70b3n0vq, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)&gm_l1) + 12) + 4));
        if (!t6) goto ie5;
        uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 0) + 8));
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 0) + 4));
        uint8_t t9 = (t7 > t8);
        if (!t9) goto ie5;
        void * t10 = (void *)((uint8_t *)&((struct gm_fr_hover_at_19okc4g *)((&F.h)->up))->l18);
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l1) + 0));
        uint8_t t12 = gm_f_offer_lap4ju5(t10, GM_CONV(gm_cseahvg8v7p0vs, t11));
        if (!t12) goto ie5;
        {
            gm_tdo2c70b3n0vq t14;
            gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l1) + 0));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t14 + 0, t15);
            gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 12));
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t14 + 12, t16);
            gm_tlsv15umjjhme t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 21, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 22, 0);
            GM_ST(gm_tdo2c70b3n0vq, ((uint8_t *)&t17), t14);
            ((uint8_t *)&t17)[20] = 1;
            GM_ST(gm_tlsv15umjjhme, ((uint8_t *)&((struct gm_fr_hover_at_19okc4g *)((&F.h)->up))->l17), t17);
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 193, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tprkpoe(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_tprkpoe F;
    GM_STACK_CHECK(&F, 209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t8scu1osgcra5 t3 = GM_LD(gm_t8scu1osgcra5, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t6 = (void *)((uint8_t *)&((struct gm_fr_hover_at_19okc4g *)((&F.h)->up))->l12);
        gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 0));
        uint8_t t8 = gm_f_offer_lap4ju5(t6, GM_CONV(gm_cseahvg8v7p0vs, t7));
        if (!t8) goto ie5;
        {
            gm_t8scu1osgcra5 t10;
            memset((uint8_t *)&t10 + 12, 0, 4);
            gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 0));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t10 + 0, t11);
            memcpy((uint8_t *)&t10 + 16, (((uint8_t *)&F.l1) + 16), 40);
            gm_teijtv56moffm t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 60, 0);
            GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t12), t10);
            ((uint8_t *)&t12)[56] = 1;
            GM_ST(gm_teijtv56moffm, ((uint8_t *)&((struct gm_fr_hover_at_19okc4g *)((&F.h)->up))->l11), t12);
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 178, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_hover_at_19okc4g(void *gm_out, void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hover_at_19okc4g F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_tdt98rnrk0psp gm_l4; /* narrowest */
    gm_tu6ladb6432hk gm_l5; /* use */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t2p74ki8sl4an gm_l8; /* r */
    gm_t2p74ki8sl4an gm_l9; /* r */
    void * gm_l13; /* <tmp> */
    void * gm_l19; /* <tmp> */
    gm_tdo2c70b3n0vq gm_l21; /* t */
    gm_tdo2c70b3n0vq gm_l22; /* t */
    GM_STACK_CHECK(&F, 2532u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_teijtv56moffm t2;
        memset(&t2, 0, sizeof t2);
        F.l3 = t2;
    }
    {
        gm_tdt98rnrk0psp t3;
        memset(&t3, 0, sizeof t3);
        gm_l4 = t3;
    }
    {
        uint32_t t4 = gm_l1;
        uint32_t t5 = gm_l2;
        gm_f_narrowest_around_10pkumk((void *)&gm_l5, t4, t5);
    }
    {
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 0) + 0));
            gm_l6 = t8;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l7;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l7;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t2p74ki8sl4an t20 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t16 * 24));
                    gm_l8 = t20;
                    {
                        void * t23 = (void *)((uint8_t *)&gm_l5);
                        gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 0));
                        uint8_t t25 = gm_f_offer_lap4ju5(t23, GM_CONV(gm_cseahvg8v7p0vs, t24));
                        if (!t25) goto ie22;
                        {
                            gm_t2p74ki8sl4an t27 = gm_l8;
                            gm_tdt98rnrk0psp t28;
                            GM_ST(uint8_t, (uint8_t *)&t28 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t28 + 26, 0);
                            GM_ST(gm_t2p74ki8sl4an, ((uint8_t *)&t28), t27);
                            ((uint8_t *)&t28)[24] = 1;
                            gm_l4 = t28;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 170, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t30 = gm_l7;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l7 = t33;
                }
            }
lb9:;
        }
    }
    {
        if (!((((uint8_t *)&gm_l4))[24] != 0)) goto ie34;
        gm_t2p74ki8sl4an t35 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)&gm_l4));
        gm_l9 = t35;
        {
            void * t38 = gm_l0;
            void * t39 = (void *)(((uint8_t *)t38) + 40);
            gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 12));
            gm_t80ma80d0n3ke t41;
            gm_f_get_2ciof6m((void *)&t41, t39, GM_CONV(gm_cseahvg8v7p0vs, t40));
            gm_t80ma80d0n3ke t42 = t41;
            if (!((((uint8_t *)&t42))[40] != 0)) goto ie37;
            gm_t6ok5tvcjc200 t43 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)&t42));
            F.l10 = t43;
            {
                gm_t8scu1osgcra5 t45;
                memset((uint8_t *)&t45 + 12, 0, 4);
                gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 0));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t45 + 0, t46);
                memcpy((uint8_t *)&t45 + 16, ((uint8_t *)&F.l10), 40);
                gm_teijtv56moffm t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 60, 0);
                GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t47), t45);
                ((uint8_t *)&t47)[56] = 1;
                F.l3 = t47;
            }
            goto ix37;
ie37:;
ix37:;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        gm_teijtv56moffm t48;
        memset(&t48, 0, sizeof t48);
        F.l11 = t48;
    }
    {
        uint32_t t49 = gm_l1;
        uint32_t t50 = gm_l2;
        gm_f_narrowest_around_10pkumk((void *)&F.l12, t49, t50);
    }
    {
        {
            void * t52 = gm_l0;
            void * t53 = (void *)(((uint8_t *)t52) + 40);
            gm_l13 = t53;
        }
        gm_hval t54 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn55 = {.eff = 1957790874, .kind = GM_LIT, .clauses = gm_hli2tfvgok6p, .env = t54.env, .hctx = t54.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        void * t58 = gm_l13;
        gm_b0_1 t59 = gm_f_entries_e4miqcm_sjd4qkf((void *)NULL, (&wn55), NULL, t58);
        if (gm_unw.kind) {
            goto gm_ll57;
        }
        F.w1 = t59;
        goto gm_ls60;
gm_ll57:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we56; }
        goto gm_lad;
gm_ls60:;
we56:;
        gm_b0_1 t61 = F.w1;
        gm_b0_1 t62 = t61;
    }
    {
        gm_teijtv56moffm t63;
        memset(&t63, 0, sizeof t63);
        F.l14 = t63;
    }
    {
        if (!((((uint8_t *)&F.l3))[56] != 0)) goto ie64;
        gm_t8scu1osgcra5 t65 = GM_LD(gm_t8scu1osgcra5, ((uint8_t *)&F.l3));
        F.l15 = t65;
        {
            if (!((((uint8_t *)&F.l11))[56] != 0)) goto ie67;
            gm_t8scu1osgcra5 t68 = GM_LD(gm_t8scu1osgcra5, ((uint8_t *)&F.l11));
            F.l16 = t68;
            {
                gm_teijtv56moffm t70;
                gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l16) + 0));
                uint32_t t73 = gm_f_width_spp7n92(GM_CONV(gm_cseahvg8v7p0vs, t72));
                gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l15) + 0));
                uint32_t t75 = gm_f_width_spp7n92(GM_CONV(gm_cseahvg8v7p0vs, t74));
                uint8_t t76 = (t73 < t75);
                if (!t76) goto ie71;
                gm_t8scu1osgcra5 t78 = F.l16;
                gm_t8scu1osgcra5 t79 = t78;
                gm_teijtv56moffm t80;
                GM_ST(uint8_t, (uint8_t *)&t80 + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&t80 + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&t80 + 60, 0);
                GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t80), t79);
                ((uint8_t *)&t80)[56] = 1;
                t70 = t80;
                goto ix71;
ie71:;
                gm_t8scu1osgcra5 t82 = F.l15;
                gm_t8scu1osgcra5 t83 = t82;
                gm_teijtv56moffm t84;
                GM_ST(uint8_t, (uint8_t *)&t84 + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&t84 + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&t84 + 60, 0);
                GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t84), t83);
                ((uint8_t *)&t84)[56] = 1;
                t70 = t84;
ix71:;
                F.l14 = t70;
            }
            goto ix67;
ie67:;
            {
                gm_t8scu1osgcra5 t86 = F.l15;
                gm_teijtv56moffm t87;
                GM_ST(uint8_t, (uint8_t *)&t87 + 57, 0);
                GM_ST(uint16_t, (uint8_t *)&t87 + 58, 0);
                GM_ST(uint32_t, (uint8_t *)&t87 + 60, 0);
                GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t87), t86);
                ((uint8_t *)&t87)[56] = 1;
                F.l14 = t87;
            }
ix67:;
        }
        goto ix64;
ie64:;
        {
            gm_teijtv56moffm t89 = F.l11;
            F.l14 = t89;
        }
ix64:;
    }
    {
        gm_tlsv15umjjhme t90;
        memset(&t90, 0, sizeof t90);
        F.l17 = t90;
    }
    {
        uint32_t t91 = gm_l1;
        uint32_t t92 = gm_l2;
        gm_f_narrowest_around_10pkumk((void *)&F.l18, t91, t92);
    }
    {
        {
            void * t94 = gm_l0;
            void * t95 = (void *)(((uint8_t *)t94) + 176);
            gm_l19 = t95;
        }
        gm_hval t96 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn97 = {.eff = 200810844, .kind = GM_LIT, .clauses = gm_hlpd8uohstag, .env = t96.env, .hctx = t96.cx, .with_frame = &F.h, .with_label = 2, .result = (void *)&F.w2, .fw = NULL, .next = cx};
        void * t100 = gm_l19;
        gm_b0_1 t101 = gm_f_entries_lr25p57_sipsive((void *)NULL, (&wn97), NULL, t100);
        if (gm_unw.kind) {
            goto gm_ll99;
        }
        F.w2 = t101;
        goto gm_ls102;
gm_ll99:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 2) { gm_unw.kind = GM_NONE; goto we98; }
        goto gm_lad;
gm_ls102:;
we98:;
        gm_b0_1 t103 = F.w2;
        gm_b0_1 t104 = t103;
    }
    {
        if (!((((uint8_t *)&F.l14))[56] != 0)) goto ie105;
        gm_t8scu1osgcra5 t106 = GM_LD(gm_t8scu1osgcra5, ((uint8_t *)&F.l14));
        F.l20 = t106;
        {
            if (!((((uint8_t *)&F.l17))[20] != 0)) goto ie108;
            gm_tdo2c70b3n0vq t109 = GM_LD(gm_tdo2c70b3n0vq, ((uint8_t *)&F.l17));
            gm_l21 = t109;
            {
                gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 0));
                uint32_t t113 = gm_f_width_spp7n92(GM_CONV(gm_cseahvg8v7p0vs, t112));
                gm_tl874t2atnlh3 t114 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l20) + 0));
                uint32_t t115 = gm_f_width_spp7n92(GM_CONV(gm_cseahvg8v7p0vs, t114));
                uint8_t t116 = (t113 < t115);
                if (!t116) goto ie111;
                {
                    gm_t8scu1osgcra5 t118;
                    memset((uint8_t *)&t118 + 12, 0, 4);
                    gm_tl874t2atnlh3 t119 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 0));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t118 + 0, t119);
                    uint32_t t120 = GM_LD(uint32_t, ((((uint8_t *)&gm_l21) + 12) + 0));
                    uint64_t t121 = (uint64_t)t120;
                    uint64_t t122 = (uintptr_t)t121;
                    void * t123 = gm_l0;
                    uint64_t t124 = GM_LD(uintptr_t, ((((uint8_t *)t123) + 232) + 0) + 8);
                    if (t122 >= (uintptr_t)(t124)) gm_trap_index(t122, t124, gm_srck9pqpjjfjkmq, 199, 70);
                    gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t123) + 232) + 0)) + t122 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&t118 + 16)) + 0, t125);
                    gm_th2lqbo77pvsc t126;
                    memset(&t126, 0, sizeof t126);
                    GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&t118 + 16)) + 16, t126);
                    gm_teijtv56moffm t127;
                    GM_ST(uint8_t, (uint8_t *)&t127 + 57, 0);
                    GM_ST(uint16_t, (uint8_t *)&t127 + 58, 0);
                    GM_ST(uint32_t, (uint8_t *)&t127 + 60, 0);
                    GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t127), t118);
                    ((uint8_t *)&t127)[56] = 1;
                    uint8_t t128 = 0;
                    F.ret = t127;
                    goto gm_fret;
                }
                goto ix111;
ie111:;
ix111:;
            }
            goto ix108;
ie108:;
ix108:;
        }
        {
            gm_t8scu1osgcra5 t129;
            memset((uint8_t *)&t129 + 12, 0, 4);
            gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l20) + 0));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t129 + 0, t130);
            memcpy((uint8_t *)&t129 + 16, (((uint8_t *)&F.l20) + 16), 40);
            gm_teijtv56moffm t131;
            GM_ST(uint8_t, (uint8_t *)&t131 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t131 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t131 + 60, 0);
            GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t131), t129);
            ((uint8_t *)&t131)[56] = 1;
            uint8_t t132 = 0;
            F.ret = t131;
            goto gm_fret;
        }
        goto ix105;
ie105:;
ix105:;
    }
    {
        if (!((((uint8_t *)&F.l17))[20] != 0)) goto ie133;
        gm_tdo2c70b3n0vq t134 = GM_LD(gm_tdo2c70b3n0vq, ((uint8_t *)&F.l17));
        gm_l22 = t134;
        {
            gm_t8scu1osgcra5 t136;
            memset((uint8_t *)&t136 + 12, 0, 4);
            gm_tl874t2atnlh3 t137 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l22) + 0));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t136 + 0, t137);
            uint32_t t138 = GM_LD(uint32_t, ((((uint8_t *)&gm_l22) + 12) + 0));
            uint64_t t139 = (uint64_t)t138;
            uint64_t t140 = (uintptr_t)t139;
            void * t141 = gm_l0;
            uint64_t t142 = GM_LD(uintptr_t, ((((uint8_t *)t141) + 232) + 0) + 8);
            if (t140 >= (uintptr_t)(t142)) gm_trap_index(t140, t142, gm_srck9pqpjjfjkmq, 205, 62);
            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t141) + 232) + 0)) + t140 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&t136 + 16)) + 0, t143);
            gm_th2lqbo77pvsc t144;
            memset(&t144, 0, sizeof t144);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6ok5tvcjc200 *)((uint8_t *)&t136 + 16)) + 16, t144);
            gm_teijtv56moffm t145;
            GM_ST(uint8_t, (uint8_t *)&t145 + 57, 0);
            GM_ST(uint16_t, (uint8_t *)&t145 + 58, 0);
            GM_ST(uint32_t, (uint8_t *)&t145 + 60, 0);
            GM_ST(gm_t8scu1osgcra5, ((uint8_t *)&t145), t136);
            ((uint8_t *)&t145)[56] = 1;
            uint8_t t146 = 0;
            F.ret = t145;
            goto gm_fret;
        }
        goto ix133;
ie133:;
ix133:;
    }
    {
        gm_teijtv56moffm t147;
        memset(&t147, 0, sizeof t147);
        uint8_t t148 = 0;
        F.ret = t147;
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
__attribute__((aligned(4))) gm_teijtv56moffm gm_f_hover_at_19okc4g_c(void * a0, uint32_t a1, uint32_t a2) { gm_teijtv56moffm gm_r; gm_f_hover_at_19okc4g(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_index_nki2g2t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_index_nki2g2t F;
    GM_STACK_CHECK(&F, 424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_h7regd0((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t3;
        gm_f_hash_map_lc3d6ha((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t4;
        gm_f_list_umenr17((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t5;
        gm_f_list_bpm3e45((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t6;
        gm_f_hash_map_apl3mkc((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t7;
        gm_f_list_epvducg((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t8;
        gm_f_hash_map_glsl7u2((void *)&t8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t9 = gm_f_empty_slice_lpi80k4_3();
        gm_b16_8 t10 = t9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b16_8 t12 = gm_f_empty_slice_lpi80k4_3();
        gm_b16_8 t13 = t12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 0, t2);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 40, t3);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 96, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 136, t5);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 176, t6);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 232, t7);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 272, t8);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 328, t11);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6vagfvc3bffs *)gm_out) + 344, t14);
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
inline __attribute__((aligned(4))) void gm_f_locals_at_2n5h0d6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_locals_at_2n5h0d6 F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_bpm3e45((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 136) + 0));
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
                    gm_tfgiovingnnea t16 = GM_LD(gm_tfgiovingnnea, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 88));
                    F.l6 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)&F.l6) + 64) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 76));
                        uint32_t t23 = gm_l2;
                        uint8_t t24 = (t22 <= t23);
                        if (!t24) goto ie18;
                        uint32_t t25 = gm_l2;
                        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 80));
                        uint8_t t27 = (t25 <= t26);
                        if (!t27) goto ie18;
                        {
                            void * t29 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t30 = gm_f_push_sige3eu(t29, &(*(gm_tfgiovingnnea *)((uint8_t *)&F.l6)));
                            gm_tftb1odc739t5 t31 = t30;
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srck9pqpjjfjkmq, 261, 67);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 260, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t34 = gm_l5;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l5 = t37;
                }
            }
lb5:;
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
inline __attribute__((aligned(4))) void gm_f_members_at_do67lp0(void *gm_out, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_members_at_do67lp0 F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* lo */
    uint32_t gm_l3; /* hi */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 272);
    uint32_t t3 = gm_l1;
    uint32_t t4 = gm_l2;
    uint32_t t5 = gm_l3;
    gm_tl874t2atnlh3 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t5);
    gm_f_get_7o29gjt((void *)&(*(gm_th2lqbo77pvsc *)gm_out), t2, GM_CONV(gm_cseahvg8v7p0vs, t6));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_members_at_do67lp0_c(void * a0, uint32_t a1, uint32_t a2, uint32_t a3) { gm_th2lqbo77pvsc gm_r; gm_f_members_at_do67lp0(&gm_r, a0, a1, a2, a3); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_narrowest_around_10pkumk(void *gm_out, uint32_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_narrowest_around_10pkumk F;
    uint32_t gm_l0; /* file */
    uint32_t gm_l1; /* off */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l0;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tu6ladb6432hk *)gm_out) + 0, t1);
    uint32_t t2 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tu6ladb6432hk *)gm_out) + 4, t2);
    gm_toceffb697p0u t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(gm_toceffb697p0u, (uint8_t *)&(*(gm_tu6ladb6432hk *)gm_out) + 8, t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tu6ladb6432hk gm_f_narrowest_around_10pkumk_c(uint32_t a0, uint32_t a1) { gm_tu6ladb6432hk gm_r; gm_f_narrowest_around_10pkumk(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_offer_lap4ju5(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_offer_lap4ju5 F;
    void * gm_l0; /* n */
    gm_tl874t2atnlh3 gm_l1; /* s */
    gm_tl874t2atnlh3 gm_l2; /* b */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        gm_tl874t2atnlh3 t3 = gm_l1;
        void * t4 = gm_l0;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 0));
        void * t6 = gm_l0;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 4));
        uint8_t t8 = gm_f_fn_contains_fesi73m(GM_CONV(gm_cseahvg8v7p0vs, t3), t5, t7);
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie2;
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
        void * t13 = gm_l0;
        if (!(((((uint8_t *)t13) + 8))[12] != 0)) goto ie12;
        gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 8));
        gm_l2 = t14;
        {
            gm_tl874t2atnlh3 t17 = gm_l1;
            uint32_t t18 = gm_f_width_spp7n92(GM_CONV(gm_cseahvg8v7p0vs, t17));
            gm_tl874t2atnlh3 t19 = gm_l2;
            uint32_t t20 = gm_f_width_spp7n92(GM_CONV(gm_cseahvg8v7p0vs, t19));
            uint8_t t21 = (t18 >= t20);
            if (!t21) goto ie16;
            {
                uint8_t t23 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t24 = gm_l0;
        gm_tl874t2atnlh3 t25 = gm_l1;
        gm_toceffb697p0u t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[12] = 1;
        GM_ST(gm_toceffb697p0u, (((uint8_t *)t24) + 8), t26);
    }
    {
        uint8_t t27 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_outline_of_01eujq2(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_outline_of_01eujq2 F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tiilmfd16v3fc gm_l4; /* o */
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 96) + 0));
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
                    gm_tiilmfd16v3fc t16 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 24));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                            gm_b16_8 t24 = t23;
                            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                            uint8_t t26 = 0;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 158, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t32 = gm_f_empty_slice_6ld1tun();
        gm_b16_8 t33 = t32;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_uses_of_nalefva(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_uses_of_nalefva F;
    void * gm_l0; /* ix */
    gm_tl874t2atnlh3 gm_l1; /* def */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t2p74ki8sl4an gm_l5; /* r */
    GM_STACK_CHECK(&F, 318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_7s4om10((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 0));
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
                    gm_t2p74ki8sl4an t16 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 24));
                    gm_l5 = t16;
                    {
                        gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 12));
                        gm_tl874t2atnlh3 t20 = gm_l1;
                        gm_tl874t2atnlh3 t21 = t19;
                        gm_tl874t2atnlh3 t22 = t20;
                        uint8_t t23 = (gm_eqff85ac10o3(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie18;
                        {
                            void * t25 = (void *)((uint8_t *)&F.l2);
                            gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 0));
                            gm_tftb1odc739t5 t27 = gm_f_push_0mk7udh(t25, GM_CONV(gm_cseahvg8v7p0vs, t26));
                            gm_tftb1odc739t5 t28 = t27;
                            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srck9pqpjjfjkmq, 249, 26);
                            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck9pqpjjfjkmq, 248, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t31 = gm_l4;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l4 = t34;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_closure t36 = ((gm_closure){GM_PTR_TAG(gm_f_fn_40fde41), NULL, cx});
        gm_f_sort_by_5e667o9(t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t37 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint64_t t39 = gm_f_dedup_ogel4tf(t38);
        gm_f_truncate_qtpovqr(t37, t39);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) uint32_t gm_f_width_spp7n92(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_width_spp7n92 F;
    gm_tl874t2atnlh3 gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t r3; bool o4 = __builtin_sub_overflow(t1, t2, &r3);
    if (o4) gm_trap("integer overflow in `-`", gm_srck9pqpjjfjkmq, 134, 32);
    uint32_t t5 = r3;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

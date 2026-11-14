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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
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
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t0ut88vjdgrt1 f0; uint8_t f1; uint8_t pe[7]; } gm_t4fb05ktf8oll;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_tl874t2atnlh3 f3; uint8_t f4; uint8_t p5[3]; void * f5; } gm_tiqopa6hu8jqe;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4fb05ktf8oll) == 48 && _Alignof(gm_t4fb05ktf8oll) == 8 && offsetof(gm_t4fb05ktf8oll, f0) == 0 && offsetof(gm_t4fb05ktf8oll, f1) == 40, "layout of `?MonoRequest`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tet9guhev5r4u f0; uint8_t f1; uint8_t pe[7]; } gm_teqc715vjig79;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
_Static_assert(sizeof(gm_tiqopa6hu8jqe) == 120 && _Alignof(gm_tiqopa6hu8jqe) == 8 && offsetof(gm_tiqopa6hu8jqe, f0) == 0 && offsetof(gm_tiqopa6hu8jqe, f1) == 16 && offsetof(gm_tiqopa6hu8jqe, f2) == 56 && offsetof(gm_tiqopa6hu8jqe, f3) == 96 && offsetof(gm_tiqopa6hu8jqe, f4) == 108 && offsetof(gm_tiqopa6hu8jqe, f5) == 112, "layout of `MonoCx`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_teqc715vjig79) == 64 && _Alignof(gm_teqc715vjig79) == 8 && offsetof(gm_teqc715vjig79, f0) == 0 && offsetof(gm_teqc715vjig79, f1) == 56, "layout of `?IntrinsicExpr`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_check_inline_n7kskmb {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* seen */
    gm_trqfvmjtt4sij l5; /* work */
    gm_trqfvmjtt4sij l9; /* calls */
    gm_b0_1 ret;
};
struct gm_fr_check_program_rltsqqp {
    gm_frame h;
    gm_tp8h583g7s5hd l6; /* o */
    gm_teqc715vjig79 l8; /* i */
    gm_tet9guhev5r4u l9; /* x */
    gm_tet9guhev5r4u l10; /* x */
    gm_b0_1 ret;
};
struct gm_fr_collect_calls_7s68jhs {
    gm_frame h;
    void * l1; /* out */
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
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
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_crpekm1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
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
struct gm_fr_find_main_ob90gra {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_find_pair_ddtpbq9 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_2d811oi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5puhsbi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ah95er7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_7l73vjb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
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
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_mono_cond_pats_armprh7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mono_expr_p8qb7fe {
    gm_frame h;
    void * l0; /* s */
    void * l1; /* mc */
    gm_tbci5ospk5nnn l13; /* i */
    gm_tbci5ospk5nnn l16; /* v */
    gm_tbci5ospk5nnn l24; /* v */
    gm_t710lsrq1m8gk l35; /* w */
    gm_t710lsrq1m8gk l56; /* x */
    gm_b0_1 ret;
};
struct gm_fr_mono_fn_aj1vgri {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* args */
    gm_tqqhoivahirr0 l13; /* sig */
    gm_trqfvmjtt4sij l14; /* params */
    gm_t5v0c8ia6a5mo l17; /* p */
    gm_t5v0c8ia6a5mo l18; /* q */
    gm_tdgfduqjheqdm l19; /* def */
    gm_trqfvmjtt4sij l21; /* locals */
    gm_tsoie1ppgdvcg l24; /* l */
    gm_tsoie1ppgdvcg l25; /* x */
    uint32_t ret;
};
struct gm_fr_mono_handler_38b78qh {
    gm_frame h;
    gm_t1cs2fjnrqap1 l4; /* lit */
    gm_trqfvmjtt4sij l6; /* clauses */
    uint32_t ret;
};
struct gm_fr_mono_pat_lipeb0h {
    gm_frame h;
    void * l0; /* s */
    void * l1; /* mc */
    gm_b0_1 ret;
};
struct gm_fr_mono_ty_atcu0jn {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_monomorphize_21k6pku {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_b0_1 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_7vc0ftk {
    gm_frame h;
    gm_t0ut88vjdgrt1 l2; /* x */
    gm_t4fb05ktf8oll ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_produce_instance_kcqgj3e {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_tdgfduqjheqdm l2; /* def */
    gm_trqfvmjtt4sij l4; /* map */
    gm_trqfvmjtt4sij l10; /* locals */
    gm_tiqopa6hu8jqe l11; /* mc */
    gm_tsoie1ppgdvcg l14; /* l */
    gm_tsoie1ppgdvcg l15; /* x */
    gm_tsoie1ppgdvcg l19; /* l */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_inst_def_tcpabg0 {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_b0_1 ret;
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
struct gm_fr_subst_eref_kf9o1q5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* args */
    gm_toa2vhe3l4hfr l6; /* a */
    gm_toa2vhe3l4hfr l8; /* v */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_teti411eujvf4 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bodies_qrmq5ll(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_consts_fsj0q4a(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_continuations_ukkdj9c(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_dangling_addresses_9hre1eh(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_decl_breaks_pk9cnmn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_declared_defaults_m12p6ao(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_escapes_brb2os4(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_handler_clauses_n3lq2s1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_inline_n7kskmb(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_program_rltsqqp(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_check_spec_4vp7jhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_static_assert_s4f7i6i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3, gm_toceffb697p0u a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_tests_ogqv9b0(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_7s68jhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_compile_error_qlau792(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handlers_c3ac89r(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_find_main_ob90gra(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_find_pair_ddtpbq9(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2d811oi(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5puhsbi(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ah95er7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_afagrs5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ht23jk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jhm2032(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l251doo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_le00do2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r4smojn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ra91g4n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_limit_nncqn56(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_cond_pats_armprh7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_expr_p8qb7fe(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint32_t gm_f_mono_fn_aj1vgri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
__attribute__((aligned(4))) uint32_t gm_f_mono_handler_38b78qh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mono_pat_lipeb0h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mono_ty_atcu0jn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_monomorphize_21k6pku(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_7vc0ftk(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_produce_instance_kcqgj3e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_33kejrs(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_static_assert_k8dt551(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_eref_kf9o1q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_kf1lpoo3h6o[40] __attribute__((aligned(8)));
extern const uint8_t gm_s6d960pgtv3l9[26];
extern const uint8_t gm_sfrcaft0lo33k[60];
extern const uint8_t gm_sisvmc7d4huip[14];
extern const uint8_t gm_sjg57h040l08d[56];
extern const uint8_t gm_sl85qrve5cdud[31];
extern const uint8_t gm_smhs805pum0dn[26];
extern const uint8_t gm_snikjq3vkoue5[14];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sq7c24l9r5rti[118];
extern const uint8_t gm_srar03v1ldkqi[49];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src5eu5l4a7gv6s[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 216) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 216) + 0)) + t3 * 16));
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_crpekm1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2d811oi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_2d811oi F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_calls_7s68jhs *)((&F.h)->up))->l1));
        gm_b0_1 t4 = gm_f_collect_calls_7s68jhs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5puhsbi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_5puhsbi F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_pat_lipeb0h *)((&F.h)->up))->l0));
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_pat_lipeb0h *)((&F.h)->up))->l1));
        void * t4 = gm_l0;
        gm_b0_1 t5 = gm_f_mono_pat_lipeb0h((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ah95er7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_ah95er7 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_expr_p8qb7fe *)((&F.h)->up))->l0));
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mono_expr_p8qb7fe *)((&F.h)->up))->l1));
        void * t4 = gm_l0;
        gm_b0_1 t5 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7l73vjb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_crpekm1();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mono_pat_lipeb0h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mono_pat_lipeb0h F;
    void * gm_l2; /* p */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t *t3 = (((uint8_t *)t2) + 0);
        void * t4 = F.l0;
        void * t5 = gm_l2;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        void * t7 = F.l1;
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t4, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, t3, t9);
    }
    {
        void * t10 = gm_l2;
        gm_closure t11 = ((gm_closure){(void *)gm_f_fn_5puhsbi, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t10, t11);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_monomorphize_21k6pku(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_monomorphize_21k6pku F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 1320);
            gm_t4fb05ktf8oll t5;
            gm_f_pop_7vc0ftk((void *)&t5, t4);
            gm_t4fb05ktf8oll t6 = t5;
            if (!((((uint8_t *)&t6))[40] != 0)) goto lb2;
            gm_t0ut88vjdgrt1 t7 = GM_LD(gm_t0ut88vjdgrt1, ((uint8_t *)&t6));
            F.l1 = t7;
            {
                void * t10 = gm_l0;
                gm_b0_1 t11 = gm_f_produce_instance_kcqgj3e((void *)NULL, cx, NULL, t10, &(*(gm_t0ut88vjdgrt1 *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_7vc0ftk(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_7vc0ftk F;
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
            gm_t4fb05ktf8oll t8;
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
        gm_t0ut88vjdgrt1 t17 = GM_LD(gm_t0ut88vjdgrt1, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
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
        gm_t0ut88vjdgrt1 t29 = F.l2;
        gm_t4fb05ktf8oll t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 44, 0);
        GM_ST(gm_t0ut88vjdgrt1, ((uint8_t *)&t30), t29);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 2064);
        uint32_t t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_put_3pvr70r(t3, t4, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srclv0dcbbfhves, 607, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf1lpoo3h6o)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t2 = t1;
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcod98slk3vnra, 561, 80);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
    gm_teti411eujvf4 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_inline_n7kskmb(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_inline_n7kskmb F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t9d4sf0vqn7dt gm_l3; /* inl */
    uint8_t gm_l6; /* recursive */
    uint32_t gm_l7; /* g */
    void * gm_l8; /* body */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* c */
    GM_STACK_CHECK(&F, 710u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 800) + 0));
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
                    uint64_t t11 = gm_l2;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_t9d4sf0vqn7dt t16 = GM_LD(gm_t9d4sf0vqn7dt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 16));
                    gm_l3 = t16;
                    {
                        gm_f_list_go2qg0b((void *)&F.l4, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        gm_f_list_go2qg0b((void *)&F.l5, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t18 = (void *)((uint8_t *)&F.l5);
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                        gm_tftb1odc739t5 t20 = gm_f_push_im9d4ro(t18, t19);
                        gm_tftb1odc739t5 t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_src5eu5l4a7gv6s, 359, 9);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    {
                        gm_l6 = ((uint8_t)0);
                    }
                    {
                        for (;;) {
                            void * t24 = (void *)((uint8_t *)&F.l5);
                            gm_t9805qtonbsag t25 = gm_f_pop_570g79n(t24);
                            gm_t9805qtonbsag t26 = t25;
                            if (!((((uint8_t *)&t26))[4] != 0)) goto lb23;
                            uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&t26));
                            gm_l7 = t27;
                            {
                                uint32_t t30 = gm_l7;
                                uint64_t t31 = (uint64_t)t30;
                                uint64_t t32 = (uintptr_t)t31;
                                void * t33 = gm_l0;
                                uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 384) + 0) + 8);
                                if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_src5eu5l4a7gv6s, 362, 21);
                                void * t35 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 384) + 0)) + t32 * 64) + 40));
                                void * t36 = t35;
                                void * t37;
                                if ((GM_LD(void *, ((uint8_t *)&t36)) != 0)) {
                                    void * t38 = GM_LD(void *, (((uint8_t *)&t36) + 0));
                                    t37 = t38;
                                } else {
                                    goto lc23;
                                }
                                gm_l8 = t37;
                            }
                            {
                                gm_f_list_go2qg0b((void *)&F.l9, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll28;
                                }
                            }
                            {
                                void * t39 = gm_l8;
                                void * t40 = (void *)((uint8_t *)&F.l9);
                                gm_b0_1 t41 = gm_f_collect_calls_7s68jhs((void *)NULL, cx, NULL, t39, t40);
                                if (gm_unw.kind) {
                                    goto gm_ll28;
                                }
                            }
                            {
                                {
                                    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                    gm_l10 = t43;
                                }
                                {
                                    gm_l11 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t45 = gm_l11;
                                        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint8_t t47 = (t45 < t46);
                                        if (!t47) goto lb44;
                                        {
                                            uint64_t t50 = gm_l11;
                                            uint64_t t51 = (uintptr_t)t50;
                                            uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t51 * 4));
                                            uint32_t t54 = t53;
                                            uint32_t t57 = GM_LD(uint32_t, ((uint8_t *)&t54));
                                            gm_l12 = t57;
                                            {
                                                uint32_t t60 = gm_l12;
                                                uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                                                uint8_t t62 = ((t60 == t61));
                                                if (!t62) goto ie59;
                                                {
                                                    gm_l6 = ((uint8_t)1);
                                                }
                                                goto ix59;
ie59:;
                                                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                                gm_b16_8 t66 = t65;
                                                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                                                uint32_t t68 = gm_l12;
                                                gm_tf5srh4mp4b3i t69 = gm_f_index_of_41b084m(t67, t68);
                                                gm_tf5srh4mp4b3i t70 = t69;
                                                uint8_t t71;
                                                if (((((uint8_t *)&t70))[8] != 0)) goto mn73;
                                                t71 = ((uint8_t)1);
                                                goto me72;
mn73:;
                                                t71 = ((uint8_t)0);
                                                goto me72;
mn74:;
                                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 368, 27);
me72:;
                                                if (!t71) goto ie64;
                                                {
                                                    void * t76 = (void *)((uint8_t *)&F.l4);
                                                    uint32_t t77 = gm_l12;
                                                    gm_tftb1odc739t5 t78 = gm_f_push_im9d4ro(t76, t77);
                                                    gm_tftb1odc739t5 t79 = t78;
                                                    if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_src5eu5l4a7gv6s, 369, 21);
                                                    gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
                                                }
                                                {
                                                    void * t81 = (void *)((uint8_t *)&F.l5);
                                                    uint32_t t82 = gm_l12;
                                                    gm_tftb1odc739t5 t83 = gm_f_push_im9d4ro(t81, t82);
                                                    gm_tftb1odc739t5 t84 = t83;
                                                    if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_src5eu5l4a7gv6s, 370, 21);
                                                    gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
                                                }
                                                goto ix64;
ie64:;
ix64:;
ix59:;
                                            }
                                            goto me55;
mn56:;
                                            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 365, 13);
me55:;
                                        }
lc44:;
                                        {
                                            uint64_t t87 = gm_l11;
                                            uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                                            uint64_t t90 = r88;
                                            gm_l11 = t90;
                                        }
                                    }
lb44:;
                                }
                            }
lc23:;
                        }
gm_ll28:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb23; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc23; }
                        }
                        goto gm_ll9;
lb23:;
                    }
                    {
                        uint8_t t92 = gm_l6;
                        if (!t92) goto ie91;
                        {
                            void * t94 = gm_l0;
                            gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 4));
                            uint32_t t96 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                            uint64_t t97 = (uint64_t)t96;
                            uint64_t t98 = (uintptr_t)t97;
                            void * t99 = gm_l0;
                            uint64_t t100 = GM_LD(uintptr_t, ((((uint8_t *)t99) + 344) + 0) + 8);
                            if (t98 >= (uintptr_t)(t100)) gm_trap_index(t98, t100, gm_src5eu5l4a7gv6s, 375, 116);
                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t99) + 344) + 0)) + t98 * 136) + 0));
                            gm_tnpifv20irnph t102;
                            GM_ST(gm_b16_8, ((uint8_t *)&t102 + 0), t101);
                            gm_tabb710d9mq2e t103;
                            gm_f_format_ra91g4n((void *)&t103, (void *)NULL, cx, NULL, t102);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tabb710d9mq2e t104 = t103;
                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_src5eu5l4a7gv6s, 375, 31);
                            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
                            gm_b16_8 t106 = t105;
                            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                            gm_b0_1 t108 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t94, t95, t107);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix91;
ie91:;
ix91:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 356, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t110 = gm_l2;
                    uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                    uint64_t t113 = r111;
                    gm_l2 = t113;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_program_rltsqqp(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint8_t a2) {
    struct gm_fr_check_program_rltsqqp F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    uint8_t gm_l2; /* tests */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    void * gm_l7; /* e */
    gm_b16_8 gm_l11; /* args */
    uint64_t gm_l12; /* d */
    uint64_t gm_l13; /* d */
    GM_STACK_CHECK(&F, 1682u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 280) + 0) + 8);
            gm_l3 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = gm_l3;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = t12;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l5 = t16;
                    {
                        uint64_t t18 = gm_l5;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
                        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 280) + 0) + 8);
                        if (t19 >= (uintptr_t)(t22)) gm_trap_index(t19, t22, gm_src5eu5l4a7gv6s, 8, 14);
                        gm_tp8h583g7s5hd t23 = GM_LD(gm_tp8h583g7s5hd, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 280) + 0)) + t19 * 168));
                        F.l6 = t23;
                    }
                    {
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 0));
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 200) + 0) + 8);
                        if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_src5eu5l4a7gv6s, 9, 12);
                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 200) + 0)) + t26 * 184) + 112));
                        if (!t30) goto ie24;
                        {
                            goto lc6;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    {
                        uint8_t t37 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        switch (t37) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma33; case ((uint8_t)3ULL): goto ma33; case ((uint8_t)4ULL): goto ma34; default: goto ma35; }
ma33:;
                        uint8_t t41 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        if (t41 != ((uint8_t)3ULL)) goto pa40;
                        goto po39;
pa40:;
                        uint8_t t43 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        if (t43 != ((uint8_t)2ULL)) goto pa42;
                        goto po39;
pa42:;
                        goto mn38;
po39:;
                        goto me32;
mn38:;
ma34:;
                        uint8_t t46 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        if (t46 != ((uint8_t)4ULL)) goto mn45;
                        void * t47 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 64) + 8));
                        gm_l7 = t47;
                        {
                            gm_teqc715vjig79 t49;
                            memset(&t49, 0, sizeof t49);
                            F.l8 = t49;
                        }
                        {
                            void * t51 = gm_l7;
                            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                            if (t52 != ((uint8_t)49ULL)) goto ie50;
                            gm_tet9guhev5r4u t53 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t51) + 8) + 8));
                            F.l9 = t53;
                            {
                                gm_tet9guhev5r4u t55 = F.l9;
                                gm_teqc715vjig79 t56;
                                GM_ST(uint8_t, (uint8_t *)&t56 + 57, 0);
                                GM_ST(uint16_t, (uint8_t *)&t56 + 58, 0);
                                GM_ST(uint32_t, (uint8_t *)&t56 + 60, 0);
                                GM_ST(gm_tet9guhev5r4u, ((uint8_t *)&t56), t55);
                                ((uint8_t *)&t56)[56] = 1;
                                F.l8 = t56;
                            }
                            goto ix50;
ie50:;
ix50:;
                        }
                        {
                            gm_teqc715vjig79 t57 = F.l8;
                            gm_teqc715vjig79 t58 = t57;
                            gm_tet9guhev5r4u t59;
                            if (((((uint8_t *)&t58))[56] != 0)) {
                                gm_tet9guhev5r4u t60 = GM_LD(gm_tet9guhev5r4u, (((uint8_t *)&t58) + 0));
                                t59 = t60;
                            } else {
                                goto lc6;
                            }
                            F.l10 = t59;
                        }
                        {
                            gm_th2lqbo77pvsc t61 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l10) + 32));
                            gm_th2lqbo77pvsc t62 = t61;
                            gm_b16_8 t63;
                            if (((((uint8_t *)&t62))[16] != 0)) {
                                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 0));
                                t63 = t64;
                            } else {
                                gm_b16_8 t65 = gm_f_empty_slice_fa4sjvq();
                                t63 = t65;
                            }
                            gm_l11 = t63;
                        }
                        {
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 13) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_snikjq3vkoue5, 13) == 0)) goto mn67;
                            {
                                {
                                    void * t70 = gm_l0;
                                    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 0));
                                    gm_b16_8 t72 = gm_l11;
                                    gm_b16_8 t73 = t72;
                                    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                                    void * t75 = gm_l7;
                                    gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
                                    void * t77 = gm_f_static_assert_k8dt551((void *)NULL, cx, NULL, t70, t71, t74, t76);
                                    if (gm_unw.kind) {
                                        goto gm_ll10;
                                    }
                                }
                            }
                            goto me66;
mn67:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 13) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_sisvmc7d4huip, 13) == 0)) goto mn78;
                            void * t79 = gm_l0;
                            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 0));
                            gm_b16_8 t81 = gm_l11;
                            gm_b16_8 t82 = t81;
                            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                            void * t84 = gm_l7;
                            gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 72));
                            gm_b0_1 t86 = gm_f_compile_error_qlau792((void *)NULL, cx, NULL, t79, t80, t83, t85);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me66;
mn78:;
                            void * t88 = gm_l0;
                            gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 8) + 4));
                            gm_b16_8 t90;
                            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sfrcaft0lo33k));
                            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(59));
                            gm_b0_1 t91 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t88, t89, t90);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me66;
mn87:;
                            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 17, 17);
me66:;
                        }
                        goto me32;
mn45:;
ma35:;
                        void * t93 = gm_l0;
                        gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 8) + 4));
                        gm_b16_8 t95;
                        GM_ST(void *, (uint8_t *)&t95, (void *)(gm_smhs805pum0dn));
                        GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(25));
                        gm_b0_1 t96 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t93, t94, t95);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        goto me32;
mn92:;
ma36:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 10, 9);
me32:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 7, 5);
me14:;
                }
lc6:;
                {
                    uint64_t t98 = gm_l4;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l4 = t101;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        void * t102 = gm_l0;
        gm_b0_1 t103 = gm_f_declare_handlers_c3ac89r((void *)NULL, cx, NULL, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t105 = gm_l2;
        if (!t105) goto ie104;
        {
            void * t107 = gm_l0;
            uint64_t t108 = gm_l1;
            gm_b0_1 t109 = gm_f_check_tests_ogqv9b0((void *)NULL, cx, NULL, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix104;
ie104:;
        void * t111 = gm_l0;
        void * t112 = GM_LD(void *, (((uint8_t *)t111) + 0));
        uint8_t t113 = (((GM_LD(uintptr_t, ((((uint8_t *)t112) + 88) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)t112) + 88) + 24)), gm_soo4rtckloi4d, 12) == 0));
        if (!t113) goto ie110;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, (((uint8_t *)t116) + 0));
            uint64_t t118 = gm_l1;
            gm_b16_8 t119;
            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sog4rksrmsqd2));
            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(4));
            gm_tf5srh4mp4b3i t120 = gm_f_lookup_59juqoo(t117, t118, t119);
            gm_tf5srh4mp4b3i t121 = t120;
            if (!((((uint8_t *)&t121))[8] != 0)) goto ie115;
            uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&t121));
            gm_l12 = t122;
            {
                void * t124 = gm_l0;
                uint64_t t125 = gm_l12;
                uint64_t t126 = (uintptr_t)t125;
                void * t127 = gm_l0;
                void * t128 = GM_LD(void *, (((uint8_t *)t127) + 0));
                uint64_t t129 = GM_LD(uintptr_t, ((((uint8_t *)t128) + 240) + 0) + 8);
                if (t126 >= (uintptr_t)(t129)) gm_trap_index(t126, t129, gm_src5eu5l4a7gv6s, 33, 21);
                gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t128) + 240) + 0)) + t126 * 216) + 0) + 16));
                gm_b16_8 t131;
                GM_ST(void *, (uint8_t *)&t131, (void *)(gm_sq7c24l9r5rti));
                GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(117));
                gm_b0_1 t132 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t124, t130, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix115;
ie115:;
ix115:;
        }
        goto ix110;
ie110:;
        void * t134 = gm_l0;
        void * t135 = GM_LD(void *, (((uint8_t *)t134) + 0));
        uint64_t t136 = gm_l1;
        gm_b16_8 t137;
        GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(4));
        gm_tf5srh4mp4b3i t138 = gm_f_lookup_59juqoo(t135, t136, t137);
        gm_tf5srh4mp4b3i t139 = t138;
        if (!((((uint8_t *)&t139))[8] != 0)) goto ie133;
        uint64_t t140 = GM_LD(uint64_t, ((uint8_t *)&t139));
        gm_l13 = t140;
        {
            void * t142 = gm_l0;
            uint64_t t143 = gm_l13;
            gm_b0_1 t144 = gm_f_find_main_ob90gra((void *)NULL, cx, NULL, t142, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix133;
ie133:;
        {
            void * t146 = gm_l0;
            gm_tl874t2atnlh3 t147 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t148;
            GM_ST(void *, (uint8_t *)&t148, (void *)(gm_srar03v1ldkqi));
            GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(48));
            gm_b0_1 t149 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t146, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix133:;
ix110:;
ix104:;
    }
    {
        void * t150 = gm_l0;
        gm_b0_1 t151 = gm_f_check_consts_fsj0q4a((void *)NULL, cx, NULL, t150);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t152 = gm_l0;
        gm_b0_1 t153 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t154 = gm_l0;
        gm_b0_1 t155 = gm_f_check_handler_clauses_n3lq2s1((void *)NULL, cx, NULL, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t156 = gm_l0;
        gm_b0_1 t157 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t158 = gm_l0;
        gm_b0_1 t159 = gm_f_check_declared_defaults_m12p6ao((void *)NULL, cx, NULL, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t160 = gm_l0;
        gm_b0_1 t161 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t162 = gm_l0;
        gm_b0_1 t163 = gm_f_check_decl_breaks_pk9cnmn((void *)NULL, cx, NULL, t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t164 = gm_l0;
        gm_b0_1 t165 = gm_f_monomorphize_21k6pku((void *)NULL, cx, NULL, t164);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t166 = gm_l0;
        gm_b0_1 t167 = gm_f_check_inline_n7kskmb((void *)NULL, cx, NULL, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t168 = gm_l0;
        gm_b0_1 t169 = gm_f_check_escapes_brb2os4((void *)NULL, cx, NULL, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t170 = gm_l0;
        gm_b0_1 t171 = gm_f_check_dangling_addresses_9hre1eh((void *)NULL, cx, NULL, t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t172 = gm_l0;
        gm_b0_1 t173 = gm_f_check_continuations_ukkdj9c((void *)NULL, cx, NULL, t172);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_7s68jhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_calls_7s68jhs F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)34ULL)) goto ie2;
        uint8_t t5 = GM_LD(uint8_t, ((((((uint8_t *)t3) + 8) + 8) + 0) + 0));
        if (t5 != ((uint8_t)0ULL)) goto ie2;
        uint32_t t6 = GM_LD(uint32_t, ((((((uint8_t *)t3) + 8) + 8) + 0) + 8));
        gm_l2 = t6;
        {
            void * t8 = F.l1;
            uint32_t t9 = gm_l2;
            gm_tftb1odc739t5 t10 = gm_f_push_im9d4ro(t8, t9);
            gm_tftb1odc739t5 t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_src5eu5l4a7gv6s, 381, 37);
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        gm_closure t14 = ((gm_closure){(void *)gm_f_fn_2d811oi, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t13, t14);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_find_main_ob90gra(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_find_main_ob90gra F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint32_t gm_l3; /* f */
    uint8_t gm_l5; /* ret_ok */
    gm_t88rcucg1b7uk gm_l6; /* ok */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* e */
    GM_STACK_CHECK(&F, 1483u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src5eu5l4a7gv6s, 387, 13);
        gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216) + 0) + 16));
        gm_l2 = t7;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = gm_l1;
        gm_t6qooom8qa2do t10;
        gm_f_resolve_decl_pus042v((void *)&t10, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t11 = t10;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma14; default: goto ma15; }
ma13:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t19 != ((uint8_t)5ULL)) goto mn18;
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&t11) + 8));
        gm_l3 = t20;
        {
            uint32_t t22 = gm_l3;
            uint64_t t23 = (uint64_t)t22;
            uint64_t t24 = (uintptr_t)t23;
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 344) + 0) + 8);
            if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_src5eu5l4a7gv6s, 390, 20);
            gm_tqqhoivahirr0 t27 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 344) + 0)) + t24 * 136));
            F.l4 = t27;
        }
        {
            uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 40) + 8);
            uint8_t t30 = (t29 > ((uint64_t)0ULL));
            if (!t30) goto ie28;
            {
                void * t32 = gm_l0;
                gm_tl874t2atnlh3 t33 = gm_l2;
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sl85qrve5cdud));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(30));
                gm_b0_1 t35 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t32, t33, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix28;
ie28:;
ix28:;
        }
        {
            uint8_t t36 = 0;
            void * t37 = gm_l0;
            void * t38 = (void *)(((uint8_t *)t37) + 8);
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            uint8_t t40 = gm_f_is_unit_n83ttnb(t38, GM_CONV(gm_csguankko4lmhd, t39));
            if (!t40) {
                gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
                void * t42 = gm_l0;
                void * t43 = (void *)(((uint8_t *)t42) + 8);
                gm_t88rcucg1b7uk t44 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t45 = t41;
                gm_t88rcucg1b7uk t46 = t44;
                uint8_t t47 = (gm_eq9m6mvtmgde(((uint8_t *)&t45), ((uint8_t *)&t46)));
                t36 = t47;
            } else t36 = 1;
            gm_l5 = t36;
        }
        {
            void * t49 = gm_l0;
            void * t50 = (void *)(((uint8_t *)t49) + 8);
            gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            gm_t53at80pfdus3 t52;
            gm_f_kind_jcnt4hm((void *)&t52, t50, GM_CONV(gm_csguankko4lmhd, t51));
            gm_t53at80pfdus3 t53 = t52;
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
            if (t54 != ((uint8_t)14ULL)) goto ie48;
            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t53) + 8) + 4));
            gm_l6 = t55;
            void * t56 = gm_l0;
            void * t57 = (void *)(((uint8_t *)t56) + 8);
            gm_t88rcucg1b7uk t58 = gm_l6;
            uint8_t t59 = gm_f_is_unit_n83ttnb(t57, GM_CONV(gm_csguankko4lmhd, t58));
            if (!t59) goto ie48;
            {
                gm_l5 = ((uint8_t)1);
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            uint8_t t62 = gm_l5;
            uint8_t t63 = (uint8_t)!t62;
            if (!t63) goto ie61;
            {
                void * t65 = gm_l0;
                gm_tl874t2atnlh3 t66 = gm_l2;
                void * t67 = gm_l0;
                gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
                gm_b16_8 t69 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t67, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t70;
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
                gm_tabb710d9mq2e t71;
                gm_f_format_l251doo((void *)&t71, (void *)NULL, cx, NULL, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t72 = t71;
                if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_src5eu5l4a7gv6s, 394, 43);
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                gm_b16_8 t74 = t73;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_b0_1 t76 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t65, t66, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix61;
ie61:;
ix61:;
        }
        {
            {
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 64) + 0));
                gm_l7 = t78;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t80 = gm_l8;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t82 = (t80 < t81);
                    if (!t82) goto lb79;
                    {
                        uint64_t t85 = gm_l8;
                        uint64_t t86 = (uintptr_t)t85;
                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_tiilmfd16v3fc t90 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t86 * 24));
                        gm_l9 = t90;
                        {
                            void * t93 = gm_l0;
                            uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
                            uint8_t t95 = gm_f_is_ambient_m41pl1e(t93, t94);
                            uint8_t t96 = (uint8_t)!t95;
                            if (!t96) goto ie92;
                            {
                                void * t98 = gm_l0;
                                gm_tl874t2atnlh3 t99 = gm_l2;
                                void * t100 = gm_l0;
                                void * t101 = (void *)(((uint8_t *)t100) + 8);
                                uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
                                gm_b16_8 t103 = gm_f_effect_name_prpp0ip(t101, t102);
                                gm_tnpifv20irnph t104;
                                GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
                                gm_tabb710d9mq2e t105;
                                gm_f_format_jhm2032((void *)&t105, (void *)NULL, cx, NULL, t104);
                                if (gm_unw.kind) {
                                    goto gm_ll83;
                                }
                                gm_tabb710d9mq2e t106 = t105;
                                if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_src5eu5l4a7gv6s, 397, 35);
                                gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
                                gm_b16_8 t108 = t107;
                                gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                                gm_b0_1 t110 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t98, t99, t109);
                                if (gm_unw.kind) {
                                    goto gm_ll83;
                                }
                            }
                            goto ix92;
ie92:;
ix92:;
                        }
                        goto me88;
mn89:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 395, 13);
me88:;
                    }
lc79:;
                    {
                        uint64_t t112 = gm_l8;
                        uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                        uint64_t t115 = r113;
                        gm_l8 = t115;
                    }
                }
gm_ll83:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
                }
                goto gm_lad;
lb79:;
            }
        }
        goto me12;
mn18:;
ma14:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t117 != ((uint8_t)11ULL)) goto mn116;
        goto me12;
mn116:;
ma15:;
        void * t120 = gm_l0;
        gm_tl874t2atnlh3 t121 = gm_l2;
        gm_b16_8 t122;
        GM_ST(void *, (uint8_t *)&t122, (void *)(gm_s6d960pgtv3l9));
        GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(25));
        gm_b0_1 t123 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t120, t121, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me12;
mn119:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 388, 5);
me12:;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_find_pair_ddtpbq9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_pair_ddtpbq9 F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* from */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l4; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_tcc1rq4n1avb2 t16 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 8));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                            gm_t9805qtonbsag t24;
                            GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t24), t23);
                            ((uint8_t *)&t24)[4] = 1;
                            uint8_t t25 = 0;
                            F.ret = t24;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 113, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
                }
            }
lb5:;
        }
    }
    {
        gm_t9805qtonbsag t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2d811oi(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5puhsbi(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ah95er7(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mono_cond_pats_armprh7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_mono_cond_pats_armprh7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    gm_b16_8 gm_l2; /* cond */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* p */
    GM_STACK_CHECK(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 24));
                    gm_l5 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t18 != ((uint8_t)1ULL)) goto ie17;
                        void * t19 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l6 = t19;
                        {
                            void * t21 = gm_l0;
                            void * t22 = gm_l1;
                            void * t23 = gm_l6;
                            gm_b0_1 t24 = gm_f_mono_pat_lipeb0h((void *)NULL, cx, NULL, t21, t22, t23);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 255, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l4;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l4 = t29;
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
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_expr_p8qb7fe(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mono_expr_p8qb7fe F;
    void * gm_l2; /* e */
    uint8_t gm_l3; /* had_param */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint32_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l9; /* b */
    gm_t428ue2u3hf5g gm_l10; /* k */
    gm_t88rcucg1b7uk gm_l11; /* kt */
    gm_b16_8 gm_l12; /* bytes */
    uint32_t gm_l14; /* f */
    gm_b16_8 gm_l15; /* bytes */
    gm_t88rcucg1b7uk gm_l17; /* of */
    uint8_t gm_l18; /* align */
    gm_t88rcucg1b7uk gm_l19; /* ot */
    gm_tbccvikg6b96e gm_l20; /* l */
    gm_t88rcucg1b7uk gm_l21; /* of */
    uint8_t gm_l22; /* max */
    gm_t88rcucg1b7uk gm_l23; /* ot */
    gm_t88rcucg1b7uk gm_l25; /* of */
    gm_t88rcucg1b7uk gm_l26; /* ot */
    gm_t88rcucg1b7uk gm_l27; /* a */
    gm_t88rcucg1b7uk gm_l28; /* b */
    uint8_t gm_l29; /* negated */
    gm_t88rcucg1b7uk gm_l30; /* sa */
    gm_t88rcucg1b7uk gm_l31; /* sb */
    void * gm_l32; /* r */
    uint16_t gm_l33; /* bits */
    double gm_l34; /* x */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    void * gm_l39; /* value */
    uint32_t gm_l40; /* c */
    gm_b16_8 gm_l41; /* args */
    gm_tiilmfd16v3fc gm_l42; /* eref */
    uint32_t gm_l43; /* op */
    gm_b16_8 gm_l44; /* args */
    uint32_t gm_l45; /* c */
    uint32_t gm_l46; /* c */
    uint32_t gm_l47; /* h */
    void * gm_l48; /* w */
    gm_tiilmfd16v3fc gm_l49; /* eref */
    uint32_t gm_l50; /* op */
    gm_b16_8 gm_l51; /* arms */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l54; /* a */
    gm_trl2khmuphncd gm_l55; /* x */
    void * gm_l57; /* w */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* k */
    gm_b16_8 gm_l61; /* msg */
    uint32_t gm_l62; /* id */
    gm_b16_8 gm_l63; /* args */
    gm_b16_8 gm_l64; /* args */
    gm_t88rcucg1b7uk gm_l65; /* src */
    uint64_t gm_l66; /* a */
    uint64_t gm_l67; /* b */
    GM_STACK_CHECK(&F, 7956u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = F.l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        void * t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t4) + 0));
        uint8_t t6 = gm_f_has_param_u9t0efs(t3, GM_CONV(gm_csguankko4lmhd, t5));
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l2;
        uint8_t *t8 = (((uint8_t *)t7) + 0);
        void * t9 = F.l0;
        void * t10 = F.l1;
        void * t11 = gm_l2;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 0));
        gm_t88rcucg1b7uk t13 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t9, t10, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, t8, t13);
    }
    {
        void * t14 = gm_l2;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
        gm_l4 = t15;
    }
    {
        void * t16 = F.l1;
        gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 96));
        gm_l5 = t17;
    }
    {
        void * t19 = gm_l2;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t20 != ((uint8_t)9ULL)) goto ie18;
        uint32_t t21 = GM_LD(uint32_t, ((((uint8_t *)t19) + 8) + 8));
        gm_l6 = t21;
        {
            {
                void * t24 = F.l1;
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 0));
                gm_l7 = t25;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t27 = gm_l8;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t29 = (t27 < t28);
                    if (!t29) goto lb26;
                    {
                        uint64_t t32 = gm_l8;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_tplpbqf3duvn3 t37 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t33 * 8));
                        gm_l9 = t37;
                        {
                            uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
                            uint32_t t41 = gm_l6;
                            uint8_t t42 = (!(t40 == t41));
                            if (!t42) goto ie39;
                            {
                                goto lc26;
                            }
                            goto ix39;
ie39:;
ix39:;
                        }
                        {
                            void * t45 = F.l0;
                            void * t46 = (void *)(((uint8_t *)t45) + 8);
                            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 4));
                            gm_t53at80pfdus3 t48;
                            gm_f_kind_jcnt4hm((void *)&t48, t46, GM_CONV(gm_csguankko4lmhd, t47));
                            gm_t53at80pfdus3 t49 = t48;
                            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                            if (t50 != ((uint8_t)21ULL)) goto ie44;
                            gm_t428ue2u3hf5g t51 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t49) + 8));
                            gm_l10 = t51;
                            {
                                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                switch (t59) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma55; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma57; default: goto ma58; }
ma54:;
                                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t61 != ((uint8_t)0ULL)) goto mn60;
                                gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l10) + 8) + 0));
                                gm_l11 = t62;
                                gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l10) + 8) + 8));
                                gm_l12 = t63;
                                {
                                    void * t66 = F.l0;
                                    gm_thrdpodrs8pgt t67;
                                    gm_f_key_int_pqscuvn((void *)&t67, (void *)NULL, cx, NULL, t66, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l10)));
                                    if (gm_unw.kind) {
                                        goto gm_ll30;
                                    }
                                    gm_thrdpodrs8pgt t68 = t67;
                                    if (!((((uint8_t *)&t68))[48] != 0)) goto ie65;
                                    gm_tbci5ospk5nnn t69 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&t68));
                                    F.l13 = t69;
                                    {
                                        void * t71 = gm_l2;
                                        gm_tbci5ospk5nnn t72 = F.l13;
                                        gm_teti411eujvf4 t73;
                                        GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t73 + 56, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t73 + 8), t72);
                                        GM_ST(gm_teti411eujvf4, (((uint8_t *)t71) + 8), t73);
                                    }
                                    goto ix65;
ie65:;
                                    void * t75 = F.l0;
                                    void * t76 = (void *)(((uint8_t *)t75) + 8);
                                    gm_t88rcucg1b7uk t77 = gm_l11;
                                    gm_t53at80pfdus3 t78;
                                    gm_f_kind_jcnt4hm((void *)&t78, t76, GM_CONV(gm_csguankko4lmhd, t77));
                                    gm_t53at80pfdus3 t79 = t78;
                                    uint8_t t80 = ((GM_LD(uint8_t, ((uint8_t *)&t79) + 0) == ((uint8_t)4ULL)));
                                    if (!t80) goto ie74;
                                    {
                                        void * t82 = gm_l2;
                                        uint8_t t83 = 0;
                                        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        uint8_t t85 = (t84 > ((uint64_t)0ULL));
                                        if (t85) {
                                            uint64_t t86 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                            if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_src5eu5l4a7gv6s, 136, 58);
                                            uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t86 * 1));
                                            uint8_t t89 = ((t88 == ((uint8_t)1ULL)));
                                            t83 = t89;
                                        } else t83 = 0;
                                        gm_teti411eujvf4 t90;
                                        memset(&t90, 0, sizeof t90);
                                        GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)4ULL));
                                        GM_ST(uint8_t, ((uint8_t *)&t90 + 8), t83);
                                        GM_ST(gm_teti411eujvf4, (((uint8_t *)t82) + 8), t90);
                                    }
                                    goto ix74;
ie74:;
                                    void * t92 = F.l1;
                                    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t92) + 108));
                                    if (!t93) goto ie91;
                                    {
                                        void * t95 = F.l0;
                                        void * t96 = gm_l2;
                                        gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
                                        gm_b16_8 t98;
                                        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sjg57h040l08d));
                                        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(55));
                                        gm_b0_1 t99 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t95, t97, t98);
                                        if (gm_unw.kind) {
                                            goto gm_ll30;
                                        }
                                    }
                                    goto ix91;
ie91:;
ix91:;
ix74:;
ix65:;
                                }
                                goto me53;
mn60:;
ma55:;
                                uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t101 != ((uint8_t)1ULL)) goto mn100;
                                uint32_t t102 = GM_LD(uint32_t, ((((uint8_t *)&gm_l10) + 8) + 0));
                                gm_l14 = t102;
                                {
                                    void * t104 = gm_l2;
                                    uint32_t t105 = gm_l14;
                                    gm_teti411eujvf4 t106;
                                    memset(&t106, 0, sizeof t106);
                                    GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)26ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&t106 + 8), t105);
                                    GM_ST(gm_teti411eujvf4, (((uint8_t *)t104) + 8), t106);
                                }
                                goto me53;
mn100:;
ma56:;
                                uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t108 != ((uint8_t)2ULL)) goto mn107;
                                {
                                    void * t110 = gm_l2;
                                    gm_teti411eujvf4 t111;
                                    memset(&t111, 0, sizeof t111);
                                    GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)7ULL));
                                    GM_ST(gm_teti411eujvf4, (((uint8_t *)t110) + 8), t111);
                                }
                                goto me53;
mn107:;
ma57:;
                                uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t113 != ((uint8_t)3ULL)) goto mn112;
                                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                                gm_l15 = t114;
                                {
                                    void * t116 = gm_l2;
                                    gm_b16_8 t117 = gm_l15;
                                    gm_teti411eujvf4 t118;
                                    memset(&t118, 0, sizeof t118);
                                    GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)5ULL));
                                    GM_ST(gm_b16_8, ((uint8_t *)&t118 + 8), t117);
                                    GM_ST(gm_teti411eujvf4, (((uint8_t *)t116) + 8), t118);
                                }
                                goto me53;
mn112:;
ma58:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 131, 13);
me53:;
                            }
                            {
                                gm_b0_1 t119;
                                uint8_t t120 = 0;
                                F.ret = t119;
                                goto gm_fret;
                            }
                            goto ix44;
ie44:;
                            {
                                goto lb26;
                            }
ix44:;
                        }
                        goto me35;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 128, 9);
me35:;
                    }
lc26:;
                    {
                        uint64_t t123 = gm_l8;
                        uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                        uint64_t t126 = r124;
                        gm_l8 = t126;
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
            gm_b0_1 t127;
            uint8_t t128 = 0;
            F.ret = t127;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t130 = gm_l2;
        uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t130) + 8) + 0));
        if (t131 != ((uint8_t)0ULL)) goto ie129;
        gm_tbci5ospk5nnn t132 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t130) + 8) + 8));
        F.l16 = t132;
        uint8_t t133 = gm_l3;
        if (!t133) goto ie129;
        {
            void * t136 = F.l0;
            void * t137 = (void *)(((uint8_t *)t136) + 8);
            gm_t88rcucg1b7uk t138 = gm_l4;
            gm_t53at80pfdus3 t139;
            gm_f_kind_jcnt4hm((void *)&t139, t137, GM_CONV(gm_csguankko4lmhd, t138));
            gm_t53at80pfdus3 t140 = t139;
            uint8_t t141;
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&t140) + 0));
            if (t144 != ((uint8_t)3ULL)) goto mn143;
            t141 = ((uint8_t)1);
            goto me142;
mn143:;
            t141 = ((uint8_t)0);
            goto me142;
mn145:;
            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 151, 12);
me142:;
            if (!t141) goto ie135;
            {
                void * t147 = gm_l2;
                uint8_t *t148 = (((uint8_t *)t147) + 8);
                gm_tngtuiuskn938 t149;
                gm_f_from_int_ofjuoqh((void *)&t149, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_untyped_float_m6lbr0g((void *)&(*(gm_teti411eujvf4 *)t148), (void *)NULL, cx, NULL, &t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix135;
ie135:;
            void * t151 = F.l0;
            gm_t88rcucg1b7uk t152 = gm_l4;
            uint8_t t153 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t151, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)), t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t154 = (uint8_t)!t153;
            if (!t154) goto ie150;
            {
                void * t156 = F.l0;
                gm_tl874t2atnlh3 t157 = gm_l5;
                gm_t6j6smj7qoctu t158;
                gm_b16_8 t159 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t158 + 0, t159);
                void * t160 = F.l0;
                gm_t88rcucg1b7uk t161 = gm_l4;
                gm_b16_8 t162 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t160, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t158 + 16, t162);
                gm_tabb710d9mq2e t163;
                gm_f_format_afagrs5((void *)&t163, (void *)NULL, cx, NULL, &t158);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t164 = t163;
                if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 8, gm_src5eu5l4a7gv6s, 154, 27);
                gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
                gm_b16_8 t166 = t165;
                gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                gm_b0_1 t168 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t156, t157, t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix150;
ie150:;
ix150:;
ix135:;
        }
        goto ix129;
ie129:;
ix129:;
    }
    {
        void * t169 = gm_l2;
        uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)t169) + 8) + 0));
        switch (t177) { case ((uint8_t)0ULL): goto ma175; case ((uint8_t)1ULL): goto ma175; case ((uint8_t)2ULL): goto ma175; case ((uint8_t)3ULL): goto ma175; case ((uint8_t)4ULL): goto ma175; case ((uint8_t)5ULL): goto ma175; case ((uint8_t)6ULL): goto ma175; case ((uint8_t)7ULL): goto ma175; case ((uint8_t)8ULL): goto ma175; case ((uint8_t)9ULL): goto ma175; case ((uint8_t)10ULL): goto ma171; case ((uint8_t)11ULL): goto ma172; case ((uint8_t)12ULL): goto ma173; case ((uint8_t)13ULL): goto ma174; case ((uint8_t)14ULL): goto ma175; case ((uint8_t)15ULL): goto ma175; case ((uint8_t)16ULL): goto ma175; case ((uint8_t)17ULL): goto ma175; case ((uint8_t)18ULL): goto ma175; case ((uint8_t)19ULL): goto ma175; case ((uint8_t)20ULL): goto ma175; case ((uint8_t)21ULL): goto ma175; case ((uint8_t)22ULL): goto ma175; case ((uint8_t)23ULL): goto ma175; case ((uint8_t)24ULL): goto ma175; case ((uint8_t)25ULL): goto ma175; case ((uint8_t)26ULL): goto ma175; case ((uint8_t)27ULL): goto ma175; case ((uint8_t)28ULL): goto ma175; case ((uint8_t)29ULL): goto ma175; case ((uint8_t)30ULL): goto ma175; case ((uint8_t)31ULL): goto ma175; case ((uint8_t)32ULL): goto ma175; case ((uint8_t)33ULL): goto ma175; case ((uint8_t)34ULL): goto ma175; case ((uint8_t)35ULL): goto ma175; case ((uint8_t)36ULL): goto ma175; case ((uint8_t)37ULL): goto ma175; case ((uint8_t)38ULL): goto ma175; case ((uint8_t)39ULL): goto ma175; case ((uint8_t)40ULL): goto ma175; case ((uint8_t)41ULL): goto ma175; case ((uint8_t)42ULL): goto ma175; case ((uint8_t)43ULL): goto ma175; case ((uint8_t)44ULL): goto ma175; case ((uint8_t)45ULL): goto ma175; case ((uint8_t)46ULL): goto ma175; case ((uint8_t)47ULL): goto ma175; case ((uint8_t)48ULL): goto ma175; case ((uint8_t)49ULL): goto ma175; case ((uint8_t)50ULL): goto ma175; case ((uint8_t)51ULL): goto ma175; case ((uint8_t)52ULL): goto ma175; case ((uint8_t)53ULL): goto ma175; case ((uint8_t)54ULL): goto ma175; case ((uint8_t)55ULL): goto ma175; case ((uint8_t)56ULL): goto ma175; case ((uint8_t)57ULL): goto ma175; case ((uint8_t)58ULL): goto ma175; default: goto ma175; }
ma171:;
        uint8_t t179 = GM_LD(uint8_t, ((((uint8_t *)t169) + 8) + 0));
        if (t179 != ((uint8_t)10ULL)) goto mn178;
        gm_t88rcucg1b7uk t180 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t169) + 8) + 8) + 0));
        gm_l17 = t180;
        uint8_t t181 = GM_LD(uint8_t, (((((uint8_t *)t169) + 8) + 8) + 4));
        gm_l18 = t181;
        {
            void * t183 = F.l0;
            gm_t88rcucg1b7uk t184 = gm_l17;
            void * t185 = F.l1;
            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)t185) + 0));
            gm_t88rcucg1b7uk t187 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t183, t184, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t187;
        }
        {
            void * t189 = F.l0;
            void * t190 = (void *)(((uint8_t *)t189) + 8);
            gm_t88rcucg1b7uk t191 = gm_l19;
            uint8_t t192 = gm_f_has_param_u9t0efs(t190, GM_CONV(gm_csguankko4lmhd, t191));
            if (!t192) goto ie188;
            {
                void * t194 = gm_l2;
                gm_t88rcucg1b7uk t195 = gm_l19;
                uint8_t t196 = gm_l18;
                gm_tkn42u7na3t81 t197;
                GM_ST(uint8_t, (uint8_t *)&t197 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t197 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t197 + 0), t195);
                GM_ST(uint8_t, ((uint8_t *)&t197 + 4), t196);
                gm_teti411eujvf4 t198;
                memset(&t198, 0, sizeof t198);
                GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)10ULL));
                GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t198 + 8), t197);
                GM_ST(gm_teti411eujvf4, (((uint8_t *)t194) + 8), t198);
            }
            goto ix188;
ie188:;
            {
                void * t200 = F.l0;
                gm_t88rcucg1b7uk t201 = gm_l19;
                gm_f_layout_a5ovnrm((void *)&gm_l20, (void *)NULL, cx, NULL, t200, t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t202 = gm_l2;
                uint8_t *t203 = (((uint8_t *)t202) + 8);
                uint64_t t204;
                uint8_t t206 = gm_l18;
                if (!t206) goto ie205;
                uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint64_t t209 = t208;
                t204 = t209;
                goto ix205;
ie205:;
                uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 0));
                uint64_t t212 = t211;
                t204 = t212;
ix205:;
                gm_tbci5ospk5nnn t213;
                gm_f_from_u64_ud13q5v((void *)&t213, (void *)NULL, cx, NULL, t204);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_teti411eujvf4 t214;
                GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t214 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t214 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t214 + 8), t213);
                GM_ST(gm_teti411eujvf4, t203, t214);
            }
ix188:;
        }
        goto me170;
mn178:;
ma172:;
        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)t169) + 8) + 0));
        if (t216 != ((uint8_t)11ULL)) goto mn215;
        gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t169) + 8) + 8) + 0));
        gm_l21 = t217;
        uint8_t t218 = GM_LD(uint8_t, (((((uint8_t *)t169) + 8) + 8) + 4));
        gm_l22 = t218;
        {
            void * t220 = F.l0;
            gm_t88rcucg1b7uk t221 = gm_l21;
            void * t222 = F.l1;
            gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)t222) + 0));
            gm_t88rcucg1b7uk t224 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t220, t221, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t224;
        }
        {
            void * t225 = gm_l2;
            uint8_t *t226 = (((uint8_t *)t225) + 8);
            gm_teti411eujvf4 t227;
            void * t229 = F.l0;
            gm_t88rcucg1b7uk t230 = gm_l23;
            uint8_t t231 = gm_l22;
            gm_thrdpodrs8pgt t232;
            gm_f_int_limit_nncqn56((void *)&t232, (void *)NULL, cx, NULL, t229, t230, t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thrdpodrs8pgt t233 = t232;
            if (!((((uint8_t *)&t233))[48] != 0)) goto ie228;
            gm_tbci5ospk5nnn t234 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&t233));
            F.l24 = t234;
            gm_tbci5ospk5nnn t236 = F.l24;
            gm_teti411eujvf4 t237;
            GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t237 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t237 + 8), t236);
            gm_teti411eujvf4 t238 = t237;
            t227 = t238;
            goto ix228;
ie228:;
            gm_t88rcucg1b7uk t240 = gm_l23;
            uint8_t t241 = gm_l22;
            gm_tkn42u7na3t81 t242;
            GM_ST(uint8_t, (uint8_t *)&t242 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t242 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t242 + 0), t240);
            GM_ST(uint8_t, ((uint8_t *)&t242 + 4), t241);
            gm_teti411eujvf4 t243;
            memset(&t243, 0, sizeof t243);
            GM_ST(uint8_t, ((uint8_t *)&t243 + 0), ((uint8_t)11ULL));
            GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t243 + 8), t242);
            gm_teti411eujvf4 t244 = t243;
            t227 = t244;
ix228:;
            GM_ST(gm_teti411eujvf4, t226, t227);
        }
        goto me170;
mn215:;
ma173:;
        uint8_t t246 = GM_LD(uint8_t, ((((uint8_t *)t169) + 8) + 0));
        if (t246 != ((uint8_t)12ULL)) goto mn245;
        gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t169) + 8) + 8));
        gm_l25 = t247;
        {
            void * t249 = F.l0;
            gm_t88rcucg1b7uk t250 = gm_l25;
            void * t251 = F.l1;
            gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)t251) + 0));
            gm_t88rcucg1b7uk t253 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t249, t250, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t253;
        }
        {
            void * t254 = gm_l2;
            uint8_t *t255 = (((uint8_t *)t254) + 8);
            gm_teti411eujvf4 t256;
            void * t258 = F.l0;
            void * t259 = (void *)(((uint8_t *)t258) + 8);
            gm_t88rcucg1b7uk t260 = gm_l26;
            uint8_t t261 = gm_f_has_param_u9t0efs(t259, GM_CONV(gm_csguankko4lmhd, t260));
            if (!t261) goto ie257;
            gm_t88rcucg1b7uk t263 = gm_l26;
            gm_teti411eujvf4 t264;
            memset(&t264, 0, sizeof t264);
            GM_ST(uint8_t, ((uint8_t *)&t264 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t264 + 8), t263);
            gm_teti411eujvf4 t265 = t264;
            t256 = t265;
            goto ix257;
ie257:;
            void * t267 = F.l0;
            gm_t88rcucg1b7uk t268 = gm_l26;
            gm_b16_8 t269 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t267, t268);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t270;
            memset(&t270, 0, sizeof t270);
            GM_ST(uint8_t, ((uint8_t *)&t270 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t270 + 8), t269);
            gm_teti411eujvf4 t271 = t270;
            t256 = t271;
ix257:;
            GM_ST(gm_teti411eujvf4, t255, t256);
        }
        goto me170;
mn245:;
ma174:;
        uint8_t t273 = GM_LD(uint8_t, ((((uint8_t *)t169) + 8) + 0));
        if (t273 != ((uint8_t)13ULL)) goto mn272;
        gm_t88rcucg1b7uk t274 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t169) + 8) + 8) + 0));
        gm_l27 = t274;
        gm_t88rcucg1b7uk t275 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t169) + 8) + 8) + 4));
        gm_l28 = t275;
        uint8_t t276 = GM_LD(uint8_t, (((((uint8_t *)t169) + 8) + 8) + 8));
        gm_l29 = t276;
        {
            void * t278 = F.l0;
            gm_t88rcucg1b7uk t279 = gm_l27;
            void * t280 = F.l1;
            gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)t280) + 0));
            gm_t88rcucg1b7uk t282 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t278, t279, t281);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t282;
        }
        {
            void * t283 = F.l0;
            gm_t88rcucg1b7uk t284 = gm_l28;
            void * t285 = F.l1;
            gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)t285) + 0));
            gm_t88rcucg1b7uk t287 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t283, t284, t286);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l31 = t287;
        }
        {
            void * t288 = gm_l2;
            uint8_t *t289 = (((uint8_t *)t288) + 8);
            gm_teti411eujvf4 t290;
            uint8_t t292 = 0;
            void * t293 = F.l0;
            void * t294 = (void *)(((uint8_t *)t293) + 8);
            gm_t88rcucg1b7uk t295 = gm_l30;
            uint8_t t296 = gm_f_has_param_u9t0efs(t294, GM_CONV(gm_csguankko4lmhd, t295));
            if (!t296) {
                void * t297 = F.l0;
                void * t298 = (void *)(((uint8_t *)t297) + 8);
                gm_t88rcucg1b7uk t299 = gm_l31;
                uint8_t t300 = gm_f_has_param_u9t0efs(t298, GM_CONV(gm_csguankko4lmhd, t299));
                t292 = t300;
            } else t292 = 1;
            if (!t292) goto ie291;
            gm_t88rcucg1b7uk t302 = gm_l30;
            gm_t88rcucg1b7uk t303 = gm_l31;
            uint8_t t304 = gm_l29;
            gm_t3rcjibl8kkg4 t305;
            GM_ST(uint8_t, (uint8_t *)&t305 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t305 + 10, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t305 + 0), t302);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t305 + 4), t303);
            GM_ST(uint8_t, ((uint8_t *)&t305 + 8), t304);
            gm_teti411eujvf4 t306;
            memset(&t306, 0, sizeof t306);
            GM_ST(uint8_t, ((uint8_t *)&t306 + 0), ((uint8_t)13ULL));
            GM_ST(gm_t3rcjibl8kkg4, ((uint8_t *)&t306 + 8), t305);
            gm_teti411eujvf4 t307 = t306;
            t290 = t307;
            goto ix291;
ie291:;
            gm_t88rcucg1b7uk t309 = gm_l30;
            gm_t88rcucg1b7uk t310 = gm_l31;
            gm_t88rcucg1b7uk t311 = t309;
            gm_t88rcucg1b7uk t312 = t310;
            uint8_t t313 = (gm_eq9m6mvtmgde(((uint8_t *)&t311), ((uint8_t *)&t312)));
            uint8_t t314 = gm_l29;
            uint8_t t315 = (!(t313 == t314));
            gm_teti411eujvf4 t316;
            memset(&t316, 0, sizeof t316);
            GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)4ULL));
            GM_ST(uint8_t, ((uint8_t *)&t316 + 8), t315);
            gm_teti411eujvf4 t317 = t316;
            t290 = t317;
ix291:;
            GM_ST(gm_teti411eujvf4, t289, t290);
        }
        goto me170;
mn272:;
ma175:;
        goto me170;
mn318:;
ma176:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 157, 5);
me170:;
    }
    {
        void * t321 = gm_l2;
        uint8_t t322 = GM_LD(uint8_t, ((((uint8_t *)t321) + 8) + 0));
        if (t322 != ((uint8_t)2ULL)) goto ie320;
        void * t323 = GM_LD(void *, ((((uint8_t *)t321) + 8) + 8));
        gm_l32 = t323;
        {
            void * t326 = F.l0;
            void * t327 = (void *)(((uint8_t *)t326) + 8);
            gm_t88rcucg1b7uk t328 = gm_l4;
            gm_t53at80pfdus3 t329;
            gm_f_kind_jcnt4hm((void *)&t329, t327, GM_CONV(gm_csguankko4lmhd, t328));
            gm_t53at80pfdus3 t330 = t329;
            uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&t330) + 0));
            if (t331 != ((uint8_t)3ULL)) goto ie325;
            uint16_t t332 = GM_LD(uint16_t, (((uint8_t *)&t330) + 8));
            gm_l33 = t332;
            {
                void * t334 = gm_l32;
                uint16_t t335 = gm_l33;
                double t336 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t334)), t335);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t336;
            }
            {
                double t338 = gm_l34;
                double t339 = gm_l34;
                double t340 = (t338 - t339);
                uint8_t t341 = (!(t340 == ((double)0e0)));
                if (!t341) goto ie337;
                {
                    void * t343 = F.l0;
                    gm_tl874t2atnlh3 t344 = gm_l5;
                    void * t345 = F.l0;
                    gm_t88rcucg1b7uk t346 = gm_l4;
                    gm_b16_8 t347 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t345, t346);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t348;
                    GM_ST(gm_b16_8, ((uint8_t *)&t348 + 0), t347);
                    gm_tabb710d9mq2e t349;
                    gm_f_format_r4smojn((void *)&t349, (void *)NULL, cx, NULL, t348);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t350 = t349;
                    if (((((uint8_t *)&t350))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t350) + 8, gm_src5eu5l4a7gv6s, 186, 31);
                    gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)&t350) + 8));
                    gm_b16_8 t352 = t351;
                    gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&t352));
                    gm_b0_1 t354 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t343, t344, t353);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix337;
ie337:;
                {
                    void * t356 = gm_l2;
                    double t357 = gm_l34;
                    gm_teti411eujvf4 t358;
                    memset(&t358, 0, sizeof t358);
                    GM_ST(uint8_t, ((uint8_t *)&t358 + 0), ((uint8_t)1ULL));
                    GM_ST(double, ((uint8_t *)&t358 + 8), t357);
                    GM_ST(gm_teti411eujvf4, (((uint8_t *)t356) + 8), t358);
                }
ix337:;
            }
            goto ix325;
ie325:;
            {
                void * t360 = F.l0;
                gm_tl874t2atnlh3 t361 = gm_l5;
                void * t362 = F.l0;
                gm_t88rcucg1b7uk t363 = gm_l4;
                gm_b16_8 t364 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t362, t363);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t365;
                GM_ST(gm_b16_8, ((uint8_t *)&t365 + 0), t364);
                gm_tabb710d9mq2e t366;
                gm_f_format_ht23jk8((void *)&t366, (void *)NULL, cx, NULL, t365);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t367 = t366;
                if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 8, gm_src5eu5l4a7gv6s, 191, 27);
                gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&t367) + 8));
                gm_b16_8 t369 = t368;
                gm_b16_8 t370 = GM_LD(gm_b16_8, ((uint8_t *)&t369));
                gm_b0_1 t371 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t360, t361, t370);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix325:;
        }
        goto ix320;
ie320:;
ix320:;
    }
    {
        void * t373 = gm_l2;
        uint8_t t374 = GM_LD(uint8_t, ((((uint8_t *)t373) + 8) + 0));
        if (t374 != ((uint8_t)58ULL)) goto ie372;
        gm_t710lsrq1m8gk t375 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t373) + 8) + 8));
        F.l35 = t375;
        {
            {
                gm_l37 = ((uint64_t)0ULL);
            }
            {
                uint64_t t378 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 8) + 8);
                gm_l36 = t378;
            }
            {
                for (;;) {
                    uint64_t t380 = gm_l37;
                    uint64_t t381 = gm_l36;
                    uint8_t t382 = (t380 < t381);
                    if (!t382) goto lb379;
                    {
                        uint64_t t385 = gm_l37;
                        uint64_t t386 = t385;
                        uint64_t t389 = GM_LD(uint64_t, ((uint8_t *)&t386));
                        gm_l38 = t389;
                        {
                            uint64_t t392 = gm_l38;
                            uint64_t t393 = (uintptr_t)t392;
                            uint64_t t394 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 8) + 8);
                            if (t393 >= (uintptr_t)(t394)) gm_trap_index(t393, t394, gm_src5eu5l4a7gv6s, 197, 35);
                            uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l35) + 8)) + t393 * 32) + 0));
                            if (t395 != ((uint8_t)1ULL)) goto ie391;
                            void * t396 = GM_LD(void *, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l35) + 8)) + t393 * 32) + 8) + 16));
                            gm_l39 = t396;
                            {
                                uint64_t t398 = gm_l38;
                                uint64_t t399 = (uintptr_t)t398;
                                uint64_t t400 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 8) + 8);
                                if (t399 >= (uintptr_t)(t400)) gm_trap_index(t399, t400, gm_src5eu5l4a7gv6s, 198, 17);
                                uint8_t *t401 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l35) + 8)) + t399 * 32);
                                gm_tpe1hv3fekn05 t402;
                                void * t403 = F.l0;
                                void * t404 = F.l0;
                                void * t405 = gm_l39;
                                gm_t88rcucg1b7uk t406 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t405) + 0));
                                void * t407 = F.l1;
                                gm_b16_8 t408 = GM_LD(gm_b16_8, (((uint8_t *)t407) + 0));
                                gm_t88rcucg1b7uk t409 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t404, t406, t408);
                                if (gm_unw.kind) {
                                    goto gm_ll383;
                                }
                                gm_b16_8 t410 = gm_f_handlers_of_type_5gareb4((void *)NULL, cx, NULL, t403, t409);
                                if (gm_unw.kind) {
                                    goto gm_ll383;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t402 + 0, t410);
                                void * t411 = gm_l39;
                                GM_ST(void *, (uint8_t *)&t402 + 16, t411);
                                gm_t4lnf71mruqpt t412;
                                GM_ST(uint8_t, (uint8_t *)&t412 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t412 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t412 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t412 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t412 + 8), t402);
                                GM_ST(gm_t4lnf71mruqpt, t401, t412);
                            }
                            goto ix391;
ie391:;
ix391:;
                        }
                        goto me387;
mn388:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 196, 9);
me387:;
                    }
lc379:;
                    {
                        uint64_t t414 = gm_l37;
                        uint64_t r415; bool o416 = __builtin_add_overflow(t414, ((uint64_t)1ULL), &r415);
                        uint64_t t417 = r415;
                        gm_l37 = t417;
                    }
                }
gm_ll383:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb379; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc379; }
                }
                goto gm_lad;
lb379:;
            }
        }
        goto ix372;
ie372:;
ix372:;
    }
    {
        void * t418 = gm_l2;
        uint8_t t428 = GM_LD(uint8_t, ((((uint8_t *)t418) + 8) + 0));
        switch (t428) { case ((uint8_t)0ULL): goto ma426; case ((uint8_t)1ULL): goto ma426; case ((uint8_t)2ULL): goto ma426; case ((uint8_t)3ULL): goto ma426; case ((uint8_t)4ULL): goto ma426; case ((uint8_t)5ULL): goto ma426; case ((uint8_t)6ULL): goto ma426; case ((uint8_t)7ULL): goto ma426; case ((uint8_t)8ULL): goto ma426; case ((uint8_t)9ULL): goto ma426; case ((uint8_t)10ULL): goto ma426; case ((uint8_t)11ULL): goto ma426; case ((uint8_t)12ULL): goto ma426; case ((uint8_t)13ULL): goto ma426; case ((uint8_t)14ULL): goto ma426; case ((uint8_t)15ULL): goto ma426; case ((uint8_t)16ULL): goto ma426; case ((uint8_t)17ULL): goto ma426; case ((uint8_t)18ULL): goto ma426; case ((uint8_t)19ULL): goto ma426; case ((uint8_t)20ULL): goto ma426; case ((uint8_t)21ULL): goto ma426; case ((uint8_t)22ULL): goto ma426; case ((uint8_t)23ULL): goto ma426; case ((uint8_t)24ULL): goto ma426; case ((uint8_t)25ULL): goto ma426; case ((uint8_t)26ULL): goto ma422; case ((uint8_t)27ULL): goto ma423; case ((uint8_t)28ULL): goto ma424; case ((uint8_t)29ULL): goto ma426; case ((uint8_t)30ULL): goto ma426; case ((uint8_t)31ULL): goto ma426; case ((uint8_t)32ULL): goto ma426; case ((uint8_t)33ULL): goto ma426; case ((uint8_t)34ULL): goto ma420; case ((uint8_t)35ULL): goto ma426; case ((uint8_t)36ULL): goto ma426; case ((uint8_t)37ULL): goto ma426; case ((uint8_t)38ULL): goto ma426; case ((uint8_t)39ULL): goto ma426; case ((uint8_t)40ULL): goto ma426; case ((uint8_t)41ULL): goto ma426; case ((uint8_t)42ULL): goto ma426; case ((uint8_t)43ULL): goto ma426; case ((uint8_t)44ULL): goto ma426; case ((uint8_t)45ULL): goto ma426; case ((uint8_t)46ULL): goto ma426; case ((uint8_t)47ULL): goto ma426; case ((uint8_t)48ULL): goto ma426; case ((uint8_t)49ULL): goto ma426; case ((uint8_t)50ULL): goto ma426; case ((uint8_t)51ULL): goto ma426; case ((uint8_t)52ULL): goto ma426; case ((uint8_t)53ULL): goto ma426; case ((uint8_t)54ULL): goto ma426; case ((uint8_t)55ULL): goto ma425; case ((uint8_t)56ULL): goto ma426; case ((uint8_t)57ULL): goto ma426; case ((uint8_t)58ULL): goto ma426; default: goto ma426; }
ma420:;
        uint8_t t430 = GM_LD(uint8_t, ((((uint8_t *)t418) + 8) + 0));
        if (t430 != ((uint8_t)34ULL)) goto mn429;
        uint8_t t431 = GM_LD(uint8_t, ((((((uint8_t *)t418) + 8) + 8) + 0) + 0));
        if (t431 != ((uint8_t)0ULL)) goto mn429;
        uint32_t t432 = GM_LD(uint32_t, ((((((uint8_t *)t418) + 8) + 8) + 0) + 8));
        gm_l40 = t432;
        gm_b16_8 t433 = GM_LD(gm_b16_8, (((((uint8_t *)t418) + 8) + 8) + 40));
        gm_l41 = t433;
        {
            void * t435 = gm_l2;
            uint8_t *t436 = (((uint8_t *)t435) + 8);
            gm_to76jli3bqddq t437;
            void * t438 = F.l0;
            void * t439 = F.l1;
            uint32_t t440 = gm_l40;
            uint32_t t441 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t438, t439, t440);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tsth9qu1dv0g3 t442;
            memset(&t442, 0, sizeof t442);
            GM_ST(uint8_t, ((uint8_t *)&t442 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t442 + 8), t441);
            GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t437 + 0, t442);
            gm_b16_8 t443 = gm_l41;
            GM_ST(gm_b16_8, (uint8_t *)&t437 + 40, t443);
            gm_teti411eujvf4 t444;
            GM_ST(uint8_t, (uint8_t *)&t444 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t444 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t444 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t444 + 0), ((uint8_t)34ULL));
            GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t444 + 8), t437);
            GM_ST(gm_teti411eujvf4, t436, t444);
        }
        goto me419;
mn429:;
ma421:;
        uint8_t t446 = GM_LD(uint8_t, ((((uint8_t *)t418) + 8) + 0));
        if (t446 != ((uint8_t)34ULL)) goto mn445;
        uint8_t t447 = GM_LD(uint8_t, ((((((uint8_t *)t418) + 8) + 8) + 0) + 0));
        if (t447 != ((uint8_t)1ULL)) goto mn445;
        gm_tiilmfd16v3fc t448 = GM_LD(gm_tiilmfd16v3fc, (((((((uint8_t *)t418) + 8) + 8) + 0) + 8) + 0));
        gm_l42 = t448;
        uint32_t t449 = GM_LD(uint32_t, (((((((uint8_t *)t418) + 8) + 8) + 0) + 8) + 24));
        gm_l43 = t449;
        gm_b16_8 t450 = GM_LD(gm_b16_8, (((((uint8_t *)t418) + 8) + 8) + 40));
        gm_l44 = t450;
        {
            void * t452 = gm_l2;
            uint8_t *t453 = (((uint8_t *)t452) + 8);
            gm_to76jli3bqddq t454;
            gm_t419mo45g7j0a t455;
            memset((uint8_t *)&t455 + 28, 0, 4);
            void * t456 = F.l0;
            void * t457 = (void *)((uint8_t *)&gm_l42);
            void * t458 = F.l1;
            gm_b16_8 t459 = GM_LD(gm_b16_8, (((uint8_t *)t458) + 0));
            gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t455 + 0)), (void *)NULL, cx, NULL, t456, t457, t459);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t460 = gm_l43;
            GM_ST(uint32_t, (uint8_t *)&t455 + 24, t460);
            gm_tsth9qu1dv0g3 t461;
            GM_ST(uint8_t, (uint8_t *)&t461 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t461 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t461 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t461 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t461 + 8), t455);
            GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t454 + 0, t461);
            gm_b16_8 t462 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t454 + 40, t462);
            gm_teti411eujvf4 t463;
            GM_ST(uint8_t, (uint8_t *)&t463 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t463 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t463 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t463 + 0), ((uint8_t)34ULL));
            GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t463 + 8), t454);
            GM_ST(gm_teti411eujvf4, t453, t463);
        }
        goto me419;
mn445:;
ma422:;
        uint8_t t465 = GM_LD(uint8_t, ((((uint8_t *)t418) + 8) + 0));
        if (t465 != ((uint8_t)26ULL)) goto mn464;
        uint32_t t466 = GM_LD(uint32_t, ((((uint8_t *)t418) + 8) + 8));
        gm_l45 = t466;
        {
            void * t468 = gm_l2;
            uint8_t *t469 = (((uint8_t *)t468) + 8);
            void * t470 = F.l0;
            void * t471 = F.l1;
            uint32_t t472 = gm_l45;
            uint32_t t473 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t470, t471, t472);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t474;
            memset(&t474, 0, sizeof t474);
            GM_ST(uint8_t, ((uint8_t *)&t474 + 0), ((uint8_t)26ULL));
            GM_ST(uint32_t, ((uint8_t *)&t474 + 8), t473);
            GM_ST(gm_teti411eujvf4, t469, t474);
        }
        goto me419;
mn464:;
ma423:;
        uint8_t t476 = GM_LD(uint8_t, ((((uint8_t *)t418) + 8) + 0));
        if (t476 != ((uint8_t)27ULL)) goto mn475;
        uint32_t t477 = GM_LD(uint32_t, ((((uint8_t *)t418) + 8) + 8));
        gm_l46 = t477;
        {
            void * t479 = gm_l2;
            uint8_t *t480 = (((uint8_t *)t479) + 8);
            void * t481 = F.l0;
            void * t482 = F.l1;
            uint32_t t483 = gm_l46;
            uint32_t t484 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t481, t482, t483);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t485;
            memset(&t485, 0, sizeof t485);
            GM_ST(uint8_t, ((uint8_t *)&t485 + 0), ((uint8_t)27ULL));
            GM_ST(uint32_t, ((uint8_t *)&t485 + 8), t484);
            GM_ST(gm_teti411eujvf4, t480, t485);
        }
        goto me419;
mn475:;
ma424:;
        uint8_t t487 = GM_LD(uint8_t, ((((uint8_t *)t418) + 8) + 0));
        if (t487 != ((uint8_t)28ULL)) goto mn486;
        uint32_t t488 = GM_LD(uint32_t, ((((uint8_t *)t418) + 8) + 8));
        gm_l47 = t488;
        {
            void * t490 = gm_l2;
            uint8_t *t491 = (((uint8_t *)t490) + 8);
            void * t492 = F.l0;
            void * t493 = F.l1;
            uint32_t t494 = gm_l47;
            uint32_t t495 = gm_f_mono_handler_38b78qh((void *)NULL, cx, NULL, t492, t493, t494);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t496;
            memset(&t496, 0, sizeof t496);
            GM_ST(uint8_t, ((uint8_t *)&t496 + 0), ((uint8_t)28ULL));
            GM_ST(uint32_t, ((uint8_t *)&t496 + 8), t495);
            GM_ST(gm_teti411eujvf4, t491, t496);
        }
        goto me419;
mn486:;
ma425:;
        uint8_t t498 = GM_LD(uint8_t, ((((uint8_t *)t418) + 8) + 0));
        if (t498 != ((uint8_t)55ULL)) goto mn497;
        void * t499 = GM_LD(void *, ((((uint8_t *)t418) + 8) + 8));
        gm_l48 = t499;
        void * t501 = gm_l48;
        uint8_t t502 = GM_LD(uint8_t, ((((uint8_t *)t501) + 0) + 0));
        if (t502 != ((uint8_t)0ULL)) goto ie500;
        gm_tiilmfd16v3fc t503 = GM_LD(gm_tiilmfd16v3fc, (((((uint8_t *)t501) + 0) + 8) + 0));
        gm_l49 = t503;
        uint32_t t504 = GM_LD(uint32_t, (((((uint8_t *)t501) + 0) + 8) + 24));
        gm_l50 = t504;
        {
            void * t506 = gm_l48;
            uint8_t *t507 = (((uint8_t *)t506) + 0);
            gm_t419mo45g7j0a t508;
            memset((uint8_t *)&t508 + 28, 0, 4);
            void * t509 = F.l0;
            void * t510 = (void *)((uint8_t *)&gm_l49);
            void * t511 = F.l1;
            gm_b16_8 t512 = GM_LD(gm_b16_8, (((uint8_t *)t511) + 0));
            gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t508 + 0)), (void *)NULL, cx, NULL, t509, t510, t512);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t513 = gm_l50;
            GM_ST(uint32_t, (uint8_t *)&t508 + 24, t513);
            gm_th1g69d7vpipg t514;
            GM_ST(uint8_t, (uint8_t *)&t514 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t514 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t514 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t514 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t514 + 8), t508);
            GM_ST(gm_th1g69d7vpipg, t507, t514);
        }
        goto ix500;
ie500:;
ix500:;
        goto me419;
mn497:;
ma426:;
        goto me419;
mn515:;
ma427:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 202, 5);
me419:;
    }
    {
        void * t517 = gm_l2;
        gm_closure t518 = ((gm_closure){(void *)gm_f_fn_ah95er7, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t517, t518);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t519 = gm_l2;
        uint8_t t526 = GM_LD(uint8_t, ((((uint8_t *)t519) + 8) + 0));
        switch (t526) { case ((uint8_t)0ULL): goto ma524; case ((uint8_t)1ULL): goto ma524; case ((uint8_t)2ULL): goto ma524; case ((uint8_t)3ULL): goto ma524; case ((uint8_t)4ULL): goto ma524; case ((uint8_t)5ULL): goto ma524; case ((uint8_t)6ULL): goto ma524; case ((uint8_t)7ULL): goto ma524; case ((uint8_t)8ULL): goto ma524; case ((uint8_t)9ULL): goto ma524; case ((uint8_t)10ULL): goto ma524; case ((uint8_t)11ULL): goto ma524; case ((uint8_t)12ULL): goto ma524; case ((uint8_t)13ULL): goto ma524; case ((uint8_t)14ULL): goto ma524; case ((uint8_t)15ULL): goto ma524; case ((uint8_t)16ULL): goto ma524; case ((uint8_t)17ULL): goto ma524; case ((uint8_t)18ULL): goto ma524; case ((uint8_t)19ULL): goto ma524; case ((uint8_t)20ULL): goto ma524; case ((uint8_t)21ULL): goto ma524; case ((uint8_t)22ULL): goto ma524; case ((uint8_t)23ULL): goto ma524; case ((uint8_t)24ULL): goto ma524; case ((uint8_t)25ULL): goto ma524; case ((uint8_t)26ULL): goto ma524; case ((uint8_t)27ULL): goto ma524; case ((uint8_t)28ULL): goto ma524; case ((uint8_t)29ULL): goto ma524; case ((uint8_t)30ULL): goto ma524; case ((uint8_t)31ULL): goto ma524; case ((uint8_t)32ULL): goto ma524; case ((uint8_t)33ULL): goto ma524; case ((uint8_t)34ULL): goto ma524; case ((uint8_t)35ULL): goto ma524; case ((uint8_t)36ULL): goto ma524; case ((uint8_t)37ULL): goto ma524; case ((uint8_t)38ULL): goto ma524; case ((uint8_t)39ULL): goto ma524; case ((uint8_t)40ULL): goto ma524; case ((uint8_t)41ULL): goto ma524; case ((uint8_t)42ULL): goto ma524; case ((uint8_t)43ULL): goto ma524; case ((uint8_t)44ULL): goto ma521; case ((uint8_t)45ULL): goto ma524; case ((uint8_t)46ULL): goto ma524; case ((uint8_t)47ULL): goto ma522; case ((uint8_t)48ULL): goto ma523; case ((uint8_t)49ULL): goto ma524; case ((uint8_t)50ULL): goto ma524; case ((uint8_t)51ULL): goto ma524; case ((uint8_t)52ULL): goto ma524; case ((uint8_t)53ULL): goto ma524; case ((uint8_t)54ULL): goto ma524; case ((uint8_t)55ULL): goto ma524; case ((uint8_t)56ULL): goto ma524; case ((uint8_t)57ULL): goto ma524; case ((uint8_t)58ULL): goto ma524; default: goto ma524; }
ma521:;
        uint8_t t528 = GM_LD(uint8_t, ((((uint8_t *)t519) + 8) + 0));
        if (t528 != ((uint8_t)44ULL)) goto mn527;
        gm_b16_8 t529 = GM_LD(gm_b16_8, (((((uint8_t *)t519) + 8) + 8) + 8));
        gm_l51 = t529;
        {
            gm_b16_8 t531 = gm_l51;
            gm_l52 = t531;
        }
        {
            gm_l53 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t533 = gm_l53;
                uint64_t t534 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                uint8_t t535 = (t533 < t534);
                if (!t535) goto lb532;
                {
                    uint64_t t538 = gm_l53;
                    uint64_t t539 = (uintptr_t)t538;
                    uint64_t t540 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                    gm_tbbqefuhv2r8h t543 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t539 * 24));
                    gm_l54 = t543;
                    {
                        void * t545 = F.l0;
                        void * t546 = F.l1;
                        void * t547 = GM_LD(void *, (((uint8_t *)&gm_l54) + 0));
                        gm_b0_1 t548 = gm_f_mono_pat_lipeb0h((void *)NULL, cx, NULL, t545, t546, t547);
                        if (gm_unw.kind) {
                            goto gm_ll536;
                        }
                    }
                    goto me541;
mn542:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 213, 28);
me541:;
                }
lc532:;
                {
                    uint64_t t550 = gm_l53;
                    uint64_t r551; bool o552 = __builtin_add_overflow(t550, ((uint64_t)1ULL), &r551);
                    uint64_t t553 = r551;
                    gm_l53 = t553;
                }
            }
gm_ll536:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb532; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc532; }
            }
            goto gm_lad;
lb532:;
        }
        goto me520;
mn527:;
ma522:;
        uint8_t t555 = GM_LD(uint8_t, ((((uint8_t *)t519) + 8) + 0));
        if (t555 != ((uint8_t)47ULL)) goto mn554;
        gm_trl2khmuphncd t556 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t519) + 8) + 8));
        gm_l55 = t556;
        void * t557 = F.l0;
        void * t558 = F.l1;
        gm_b16_8 t559 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 0));
        gm_b0_1 t560 = gm_f_mono_cond_pats_armprh7((void *)NULL, cx, NULL, t557, t558, t559);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me520;
mn554:;
ma523:;
        uint8_t t562 = GM_LD(uint8_t, ((((uint8_t *)t519) + 8) + 0));
        if (t562 != ((uint8_t)48ULL)) goto mn561;
        gm_t710lsrq1m8gk t563 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t519) + 8) + 8));
        F.l56 = t563;
        void * t564 = F.l0;
        void * t565 = F.l1;
        gm_b16_8 t566 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 8));
        gm_b0_1 t567 = gm_f_mono_cond_pats_armprh7((void *)NULL, cx, NULL, t564, t565, t566);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me520;
mn561:;
ma524:;
        goto me520;
mn568:;
ma525:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 212, 5);
me520:;
    }
    {
        void * t571 = gm_l2;
        uint8_t t572 = GM_LD(uint8_t, ((((uint8_t *)t571) + 8) + 0));
        if (t572 != ((uint8_t)55ULL)) goto ie570;
        void * t573 = GM_LD(void *, ((((uint8_t *)t571) + 8) + 8));
        gm_l57 = t573;
        void * t574 = F.l1;
        uint64_t t575 = GM_LD(uintptr_t, (((uint8_t *)t574) + 0) + 8);
        uint8_t t576 = (t575 > ((uint64_t)0ULL));
        if (!t576) goto ie570;
        {
            {
                gm_l59 = ((uint64_t)0ULL);
            }
            {
                void * t579 = gm_l57;
                uint64_t t580 = GM_LD(uintptr_t, (((uint8_t *)t579) + 56) + 8);
                void * t581 = gm_l57;
                uint64_t t582 = GM_LD(uintptr_t, (((uint8_t *)t581) + 72) + 8);
                uint64_t t583 = (t580 < t582 ? t580 : t582);
                gm_l58 = t583;
            }
            {
                for (;;) {
                    uint64_t t585 = gm_l59;
                    uint64_t t586 = gm_l58;
                    uint8_t t587 = (t585 < t586);
                    if (!t587) goto lb584;
                    {
                        uint64_t t590 = gm_l59;
                        uint64_t t591 = t590;
                        uint64_t t594 = GM_LD(uint64_t, ((uint8_t *)&t591));
                        gm_l60 = t594;
                        {
                            void * t597 = F.l0;
                            uint64_t t598 = gm_l60;
                            uint64_t t599 = (uintptr_t)t598;
                            void * t600 = gm_l57;
                            uint64_t t601 = GM_LD(uintptr_t, (((uint8_t *)t600) + 56) + 8);
                            if (t599 >= (uintptr_t)(t601)) gm_trap_index(t599, t601, gm_src5eu5l4a7gv6s, 222, 38);
                            void * t602 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t600) + 56)) + t599 * 24);
                            uint64_t t603 = gm_l60;
                            uint64_t t604 = (uintptr_t)t603;
                            void * t605 = gm_l57;
                            uint64_t t606 = GM_LD(uintptr_t, (((uint8_t *)t605) + 72) + 8);
                            if (t604 >= (uintptr_t)(t606)) gm_trap_index(t604, t606, gm_src5eu5l4a7gv6s, 222, 50);
                            gm_t88rcucg1b7uk t607 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t605) + 72)) + t604 * 88) + 0));
                            gm_th2lqbo77pvsc t608;
                            gm_f_check_spec_4vp7jhg((void *)&t608, (void *)NULL, cx, NULL, t597, t602, t607);
                            if (gm_unw.kind) {
                                goto gm_ll588;
                            }
                            gm_th2lqbo77pvsc t609 = t608;
                            if (!((((uint8_t *)&t609))[16] != 0)) goto ie596;
                            gm_b16_8 t610 = GM_LD(gm_b16_8, ((uint8_t *)&t609));
                            gm_l61 = t610;
                            {
                                void * t612 = F.l0;
                                gm_tl874t2atnlh3 t613 = gm_l5;
                                gm_b16_8 t614 = gm_l61;
                                gm_tnpifv20irnph t615;
                                GM_ST(gm_b16_8, ((uint8_t *)&t615 + 0), t614);
                                gm_tabb710d9mq2e t616;
                                gm_f_format_mlqiori((void *)&t616, (void *)NULL, cx, NULL, t615);
                                if (gm_unw.kind) {
                                    goto gm_ll588;
                                }
                                gm_tabb710d9mq2e t617 = t616;
                                if (((((uint8_t *)&t617))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t617) + 8, gm_src5eu5l4a7gv6s, 223, 31);
                                gm_b16_8 t618 = GM_LD(gm_b16_8, (((uint8_t *)&t617) + 8));
                                gm_b16_8 t619 = t618;
                                gm_b16_8 t620 = GM_LD(gm_b16_8, ((uint8_t *)&t619));
                                gm_b0_1 t621 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t612, t613, t620);
                                if (gm_unw.kind) {
                                    goto gm_ll588;
                                }
                            }
                            goto ix596;
ie596:;
ix596:;
                        }
                        goto me592;
mn593:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 221, 9);
me592:;
                    }
lc584:;
                    {
                        uint64_t t623 = gm_l59;
                        uint64_t r624; bool o625 = __builtin_add_overflow(t623, ((uint64_t)1ULL), &r624);
                        uint64_t t626 = r624;
                        gm_l59 = t626;
                    }
                }
gm_ll588:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb584; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc584; }
                }
                goto gm_lad;
lb584:;
            }
        }
        goto ix570;
ie570:;
ix570:;
    }
    {
        void * t628 = gm_l2;
        uint8_t t629;
        uint8_t t632 = GM_LD(uint8_t, ((((uint8_t *)t628) + 8) + 0));
        if (t632 != ((uint8_t)56ULL)) goto mn631;
        t629 = ((uint8_t)1);
        goto me630;
mn631:;
        t629 = ((uint8_t)0);
        goto me630;
mn633:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 227, 8);
me630:;
        if (!t629) goto ie627;
        {
            void * t635 = F.l0;
            void * t636 = F.l1;
            void * t637 = gm_l2;
            gm_b0_1 t638 = gm_f_resolve_write_e97fjom((void *)NULL, cx, NULL, t635, t636, t637);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t639;
            uint8_t t640 = 0;
            F.ret = t639;
            goto gm_fret;
        }
        goto ix627;
ie627:;
ix627:;
    }
    {
        void * t642 = gm_l2;
        uint8_t t643 = GM_LD(uint8_t, ((((uint8_t *)t642) + 8) + 0));
        if (t643 != ((uint8_t)33ULL)) goto ie641;
        uint8_t t644 = GM_LD(uint8_t, ((((((uint8_t *)t642) + 8) + 8) + 0) + 0));
        if (t644 != ((uint8_t)80ULL)) goto ie641;
        uint32_t t645 = GM_LD(uint32_t, ((((((uint8_t *)t642) + 8) + 8) + 0) + 4));
        gm_l62 = t645;
        gm_b16_8 t646 = GM_LD(gm_b16_8, (((((uint8_t *)t642) + 8) + 8) + 16));
        gm_l63 = t646;
        {
            void * t648 = F.l0;
            uint64_t t649 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t650 = GM_LD(uintptr_t, ((uint8_t *)&gm_l63) + 8);
            if (t649 >= (uintptr_t)(t650)) gm_trap_index(t649, t650, gm_src5eu5l4a7gv6s, 233, 33);
            void * t651 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l63)) + t649 * 88);
            uint32_t t652 = gm_l62;
            uint64_t t653 = (uint64_t)t652;
            uint64_t t654 = (uintptr_t)t653;
            void * t655 = F.l0;
            uint64_t t656 = GM_LD(uintptr_t, ((((uint8_t *)t655) + 2232) + 0) + 8);
            if (t654 >= (uintptr_t)(t656)) gm_trap_index(t654, t656, gm_src5eu5l4a7gv6s, 233, 42);
            void * t657 = gm_l2;
            gm_tl874t2atnlh3 t658 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t657) + 72));
            void * t659 = F.l1;
            gm_tl874t2atnlh3 t660 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t659) + 96));
            gm_toceffb697p0u t661;
            GM_ST(uint8_t, (uint8_t *)&t661 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t661 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t661), t660);
            ((uint8_t *)&t661)[12] = 1;
            gm_b0_1 t662 = gm_f_check_static_assert_s4f7i6i((void *)NULL, cx, NULL, t648, t651, &(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t655) + 2232) + 0)) + t654 * 24)), t658, t661);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t663 = gm_l2;
            uint8_t *t664 = ((uint8_t *)t663);
            void * t665 = F.l0;
            void * t666 = gm_l2;
            gm_tl874t2atnlh3 t667 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t666) + 72));
            void * t668 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t665, t667);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t69m56meqefs8 t669 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t668));
            GM_ST(gm_t69m56meqefs8, t664, t669);
        }
        {
            gm_b0_1 t670;
            uint8_t t671 = 0;
            F.ret = t670;
            goto gm_fret;
        }
        goto ix641;
ie641:;
ix641:;
    }
    {
        void * t673 = gm_l2;
        uint8_t t674;
        uint8_t t677 = GM_LD(uint8_t, ((((uint8_t *)t673) + 8) + 0));
        if (t677 != ((uint8_t)57ULL)) goto mn676;
        t674 = ((uint8_t)1);
        goto me675;
mn676:;
        t674 = ((uint8_t)0);
        goto me675;
mn678:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 237, 8);
me675:;
        if (!t674) goto ie672;
        {
            void * t680 = gm_l2;
            uint8_t *t681 = ((uint8_t *)t680);
            void * t682 = F.l0;
            void * t683 = F.l1;
            void * t684 = GM_LD(void *, (((uint8_t *)t683) + 112));
            void * t685 = gm_l2;
            void * t686 = gm_f_soa_expand_2e8rmqf((void *)NULL, cx, NULL, t682, t684, t685);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t69m56meqefs8 t687 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t686));
            GM_ST(gm_t69m56meqefs8, t681, t687);
        }
        {
            gm_b0_1 t688;
            uint8_t t689 = 0;
            F.ret = t688;
            goto gm_fret;
        }
        goto ix672;
ie672:;
ix672:;
    }
    {
        void * t691 = gm_l2;
        uint8_t t692 = GM_LD(uint8_t, ((((uint8_t *)t691) + 8) + 0));
        if (t692 != ((uint8_t)33ULL)) goto ie690;
        uint8_t t693 = GM_LD(uint8_t, ((((((uint8_t *)t691) + 8) + 8) + 0) + 0));
        if (t693 != ((uint8_t)7ULL)) goto ie690;
        gm_b16_8 t694 = GM_LD(gm_b16_8, (((((uint8_t *)t691) + 8) + 8) + 16));
        gm_l64 = t694;
        void * t695 = F.l1;
        uint64_t t696 = GM_LD(uintptr_t, (((uint8_t *)t695) + 0) + 8);
        uint8_t t697 = (t696 > ((uint64_t)0ULL));
        if (!t697) goto ie690;
        void * t698 = F.l0;
        void * t699 = (void *)(((uint8_t *)t698) + 8);
        gm_t88rcucg1b7uk t700 = gm_l4;
        uint8_t t701 = gm_f_has_param_u9t0efs(t699, GM_CONV(gm_csguankko4lmhd, t700));
        uint8_t t702 = (uint8_t)!t701;
        if (!t702) goto ie690;
        {
            uint64_t t704 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t705 = GM_LD(uintptr_t, ((uint8_t *)&gm_l64) + 8);
            if (t704 >= (uintptr_t)(t705)) gm_trap_index(t704, t705, gm_src5eu5l4a7gv6s, 243, 16);
            gm_t88rcucg1b7uk t706 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l64)) + t704 * 88) + 0));
            gm_l65 = t706;
        }
        {
            void * t708 = F.l0;
            void * t709 = (void *)(((uint8_t *)t708) + 8);
            gm_t88rcucg1b7uk t710 = gm_l65;
            uint8_t t711 = gm_f_has_param_u9t0efs(t709, GM_CONV(gm_csguankko4lmhd, t710));
            uint8_t t712 = (uint8_t)!t711;
            if (!t712) goto ie707;
            {
                void * t714 = F.l0;
                gm_t88rcucg1b7uk t715 = gm_l65;
                gm_tbccvikg6b96e t716;
                gm_f_layout_a5ovnrm((void *)&t716, (void *)NULL, cx, NULL, t714, t715);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbccvikg6b96e t717 = t716;
                uint64_t t718 = GM_LD(uint64_t, (((uint8_t *)&t717) + 0));
                gm_l66 = t718;
            }
            {
                void * t719 = F.l0;
                gm_t88rcucg1b7uk t720 = gm_l4;
                gm_tbccvikg6b96e t721;
                gm_f_layout_a5ovnrm((void *)&t721, (void *)NULL, cx, NULL, t719, t720);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbccvikg6b96e t722 = t721;
                uint64_t t723 = GM_LD(uint64_t, (((uint8_t *)&t722) + 0));
                gm_l67 = t723;
            }
            {
                uint64_t t725 = gm_l66;
                uint64_t t726 = gm_l67;
                uint8_t t727 = (!(t725 == t726));
                if (!t727) goto ie724;
                {
                    void * t729 = F.l0;
                    gm_tl874t2atnlh3 t730 = gm_l5;
                    gm_thddbqb6snabm t731;
                    void * t732 = F.l0;
                    gm_t88rcucg1b7uk t733 = gm_l65;
                    gm_b16_8 t734 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t732, t733);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t731 + 0, t734);
                    uint64_t t735 = gm_l66;
                    GM_ST(uint64_t, (uint8_t *)&t731 + 16, t735);
                    void * t736 = F.l0;
                    gm_t88rcucg1b7uk t737 = gm_l4;
                    gm_b16_8 t738 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t736, t737);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t731 + 24, t738);
                    uint64_t t739 = gm_l67;
                    GM_ST(uint64_t, (uint8_t *)&t731 + 40, t739);
                    gm_tabb710d9mq2e t740;
                    gm_f_format_le00do2((void *)&t740, (void *)NULL, cx, NULL, &t731);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t741 = t740;
                    if (((((uint8_t *)&t741))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t741) + 8, gm_src5eu5l4a7gv6s, 248, 31);
                    gm_b16_8 t742 = GM_LD(gm_b16_8, (((uint8_t *)&t741) + 8));
                    gm_b16_8 t743 = t742;
                    gm_b16_8 t744 = GM_LD(gm_b16_8, ((uint8_t *)&t743));
                    gm_b0_1 t745 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t729, t730, t744);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix724;
ie724:;
ix724:;
            }
            goto ix707;
ie707:;
ix707:;
        }
        goto ix690;
ie690:;
ix690:;
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
__attribute__((aligned(4))) uint32_t gm_f_mono_fn_aj1vgri(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_mono_fn_aj1vgri F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    uint32_t gm_l2; /* id */
    uint32_t gm_l3; /* n */
    gm_tiilmfd16v3fc gm_l4; /* inst */
    uint8_t gm_l5; /* has_param */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* a */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t88rcucg1b7uk gm_l12; /* a */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint32_t gm_l20; /* new_id */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t9805qtonbsag gm_l26; /* parent */
    uint32_t gm_l27; /* p */
    void * gm_l28; /* body */
    void * gm_l29; /* b */
    void * gm_l30; /* copy */
    uint8_t gm_l31; /* was */
    void * gm_l32; /* outer_locals */
    void * gm_l33; /* nd */
    GM_STACK_CHECK(&F, 1935u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        void * t4 = (void *)(((uint8_t *)t3) + 16);
        uint32_t t5 = gm_l2;
        gm_t9805qtonbsag t6 = gm_f_find_pair_ddtpbq9(t4, t5);
        gm_t9805qtonbsag t7 = t6;
        if (!((((uint8_t *)&t7))[4] != 0)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            uint32_t t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t13 = gm_l2;
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 344) + 0) + 8);
        if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_src5eu5l4a7gv6s, 288, 16);
        if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 344) + 0)) + t15 * 136) + 96))[24] != 0)) goto ie12;
        gm_tiilmfd16v3fc t18 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 344) + 0)) + t15 * 136) + 96));
        gm_l4 = t18;
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_l6 = t21;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t23 = gm_l7;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t25 = (t23 < t24);
                    if (!t25) goto lb22;
                    {
                        uint64_t t28 = gm_l7;
                        uint64_t t29 = (uintptr_t)t28;
                        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t29 * 4));
                        gm_l8 = t33;
                        {
                            void * t36 = gm_l0;
                            void * t37 = (void *)(((uint8_t *)t36) + 8);
                            gm_t88rcucg1b7uk t38 = gm_l8;
                            uint8_t t39 = gm_f_has_param_u9t0efs(t37, GM_CONV(gm_csguankko4lmhd, t38));
                            if (!t39) goto ie35;
                            {
                                gm_l5 = ((uint8_t)1);
                            }
                            goto ix35;
ie35:;
ix35:;
                        }
                        goto me31;
mn32:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 290, 9);
me31:;
                    }
lc22:;
                    {
                        uint64_t t42 = gm_l7;
                        uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                        uint64_t t45 = r43;
                        gm_l7 = t45;
                    }
                }
lb22:;
            }
        }
        {
            uint8_t t47 = gm_l5;
            uint8_t t48 = (uint8_t)!t47;
            if (!t48) goto ie46;
            {
                uint32_t t50 = gm_l2;
                uint8_t t51 = 0;
                F.ret = t50;
                goto gm_fret;
            }
            goto ix46;
ie46:;
ix46:;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_l10 = t53;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t55 = gm_l11;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t57 = (t55 < t56);
                    if (!t57) goto lb54;
                    {
                        uint64_t t60 = gm_l11;
                        uint64_t t61 = (uintptr_t)t60;
                        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t61 * 4));
                        gm_l12 = t65;
                        {
                            void * t67 = (void *)((uint8_t *)&F.l9);
                            void * t68 = gm_l0;
                            gm_t88rcucg1b7uk t69 = gm_l12;
                            void * t70 = gm_l1;
                            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t70) + 0));
                            gm_t88rcucg1b7uk t72 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t68, t69, t71);
                            if (gm_unw.kind) {
                                goto gm_ll58;
                            }
                            gm_tftb1odc739t5 t73 = gm_f_push_k3l9nic(t67, GM_CONV(gm_csguankko4lmhd, t72));
                            gm_tftb1odc739t5 t74 = t73;
                            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_src5eu5l4a7gv6s, 295, 30);
                            gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
                        }
                        goto me63;
mn64:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 295, 9);
me63:;
                    }
lc54:;
                    {
                        uint64_t t77 = gm_l11;
                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                        uint64_t t80 = r78;
                        gm_l11 = t80;
                    }
                }
gm_ll58:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb54; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc54; }
                }
                goto gm_lad;
lb54:;
            }
        }
        {
            void * t81 = gm_l0;
            uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t84 = t83;
            gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
            void * t86 = gm_l1;
            gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t86) + 96));
            uint32_t t88 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t81, t82, t85, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        uint32_t t91 = gm_l2;
        uint64_t t92 = (uint64_t)t91;
        uint64_t t93 = (uintptr_t)t92;
        void * t94 = gm_l0;
        uint64_t t95 = GM_LD(uintptr_t, ((((uint8_t *)t94) + 384) + 0) + 8);
        if (t93 >= (uintptr_t)(t95)) gm_trap_index(t93, t95, gm_src5eu5l4a7gv6s, 298, 12);
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t94) + 384) + 0)) + t93 * 64) + 56));
        uint8_t t97 = (uint8_t)!t96;
        if (!t97) goto ie90;
        {
            uint32_t t99 = gm_l2;
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
        }
        goto ix90;
ie90:;
ix90:;
    }
    {
        uint32_t t101 = gm_l2;
        uint64_t t102 = (uint64_t)t101;
        uint64_t t103 = (uintptr_t)t102;
        void * t104 = gm_l0;
        uint64_t t105 = GM_LD(uintptr_t, ((((uint8_t *)t104) + 344) + 0) + 8);
        if (t103 >= (uintptr_t)(t105)) gm_trap_index(t103, t105, gm_src5eu5l4a7gv6s, 300, 12);
        gm_tqqhoivahirr0 t106 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t104) + 344) + 0)) + t103 * 136));
        F.l13 = t106;
    }
    {
        gm_f_list_af74s0q((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 40));
            gm_l15 = t108;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t110 = gm_l16;
                uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t112 = (t110 < t111);
                if (!t112) goto lb109;
                {
                    uint64_t t115 = gm_l16;
                    uint64_t t116 = (uintptr_t)t115;
                    uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_t5v0c8ia6a5mo t120 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t116 * 56));
                    F.l17 = t120;
                    {
                        gm_t5v0c8ia6a5mo t122 = F.l17;
                        F.l18 = t122;
                    }
                    {
                        void * t123 = gm_l0;
                        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                        void * t125 = gm_l1;
                        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)t125) + 0));
                        gm_t88rcucg1b7uk t127 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t123, t124, t126);
                        if (gm_unw.kind) {
                            goto gm_ll113;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16), t127);
                    }
                    {
                        void * t128 = (void *)((uint8_t *)&F.l14);
                        gm_tftb1odc739t5 t129 = gm_f_push_o8u01sh(t128, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l18)));
                        gm_tftb1odc739t5 t130 = t129;
                        if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 1, gm_src5eu5l4a7gv6s, 305, 9);
                        gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t130) + 1));
                    }
                    goto me118;
mn119:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 302, 5);
me118:;
                }
lc109:;
                {
                    uint64_t t133 = gm_l16;
                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                    uint64_t t136 = r134;
                    gm_l16 = t136;
                }
            }
gm_ll113:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb109; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc109; }
            }
            goto gm_lad;
lb109:;
        }
    }
    {
        uint32_t t137 = gm_l2;
        uint64_t t138 = (uint64_t)t137;
        uint64_t t139 = (uintptr_t)t138;
        void * t140 = gm_l0;
        uint64_t t141 = GM_LD(uintptr_t, ((((uint8_t *)t140) + 384) + 0) + 8);
        if (t139 >= (uintptr_t)(t141)) gm_trap_index(t139, t141, gm_src5eu5l4a7gv6s, 307, 12);
        gm_tdgfduqjheqdm t142 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t140) + 384) + 0)) + t139 * 64));
        F.l19 = t142;
    }
    {
        void * t143 = gm_l0;
        gm_t9qkh0rsooc8d t144 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l19) + 0));
        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 16));
        uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 32));
        gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t149 = gm_l0;
        gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 56));
        void * t151 = gm_l1;
        gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)t151) + 0));
        gm_t88rcucg1b7uk t153 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t149, t150, t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t154 = gm_l0;
        void * t155 = (void *)(((uint8_t *)&F.l13) + 64);
        void * t156 = gm_l1;
        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)t156) + 0));
        gm_tnpifv20irnph t158 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t154, t155, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t159 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t143, t144, t145, t146, t147, t148, t153, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t159;
    }
    {
        void * t160 = gm_l1;
        void * t161 = (void *)(((uint8_t *)t160) + 16);
        uint32_t t162 = gm_l2;
        uint32_t t163 = gm_l20;
        gm_tcc1rq4n1avb2 t164;
        GM_ST(uint32_t, ((uint8_t *)&t164 + 0), t162);
        GM_ST(uint32_t, ((uint8_t *)&t164 + 4), t163);
        gm_tftb1odc739t5 t165 = gm_f_push_1ncolkq(t161, GM_CONV(gm_cs9ufftihtbpo2, t164));
        gm_tftb1odc739t5 t166 = t165;
        if (((((uint8_t *)&t166))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t166) + 1, gm_src5eu5l4a7gv6s, 309, 5);
        gm_b0_1 t167 = GM_LD(gm_b0_1, (((uint8_t *)&t166) + 1));
    }
    {
        void * t168 = gm_l0;
        void * t169 = (void *)(((uint8_t *)t168) + 2736);
        uint32_t t170 = gm_l20;
        uint32_t t171 = gm_l20;
        gm_tftb1odc739t5 t172 = gm_f_put_33kejrs(t169, t170, t171);
        gm_tftb1odc739t5 t173 = t172;
        if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 1, gm_src5eu5l4a7gv6s, 310, 5);
        gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 1));
    }
    {
        gm_f_list_p9s2t99((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 24));
            gm_l22 = t176;
        }
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t178 = gm_l23;
                uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                uint8_t t180 = (t178 < t179);
                if (!t180) goto lb177;
                {
                    uint64_t t183 = gm_l23;
                    uint64_t t184 = (uintptr_t)t183;
                    uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    gm_tsoie1ppgdvcg t188 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t184 * 40));
                    F.l24 = t188;
                    {
                        gm_tsoie1ppgdvcg t190 = F.l24;
                        F.l25 = t190;
                    }
                    {
                        void * t191 = gm_l0;
                        void * t192 = gm_l1;
                        gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l24) + 16));
                        gm_t88rcucg1b7uk t194 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t191, t192, t193);
                        if (gm_unw.kind) {
                            goto gm_ll181;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l25) + 16), t194);
                    }
                    {
                        void * t195 = (void *)((uint8_t *)&F.l21);
                        gm_tftb1odc739t5 t196 = gm_f_push_am7bo0v(t195, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l25)));
                        gm_tftb1odc739t5 t197 = t196;
                        if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_src5eu5l4a7gv6s, 315, 9);
                        gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
                    }
                    goto me186;
mn187:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 312, 5);
me186:;
                }
lc177:;
                {
                    uint64_t t200 = gm_l23;
                    uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                    uint64_t t203 = r201;
                    gm_l23 = t203;
                }
            }
gm_ll181:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb177; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc177; }
            }
            goto gm_lad;
lb177:;
        }
    }
    {
        gm_t9805qtonbsag t204;
        memset(&t204, 0, sizeof t204);
        gm_l26 = t204;
    }
    {
        if (!(((((uint8_t *)&F.l19) + 48))[4] != 0)) goto ie205;
        uint32_t t206 = GM_LD(uint32_t, (((uint8_t *)&F.l19) + 48));
        gm_l27 = t206;
        {
            void * t208 = gm_l0;
            void * t209 = gm_l1;
            uint32_t t210 = gm_l27;
            uint32_t t211 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t208, t209, t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t212;
            GM_ST(uint8_t, (uint8_t *)&t212 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t212 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t212), t211);
            ((uint8_t *)&t212)[4] = 1;
            gm_l26 = t212;
        }
        goto ix205;
ie205:;
ix205:;
    }
    {
        gm_l28 = ((void *)0);
    }
    {
        void * t214 = GM_LD(void *, (((uint8_t *)&F.l19) + 40));
        void * t215 = t214;
        if (!(GM_LD(void *, ((uint8_t *)&t215)) != 0)) goto ie213;
        void * t216 = GM_LD(void *, ((uint8_t *)&t215));
        gm_l29 = t216;
        {
            void * t218 = gm_l29;
            void * t219 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t218);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t219;
        }
        {
            void * t220 = gm_l1;
            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)t220) + 108));
            gm_l31 = t221;
        }
        {
            void * t222 = gm_l1;
            void * t223 = GM_LD(void *, (((uint8_t *)t222) + 112));
            gm_l32 = t223;
        }
        {
            void * t224 = gm_l1;
            GM_ST(uint8_t, (((uint8_t *)t224) + 108), ((uint8_t)1));
        }
        {
            void * t225 = gm_l1;
            void * t226 = (void *)((uint8_t *)&F.l21);
            GM_ST(void *, (((uint8_t *)t225) + 112), t226);
        }
        {
            void * t227 = gm_l0;
            void * t228 = gm_l1;
            void * t229 = gm_l30;
            gm_b0_1 t230 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t227, t228, t229);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t231 = gm_l1;
            uint8_t t232 = gm_l31;
            GM_ST(uint8_t, (((uint8_t *)t231) + 108), t232);
        }
        {
            void * t233 = gm_l1;
            void * t234 = gm_l32;
            GM_ST(void *, (((uint8_t *)t233) + 112), t234);
        }
        {
            void * t235 = gm_l30;
            gm_l28 = t235;
        }
        goto ix213;
ie213:;
ix213:;
    }
    {
        uint32_t t236 = gm_l20;
        uint64_t t237 = (uint64_t)t236;
        uint64_t t238 = (uintptr_t)t237;
        void * t239 = gm_l0;
        uint64_t t240 = GM_LD(uintptr_t, ((((uint8_t *)t239) + 384) + 0) + 8);
        if (t238 >= (uintptr_t)(t240)) gm_trap_index(t238, t240, gm_src5eu5l4a7gv6s, 332, 12);
        void * t241 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t239) + 384) + 0)) + t238 * 64);
        gm_l33 = t241;
    }
    {
        void * t242 = gm_l33;
        gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 8));
        GM_ST(gm_b16_8, (((uint8_t *)t242) + 8), t243);
    }
    {
        void * t244 = gm_l33;
        gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)t244) + 24), t245);
    }
    {
        void * t246 = gm_l33;
        void * t247 = gm_l28;
        GM_ST(void *, (((uint8_t *)t246) + 40), t247);
    }
    {
        void * t248 = gm_l33;
        gm_t9805qtonbsag t249 = gm_l26;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)t248) + 48), t249);
    }
    {
        void * t250 = gm_l33;
        GM_ST(uint8_t, (((uint8_t *)t250) + 56), ((uint8_t)1));
    }
    {
        uint32_t t251 = gm_l20;
        uint8_t t252 = 0;
        F.ret = t251;
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
__attribute__((aligned(4))) uint32_t gm_f_mono_handler_38b78qh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_mono_handler_38b78qh F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    uint32_t gm_l2; /* h */
    uint32_t gm_l3; /* n */
    uint32_t gm_l5; /* new_h */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* c */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        void * t4 = (void *)(((uint8_t *)t3) + 56);
        uint32_t t5 = gm_l2;
        gm_t9805qtonbsag t6 = gm_f_find_pair_ddtpbq9(t4, t5);
        gm_t9805qtonbsag t7 = t6;
        if (!((((uint8_t *)&t7))[4] != 0)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            uint32_t t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t12 = gm_l2;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 504) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src5eu5l4a7gv6s, 344, 12);
        gm_t1cs2fjnrqap1 t17 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 504) + 0)) + t14 * 40));
        F.l4 = t17;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 504) + 0) + 8);
        uint32_t t20 = (uint32_t)t19;
        gm_l5 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 504);
        gm_t1cs2fjnrqap1 t23;
        void * t24 = gm_l0;
        void * t25 = (void *)(((uint8_t *)&F.l4) + 0);
        void * t26 = gm_l1;
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t26) + 0));
        gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t23 + 0)), (void *)NULL, cx, NULL, t24, t25, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t28 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 24, t28);
        gm_tftb1odc739t5 t29 = gm_f_push_bu0mlkm(t22, &t23);
        gm_tftb1odc739t5 t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_src5eu5l4a7gv6s, 346, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        void * t32 = gm_l1;
        void * t33 = (void *)(((uint8_t *)t32) + 56);
        uint32_t t34 = gm_l2;
        uint32_t t35 = gm_l5;
        gm_tcc1rq4n1avb2 t36;
        GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t34);
        GM_ST(uint32_t, ((uint8_t *)&t36 + 4), t35);
        gm_tftb1odc739t5 t37 = gm_f_push_1ncolkq(t33, GM_CONV(gm_cs9ufftihtbpo2, t36));
        gm_tftb1odc739t5 t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src5eu5l4a7gv6s, 347, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 24));
            gm_l7 = t41;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t43 = gm_l8;
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t45 = (t43 < t44);
                if (!t45) goto lb42;
                {
                    uint64_t t48 = gm_l8;
                    uint64_t t49 = (uintptr_t)t48;
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t49 * 4));
                    uint32_t t52 = t51;
                    uint32_t t55 = GM_LD(uint32_t, ((uint8_t *)&t52));
                    gm_l9 = t55;
                    {
                        void * t57 = (void *)((uint8_t *)&F.l6);
                        void * t58 = gm_l0;
                        void * t59 = gm_l1;
                        uint32_t t60 = gm_l9;
                        uint32_t t61 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t58, t59, t60);
                        if (gm_unw.kind) {
                            goto gm_ll46;
                        }
                        gm_tftb1odc739t5 t62 = gm_f_push_im9d4ro(t57, t61);
                        gm_tftb1odc739t5 t63 = t62;
                        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_src5eu5l4a7gv6s, 349, 28);
                        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 349, 5);
me53:;
                }
lc42:;
                {
                    uint64_t t66 = gm_l8;
                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                    uint64_t t69 = r67;
                    gm_l8 = t69;
                }
            }
gm_ll46:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb42; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc42; }
            }
            goto gm_lad;
lb42:;
        }
    }
    {
        uint32_t t70 = gm_l5;
        uint64_t t71 = (uint64_t)t70;
        uint64_t t72 = (uintptr_t)t71;
        void * t73 = gm_l0;
        uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 504) + 0) + 8);
        if (t72 >= (uintptr_t)(t74)) gm_trap_index(t72, t74, gm_src5eu5l4a7gv6s, 350, 5);
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t73) + 504) + 0)) + t72 * 40) + 24), t75);
    }
    {
        uint32_t t76 = gm_l5;
        uint8_t t77 = 0;
        F.ret = t76;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_mono_pat_lipeb0h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mono_ty_atcu0jn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_mono_ty_atcu0jn F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint32_t gm_l3; /* h */
    GM_STACK_CHECK(&F, 311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)25ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 8));
        gm_l3 = t9;
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 8);
            void * t13 = gm_l0;
            void * t14 = gm_l1;
            uint32_t t15 = gm_l3;
            uint32_t t16 = gm_f_mono_handler_38b78qh((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t17;
            memset(&t17, 0, sizeof t17);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)25ULL));
            GM_ST(uint32_t, ((uint8_t *)&t17 + 8), t16);
            gm_t88rcucg1b7uk t18 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t12, &t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l2;
        void * t22 = gm_l1;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
        gm_t88rcucg1b7uk t24 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t20, t21, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = 0;
        F.ret = t24;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_monomorphize_21k6pku(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_produce_instance_kcqgj3e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_produce_instance_kcqgj3e F;
    void * gm_l0; /* s */
    void * gm_l3; /* body */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l8; /* g */
    uint32_t gm_l9; /* outer */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l16; /* copy */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    GM_STACK_CHECK(&F, 1284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)a1) + 0));
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 384) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src5eu5l4a7gv6s, 66, 12);
        gm_tdgfduqjheqdm t7 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 384) + 0)) + t4 * 64));
        F.l2 = t7;
    }
    {
        void * t8 = GM_LD(void *, (((uint8_t *)&F.l2) + 40));
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        gm_f_list_oalq03d((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)a1) + 0));
            uint64_t t16 = (uint64_t)t15;
            uint64_t t17 = (uintptr_t)t16;
            void * t18 = gm_l0;
            uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 344) + 0) + 8);
            if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_src5eu5l4a7gv6s, 69, 27);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 344) + 0)) + t17 * 136) + 80));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_l5 = t22;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t24 = gm_l6;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t26 = (t24 < t25);
                if (!t26) goto lb23;
                {
                    uint64_t t29 = gm_l6;
                    uint64_t t30 = gm_l6;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t33 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t31 * 4));
                    gm_tn68el8d14vku t34;
                    GM_ST(uint32_t, (uint8_t *)&t34 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t34 + 0), t29);
                    GM_ST(uint32_t, ((uint8_t *)&t34 + 8), t33);
                    gm_tn68el8d14vku t35 = t34;
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&t35) + 0));
                    gm_l7 = t38;
                    uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&t35) + 8));
                    gm_l8 = t39;
                    {
                        void * t41 = (void *)((uint8_t *)&F.l4);
                        uint32_t t42 = gm_l8;
                        uint64_t t43 = gm_l7;
                        uint64_t t44 = (uintptr_t)t43;
                        uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
                        if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_src5eu5l4a7gv6s, 69, 105);
                        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)a1) + 8)) + t44 * 4));
                        gm_tplpbqf3duvn3 t47;
                        GM_ST(uint32_t, ((uint8_t *)&t47 + 0), t42);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t47 + 4), t46);
                        gm_tftb1odc739t5 t48 = gm_f_push_h69n5fi(t41, GM_CONV(gm_cs7e9ppsd384bj, t47));
                        gm_tftb1odc739t5 t49 = t48;
                        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_src5eu5l4a7gv6s, 69, 71);
                        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 69, 5);
me36:;
                }
lc23:;
                {
                    uint64_t t52 = gm_l6;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    uint64_t t55 = r53;
                    gm_l6 = t55;
                }
            }
lb23:;
        }
    }
    {
        void * t56 = gm_l0;
        uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)t56) + 1360));
        gm_l9 = t57;
    }
    {
        void * t58 = gm_l0;
        uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)a1) + 36));
        GM_ST(uint32_t, (((uint8_t *)t58) + 1360), t59);
    }
    {
        void * t60 = gm_l0;
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t62 = t61;
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_toceffb697p0u t65;
        GM_ST(uint8_t, (uint8_t *)&t65 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t65 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t65), t64);
        ((uint8_t *)&t65)[12] = 1;
        gm_b0_1 t66 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t60, t63, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_p9s2t99((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&F.l11 + 109, 0, 3);
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t68 = t67;
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
        GM_ST(gm_b16_8, (uint8_t *)&F.l11 + 0, t69);
        gm_f_list_7l73vjb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11 + 16)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_7l73vjb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11 + 56)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l11 + 96, t70);
        GM_ST(uint8_t, (uint8_t *)&F.l11 + 108, ((uint8_t)0));
        void * t71 = (void *)((uint8_t *)&F.l10);
        GM_ST(void *, (uint8_t *)&F.l11 + 112, t71);
    }
    {
        void * t72 = (void *)(((uint8_t *)&F.l11) + 16);
        uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)a1) + 0));
        uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        gm_tcc1rq4n1avb2 t75;
        GM_ST(uint32_t, ((uint8_t *)&t75 + 0), t73);
        GM_ST(uint32_t, ((uint8_t *)&t75 + 4), t74);
        gm_tftb1odc739t5 t76 = gm_f_push_1ncolkq(t72, GM_CONV(gm_cs9ufftihtbpo2, t75));
        gm_tftb1odc739t5 t77 = t76;
        if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_src5eu5l4a7gv6s, 75, 5);
        gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
    }
    {
        {
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
            gm_l12 = t80;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t82 = gm_l13;
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l13;
                    uint64_t t88 = (uintptr_t)t87;
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_tsoie1ppgdvcg t92 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t88 * 40));
                    F.l14 = t92;
                    {
                        gm_tsoie1ppgdvcg t94 = F.l14;
                        F.l15 = t94;
                    }
                    {
                        void * t95 = gm_l0;
                        void * t96 = (void *)((uint8_t *)&F.l11);
                        gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                        gm_t88rcucg1b7uk t98 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t95, t96, t97);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16), t98);
                    }
                    {
                        void * t99 = (void *)((uint8_t *)&F.l10);
                        gm_tftb1odc739t5 t100 = gm_f_push_am7bo0v(t99, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l15)));
                        gm_tftb1odc739t5 t101 = t100;
                        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_src5eu5l4a7gv6s, 79, 9);
                        gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                    }
                    goto me90;
mn91:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 76, 5);
me90:;
                }
lc81:;
                {
                    uint64_t t104 = gm_l13;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l13 = t107;
                }
            }
gm_ll85:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
            }
            goto gm_lad;
lb81:;
        }
    }
    {
        void * t108 = gm_l3;
        void * t109 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t109;
    }
    {
        void * t110 = gm_l0;
        void * t111 = (void *)((uint8_t *)&F.l11);
        void * t112 = gm_l16;
        gm_b0_1 t113 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t110, t111, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t114 = gm_l0;
        gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_b0_1 t116 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t114, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t117 = gm_l0;
        uint32_t t118 = gm_l9;
        GM_ST(uint32_t, (((uint8_t *)t117) + 1360), t118);
    }
    {
        void * t119 = gm_l0;
        uint32_t t120 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        gm_tdgfduqjheqdm t121;
        memset((uint8_t *)&t121 + 1, 0, 7);
        memset((uint8_t *)&t121 + 57, 0, 7);
        gm_t9qkh0rsooc8d t122 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t121 + 0, t122);
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t121 + 8, t123);
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t121 + 24, t124);
        void * t125 = gm_l16;
        GM_ST(void *, (uint8_t *)&t121 + 40, t125);
        gm_t9805qtonbsag t126 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l2) + 48));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t121 + 48, t126);
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 56));
        GM_ST(uint8_t, (uint8_t *)&t121 + 56, t127);
        gm_b0_1 t128 = gm_f_set_inst_def_tcpabg0((void *)NULL, cx, NULL, t119, t120, &t121);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
            gm_l17 = t130;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t132 = gm_l18;
                uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t134 = (t132 < t133);
                if (!t134) goto lb131;
                {
                    uint64_t t137 = gm_l18;
                    uint64_t t138 = (uintptr_t)t137;
                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_tsoie1ppgdvcg t142 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t138 * 40));
                    F.l19 = t142;
                    {
                        void * t145 = gm_l0;
                        void * t146 = gm_l0;
                        gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                        gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t149 = t148;
                        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                        gm_t88rcucg1b7uk t151 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t146, t147, t150);
                        if (gm_unw.kind) {
                            goto gm_ll135;
                        }
                        gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                        uint8_t t153 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t145, t151, t152);
                        if (gm_unw.kind) {
                            goto gm_ll135;
                        }
                        uint8_t t154 = (uint8_t)!t153;
                        if (!t154) goto ie144;
                        {
                            goto lb131;
                        }
                        goto ix144;
ie144:;
ix144:;
                    }
                    goto me140;
mn141:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 87, 5);
me140:;
                }
lc131:;
                {
                    uint64_t t157 = gm_l18;
                    uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                    uint64_t t160 = r158;
                    gm_l18 = t160;
                }
            }
gm_ll135:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb131; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc131; }
            }
            goto gm_lad;
lb131:;
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
inline __attribute__((aligned(4))) void gm_f_subst_eref_kf9o1q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_eref_kf9o1q5 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 450u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_vmvl81b((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 8));
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
                    gm_toa2vhe3l4hfr t16 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 40));
                    F.l6 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l3);
                        gm_toa2vhe3l4hfr t19;
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t22 != ((uint8_t)0ULL)) goto mn21;
                        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                        gm_l7 = t23;
                        void * t24 = gm_l0;
                        gm_t88rcucg1b7uk t25 = gm_l7;
                        gm_b16_8 t26 = gm_l2;
                        gm_t88rcucg1b7uk t27 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t24, t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_toa2vhe3l4hfr t28;
                        memset(&t28, 0, sizeof t28);
                        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28 + 8), t27);
                        t19 = t28;
                        goto me20;
mn21:;
                        gm_toa2vhe3l4hfr t30 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)&F.l6));
                        F.l8 = t30;
                        gm_toa2vhe3l4hfr t31 = F.l8;
                        t19 = t31;
                        goto me20;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 276, 19);
me20:;
                        gm_tftb1odc739t5 t32 = gm_f_push_s5vr2a2(t18, &t19);
                        gm_tftb1odc739t5 t33 = t32;
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_src5eu5l4a7gv6s, 276, 9);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 275, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t36 = gm_l5;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l5 = t39;
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
        memset((uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 4, 0, 4);
        void * t40 = gm_l1;
        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t40) + 0));
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0, t41);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8, t44);
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

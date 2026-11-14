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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; } gm_thpdbp3q7rdrc;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thpdbp3q7rdrc) == 16 && _Alignof(gm_thpdbp3q7rdrc) == 4 && offsetof(gm_thpdbp3q7rdrc, f0) == 0 && offsetof(gm_thpdbp3q7rdrc, f1) == 4 && offsetof(gm_thpdbp3q7rdrc, f2) == 8 && offsetof(gm_thpdbp3q7rdrc, f3) == 12, "layout of `(u32, u32, u32, u32)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint32_t f3; } gm_thrm96fhhdpt5;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { uint64_t f0; gm_tbgd14esf0jvi f1; } gm_tdlq2ugehok8g;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thrm96fhhdpt5) == 40 && _Alignof(gm_thrm96fhhdpt5) == 8 && offsetof(gm_thrm96fhhdpt5, f0) == 0 && offsetof(gm_thrm96fhhdpt5, f1) == 16 && offsetof(gm_thrm96fhhdpt5, f2) == 32 && offsetof(gm_thrm96fhhdpt5, f3) == 36, "layout of `(string, string, u32, u32)`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_tdlq2ugehok8g) == 40 && _Alignof(gm_tdlq2ugehok8g) == 8 && offsetof(gm_tdlq2ugehok8g, f0) == 0 && offsetof(gm_tdlq2ugehok8g, f1) == 8, "layout of `(i: usize, x: Edit)`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { uint64_t f0; gm_tl7hpf5nlilqq f1; } gm_tm101l8077enc;
_Static_assert(sizeof(gm_tm101l8077enc) == 64 && _Alignof(gm_tm101l8077enc) == 8 && offsetof(gm_tm101l8077enc, f0) == 0 && offsetof(gm_tm101l8077enc, f1) == 8, "layout of `(i: usize, x: Fix)`");
struct gm_fr_add_fix_0gkcr4c {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_add_fix_edits_em0b48a {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* l */
    gm_b0_1 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_any_since_o1kpm84 {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    uint8_t ret;
};
struct gm_fr_before_05k9hcj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dgpnm3t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s801pu6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_s6hn871 {
    gm_frame h;
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_1eek7pr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_heading_jklompb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_ascii_t9jud42 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_json_after_line_c5i6134 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_before_line_2trhgro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_location_e84g9mr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_location_split_5jggoqp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_string_n2ks51u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lint_eo1tub5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jekodor {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_l10mna9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rk274m7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_location_3clcfto {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mark_vedqs3l {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_push_0o3323p {
    gm_frame h;
    gm_tl7hpf5nlilqq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k96mobn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l0rgbjf {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o4ckdr1 {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_t4gut2c9fj1kt l4; /* old */
    gm_b0_1 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_untabbed_n2b9umq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_58vn30g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kfs9rll {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_urfcki5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_render_human_fmipkqr {
    gm_frame h;
    gm_trqfvmjtt4sij l14; /* shown */
    gm_b0_1 ret;
};
struct gm_fr_render_json_a7n3ru2 {
    gm_frame h;
    gm_tl7hpf5nlilqq l8; /* fx */
    gm_b0_1 ret;
};
struct gm_fr_render_pi7hvmf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_render_short_oijr4gl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_reset_dg0kphe {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_severity_name_auihisp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_s8jqb1h {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_b0_1 ret;
};
struct gm_fr_spaces_d2e4env {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* s */
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_suppress_sdgq6qj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_2jbb2bh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsuppress_722t1fb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo886974tou(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_any_since_o1kpm84(void * a0, gm_csc7lc6jlkqkuq a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_before_05k9hcj(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) uint64_t gm_f_char_count_76unqru(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_char_offset_kr8000h(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dgpnm3t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6(void);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_1eek7pr(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_l21cpr6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_heading_jklompb(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_t9jud42(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_after_line_c5i6134(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_before_line_2trhgro(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_location_e84g9mr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_location_split_5jggoqp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_string_n2ks51u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_line_text_lc2jnq4(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jekodor(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_l10mna9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_location_3clcfto(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2);
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0o3323p(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k96mobn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1_c(void * a0, gm_t4gut2c9fj1kt a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_untabbed_n2b9umq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_58vn30g(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_human_fmipkqr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_json_a7n3ru2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_pi7hvmf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_short_oijr4gl(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_reset_dg0kphe(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_severity_name_auihisp(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sort_s8jqb1h(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spaces_d2e4env(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_suppress_sdgq6qj(void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_2jbb2bh(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_unsuppress_722t1fb(void * a0);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_35ac6au(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5ldgli3(void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6qt0u2d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bvpfm1k(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_capundt(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ef6g7s0(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gelas9q(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_he17klq(void * a0, gm_thpdbp3q7rdrc a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_nsck6rd(void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oig1hhm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oouhb7d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s6arg96(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u0r3vc4(void * a0, const void * a1);
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_c4h9gubvsld[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s0g8veu8021ch[3];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s4k2ktjht9l4a[13];
extern const uint8_t gm_s5rqsv8c71bds[11];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s9caepulv0516[9];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_s9qu470anjl4i[8];
extern const uint8_t gm_sap9oatgj29c2[13];
extern const uint8_t gm_scp1b2sk2nlmr[8];
extern const uint8_t gm_sgcj0gjfdljat[11];
extern const uint8_t gm_sh75rh8dk51gf[12];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sjbb259gf4uuc[8];
extern const uint8_t gm_skg16mqffvnq6[4];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_spchsf3taendr[3];
extern const uint8_t gm_squbucrq6qmns[5];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_su6414ei8c1ir[10];
extern const uint8_t gm_sue26fe85rntf[5];
extern const uint8_t gm_sun80ccb875r1[13];
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
inline bool gm_eqo886974tou(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 114u);
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
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_before_05k9hcj(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_before_05k9hcj F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    gm_tl874t2atnlh3 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 59u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_tcc1rq4n1avb2 t3;
    GM_ST(uint32_t, ((uint8_t *)&t3 + 0), t1);
    GM_ST(uint32_t, ((uint8_t *)&t3 + 4), t2);
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t4);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t5);
    uint8_t t7 = (uint8_t)gm_lt8l669blqfv(((uint8_t *)&t3), ((uint8_t *)&t6));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_prbqo9j((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48, ((uint64_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56, ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 16)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dgpnm3t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dgpnm3t F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s801pu6 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_1eek7pr(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_1eek7pr F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_urfcki5(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 32);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 82, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 40));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_t9jud42(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ascii_t9jud42 F;
    void * gm_l0; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)t1) + 72));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_eo1tub5 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_f_warning_q0m5jjs((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_l1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 160)) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 160)) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 160)) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 160)))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 160)) + 0, t4);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 184);
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
inline __attribute__((aligned(4))) void gm_f_list_jekodor(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jekodor F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s801pu6();
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
inline __attribute__((aligned(4))) void gm_f_list_l10mna9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_l10mna9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dgpnm3t();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rk274m7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jbu9fo3();
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
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_vedqs3l F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 0) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0o3323p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0o3323p F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_dgpnm3t();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_58vn30g(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tl7hpf5nlilqq t21 = GM_LD(gm_tl7hpf5nlilqq, ((uint8_t *)a1));
        GM_ST(gm_tl7hpf5nlilqq, ((uint8_t *)t20 + t18 * 56), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k96mobn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k96mobn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_s801pu6();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_urfcki5(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_jbu9fo3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8skjmj4(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l0rgbjf F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_1th396a();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_kfs9rll(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t4gut2c9fj1kt t21 = F.l1;
        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)t20 + t18 * 184), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 184)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_58vn30g(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_58vn30g F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kfs9rll F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)184ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_urfcki5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
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
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_suppress_sdgq6qj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_suppress_sdgq6qj F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 56));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 156, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 56), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_2jbb2bh(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_2jbb2bh F;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 184)));
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
inline __attribute__((aligned(4))) void gm_f_unsuppress_722t1fb(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unsuppress_722t1fb F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 56));
        uint8_t t5 = (t4 > ((uint32_t)0ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = gm_l0;
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 56));
            uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
            if (o11) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 160, 28);
            uint32_t t12 = r10;
            GM_ST(uint32_t, (((uint8_t *)t7) + 56), t12);
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
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_warning_q0m5jjs F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* message */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0), t4);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 184);
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
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) < GM_LD(uint32_t, b);
}
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return gm_lt467d5cm1d5(a + 0, b + 0);
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return gm_lt467d5cm1d5(a + 4, b + 4);
    return false;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3) {
    struct gm_fr_add_fix_0gkcr4c F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* message */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* text */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_jekodor((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l4);
        gm_tbgd14esf0jvi t3;
        memset((uint8_t *)&t3 + 12, 0, 4);
        gm_tl874t2atnlh3 t4 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t3 + 0, t4);
        gm_b16_8 t5 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 16, t5);
        gm_tftb1odc739t5 t6 = gm_f_push_k96mobn(t2, &t3);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcgfglnv8p059r, 102, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 120);
        gm_tl7hpf5nlilqq t10;
        gm_b16_8 t11 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
        memcpy((uint8_t *)&t10 + 16, ((uint8_t *)&F.l4), 40);
        gm_tftb1odc739t5 t12 = gm_f_push_0o3323p(t9, &t10);
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcgfglnv8p059r, 103, 5);
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_add_fix_edits_em0b48a F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* message */
    gm_b16_8 gm_l2; /* edits */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_jekodor((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t3 = gm_l2;
        gm_tftb1odc739t5 t4 = gm_f_extend_1eek7pr(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcgfglnv8p059r, 109, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 120);
        gm_tl7hpf5nlilqq t8;
        gm_b16_8 t9 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t9);
        memcpy((uint8_t *)&t8 + 16, ((uint8_t *)&F.l3), 40);
        gm_tftb1odc739t5 t10 = gm_f_push_0o3323p(t7, &t8);
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcgfglnv8p059r, 110, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
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
extern __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_since_o1kpm84(void * a0, gm_csc7lc6jlkqkuq a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_since_o1kpm84 F;
    void * gm_l0; /* ds */
    gm_t2lqu8jsj511d gm_l1; /* m */
    gm_b16_8 gm_l2; /* prefix */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 0) + 0)));
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            uint64_t t7 = (uintptr_t)t6;
            if (t7 > (uintptr_t)t4 || (uintptr_t)t4 > (uintptr_t)t4) gm_trap_slice(t7, (uintptr_t)t4, t4, gm_srcgfglnv8p059r, 148, 14);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t7 * 184)));
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
                    gm_t4gut2c9fj1kt t20 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 184));
                    F.l5 = t20;
                    {
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                        gm_b16_8 t24 = gm_l2;
                        uint8_t t25 = gm_f_starts_with_6vvt01e(t23, t24);
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
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 148, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l4 = t32;
                }
            }
lb9:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_before_05k9hcj(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
extern __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_error_s6hn871 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* message */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 1, 0, 7);
        memset((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 36, 0, 4);
        gm_t9qkh0rsooc8d t2;
        GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)2ULL));
        gm_b16_8 t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = gm_l0;
        gm_trqfvmjtt4sij t5;
        gm_f_list_rk274m7((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t6;
        gm_f_list_epvducg((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t7;
        gm_f_list_l10mna9((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t8;
        memset(&t8, 0, sizeof t8);
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 0, t2);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 8, t3);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 24, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 40, t5);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 80, t6);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 120, t7);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 160, t8);
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
extern __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_heading_jklompb(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_heading_jklompb F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 276u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_th2lqbo77pvsc t3 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t2) + 160));
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            void * t6 = gm_l0;
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t6) + 0));
            gm_b16_8 t8 = gm_f_severity_name_auihisp(t7);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        gm_t6j6smj7qoctu t10;
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t11) + 0));
        gm_b16_8 t13 = gm_f_severity_name_auihisp(t12);
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t13);
        gm_b16_8 t14 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_l21cpr6((void *)&t15, (void *)NULL, cx, NULL, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srcgfglnv8p059r, 88, 12);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        uint8_t t18 = 0;
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_after_line_c5i6134(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_json_after_line_c5i6134 F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* message */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    GM_STACK_CHECK(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t4 = gm_f_file_o15fqku(t2, t3);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t6 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t4, t5));
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        gm_t88rcucg1b7uk t9;
        GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t8);
        gm_tftb1odc739t5 t10 = gm_f_write_nsck6rd(t7, t9);
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcgfglnv8p059r, 377, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        gm_b16_8 t12 = gm_l3;
        void * t13 = gm_l2;
        gm_b0_1 t14 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t12, t13);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_before_line_2trhgro(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2) {
    struct gm_fr_json_before_line_2trhgro F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    void * gm_l3; /* f */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    gm_tcc1rq4n1avb2 gm_l5; /* end */
    GM_STACK_CHECK(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t4 = gm_f_file_o15fqku(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l3;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t7 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t5, t6));
        gm_l4 = t7;
    }
    {
        void * t8 = gm_l3;
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        gm_tcc1rq4n1avb2 t10 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t8, t9));
        gm_l5 = t10;
    }
    {
        void * t11 = gm_l2;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        gm_thpdbp3q7rdrc t16;
        GM_ST(uint32_t, ((uint8_t *)&t16 + 0), t12);
        GM_ST(uint32_t, ((uint8_t *)&t16 + 4), t13);
        GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t14);
        GM_ST(uint32_t, ((uint8_t *)&t16 + 12), t15);
        gm_tftb1odc739t5 t17 = gm_f_write_he17klq(t11, t16);
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcgfglnv8p059r, 370, 5);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
        void * t19 = gm_l3;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 0));
        void * t21 = gm_l2;
        gm_b0_1 t22 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t20, t21);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_location_e84g9mr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2) {
    struct gm_fr_json_location_e84g9mr F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_b0_1 t5 = gm_f_json_before_line_2trhgro((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t8 = gm_f_file_o15fqku(t6, t7);
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t10 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t8, t9));
        gm_l3 = t10;
    }
    {
        void * t11 = gm_l2;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t14;
        GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t12);
        GM_ST(uint32_t, ((uint8_t *)&t14 + 4), t13);
        gm_tftb1odc739t5 t15 = gm_f_write_s6arg96(t11, t14);
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcgfglnv8p059r, 355, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_location_split_5jggoqp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_json_location_split_5jggoqp F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* message */
    GM_STACK_CHECK(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_b0_1 t5 = gm_f_json_before_line_2trhgro((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        gm_tl874t2atnlh3 t7 = gm_l1;
        void * t8 = gm_l2;
        gm_b16_8 t9 = gm_l3;
        gm_b0_1 t10 = gm_f_json_after_line_c5i6134((void *)NULL, cx, NULL, t6, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l2;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_t88rcucg1b7uk t13;
        GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t12);
        gm_tftb1odc739t5 t14 = gm_f_write_5ldgli3(t11, t13);
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcgfglnv8p059r, 362, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_json_string_n2ks51u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_json_string_n2ks51u F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 446u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)34ULL));
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 1, gm_srcgfglnv8p059r, 386, 5);
        gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
    }
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l2 = t6;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l3;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 1));
                    uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)&t16));
                    gm_l4 = t19;
                    {
                        uint8_t t21 = gm_l4;
                        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t24 == ((uint8_t)34ULL))) goto mn23;
                        void * t25 = gm_l1;
                        gm_b16_8 t26;
                        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t27 = gm_f_extend_0cs85gs(t25, t26);
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcgfglnv8p059r, 389, 20);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        goto me22;
mn23:;
                        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t30 == ((uint8_t)92ULL))) goto mn29;
                        void * t31 = gm_l1;
                        gm_b16_8 t32;
                        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcgfglnv8p059r, 390, 21);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                        goto me22;
mn29:;
                        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t36 == ((uint8_t)10ULL))) goto mn35;
                        void * t37 = gm_l1;
                        gm_b16_8 t38;
                        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s6gt8b7hsisgu));
                        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
                        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcgfglnv8p059r, 391, 21);
                        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                        goto me22;
mn35:;
                        uint8_t t42 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t42 == ((uint8_t)13ULL))) goto mn41;
                        void * t43 = gm_l1;
                        gm_b16_8 t44;
                        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s9cb6j5ok7bdk));
                        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t45 = gm_f_extend_0cs85gs(t43, t44);
                        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcgfglnv8p059r, 392, 21);
                        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                        goto me22;
mn41:;
                        uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t48 == ((uint8_t)9ULL))) goto mn47;
                        void * t49 = gm_l1;
                        gm_b16_8 t50;
                        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s22g482qk7a40));
                        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
                        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srcgfglnv8p059r, 393, 21);
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                        goto me22;
mn47:;
                        uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t54 == ((uint8_t)8ULL))) goto mn53;
                        void * t55 = gm_l1;
                        gm_b16_8 t56;
                        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s0g8veu8021ch));
                        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t55, t56);
                        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcgfglnv8p059r, 394, 21);
                        gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                        goto me22;
mn53:;
                        uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t60 == ((uint8_t)12ULL))) goto mn59;
                        void * t61 = gm_l1;
                        gm_b16_8 t62;
                        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_spchsf3taendr));
                        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t63 = gm_f_extend_0cs85gs(t61, t62);
                        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcgfglnv8p059r, 395, 21);
                        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                        goto me22;
mn59:;
                        uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if ((t66 < ((uint8_t)0ULL)) || (t66 > ((uint8_t)31ULL))) goto mn65;
                        {
                            void * t68 = gm_l1;
                            gm_b16_8 t69;
                            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_squbucrq6qmns));
                            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(4));
                            gm_tftb1odc739t5 t70 = gm_f_extend_0cs85gs(t68, t69);
                            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcgfglnv8p059r, 397, 17);
                            gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                        }
                        {
                            void * t72 = gm_l1;
                            uint8_t t73 = gm_l4;
                            unsigned __int128 n74 = (unsigned __int128)((uint64_t)4ULL);
                            if (n74 >= 8) gm_trap_shift((uint64_t)n74, "u8", gm_srcgfglnv8p059r, 398, 36);
                            uint8_t t75 = (uint8_t)(t73 >> (int)n74);
                            uint64_t t76 = (uint64_t)t75;
                            uint64_t t77 = (uintptr_t)t76;
                            uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)gm_c4h9gubvsld) + 8);
                            if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_srcgfglnv8p059r, 398, 26);
                            uint8_t t79 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)gm_c4h9gubvsld)) + t77 * 1));
                            gm_tftb1odc739t5 t80 = gm_f_push_t83rg2m(t72, t79);
                            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcgfglnv8p059r, 398, 17);
                            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                        }
                        {
                            void * t82 = gm_l1;
                            uint8_t t83 = gm_l4;
                            uint8_t t84 = (uint8_t)(t83 & ((uint8_t)15ULL));
                            uint64_t t85 = (uint64_t)t84;
                            uint64_t t86 = (uintptr_t)t85;
                            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)gm_c4h9gubvsld) + 8);
                            if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcgfglnv8p059r, 399, 26);
                            uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)gm_c4h9gubvsld)) + t86 * 1));
                            gm_tftb1odc739t5 t89 = gm_f_push_t83rg2m(t82, t88);
                            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srcgfglnv8p059r, 399, 17);
                            gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                        }
                        goto me22;
mn65:;
                        void * t92 = gm_l1;
                        uint8_t t93 = gm_l4;
                        gm_tftb1odc739t5 t94 = gm_f_push_t83rg2m(t92, t93);
                        if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcgfglnv8p059r, 401, 18);
                        gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                        goto me22;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 388, 9);
me22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 387, 5);
me17:;
                }
lc7:;
                {
                    uint64_t t97 = gm_l3;
                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                    uint64_t t100 = r98;
                    gm_l3 = t100;
                }
            }
lb7:;
        }
    }
    {
        void * t101 = gm_l1;
        gm_tftb1odc739t5 t102 = gm_f_push_t83rg2m(t101, ((uint8_t)34ULL));
        if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_srcgfglnv8p059r, 404, 5);
        gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_location_3clcfto(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2) {
    struct gm_fr_location_3clcfto F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    void * gm_l3; /* f */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t5 = (gm_eqff85ac10o3(((uint8_t *)&t3), ((uint8_t *)&t4)));
        if (!t5) goto ie2;
        {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t11 = gm_f_file_o15fqku(t9, t10);
        gm_l3 = t11;
    }
    {
        void * t12 = gm_l3;
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t14 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t12, t13));
        gm_l4 = t14;
    }
    {
        void * t15 = gm_l2;
        gm_t5hdnnftkup6m t16;
        void * t17 = gm_l3;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t18);
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        GM_ST(uint32_t, (uint8_t *)&t16 + 16, t19);
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
        GM_ST(uint32_t, (uint8_t *)&t16 + 20, t20);
        gm_tftb1odc739t5 t21 = gm_f_write_u0r3vc4(t15, &t16);
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcgfglnv8p059r, 293, 5);
        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o4ckdr1 F;
    void * gm_l0; /* ds */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint8_t t3 = ((GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0) + 0) == ((uint8_t)2ULL)));
        if (!t3) goto ie2;
        {
            void * t5 = gm_l0;
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 48));
            uint64_t r8; bool o9 = __builtin_add_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 116, 31);
            uint64_t t10 = r8;
            GM_ST(uint64_t, (((uint8_t *)t5) + 48), t10);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 56));
        uint8_t t14 = (t13 > ((uint32_t)0ULL));
        if (!t14) goto ie11;
        {
            gm_b0_1 t16;
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        uint8_t t19 = ((GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0) + 0) == ((uint8_t)2ULL)));
        if (!t19) goto ie18;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_scp1b2sk2nlmr));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(7));
        uint8_t t22 = gm_f_contains_gkki4kq(t20, t21);
        if (!t22) goto ie18;
        {
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 24));
        gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t29 = (!gm_eqff85ac10o3(((uint8_t *)&t27), ((uint8_t *)&t28)));
        if (!t29) goto ie26;
        {
            {
                void * t32 = gm_l0;
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 0) + 0));
                gm_l2 = t33;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t35 = gm_l3;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t37 = (t35 < t36);
                    if (!t37) goto lb34;
                    {
                        uint64_t t40 = gm_l3;
                        uint64_t t41 = (uintptr_t)t40;
                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        gm_t4gut2c9fj1kt t45 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t41 * 184));
                        F.l4 = t45;
                        {
                            gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                            gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 24));
                            uint8_t t50 = (gm_eqff85ac10o3(((uint8_t *)&t48), ((uint8_t *)&t49)));
                            if (!t50) goto ie47;
                            gm_t9qkh0rsooc8d t51 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l4) + 0));
                            gm_t9qkh0rsooc8d t52 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 0));
                            uint8_t t53 = (gm_eqo886974tou(((uint8_t *)&t51), ((uint8_t *)&t52)));
                            if (!t53) goto ie47;
                            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
                            uint8_t t56 = (gm_eq0iqiubfm7e(((uint8_t *)&t54), ((uint8_t *)&t55)));
                            if (!t56) goto ie47;
                            {
                                gm_b0_1 t58;
                                uint8_t t59 = 0;
                                F.ret = t58;
                                goto gm_fret;
                            }
                            goto ix47;
ie47:;
ix47:;
                        }
                        goto me43;
mn44:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 121, 9);
me43:;
                    }
lc34:;
                    {
                        uint64_t t61 = gm_l3;
                        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                        uint64_t t64 = r62;
                        gm_l3 = t64;
                    }
                }
lb34:;
            }
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        uint8_t t66 = ((GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0) + 0) == ((uint8_t)2ULL)));
        if (!t66) goto ie65;
        {
            void * t68 = gm_l0;
            void * t69 = gm_l0;
            uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 40));
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            if (o72) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 125, 31);
            uint64_t t73 = r71;
            GM_ST(uint64_t, (((uint8_t *)t68) + 40), t73);
        }
        goto ix65;
ie65:;
ix65:;
    }
    {
        void * t74 = gm_l0;
        void * t75 = (void *)(((uint8_t *)t74) + 0);
        gm_tftb1odc739t5 t76 = gm_f_push_l0rgbjf(t75, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcgfglnv8p059r, 126, 5);
        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_push_o4ckdr1_c(void * a0, gm_t4gut2c9fj1kt a1) { gm_f_push_o4ckdr1(a0, &a1); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_untabbed_n2b9umq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_push_untabbed_n2b9umq F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t13));
                    gm_l4 = t16;
                    {
                        void * t18 = gm_l0;
                        uint8_t t19;
                        uint8_t t21 = gm_l4;
                        uint8_t t22 = ((t21 == ((uint8_t)9ULL)));
                        if (!t22) goto ie20;
                        uint8_t t24 = ((uint8_t)32ULL);
                        t19 = t24;
                        goto ix20;
ie20:;
                        uint8_t t26 = gm_l4;
                        uint8_t t27 = t26;
                        t19 = t27;
ix20:;
                        gm_tftb1odc739t5 t28 = gm_f_push_t83rg2m(t18, t19);
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcgfglnv8p059r, 271, 21);
                        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 271, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t31 = gm_l3;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l3 = t34;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_render_human_fmipkqr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_render_human_fmipkqr F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    void * gm_l2; /* out */
    void * gm_l3; /* f */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    uint32_t gm_l5; /* line */
    uint32_t gm_l6; /* col */
    gm_b16_8 gm_l7; /* number */
    uint64_t gm_l8; /* gutter */
    gm_b16_8 gm_l9; /* pad */
    gm_b16_8 gm_l10; /* text */
    uint32_t gm_l11; /* len */
    gm_tcc1rq4n1avb2 gm_l12; /* end */
    uint32_t gm_l13; /* width */
    uint32_t gm_l15; /* caret */
    uint32_t gm_l16; /* start */
    uint32_t gm_l17; /* stop */
    gm_b16_8 gm_l18; /* body */
    uint32_t gm_l19; /* <tmp> */
    uint32_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tbgd14esf0jvi gm_l23; /* l */
    void * gm_l24; /* f */
    gm_tcc1rq4n1avb2 gm_l25; /* lc */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_b16_8 gm_l28; /* n */
    GM_STACK_CHECK(&F, 2038u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        gm_t6j6smj7qoctu t3;
        void * t4 = gm_l1;
        gm_b16_8 t5 = gm_f_heading_jklompb((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t5);
        void * t6 = gm_l1;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 16, t7);
        gm_tftb1odc739t5 t8 = gm_f_write_oouhb7d(t2, &t3);
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 217, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t11 = gm_l1;
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t11) + 24));
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t14 = (!gm_eqff85ac10o3(((uint8_t *)&t12), ((uint8_t *)&t13)));
        if (!t14) goto ie10;
        {
            void * t16 = gm_l0;
            void * t17 = gm_l1;
            uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)t17) + 24) + 0));
            void * t19 = gm_f_file_o15fqku(t16, t18);
            gm_l3 = t19;
        }
        {
            void * t20 = gm_l3;
            void * t21 = gm_l1;
            uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t21) + 24) + 4));
            gm_tcc1rq4n1avb2 t23 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t20, t22));
            gm_l4 = t23;
        }
        {
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
            gm_l5 = t24;
        }
        {
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
            gm_l6 = t25;
        }
        {
            uint32_t t26 = gm_l5;
            gm_t88rcucg1b7uk t27;
            GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t26);
            gm_tabb710d9mq2e t28;
            gm_f_format_32n178h((void *)&t28, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcgfglnv8p059r, 223, 19);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_l7 = t29;
        }
        {
            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint64_t t31 = (t30 > ((uint64_t)2ULL) ? t30 : ((uint64_t)2ULL));
            gm_l8 = t31;
        }
        {
            uint64_t t32 = gm_l8;
            gm_b16_8 t33 = gm_f_spaces_d2e4env((void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t33;
        }
        {
            void * t34 = gm_l2;
            gm_thrm96fhhdpt5 t35;
            gm_b16_8 t36 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t35 + 0, t36);
            void * t37 = gm_l3;
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t35 + 16, t38);
            uint32_t t39 = gm_l5;
            GM_ST(uint32_t, (uint8_t *)&t35 + 32, t39);
            uint32_t t40 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t35 + 36, t40);
            gm_tftb1odc739t5 t41 = gm_f_write_oig1hhm(t34, &t35);
            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcgfglnv8p059r, 226, 9);
            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
        }
        {
            void * t43 = gm_l3;
            uint32_t t44 = gm_l5;
            gm_b16_8 t45 = gm_f_line_text_lc2jnq4(t43, t44);
            gm_l10 = t45;
        }
        {
            uint32_t t46;
            void * t48 = gm_l3;
            uint8_t t49 = gm_f_is_ascii_t9jud42(t48);
            if (!t49) goto ie47;
            uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            uint32_t t52 = (uint32_t)t51;
            uint32_t t53 = t52;
            t46 = t53;
            goto ix47;
ie47:;
            gm_b16_8 t55 = gm_l10;
            uint64_t t56 = gm_f_char_count_76unqru(t55);
            uint32_t t57 = (uint32_t)t56;
            uint32_t t58 = t57;
            t46 = t58;
ix47:;
            gm_l11 = t46;
        }
        {
            void * t59 = gm_l3;
            void * t60 = gm_l1;
            uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)t60) + 24) + 8));
            gm_tcc1rq4n1avb2 t62 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t59, t61));
            gm_l12 = t62;
        }
        {
            uint32_t t63;
            uint32_t t65 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 0));
            uint32_t t66 = gm_l5;
            uint8_t t67 = ((t65 == t66));
            if (!t67) goto ie64;
            uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
            uint32_t t70 = gm_l6;
            uint32_t r71; bool o72 = __builtin_sub_overflow(t69, t70, &r71);
            if (o72) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 230, 41);
            uint32_t t73 = r71;
            uint32_t t74 = (t73 > ((uint32_t)1ULL) ? t73 : ((uint32_t)1ULL));
            uint32_t t75 = t74;
            t63 = t75;
            goto ix64;
ie64:;
            uint32_t t77 = gm_l11;
            uint32_t r78; bool o79 = __builtin_add_overflow(t77, ((uint32_t)1ULL), &r78);
            if (o79) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 230, 73);
            uint32_t t80 = r78;
            uint32_t t81 = gm_l6;
            uint32_t r82; bool o83 = __builtin_sub_overflow(t80, t81, &r82);
            if (o83) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 230, 73);
            uint32_t t84 = r82;
            uint32_t t85 = (t84 > ((uint32_t)1ULL) ? t84 : ((uint32_t)1ULL));
            uint32_t t86 = t85;
            t63 = t86;
ix64:;
            gm_l13 = t63;
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint32_t t87 = gm_l6;
            uint32_t r88; bool o89 = __builtin_sub_overflow(t87, ((uint32_t)1ULL), &r88);
            if (o89) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 232, 18);
            uint32_t t90 = r88;
            gm_l15 = t90;
        }
        {
            uint32_t t92 = gm_l11;
            uint8_t t93 = (t92 > ((uint32_t)200ULL));
            if (!t93) goto ie91;
            {
                uint32_t t95 = gm_l6;
                uint32_t r96; bool o97 = __builtin_sub_overflow(t95, ((uint32_t)1ULL), &r96);
                if (o97) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 236, 23);
                uint32_t t98 = r96;
                if (((uint32_t)3ULL) == 0) gm_trap("division by zero", gm_srcgfglnv8p059r, 236, 35);
                uint32_t t99 = (uint32_t)(((uint32_t)120ULL) / ((uint32_t)3ULL));
                uint32_t r100; bool o101 = __builtin_sub_overflow(t98, t99, &r100);
                uint32_t t102 = o101 ? (0 ? ((uint32_t)4294967295ULL) : ((uint32_t)0ULL)) : r100;
                gm_l16 = t102;
            }
            {
                uint32_t t103 = gm_l16;
                uint32_t r104; bool o105 = __builtin_add_overflow(t103, ((uint32_t)120ULL), &r104);
                if (o105) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 237, 22);
                uint32_t t106 = r104;
                uint32_t t107 = gm_l11;
                uint32_t t108 = (t106 < t107 ? t106 : t107);
                gm_l17 = t108;
            }
            {
                uint32_t t110 = gm_l16;
                uint8_t t111 = (t110 > ((uint32_t)0ULL));
                if (!t111) goto ie109;
                {
                    void * t113 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t114;
                    GM_ST(void *, (uint8_t *)&t114, (void *)(gm_skg16mqffvnq6));
                    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t115 = gm_f_extend_0cs85gs(t113, t114);
                    if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcgfglnv8p059r, 238, 28);
                    gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                }
                goto ix109;
ie109:;
ix109:;
            }
            {
                gm_b16_8 t117;
                void * t119 = gm_l3;
                uint8_t t120 = gm_f_is_ascii_t9jud42(t119);
                if (!t120) goto ie118;
                uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                void * t123 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)));
                uint32_t t124 = gm_l16;
                uint64_t t125 = (uint64_t)t124;
                uint64_t t126 = (uintptr_t)t125;
                uint32_t t127 = gm_l17;
                uint64_t t128 = (uint64_t)t127;
                uint64_t t129 = (uintptr_t)t128;
                if (t126 > t129 || t129 > (uintptr_t)t122) gm_trap_slice(t126, t129, t122, gm_srcgfglnv8p059r, 239, 39);
                gm_b16_8 t130;
                GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t123 + t126 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(t129 - t126));
                gm_b16_8 t131 = t130;
                t117 = t131;
                goto ix118;
ie118:;
                gm_b16_8 t133 = gm_l10;
                uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&t133) + 8);
                void * t135 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t133)));
                gm_b16_8 t136 = gm_l10;
                uint32_t t137 = gm_l16;
                uint64_t t138 = (uint64_t)t137;
                uint64_t t139 = gm_f_char_offset_kr8000h(t136, t138);
                uint64_t t140 = (uintptr_t)t139;
                gm_b16_8 t141 = gm_l10;
                uint32_t t142 = gm_l17;
                uint64_t t143 = (uint64_t)t142;
                uint64_t t144 = gm_f_char_offset_kr8000h(t141, t143);
                uint64_t t145 = (uintptr_t)t144;
                if (t140 > t145 || t145 > (uintptr_t)t134) gm_trap_slice(t140, t145, t134, gm_srcgfglnv8p059r, 239, 80);
                gm_b16_8 t146;
                GM_ST(void *, (uint8_t *)&t146, (void *)(((uint8_t *)t135 + t140 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(t145 - t140));
                gm_b16_8 t147 = t146;
                t117 = t147;
ix118:;
                gm_l18 = t117;
            }
            {
                void * t148 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t149 = gm_l18;
                gm_b0_1 t150 = gm_f_push_untabbed_n2b9umq((void *)NULL, cx, NULL, t148, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint32_t t152 = gm_l17;
                uint32_t t153 = gm_l11;
                uint8_t t154 = (t152 < t153);
                if (!t154) goto ie151;
                {
                    void * t156 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t157;
                    GM_ST(void *, (uint8_t *)&t157, (void *)(gm_skg16mqffvnq6));
                    GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t158 = gm_f_extend_0cs85gs(t156, t157);
                    if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_srcgfglnv8p059r, 241, 29);
                    gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                }
                goto ix151;
ie151:;
ix151:;
            }
            {
                uint32_t t160 = gm_l13;
                uint32_t t161 = gm_l17;
                uint32_t r162; bool o163 = __builtin_add_overflow(t161, ((uint32_t)1ULL), &r162);
                if (o163) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 242, 31);
                uint32_t t164 = r162;
                uint32_t t165 = gm_l6;
                uint32_t r166; bool o167 = __builtin_sub_overflow(t164, t165, &r166);
                if (o167) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 242, 31);
                uint32_t t168 = r166;
                uint32_t t169 = (t160 < t168 ? t160 : t168);
                gm_l13 = t169;
            }
            {
                uint32_t t170 = gm_l6;
                uint32_t r171; bool o172 = __builtin_sub_overflow(t170, ((uint32_t)1ULL), &r171);
                if (o172) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 243, 21);
                uint32_t t173 = r171;
                uint32_t t174 = gm_l16;
                uint32_t r175; bool o176 = __builtin_sub_overflow(t173, t174, &r175);
                if (o176) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 243, 21);
                uint32_t t177 = r175;
                uint32_t t178;
                uint32_t t180 = gm_l16;
                uint8_t t181 = (t180 > ((uint32_t)0ULL));
                if (!t181) goto ie179;
                uint32_t t183 = ((uint32_t)1ULL);
                t178 = t183;
                goto ix179;
ie179:;
                uint32_t t185 = ((uint32_t)0ULL);
                t178 = t185;
ix179:;
                uint32_t r186; bool o187 = __builtin_add_overflow(t177, t178, &r186);
                if (o187) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 243, 21);
                uint32_t t188 = r186;
                gm_l15 = t188;
            }
            goto ix91;
ie91:;
            {
                void * t190 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t191 = gm_l10;
                gm_b0_1 t192 = gm_f_push_untabbed_n2b9umq((void *)NULL, cx, NULL, t190, t191);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix91:;
        }
        {
            void * t193 = gm_l2;
            gm_b16_8 t194 = gm_l9;
            gm_tnpifv20irnph t195;
            GM_ST(gm_b16_8, ((uint8_t *)&t195 + 0), t194);
            gm_tftb1odc739t5 t196 = gm_f_write_ef6g7s0(t193, t195);
            if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 1, gm_srcgfglnv8p059r, 247, 9);
            gm_b0_1 t197 = GM_LD(gm_b0_1, (((uint8_t *)&t196) + 1));
        }
        {
            void * t198 = gm_l2;
            gm_tfeab279c16rr t199;
            uint64_t t200 = gm_l8;
            uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint64_t r202; bool o203 = __builtin_sub_overflow(t200, t201, &r202);
            if (o203) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 248, 41);
            uint64_t t204 = r202;
            gm_b16_8 t205 = gm_f_spaces_d2e4env((void *)NULL, cx, NULL, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t199 + 0, t205);
            gm_b16_8 t206 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t199 + 16, t206);
            gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
            GM_ST(gm_b16_8, (uint8_t *)&t199 + 32, t208);
            gm_tftb1odc739t5 t209 = gm_f_write_gelas9q(t198, &t199);
            if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 1, gm_srcgfglnv8p059r, 248, 9);
            gm_b0_1 t210 = GM_LD(gm_b0_1, (((uint8_t *)&t209) + 1));
        }
        {
            void * t211 = gm_l2;
            gm_t6j6smj7qoctu t212;
            gm_b16_8 t213 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t213);
            uint32_t t214 = gm_l15;
            uint64_t t215 = (uint64_t)t214;
            gm_b16_8 t216 = gm_f_spaces_d2e4env((void *)NULL, cx, NULL, t215);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t212 + 16, t216);
            gm_tftb1odc739t5 t217 = gm_f_write_6qt0u2d(t211, &t212);
            if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srcgfglnv8p059r, 249, 9);
            gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
        }
        {
            {
                gm_l20 = ((uint32_t)0ULL);
            }
            {
                uint32_t t220 = gm_l13;
                uint32_t t221 = (t220 > ((uint32_t)1ULL) ? t220 : ((uint32_t)1ULL));
                gm_l19 = t221;
            }
            {
                for (;;) {
                    uint32_t t223 = gm_l20;
                    uint32_t t224 = gm_l19;
                    uint8_t t225 = (t223 < t224);
                    if (!t225) goto lb222;
                    {
                        uint32_t t228 = gm_l20;
                        {
                            void * t232 = gm_l2;
                            gm_tftb1odc739t5 t233 = gm_f_push_t83rg2m(t232, ((uint8_t)94ULL));
                            if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 1, gm_srcgfglnv8p059r, 250, 36);
                            gm_b0_1 t234 = GM_LD(gm_b0_1, (((uint8_t *)&t233) + 1));
                        }
                        goto me229;
mn230:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 250, 9);
me229:;
                    }
lc222:;
                    {
                        uint32_t t236 = gm_l20;
                        uint32_t r237; bool o238 = __builtin_add_overflow(t236, ((uint32_t)1ULL), &r237);
                        uint32_t t239 = r237;
                        gm_l20 = t239;
                    }
                }
lb222:;
            }
        }
        {
            void * t240 = gm_l2;
            gm_tftb1odc739t5 t241 = gm_f_push_t83rg2m(t240, ((uint8_t)10ULL));
            if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_srcgfglnv8p059r, 251, 9);
            gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        {
            void * t244 = gm_l1;
            gm_b16_8 t245 = GM_LD(gm_b16_8, ((((uint8_t *)t244) + 40) + 0));
            gm_l21 = t245;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t247 = gm_l22;
                uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t249 = (t247 < t248);
                if (!t249) goto lb246;
                {
                    uint64_t t252 = gm_l22;
                    uint64_t t253 = (uintptr_t)t252;
                    uint64_t t254 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_tbgd14esf0jvi t257 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t253 * 32));
                    gm_l23 = t257;
                    {
                        gm_tl874t2atnlh3 t260 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l23) + 0));
                        gm_tl874t2atnlh3 t261 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        uint8_t t262 = (gm_eqff85ac10o3(((uint8_t *)&t260), ((uint8_t *)&t261)));
                        if (!t262) goto ie259;
                        {
                            goto lc246;
                        }
                        goto ix259;
ie259:;
ix259:;
                    }
                    {
                        void * t264 = gm_l0;
                        uint32_t t265 = GM_LD(uint32_t, ((((uint8_t *)&gm_l23) + 0) + 0));
                        void * t266 = gm_f_file_o15fqku(t264, t265);
                        gm_l24 = t266;
                    }
                    {
                        void * t267 = gm_l24;
                        uint32_t t268 = GM_LD(uint32_t, ((((uint8_t *)&gm_l23) + 0) + 4));
                        gm_tcc1rq4n1avb2 t269 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t267, t268));
                        gm_l25 = t269;
                    }
                    {
                        void * t270 = gm_l2;
                        gm_thrm96fhhdpt5 t271;
                        gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t271 + 0, t272);
                        void * t273 = gm_l24;
                        gm_b16_8 t274 = GM_LD(gm_b16_8, (((uint8_t *)t273) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t271 + 16, t274);
                        uint32_t t275 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t271 + 32, t275);
                        uint32_t t276 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 4));
                        GM_ST(uint32_t, (uint8_t *)&t271 + 36, t276);
                        gm_tftb1odc739t5 t277 = gm_f_write_capundt(t270, &t271);
                        if (((((uint8_t *)&t277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t277) + 1, gm_srcgfglnv8p059r, 257, 9);
                        gm_b0_1 t278 = GM_LD(gm_b0_1, (((uint8_t *)&t277) + 1));
                    }
                    goto me255;
mn256:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 253, 5);
me255:;
                }
lc246:;
                {
                    uint64_t t280 = gm_l22;
                    uint64_t r281; bool o282 = __builtin_add_overflow(t280, ((uint64_t)1ULL), &r281);
                    uint64_t t283 = r281;
                    gm_l22 = t283;
                }
            }
lb246:;
        }
    }
    {
        {
            void * t285 = gm_l1;
            gm_b16_8 t286 = GM_LD(gm_b16_8, ((((uint8_t *)t285) + 80) + 0));
            gm_l26 = t286;
        }
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t288 = gm_l27;
                uint64_t t289 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                uint8_t t290 = (t288 < t289);
                if (!t290) goto lb287;
                {
                    uint64_t t293 = gm_l27;
                    uint64_t t294 = (uintptr_t)t293;
                    uint64_t t295 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t294 * 16));
                    gm_l28 = t298;
                    {
                        void * t300 = gm_l2;
                        gm_b16_8 t301 = gm_l28;
                        gm_tnpifv20irnph t302;
                        GM_ST(gm_b16_8, ((uint8_t *)&t302 + 0), t301);
                        gm_tftb1odc739t5 t303 = gm_f_write_bvpfm1k(t300, t302);
                        if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 1, gm_srcgfglnv8p059r, 259, 30);
                        gm_b0_1 t304 = GM_LD(gm_b0_1, (((uint8_t *)&t303) + 1));
                    }
                    goto me296;
mn297:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 259, 5);
me296:;
                }
lc287:;
                {
                    uint64_t t306 = gm_l27;
                    uint64_t r307; bool o308 = __builtin_add_overflow(t306, ((uint64_t)1ULL), &r307);
                    uint64_t t309 = r307;
                    gm_l27 = t309;
                }
            }
lb287:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_render_json_a7n3ru2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_render_json_a7n3ru2 F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    void * gm_l2; /* out */
    uint8_t gm_l3; /* located */
    gm_b16_8 gm_l4; /* c */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* j */
    gm_tbgd14esf0jvi gm_l12; /* e */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_tbgd14esf0jvi gm_l16; /* l */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l20; /* n */
    GM_STACK_CHECK(&F, 1776u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 24));
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t5 = (!gm_eqff85ac10o3(((uint8_t *)&t3), ((uint8_t *)&t4)));
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l2;
        gm_tftb1odc739t5 t7 = gm_f_push_t83rg2m(t6, ((uint8_t)123ULL));
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgfglnv8p059r, 299, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t10 = gm_l1;
        if (!(((((uint8_t *)t10) + 160))[16] != 0)) goto ie9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 160));
        gm_l4 = t11;
        {
            void * t13 = gm_l2;
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sjbb259gf4uuc));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
            gm_tftb1odc739t5 t15 = gm_f_extend_0cs85gs(t13, t14);
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcgfglnv8p059r, 301, 9);
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
        }
        {
            gm_b16_8 t17 = gm_l4;
            void * t18 = gm_l2;
            gm_b0_1 t19 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t20 = gm_l2;
            gm_tftb1odc739t5 t21 = gm_f_push_t83rg2m(t20, ((uint8_t)44ULL));
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcgfglnv8p059r, 303, 9);
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint8_t t24 = gm_l3;
        if (!t24) goto ie23;
        {
            void * t26 = gm_l0;
            void * t27 = gm_l1;
            gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 24));
            void * t29 = gm_l2;
            gm_b0_1 t30 = gm_f_json_before_line_2trhgro((void *)NULL, cx, NULL, t26, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t31 = gm_l2;
            gm_tftb1odc739t5 t32 = gm_f_push_t83rg2m(t31, ((uint8_t)44ULL));
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcgfglnv8p059r, 307, 9);
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t34 = gm_l2;
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_su6414ei8c1ir));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(9));
        gm_tftb1odc739t5 t36 = gm_f_extend_0cs85gs(t34, t35);
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcgfglnv8p059r, 309, 5);
        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
    }
    {
        {
            void * t39 = gm_l1;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t39) + 120) + 0));
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_l5 = t41;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t43 = gm_l6;
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t45 = (t43 < t44);
                if (!t45) goto lb42;
                {
                    gm_tm101l8077enc t48;
                    uint64_t t49 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t48 + 0, t49);
                    uint64_t t50 = gm_l6;
                    uint64_t t51 = (uintptr_t)t50;
                    uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t48 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t51 * 56), 56);
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&t48) + 0));
                    gm_l7 = t55;
                    F.l8 = (*(gm_tl7hpf5nlilqq *)(((uint8_t *)&t48) + 8));
                    {
                        uint64_t t58 = gm_l7;
                        uint8_t t59 = (t58 > ((uint64_t)0ULL));
                        if (!t59) goto ie57;
                        {
                            void * t61 = gm_l2;
                            gm_tftb1odc739t5 t62 = gm_f_push_t83rg2m(t61, ((uint8_t)44ULL));
                            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcgfglnv8p059r, 311, 20);
                            gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                        }
                        goto ix57;
ie57:;
ix57:;
                    }
                    {
                        void * t64 = gm_l2;
                        gm_b16_8 t65;
                        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s5rqsv8c71bds));
                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(10));
                        gm_tftb1odc739t5 t66 = gm_f_extend_0cs85gs(t64, t65);
                        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcgfglnv8p059r, 312, 9);
                        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                    }
                    {
                        {
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 16) + 0));
                            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                            gm_l9 = t70;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t72 = gm_l10;
                                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t74 = (t72 < t73);
                                if (!t74) goto lb71;
                                {
                                    gm_tdlq2ugehok8g t77;
                                    uint64_t t78 = gm_l10;
                                    GM_ST(uint64_t, (uint8_t *)&t77 + 0, t78);
                                    uint64_t t79 = gm_l10;
                                    uint64_t t80 = (uintptr_t)t79;
                                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    memcpy((uint8_t *)&t77 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t80 * 32), 32);
                                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&t77) + 0));
                                    gm_l11 = t84;
                                    gm_l12 = (*(gm_tbgd14esf0jvi *)(((uint8_t *)&t77) + 8));
                                    {
                                        uint64_t t87 = gm_l11;
                                        uint8_t t88 = (t87 > ((uint64_t)0ULL));
                                        if (!t88) goto ie86;
                                        {
                                            void * t90 = gm_l2;
                                            gm_tftb1odc739t5 t91 = gm_f_push_t83rg2m(t90, ((uint8_t)44ULL));
                                            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcgfglnv8p059r, 314, 24);
                                            gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
                                        }
                                        goto ix86;
ie86:;
ix86:;
                                    }
                                    {
                                        void * t93 = gm_l2;
                                        gm_tftb1odc739t5 t94 = gm_f_push_t83rg2m(t93, ((uint8_t)123ULL));
                                        if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcgfglnv8p059r, 315, 13);
                                        gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                                    }
                                    {
                                        void * t96 = gm_l0;
                                        gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 0));
                                        void * t98 = gm_l2;
                                        gm_b0_1 t99 = gm_f_json_location_e84g9mr((void *)NULL, cx, NULL, t96, t97, t98);
                                        if (gm_unw.kind) {
                                            goto gm_ll75;
                                        }
                                    }
                                    {
                                        void * t100 = gm_l2;
                                        gm_b16_8 t101;
                                        GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s9caepulv0516));
                                        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(8));
                                        gm_tftb1odc739t5 t102 = gm_f_extend_0cs85gs(t100, t101);
                                        if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_srcgfglnv8p059r, 317, 13);
                                        gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
                                    }
                                    {
                                        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                        void * t105 = gm_l2;
                                        gm_b0_1 t106 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t104, t105);
                                        if (gm_unw.kind) {
                                            goto gm_ll75;
                                        }
                                    }
                                    {
                                        void * t107 = gm_l2;
                                        gm_tftb1odc739t5 t108 = gm_f_push_t83rg2m(t107, ((uint8_t)125ULL));
                                        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srcgfglnv8p059r, 319, 13);
                                        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                                    }
                                    goto me82;
mn83:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 313, 9);
me82:;
                                }
lc71:;
                                {
                                    uint64_t t111 = gm_l10;
                                    uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                                    uint64_t t114 = r112;
                                    gm_l10 = t114;
                                }
                            }
gm_ll75:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb71; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc71; }
                            }
                            goto gm_ll46;
lb71:;
                        }
                    }
                    {
                        void * t115 = gm_l2;
                        gm_b16_8 t116;
                        GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sap9oatgj29c2));
                        GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(12));
                        gm_tftb1odc739t5 t117 = gm_f_extend_0cs85gs(t115, t116);
                        if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 1, gm_srcgfglnv8p059r, 321, 9);
                        gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 1));
                    }
                    {
                        gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        void * t120 = gm_l2;
                        gm_b0_1 t121 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t119, t120);
                        if (gm_unw.kind) {
                            goto gm_ll46;
                        }
                    }
                    {
                        void * t122 = gm_l2;
                        gm_tftb1odc739t5 t123 = gm_f_push_t83rg2m(t122, ((uint8_t)125ULL));
                        if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srcgfglnv8p059r, 323, 9);
                        gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 310, 5);
me53:;
                }
lc42:;
                {
                    uint64_t t126 = gm_l6;
                    uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                    uint64_t t129 = r127;
                    gm_l6 = t129;
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
        void * t130 = gm_l2;
        gm_b16_8 t131;
        GM_ST(void *, (uint8_t *)&t131, (void *)(gm_s4k2ktjht9l4a));
        GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(12));
        gm_tftb1odc739t5 t132 = gm_f_extend_0cs85gs(t130, t131);
        if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_srcgfglnv8p059r, 325, 5);
        gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
    }
    {
        {
            void * t135 = gm_l1;
            gm_b16_8 t136 = GM_LD(gm_b16_8, ((((uint8_t *)t135) + 40) + 0));
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
            gm_l13 = t137;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t139 = gm_l14;
                uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t141 = (t139 < t140);
                if (!t141) goto lb138;
                {
                    gm_tdlq2ugehok8g t144;
                    uint64_t t145 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t144 + 0, t145);
                    uint64_t t146 = gm_l14;
                    uint64_t t147 = (uintptr_t)t146;
                    uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    memcpy((uint8_t *)&t144 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t147 * 32), 32);
                    uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&t144) + 0));
                    gm_l15 = t151;
                    gm_l16 = (*(gm_tbgd14esf0jvi *)(((uint8_t *)&t144) + 8));
                    {
                        uint64_t t154 = gm_l15;
                        uint8_t t155 = (t154 > ((uint64_t)0ULL));
                        if (!t155) goto ie153;
                        {
                            void * t157 = gm_l2;
                            gm_tftb1odc739t5 t158 = gm_f_push_t83rg2m(t157, ((uint8_t)44ULL));
                            if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_srcgfglnv8p059r, 327, 20);
                            gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                        }
                        goto ix153;
ie153:;
ix153:;
                    }
                    {
                        void * t160 = gm_l2;
                        gm_tftb1odc739t5 t161 = gm_f_push_t83rg2m(t160, ((uint8_t)123ULL));
                        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srcgfglnv8p059r, 328, 9);
                        gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                    }
                    {
                        void * t163 = gm_l0;
                        gm_tl874t2atnlh3 t164 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l16) + 0));
                        void * t165 = gm_l2;
                        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 16));
                        gm_b0_1 t167 = gm_f_json_location_split_5jggoqp((void *)NULL, cx, NULL, t163, t164, t165, t166);
                        if (gm_unw.kind) {
                            goto gm_ll142;
                        }
                    }
                    {
                        void * t168 = gm_l2;
                        gm_tftb1odc739t5 t169 = gm_f_push_t83rg2m(t168, ((uint8_t)125ULL));
                        if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_srcgfglnv8p059r, 330, 9);
                        gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
                    }
                    goto me149;
mn150:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 326, 5);
me149:;
                }
lc138:;
                {
                    uint64_t t172 = gm_l14;
                    uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)1ULL), &r173);
                    uint64_t t175 = r173;
                    gm_l14 = t175;
                }
            }
gm_ll142:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb138; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc138; }
            }
            goto gm_lad;
lb138:;
        }
    }
    {
        void * t176 = gm_l2;
        gm_b16_8 t177;
        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_s2053hhvf16fm));
        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(1));
        gm_tftb1odc739t5 t178 = gm_f_extend_0cs85gs(t176, t177);
        if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 1, gm_srcgfglnv8p059r, 332, 5);
        gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 1));
    }
    {
        uint8_t t181 = gm_l3;
        if (!t181) goto ie180;
        {
            void * t183 = gm_l0;
            void * t184 = gm_l1;
            gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t184) + 24));
            void * t186 = gm_l2;
            void * t187 = gm_l1;
            gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)t187) + 8));
            gm_b0_1 t189 = gm_f_json_after_line_c5i6134((void *)NULL, cx, NULL, t183, t185, t186, t188);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix180;
ie180:;
        {
            void * t191 = gm_l2;
            gm_b16_8 t192;
            GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sh75rh8dk51gf));
            GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(11));
            gm_tftb1odc739t5 t193 = gm_f_extend_0cs85gs(t191, t192);
            if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 1, gm_srcgfglnv8p059r, 336, 9);
            gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 1));
        }
        {
            void * t195 = gm_l1;
            gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)t195) + 8));
            void * t197 = gm_l2;
            gm_b0_1 t198 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t196, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix180:;
    }
    {
        void * t199 = gm_l2;
        gm_b16_8 t200;
        GM_ST(void *, (uint8_t *)&t200, (void *)(gm_sgcj0gjfdljat));
        GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(10));
        gm_tftb1odc739t5 t201 = gm_f_extend_0cs85gs(t199, t200);
        if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srcgfglnv8p059r, 339, 5);
        gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
    }
    {
        {
            void * t204 = gm_l1;
            gm_b16_8 t205 = GM_LD(gm_b16_8, ((((uint8_t *)t204) + 80) + 0));
            gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
            gm_l17 = t206;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t208 = gm_l18;
                uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t210 = (t208 < t209);
                if (!t210) goto lb207;
                {
                    gm_tf1bjg81u0glv t213;
                    uint64_t t214 = gm_l18;
                    GM_ST(uint64_t, (uint8_t *)&t213 + 0, t214);
                    uint64_t t215 = gm_l18;
                    uint64_t t216 = (uintptr_t)t215;
                    uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t216 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t213 + 8, t218);
                    uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&t213) + 0));
                    gm_l19 = t221;
                    gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&t213) + 8));
                    gm_l20 = t222;
                    {
                        uint64_t t225 = gm_l19;
                        uint8_t t226 = (t225 > ((uint64_t)0ULL));
                        if (!t226) goto ie224;
                        {
                            void * t228 = gm_l2;
                            gm_tftb1odc739t5 t229 = gm_f_push_t83rg2m(t228, ((uint8_t)44ULL));
                            if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srcgfglnv8p059r, 341, 20);
                            gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
                        }
                        goto ix224;
ie224:;
ix224:;
                    }
                    {
                        gm_b16_8 t231 = gm_l20;
                        void * t232 = gm_l2;
                        gm_b0_1 t233 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t231, t232);
                        if (gm_unw.kind) {
                            goto gm_ll211;
                        }
                    }
                    goto me219;
mn220:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 340, 5);
me219:;
                }
lc207:;
                {
                    uint64_t t235 = gm_l18;
                    uint64_t r236; bool o237 = __builtin_add_overflow(t235, ((uint64_t)1ULL), &r236);
                    uint64_t t238 = r236;
                    gm_l18 = t238;
                }
            }
gm_ll211:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb207; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc207; }
            }
            goto gm_lad;
lb207:;
        }
    }
    {
        void * t239 = gm_l2;
        gm_b16_8 t240;
        GM_ST(void *, (uint8_t *)&t240, (void *)(gm_s2053hhvf16fm));
        GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(1));
        gm_tftb1odc739t5 t241 = gm_f_extend_0cs85gs(t239, t240);
        if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_srcgfglnv8p059r, 344, 5);
        gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
    }
    {
        uint8_t t244 = gm_l3;
        if (!t244) goto ie243;
        {
            void * t246 = gm_l2;
            void * t247 = gm_l1;
            uint32_t t248 = GM_LD(uint32_t, ((((uint8_t *)t247) + 24) + 4));
            gm_t88rcucg1b7uk t249;
            GM_ST(uint32_t, ((uint8_t *)&t249 + 0), t248);
            gm_tftb1odc739t5 t250 = gm_f_write_5ldgli3(t246, t249);
            if (((((uint8_t *)&t250))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t250) + 1, gm_srcgfglnv8p059r, 345, 18);
            gm_b0_1 t251 = GM_LD(gm_b0_1, (((uint8_t *)&t250) + 1));
        }
        goto ix243;
ie243:;
ix243:;
    }
    {
        void * t252 = gm_l2;
        gm_b16_8 t253;
        GM_ST(void *, (uint8_t *)&t253, (void *)(gm_sun80ccb875r1));
        GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(12));
        gm_tftb1odc739t5 t254 = gm_f_extend_0cs85gs(t252, t253);
        if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srcgfglnv8p059r, 346, 5);
        gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
    }
    {
        void * t256 = gm_l1;
        gm_t9qkh0rsooc8d t257 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t256) + 0));
        gm_b16_8 t258 = gm_f_severity_name_auihisp(t257);
        void * t259 = gm_l2;
        gm_b0_1 t260 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t258, t259);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t261 = gm_l2;
        gm_b16_8 t262;
        GM_ST(void *, (uint8_t *)&t262, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t263 = gm_f_extend_0cs85gs(t261, t262);
        if (((((uint8_t *)&t263))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t263) + 1, gm_srcgfglnv8p059r, 348, 5);
        gm_b0_1 t264 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_render_pi7hvmf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_render_pi7hvmf F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    gm_t9qkh0rsooc8d gm_l2; /* format */
    void * gm_l3; /* out */
    GM_STACK_CHECK(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; default: goto ma6; }
ma3:;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        void * t10 = gm_l0;
        void * t11 = gm_l1;
        void * t12 = gm_l3;
        gm_b0_1 t13 = gm_f_render_human_fmipkqr((void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn8:;
ma4:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        void * t16 = gm_l0;
        void * t17 = gm_l1;
        void * t18 = gm_l3;
        gm_b0_1 t19 = gm_f_render_short_oijr4gl((void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn14:;
ma5:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t21 != ((uint8_t)2ULL)) goto mn20;
        void * t22 = gm_l0;
        void * t23 = gm_l1;
        void * t24 = gm_l3;
        gm_b0_1 t25 = gm_f_render_json_a7n3ru2((void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn20:;
ma6:;
        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 204, 5);
me2:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_render_short_oijr4gl(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_render_short_oijr4gl F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tbgd14esf0jvi gm_l5; /* l */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* n */
    GM_STACK_CHECK(&F, 629u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 24));
        void * t5 = gm_l2;
        gm_b0_1 t6 = gm_f_location_3clcfto((void *)NULL, cx, NULL, t2, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l2;
        gm_t6j6smj7qoctu t8;
        void * t9 = gm_l1;
        gm_b16_8 t10 = gm_f_heading_jklompb((void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t10);
        void * t11 = gm_l1;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 16, t12);
        gm_tftb1odc739t5 t13 = gm_f_write_oouhb7d(t7, &t8);
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcgfglnv8p059r, 276, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        {
            void * t16 = gm_l1;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)t16) + 40) + 0));
            gm_l3 = t17;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l4;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l4;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tbgd14esf0jvi t29 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t25 * 32));
                    gm_l5 = t29;
                    {
                        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 0));
                        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        uint8_t t34 = (gm_eqff85ac10o3(((uint8_t *)&t32), ((uint8_t *)&t33)));
                        if (!t34) goto ie31;
                        {
                            goto lc18;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    {
                        void * t36 = gm_l0;
                        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 0));
                        void * t38 = gm_l2;
                        gm_b0_1 t39 = gm_f_location_3clcfto((void *)NULL, cx, NULL, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                    }
                    {
                        void * t40 = gm_l2;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
                        gm_tnpifv20irnph t42;
                        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                        gm_tftb1odc739t5 t43 = gm_f_write_35ac6au(t40, t42);
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcgfglnv8p059r, 280, 9);
                        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 277, 5);
me27:;
                }
lc18:;
                {
                    uint64_t t46 = gm_l4;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l4 = t49;
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
        {
            void * t51 = gm_l1;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)t51) + 80) + 0));
            gm_l6 = t52;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t54 = gm_l7;
                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t56 = (t54 < t55);
                if (!t56) goto lb53;
                {
                    uint64_t t59 = gm_l7;
                    uint64_t t60 = (uintptr_t)t59;
                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t60 * 16));
                    gm_l8 = t64;
                    {
                        void * t66 = gm_l0;
                        void * t67 = gm_l1;
                        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 24));
                        void * t69 = gm_l2;
                        gm_b0_1 t70 = gm_f_location_3clcfto((void *)NULL, cx, NULL, t66, t68, t69);
                        if (gm_unw.kind) {
                            goto gm_ll57;
                        }
                    }
                    {
                        void * t71 = gm_l2;
                        gm_b16_8 t72 = gm_l8;
                        gm_tnpifv20irnph t73;
                        GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                        gm_tftb1odc739t5 t74 = gm_f_write_35ac6au(t71, t73);
                        if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcgfglnv8p059r, 284, 9);
                        gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
                    }
                    goto me62;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 282, 5);
me62:;
                }
lc53:;
                {
                    uint64_t t77 = gm_l7;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    uint64_t t80 = r78;
                    gm_l7 = t80;
                }
            }
gm_ll57:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb53; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc53; }
            }
            goto gm_lad;
lb53:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_reset_dg0kphe(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reset_dg0kphe F;
    void * gm_l0; /* ds */
    gm_t2lqu8jsj511d gm_l1; /* m */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 418u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
            void * t5 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 0) + 0)));
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            uint64_t t7 = (uintptr_t)t6;
            if (t7 > (uintptr_t)t4 || (uintptr_t)t4 > (uintptr_t)t4) gm_trap_slice(t7, (uintptr_t)t4, t4, gm_srcgfglnv8p059r, 139, 14);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t7 * 184)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)t4 - t7));
            gm_l2 = t8;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l3;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l3;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t4gut2c9fj1kt t20 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 184));
                    F.l4 = t20;
                    {
                        uint8_t t23 = ((GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0) + 0) == ((uint8_t)2ULL)));
                        if (!t23) goto ie22;
                        {
                            void * t25 = gm_l0;
                            void * t26 = gm_l0;
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 40));
                            uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 140, 35);
                            uint64_t t30 = r28;
                            GM_ST(uint64_t, (((uint8_t *)t25) + 40), t30);
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 139, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
lb9:;
        }
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 0);
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        gm_f_truncate_2jbb2bh(t37, t38);
    }
    {
        void * t39 = gm_l0;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, (((uint8_t *)t39) + 48), t40);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_severity_name_auihisp(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_severity_name_auihisp F;
    gm_t9qkh0rsooc8d gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sue26fe85rntf));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s9qu470anjl4i));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t15 != ((uint8_t)2ULL)) goto mn14;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s79s3h98matgr));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
    t1 = t16;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 10, 47);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_s8jqb1h(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_s8jqb1h F;
    void * gm_l0; /* ds */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l6; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 883u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 0) + 0));
        gm_l1 = t3;
    }
    {
        {
            gm_l3 = ((uint64_t)1ULL);
        }
        {
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l2 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = gm_l2;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        uint64_t t17 = gm_l4;
                        uint64_t t18 = (uintptr_t)t17;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srcgfglnv8p059r, 178, 14);
                        gm_t4gut2c9fj1kt t20 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 184));
                        F.l5 = t20;
                    }
                    {
                        uint64_t t21 = gm_l4;
                        gm_l6 = t21;
                    }
                    {
                        for (;;) {
                            uint64_t t23 = gm_l6;
                            uint8_t t24 = (t23 > ((uint64_t)0ULL));
                            if (!t24) goto lb22;
                            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 24));
                            uint64_t t26 = gm_l6;
                            uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
                            if (o28) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 180, 43);
                            uint64_t t29 = r27;
                            uint64_t t30 = (uintptr_t)t29;
                            uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcgfglnv8p059r, 180, 40);
                            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t30 * 184) + 24));
                            uint8_t t33 = gm_f_before_05k9hcj(GM_CONV(gm_cseahvg8v7p0vs, t25), GM_CONV(gm_cseahvg8v7p0vs, t32));
                            if (!t33) goto lb22;
                            {
                                uint64_t t36 = gm_l6;
                                uint64_t t37 = (uintptr_t)t36;
                                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srcgfglnv8p059r, 181, 13);
                                uint64_t t39 = gm_l6;
                                uint64_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint64_t)1ULL), &r40);
                                if (o41) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 181, 24);
                                uint64_t t42 = r40;
                                uint64_t t43 = (uintptr_t)t42;
                                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srcgfglnv8p059r, 181, 21);
                                gm_t4gut2c9fj1kt t45 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t43 * 184));
                                GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t37 * 184), t45);
                            }
                            {
                                uint64_t t46 = gm_l6;
                                uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
                                if (o48) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 182, 13);
                                uint64_t t49 = r47;
                                gm_l6 = t49;
                            }
lc22:;
                        }
lb22:;
                    }
                    {
                        uint64_t t50 = gm_l6;
                        uint64_t t51 = (uintptr_t)t50;
                        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t51 >= (uintptr_t)(t52)) gm_trap_index(t51, t52, gm_srcgfglnv8p059r, 184, 9);
                        gm_t4gut2c9fj1kt t53 = F.l5;
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t51 * 184), t53);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 177, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t55 = gm_l3;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l3 = t58;
                }
            }
lb6:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_spaces_d2e4env(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_spaces_d2e4env F;
    uint64_t gm_l0; /* n */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 148u);
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
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = gm_l2;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    {
                        void * t14 = (void *)((uint8_t *)&F.l1);
                        gm_tftb1odc739t5 t15 = gm_f_push_t83rg2m(t14, ((uint8_t)32ULL));
                        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcgfglnv8p059r, 265, 21);
                        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 265, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t18 = gm_l3;
                    uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
                    uint64_t t21 = r19;
                    gm_l3 = t21;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        uint8_t t24 = 0;
        F.ret = t23;
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
extern __attribute__((aligned(4))) void gm_f_suppress_sdgq6qj(void * a0);
extern __attribute__((aligned(4))) void gm_f_unsuppress_722t1fb(void * a0);
extern __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);

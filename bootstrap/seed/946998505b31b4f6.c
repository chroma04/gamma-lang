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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csnt0cc832rgst { _Bool f0; uint64_t f1; uint64_t f2; uint64_t f3; uint64_t f4; int64_t f5; } gm_csnt0cc832rgst;
typedef struct gm_csv08vjo2a94sp { uint64_t f0; uint64_t f1; int64_t f2; } gm_csv08vjo2a94sp;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; uint64_t f2; uint64_t f3; uint64_t f4; int64_t f5; } gm_t3072hbjtr5t6;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint64_t f1; int64_t f2; } gm_tcs9tli4q9d6j;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csnt0cc832rgst) == 48 && _Alignof(gm_csnt0cc832rgst) == 8 && offsetof(gm_csnt0cc832rgst, f0) == 0 && offsetof(gm_csnt0cc832rgst, f1) == 8 && offsetof(gm_csnt0cc832rgst, f2) == 16 && offsetof(gm_csnt0cc832rgst, f3) == 24 && offsetof(gm_csnt0cc832rgst, f4) == 32 && offsetof(gm_csnt0cc832rgst, f5) == 40, "layout of `Parts`");
_Static_assert(sizeof(gm_csv08vjo2a94sp) == 24 && _Alignof(gm_csv08vjo2a94sp) == 8 && offsetof(gm_csv08vjo2a94sp, f0) == 0 && offsetof(gm_csv08vjo2a94sp, f1) == 8 && offsetof(gm_csv08vjo2a94sp, f2) == 16, "layout of `Normal`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3072hbjtr5t6) == 48 && _Alignof(gm_t3072hbjtr5t6) == 8 && offsetof(gm_t3072hbjtr5t6, f0) == 0 && offsetof(gm_t3072hbjtr5t6, f1) == 8 && offsetof(gm_t3072hbjtr5t6, f2) == 16 && offsetof(gm_t3072hbjtr5t6, f3) == 24 && offsetof(gm_t3072hbjtr5t6, f4) == 32 && offsetof(gm_t3072hbjtr5t6, f5) == 40, "layout of `Parts`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tcs9tli4q9d6j) == 24 && _Alignof(gm_tcs9tli4q9d6j) == 8 && offsetof(gm_tcs9tli4q9d6j, f0) == 0 && offsetof(gm_tcs9tli4q9d6j, f1) == 8 && offsetof(gm_tcs9tli4q9d6j, f2) == 16, "layout of `Normal`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; uint64_t v1; uint64_t v2; uint64_t v3; uint64_t v4; uint64_t v5; uint64_t v6; uint64_t v7; uint64_t v8; gm_t9qkh0rsooc8d v9; } f1; } gm_tekvgkj2hms37;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t3mf58gv9kf3n f0; void * f1; uint64_t f2; uint64_t f3; uint64_t f4; } gm_tjt4ka19str13;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_t2lqu8jsj511d v1; } f1; } gm_t1dp1jo1gr2pm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_tnpifv20irnph v1; } f1; } gm_t24hlg9v4qo9a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; uint32_t v1; } f1; } gm_t9mtkhurfjls7;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; } f1; } gm_tekh1vgsjf5ak;
_Static_assert(sizeof(gm_tekvgkj2hms37) == 16 && _Alignof(gm_tekvgkj2hms37) == 8 && offsetof(gm_tekvgkj2hms37, f0) == 0 && offsetof(gm_tekvgkj2hms37, f1) == 8, "layout of `ParseError`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_b16_8 v1; } f1; } gm_tfe4nnqq2l987;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tjt4ka19str13) == 48 && _Alignof(gm_tjt4ka19str13) == 8 && offsetof(gm_tjt4ka19str13, f0) == 0 && offsetof(gm_tjt4ka19str13, f1) == 16 && offsetof(gm_tjt4ka19str13, f2) == 24 && offsetof(gm_tjt4ka19str13, f3) == 32 && offsetof(gm_tjt4ka19str13, f4) == 40, "layout of `Arena`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t1dp1jo1gr2pm) == 24 && _Alignof(gm_t1dp1jo1gr2pm) == 8 && offsetof(gm_t1dp1jo1gr2pm, f0) == 0 && offsetof(gm_t1dp1jo1gr2pm, f1) == 8, "layout of `ParseError!(end: usize, len: usize)`");
_Static_assert(sizeof(gm_t24hlg9v4qo9a) == 24 && _Alignof(gm_t24hlg9v4qo9a) == 8 && offsetof(gm_t24hlg9v4qo9a, f0) == 0 && offsetof(gm_t24hlg9v4qo9a, f1) == 8, "layout of `ParseError!Number`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_t9mtkhurfjls7) == 24 && _Alignof(gm_t9mtkhurfjls7) == 8 && offsetof(gm_t9mtkhurfjls7, f0) == 0 && offsetof(gm_t9mtkhurfjls7, f1) == 8, "layout of `ParseError!u32`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_t8vuock3jnv59 v1; } f1; } gm_te0t4fvim36sr;
_Static_assert(sizeof(gm_tekh1vgsjf5ak) == 24 && _Alignof(gm_tekh1vgsjf5ak) == 8 && offsetof(gm_tekh1vgsjf5ak, f0) == 0 && offsetof(gm_tekh1vgsjf5ak, f1) == 8, "layout of `ParseError!()`");
_Static_assert(sizeof(gm_tfe4nnqq2l987) == 24 && _Alignof(gm_tfe4nnqq2l987) == 8 && offsetof(gm_tfe4nnqq2l987, f0) == 0 && offsetof(gm_tfe4nnqq2l987, f1) == 8, "layout of `ParseError!string`");
typedef struct { gm_b16_8 f0; gm_t8vuock3jnv59 f1; } gm_tfkvqluahdpno;
typedef struct { gm_t8vuock3jnv59 f0; gm_tjt4ka19str13 f1; } gm_thshlh2q7ekfv;
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_te0t4fvim36sr) == 32 && _Alignof(gm_te0t4fvim36sr) == 8 && offsetof(gm_te0t4fvim36sr, f0) == 0 && offsetof(gm_te0t4fvim36sr, f1) == 8, "layout of `ParseError!Value`");
_Static_assert(sizeof(gm_tfkvqluahdpno) == 40 && _Alignof(gm_tfkvqluahdpno) == 8 && offsetof(gm_tfkvqluahdpno, f0) == 0 && offsetof(gm_tfkvqluahdpno, f1) == 16, "layout of `Member`");
_Static_assert(sizeof(gm_thshlh2q7ekfv) == 72 && _Alignof(gm_thshlh2q7ekfv) == 8 && offsetof(gm_thshlh2q7ekfv, f0) == 0 && offsetof(gm_thshlh2q7ekfv, f1) == 24, "layout of `Document`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tekvgkj2hms37 v0; gm_thshlh2q7ekfv v1; } f1; } gm_tit4ajsn3pv91;
_Static_assert(sizeof(gm_tit4ajsn3pv91) == 80 && _Alignof(gm_tit4ajsn3pv91) == 8 && offsetof(gm_tit4ajsn3pv91, f0) == 0 && offsetof(gm_tit4ajsn3pv91, f1) == 8, "layout of `ParseError!Document`");
struct gm_fr_allocator_5m783ja {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_l8j91qd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_digit_at_1oh5ota {
    gm_frame h;
    gm_t3072hbjtr5t6 l1; /* p */
    uint8_t ret;
};
struct gm_fr_empty_slice_ab79bmt_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_encode_utf8_s8ajso1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_get_fjdb5ou {
    gm_frame h;
    gm_t3ph3kbiqt1ah ret;
};
struct gm_fr_hex4_23k9kdb {
    gm_frame h;
    gm_t9mtkhurfjls7 ret;
};
struct gm_fr_hex_value_q8sogka {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_k7levme {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_0202sdh_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_literal_9ia3g6h {
    gm_frame h;
    gm_tekh1vgsjf5ak ret;
};
struct gm_fr_new_jr5ejs0 {
    gm_frame h;
    gm_tjt4ka19str13 ret;
};
struct gm_fr_normalize_pcair89 {
    gm_frame h;
    gm_t3072hbjtr5t6 l1; /* p */
    gm_tcs9tli4q9d6j ret;
};
struct gm_fr_parse_array_54l90b7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_te0t4fvim36sr ret;
};
struct gm_fr_parse_document_0bpimpc {
    gm_frame h;
    gm_te0t4fvim36sr ret;
};
struct gm_fr_parse_invslf8 {
    gm_frame h;
    gm_tjt4ka19str13 l2; /* a */
    gm_tit4ajsn3pv91 ret;
    gm_b0_1 w0;
};
struct gm_fr_parse_number_buivaka {
    gm_frame h;
    gm_t24hlg9v4qo9a ret;
};
struct gm_fr_parse_object_4os6ms3 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* members */
    gm_te0t4fvim36sr ret;
};
struct gm_fr_parse_string_o9jl88i {
    gm_frame h;
    gm_tfe4nnqq2l987 ret;
};
struct gm_fr_parse_value_cgemdat {
    gm_frame h;
    gm_te0t4fvim36sr ret;
};
struct gm_fr_position_f7cepnk {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_push_0pgacsj_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_m55rf90_2 {
    gm_frame h;
    gm_tfkvqluahdpno l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_string_b2p27kc {
    gm_frame h;
    gm_t1dp1jo1gr2pm ret;
};
struct gm_fr_regrow_ddqa9t6_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ocvi9r0_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_skip_ws_g70e352 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_split_25tpoqf {
    gm_frame h;
    gm_t3072hbjtr5t6 ret;
};
struct gm_fr_to_i64_d1vui09 {
    gm_frame h;
    gm_t3072hbjtr5t6 l1; /* p */
    gm_t913bb735pvt5 ret;
};
struct gm_fr_utf8_len_u8bogbn {
    gm_frame h;
    uint64_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sejdpqv(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota(gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota_c(gm_b16_8 a0, gm_csnt0cc832rgst a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint64_t gm_f_encode_utf8_s8ajso1(uint32_t a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_get_fjdb5ou(void *gm_out, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t3ph3kbiqt1ah gm_f_get_fjdb5ou_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hex4_23k9kdb(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9mtkhurfjls7 gm_f_hex4_23k9kdb_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_hex_value_q8sogka(uint8_t a0);
__attribute__((aligned(4))) void gm_f_into_slice_e14ea2s(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_into_slice_hns5a32(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_k7levme(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_literal_9ia3g6h(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tekh1vgsjf5ak gm_f_literal_9ia3g6h_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_normalize_pcair89(void *gm_out, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_csv08vjo2a94sp gm_f_normalize_pcair89_c(gm_b16_8 a0, gm_csnt0cc832rgst a1);
__attribute__((aligned(4))) void gm_f_parse_array_54l90b7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_document_0bpimpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_invslf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_number_buivaka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_object_4os6ms3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_string_o9jl88i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_value_cgemdat(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_position_f7cepnk(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_read_string_b2p27kc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t1dp1jo1gr2pm gm_f_read_string_b2p27kc_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_skip_ws_g70e352(void * a0);
__attribute__((aligned(4))) void gm_f_split_25tpoqf(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_csnt0cc832rgst gm_f_split_25tpoqf_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_d1vui09(gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint64_t gm_f_utf8_len_u8bogbn(gm_b16_8 a0, uint64_t a1);
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_kdjk0ti7atv[40] __attribute__((aligned(8)));
extern uint8_t gm_kfqhpvvf5lv[40] __attribute__((aligned(8)));
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcge9itd5fie17[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcrgc9bkquf6nr[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_allocator_5m783ja(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_allocator_5m783ja F;
    void * gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = t1;
    void * t3 = (void *)((uint8_t *)gm_cjlj93gp0or);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_l8j91qd F;
    void * gm_l0; /* d */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 24);
        gm_f_deinit_sejdpqv(t3);
    }
    {
        void * t4 = gm_l0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)(((uint8_t *)t4) + 0)) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)(((uint8_t *)t4) + 0)) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)(((uint8_t *)t4) + 0)) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)(((uint8_t *)t4) + 0)) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)(((uint8_t *)t4) + 0)) + 16, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t8vuock3jnv59 *)(((uint8_t *)t4) + 0)) + 0, ((uint8_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_3 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_k7levme(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_digit_k7levme F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)48ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)57ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi_3();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ab79bmt_2();
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
inline __attribute__((aligned(4))) void gm_f_new_jr5ejs0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_jr5ejs0 F;
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_node *pn1 = gm_find(cx, 587034433);
    if (!pn1) gm_trap("no handler installed for effect `Alloc`", gm_srcip69en21ht7v, 41, 55);
    gm_t3mf58gv9kf3n t2;
    t2 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn1->clauses[0])(pn1->env, pn1->hctx, pn1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 0, t2);
    GM_ST(void *, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 16, ((void *)0));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 24, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 32, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tjt4ka19str13 *)gm_out) + 40, ((uint64_t)0ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0pgacsj_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0pgacsj_3 F;
    void * gm_l0; /* l */
    gm_t8vuock3jnv59 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_dheabhi_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ddqa9t6_3(t10, ((uint64_t)1ULL), t12);
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
        gm_t8vuock3jnv59 t21 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)a1));
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)t20 + t18 * 24), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_m55rf90_2 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_ab79bmt_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ocvi9r0_2(t10, ((uint64_t)1ULL), t12);
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
        gm_tfkvqluahdpno t21 = GM_LD(gm_tfkvqluahdpno, ((uint8_t *)a1));
        GM_ST(gm_tfkvqluahdpno, ((uint8_t *)t20 + t18 * 40), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 40)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ddqa9t6_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ddqa9t6_3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ocvi9r0_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_deinit_l8j91qd(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota(gm_b16_8 a0, const void * a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digit_at_1oh5ota F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t r4; bool o5 = __builtin_sub_overflow(t2, t3, &r4);
        if (o5) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 223, 10);
        uint64_t t6 = r4;
        gm_l3 = t6;
    }
    {
        uint64_t t8 = gm_l2;
        uint64_t t9 = gm_l3;
        uint8_t t10 = (t8 < t9);
        if (!t10) goto ie7;
        {
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t13 = gm_l2;
            uint64_t r14; bool o15 = __builtin_add_overflow(t12, t13, &r14);
            if (o15) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 224, 25);
            uint64_t t16 = r14;
            uint64_t t17 = (uintptr_t)t16;
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcge9itd5fie17, 224, 23);
            uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t17 * 1));
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        uint64_t t22 = gm_l2;
        uint64_t t23 = gm_l3;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t22, t23, &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 225, 30);
        uint64_t t26 = r24;
        uint64_t r27; bool o28 = __builtin_add_overflow(t21, t26, &r27);
        if (o28) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 225, 14);
        uint64_t t29 = r27;
        uint64_t t30 = (uintptr_t)t29;
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcge9itd5fie17, 225, 12);
        uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t30 * 1));
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_digit_at_1oh5ota_c(gm_b16_8 a0, gm_csnt0cc832rgst a1, uint64_t a2) { return gm_f_digit_at_1oh5ota(a0, &a1, a2); }
__attribute__((aligned(4))) uint64_t gm_f_encode_utf8_s8ajso1(uint32_t a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_encode_utf8_s8ajso1 F;
    uint32_t gm_l0; /* cp */
    gm_b16_8 gm_l1; /* buf */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* len */
    GM_STACK_CHECK_SHALLOW(&F, 599u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2;
        uint32_t t4 = gm_l0;
        uint8_t t5 = (t4 < ((uint32_t)128ULL));
        if (!t5) goto ie3;
        uint64_t t7 = ((uint64_t)1ULL);
        t2 = t7;
        goto ix3;
ie3:;
        uint64_t t8;
        uint32_t t10 = gm_l0;
        uint8_t t11 = (t10 < ((uint32_t)2048ULL));
        if (!t11) goto ie9;
        uint64_t t13 = ((uint64_t)2ULL);
        t8 = t13;
        goto ix9;
ie9:;
        uint64_t t14;
        uint32_t t16 = gm_l0;
        uint8_t t17 = (t16 < ((uint32_t)65536ULL));
        if (!t17) goto ie15;
        uint64_t t19 = ((uint64_t)3ULL);
        t14 = t19;
        goto ix15;
ie15:;
        uint64_t t21 = ((uint64_t)4ULL);
        t14 = t21;
ix15:;
        t8 = t14;
ix9:;
        t2 = t8;
ix3:;
        gm_l3 = t2;
    }
    {
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
        if (!t24) goto ie22;
        {
            uint64_t t26 = gm_l3;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        uint64_t t29 = gm_l3;
        uint8_t t30 = ((t29 == ((uint64_t)1ULL)));
        if (!t30) goto ie28;
        {
            uint64_t t32 = gm_l2;
            uint64_t t33 = (uintptr_t)t32;
            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcge9itd5fie17, 458, 9);
            uint32_t t35 = gm_l0;
            uint8_t t36 = (uint8_t)t35;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t33 * 1), t36);
        }
        goto ix28;
ie28:;
        uint64_t t38 = gm_l3;
        uint8_t t39 = ((t38 == ((uint64_t)2ULL)));
        if (!t39) goto ie37;
        {
            uint64_t t41 = gm_l2;
            uint64_t t42 = (uintptr_t)t41;
            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srcge9itd5fie17, 460, 9);
            uint32_t t44 = gm_l0;
            unsigned __int128 n45 = (unsigned __int128)((uint64_t)6ULL);
            if (n45 >= 32) gm_trap_shift((uint64_t)n45, "u32", gm_srcge9itd5fie17, 460, 27);
            uint32_t t46 = (uint32_t)(t44 >> (int)n45);
            uint32_t t47 = (uint32_t)(((uint32_t)192ULL) | t46);
            uint8_t t48 = (uint8_t)t47;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t42 * 1), t48);
        }
        {
            uint64_t t49 = gm_l2;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            if (o51) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 461, 13);
            uint64_t t52 = r50;
            uint64_t t53 = (uintptr_t)t52;
            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_srcge9itd5fie17, 461, 9);
            uint32_t t55 = gm_l0;
            uint32_t t56 = (uint32_t)(t55 & ((uint32_t)63ULL));
            uint32_t t57 = (uint32_t)(((uint32_t)128ULL) | t56);
            uint8_t t58 = (uint8_t)t57;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t53 * 1), t58);
        }
        goto ix37;
ie37:;
        uint64_t t60 = gm_l3;
        uint8_t t61 = ((t60 == ((uint64_t)3ULL)));
        if (!t61) goto ie59;
        {
            uint64_t t63 = gm_l2;
            uint64_t t64 = (uintptr_t)t63;
            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcge9itd5fie17, 463, 9);
            uint32_t t66 = gm_l0;
            unsigned __int128 n67 = (unsigned __int128)((uint64_t)12ULL);
            if (n67 >= 32) gm_trap_shift((uint64_t)n67, "u32", gm_srcge9itd5fie17, 463, 27);
            uint32_t t68 = (uint32_t)(t66 >> (int)n67);
            uint32_t t69 = (uint32_t)(((uint32_t)224ULL) | t68);
            uint8_t t70 = (uint8_t)t69;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t64 * 1), t70);
        }
        {
            uint64_t t71 = gm_l2;
            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
            if (o73) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 464, 13);
            uint64_t t74 = r72;
            uint64_t t75 = (uintptr_t)t74;
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcge9itd5fie17, 464, 9);
            uint32_t t77 = gm_l0;
            unsigned __int128 n78 = (unsigned __int128)((uint64_t)6ULL);
            if (n78 >= 32) gm_trap_shift((uint64_t)n78, "u32", gm_srcge9itd5fie17, 464, 32);
            uint32_t t79 = (uint32_t)(t77 >> (int)n78);
            uint32_t t80 = (uint32_t)(t79 & ((uint32_t)63ULL));
            uint32_t t81 = (uint32_t)(((uint32_t)128ULL) | t80);
            uint8_t t82 = (uint8_t)t81;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t75 * 1), t82);
        }
        {
            uint64_t t83 = gm_l2;
            uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)2ULL), &r84);
            if (o85) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 465, 13);
            uint64_t t86 = r84;
            uint64_t t87 = (uintptr_t)t86;
            uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t87 >= (uintptr_t)(t88)) gm_trap_index(t87, t88, gm_srcge9itd5fie17, 465, 9);
            uint32_t t89 = gm_l0;
            uint32_t t90 = (uint32_t)(t89 & ((uint32_t)63ULL));
            uint32_t t91 = (uint32_t)(((uint32_t)128ULL) | t90);
            uint8_t t92 = (uint8_t)t91;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t87 * 1), t92);
        }
        goto ix59;
ie59:;
        {
            uint64_t t94 = gm_l2;
            uint64_t t95 = (uintptr_t)t94;
            uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcge9itd5fie17, 467, 9);
            uint32_t t97 = gm_l0;
            unsigned __int128 n98 = (unsigned __int128)((uint64_t)18ULL);
            if (n98 >= 32) gm_trap_shift((uint64_t)n98, "u32", gm_srcge9itd5fie17, 467, 27);
            uint32_t t99 = (uint32_t)(t97 >> (int)n98);
            uint32_t t100 = (uint32_t)(((uint32_t)240ULL) | t99);
            uint8_t t101 = (uint8_t)t100;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1), t101);
        }
        {
            uint64_t t102 = gm_l2;
            uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
            if (o104) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 468, 13);
            uint64_t t105 = r103;
            uint64_t t106 = (uintptr_t)t105;
            uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_srcge9itd5fie17, 468, 9);
            uint32_t t108 = gm_l0;
            unsigned __int128 n109 = (unsigned __int128)((uint64_t)12ULL);
            if (n109 >= 32) gm_trap_shift((uint64_t)n109, "u32", gm_srcge9itd5fie17, 468, 32);
            uint32_t t110 = (uint32_t)(t108 >> (int)n109);
            uint32_t t111 = (uint32_t)(t110 & ((uint32_t)63ULL));
            uint32_t t112 = (uint32_t)(((uint32_t)128ULL) | t111);
            uint8_t t113 = (uint8_t)t112;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t106 * 1), t113);
        }
        {
            uint64_t t114 = gm_l2;
            uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)2ULL), &r115);
            if (o116) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 469, 13);
            uint64_t t117 = r115;
            uint64_t t118 = (uintptr_t)t117;
            uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcge9itd5fie17, 469, 9);
            uint32_t t120 = gm_l0;
            unsigned __int128 n121 = (unsigned __int128)((uint64_t)6ULL);
            if (n121 >= 32) gm_trap_shift((uint64_t)n121, "u32", gm_srcge9itd5fie17, 469, 32);
            uint32_t t122 = (uint32_t)(t120 >> (int)n121);
            uint32_t t123 = (uint32_t)(t122 & ((uint32_t)63ULL));
            uint32_t t124 = (uint32_t)(((uint32_t)128ULL) | t123);
            uint8_t t125 = (uint8_t)t124;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1), t125);
        }
        {
            uint64_t t126 = gm_l2;
            uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)3ULL), &r127);
            if (o128) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 470, 13);
            uint64_t t129 = r127;
            uint64_t t130 = (uintptr_t)t129;
            uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t130 >= (uintptr_t)(t131)) gm_trap_index(t130, t131, gm_srcge9itd5fie17, 470, 9);
            uint32_t t132 = gm_l0;
            uint32_t t133 = (uint32_t)(t132 & ((uint32_t)63ULL));
            uint32_t t134 = (uint32_t)(((uint32_t)128ULL) | t133);
            uint8_t t135 = (uint8_t)t134;
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t130 * 1), t135);
        }
ix59:;
ix37:;
ix28:;
    }
    {
        uint64_t t136 = gm_l3;
        uint8_t t137 = 0;
        F.ret = t136;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_get_fjdb5ou(void *gm_out, const void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_fjdb5ou F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* key */
    gm_b16_8 gm_l2; /* members */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t4 != ((uint8_t)5ULL)) goto mn3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l2 = t5;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l3 = t7;
        }
        {
            for (;;) {
                uint64_t t9 = gm_l3;
                uint8_t t10 = (t9 > ((uint64_t)0ULL));
                if (!t10) goto lb8;
                {
                    uint64_t t13 = gm_l3;
                    uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
                    if (o15) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 132, 17);
                    uint64_t t16 = r14;
                    gm_l3 = t16;
                }
                {
                    uint64_t t18 = gm_l3;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcge9itd5fie17, 133, 20);
                    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t19 * 40) + 0));
                    gm_b16_8 t22 = gm_l1;
                    uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                    if (!t23) goto ie17;
                    {
                        uint64_t t25 = gm_l3;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcge9itd5fie17, 133, 51);
                        const uint8_t *t28 = (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t26 * 40) + 16);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out) + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out) + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out) + 28, 0);
                        ((uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out))[24] = 1;
                        memmove((uint8_t *)&(*(gm_t3ph3kbiqt1ah *)gm_out) + 0, t28, 24);
                        goto gm_fdone;
                    }
                    goto ix17;
ie17:;
ix17:;
                }
lc8:;
            }
lb8:;
        }
        {
            gm_t3ph3kbiqt1ah t29;
            memset(&t29, 0, sizeof t29);
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto me2;
mn3:;
        gm_t3ph3kbiqt1ah t32;
        memset(&t32, 0, sizeof t32);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me2;
mn31:;
        gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 128, 5);
me2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t3ph3kbiqt1ah gm_f_get_fjdb5ou_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1) { gm_t3ph3kbiqt1ah gm_r; gm_f_get_fjdb5ou(&gm_r, &a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_hex4_23k9kdb(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hex4_23k9kdb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* at */
    uint32_t gm_l2; /* v */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    uint8_t gm_l6; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint32_t)0ULL);
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            gm_l3 = ((uint64_t)4ULL);
        }
        {
            for (;;) {
                uint64_t t4 = gm_l4;
                uint64_t t5 = gm_l3;
                uint8_t t6 = (t4 < t5);
                if (!t6) goto lb3;
                {
                    uint64_t t9 = gm_l4;
                    uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&t9));
                    gm_l5 = t12;
                    {
                        uint64_t t15 = gm_l1;
                        uint64_t t16 = gm_l5;
                        uint64_t r17; bool o18 = __builtin_add_overflow(t15, t16, &r17);
                        if (o18) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 416, 12);
                        uint64_t t19 = r17;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8);
                        uint8_t t22 = (t19 >= t21);
                        if (!t22) goto ie14;
                        {
                            void * t24 = gm_l0;
                            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)t24) + 0) + 8);
                            gm_tekvgkj2hms37 t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t26 + 8), t25);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 8, t26);
                            goto gm_fdone;
                        }
                        goto ix14;
ie14:;
ix14:;
                    }
                    {
                        uint64_t t27 = gm_l1;
                        uint64_t t28 = gm_l5;
                        uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
                        if (o30) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 417, 31);
                        uint64_t t31 = r29;
                        uint64_t t32 = (uintptr_t)t31;
                        void * t33 = gm_l0;
                        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 0) + 8);
                        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srcge9itd5fie17, 417, 24);
                        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 0)) + t32 * 1));
                        uint8_t t36 = gm_f_hex_value_q8sogka(t35);
                        gm_l6 = t36;
                    }
                    {
                        uint8_t t38 = gm_l6;
                        uint8_t t39 = ((t38 == ((uint8_t)255ULL)));
                        if (!t39) goto ie37;
                        {
                            uint64_t t41 = gm_l1;
                            uint64_t t42 = gm_l5;
                            uint64_t r43; bool o44 = __builtin_add_overflow(t41, t42, &r43);
                            if (o44) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 418, 46);
                            uint64_t t45 = r43;
                            gm_tekvgkj2hms37 t46;
                            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)3ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t46 + 8), t45);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 8, t46);
                            goto gm_fdone;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    {
                        uint32_t t47 = gm_l2;
                        uint32_t r48; bool o49 = __builtin_mul_overflow(t47, ((uint32_t)16ULL), &r48);
                        if (o49) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 419, 13);
                        uint32_t t50 = r48;
                        uint8_t t51 = gm_l6;
                        uint32_t t52 = (uint32_t)t51;
                        uint32_t r53; bool o54 = __builtin_add_overflow(t50, t52, &r53);
                        if (o54) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 419, 13);
                        uint32_t t55 = r53;
                        gm_l2 = t55;
                    }
                    goto me10;
mn11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 415, 5);
me10:;
                }
lc3:;
                {
                    uint64_t t57 = gm_l4;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l4 = t60;
                }
            }
lb3:;
        }
    }
    {
        uint32_t t61 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out))[0] = 0;
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9mtkhurfjls7 *)gm_out) + 8, t61);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t9mtkhurfjls7 gm_f_hex4_23k9kdb_c(void * a0, uint64_t a1) { gm_t9mtkhurfjls7 gm_r; gm_f_hex4_23k9kdb(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_hex_value_q8sogka(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hex_value_q8sogka F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = gm_l0;
        uint8_t t4 = (t3 >= ((uint8_t)48ULL));
        if (!t4) goto ie2;
        uint8_t t5 = gm_l0;
        uint8_t t6 = (t5 <= ((uint8_t)57ULL));
        if (!t6) goto ie2;
        {
            uint8_t t8 = gm_l0;
            uint8_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint8_t)48ULL), &r9);
            if (o10) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 406, 39);
            uint8_t t11 = r9;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t14 = gm_l0;
        uint8_t t15 = (t14 >= ((uint8_t)97ULL));
        if (!t15) goto ie13;
        uint8_t t16 = gm_l0;
        uint8_t t17 = (t16 <= ((uint8_t)102ULL));
        if (!t17) goto ie13;
        {
            uint8_t t19 = gm_l0;
            uint8_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint8_t)97ULL), &r20);
            if (o21) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 407, 39);
            uint8_t t22 = r20;
            uint8_t r23; bool o24 = __builtin_add_overflow(t22, ((uint8_t)10ULL), &r23);
            if (o24) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 407, 39);
            uint8_t t25 = r23;
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint8_t t28 = gm_l0;
        uint8_t t29 = (t28 >= ((uint8_t)65ULL));
        if (!t29) goto ie27;
        uint8_t t30 = gm_l0;
        uint8_t t31 = (t30 <= ((uint8_t)70ULL));
        if (!t31) goto ie27;
        {
            uint8_t t33 = gm_l0;
            uint8_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint8_t)65ULL), &r34);
            if (o35) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 408, 39);
            uint8_t t36 = r34;
            uint8_t r37; bool o38 = __builtin_add_overflow(t36, ((uint8_t)10ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 408, 39);
            uint8_t t39 = r37;
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        uint8_t t41 = 0;
        F.ret = ((uint8_t)255ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_digit_k7levme(uint8_t a0);
__attribute__((aligned(4))) void gm_f_literal_9ia3g6h(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_literal_9ia3g6h F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
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
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l4 = t13;
                    {
                        void * t16 = gm_l0;
                        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 16));
                        uint64_t t18 = gm_l4;
                        uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
                        if (o20) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 366, 12);
                        uint64_t t21 = r19;
                        void * t22 = gm_l0;
                        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8);
                        uint8_t t24 = (t21 >= t23);
                        if (!t24) goto ie15;
                        {
                            void * t26 = gm_l0;
                            uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
                            gm_tekvgkj2hms37 t28;
                            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t28 + 8), t27);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8, t28);
                            goto gm_fdone;
                        }
                        goto ix15;
ie15:;
ix15:;
                    }
                    {
                        void * t30 = gm_l0;
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 16));
                        uint64_t t32 = gm_l4;
                        uint64_t r33; bool o34 = __builtin_add_overflow(t31, t32, &r33);
                        if (o34) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 367, 19);
                        uint64_t t35 = r33;
                        uint64_t t36 = (uintptr_t)t35;
                        void * t37 = gm_l0;
                        uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 0) + 8);
                        if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_srcge9itd5fie17, 367, 12);
                        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t37) + 0)) + t36 * 1));
                        uint64_t t40 = gm_l4;
                        uint64_t t41 = (uintptr_t)t40;
                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcge9itd5fie17, 367, 37);
                        uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                        uint8_t t44 = (!(t39 == t43));
                        if (!t44) goto ie29;
                        {
                            void * t46 = gm_l0;
                            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 16));
                            uint64_t t48 = gm_l4;
                            uint64_t r49; bool o50 = __builtin_add_overflow(t47, t48, &r49);
                            if (o50) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 367, 71);
                            uint64_t t51 = r49;
                            gm_tekvgkj2hms37 t52;
                            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t52 + 8), t51);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8, t52);
                            goto gm_fdone;
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 365, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t54 = gm_l3;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l3 = t57;
                }
            }
lb4:;
        }
    }
    {
        void * t58 = gm_l0;
        void * t59 = gm_l0;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 16));
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r62; bool o63 = __builtin_add_overflow(t60, t61, &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 369, 5);
        uint64_t t64 = r62;
        GM_ST(uint64_t, (((uint8_t *)t58) + 16), t64);
    }
    {
        gm_b0_1 t65;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tekh1vgsjf5ak *)gm_out) + 8, t65);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tekh1vgsjf5ak gm_f_literal_9ia3g6h_c(void * a0, gm_b16_8 a1) { gm_tekh1vgsjf5ak gm_r; gm_f_literal_9ia3g6h(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_normalize_pcair89(void *gm_out, gm_b16_8 a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_normalize_pcair89 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l2; /* total */
    uint64_t gm_l3; /* lo */
    uint64_t gm_l4; /* hi */
    int64_t gm_l5; /* int_len */
    GM_STACK_CHECK_SHALLOW(&F, 358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t r4; bool o5 = __builtin_sub_overflow(t2, t3, &r4);
        if (o5) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 229, 15);
        uint64_t t6 = r4;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 32));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        uint64_t r9; bool o10 = __builtin_sub_overflow(t7, t8, &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 229, 43);
        uint64_t t11 = r9;
        uint64_t r12; bool o13 = __builtin_add_overflow(t6, t11, &r12);
        if (o13) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 229, 14);
        uint64_t t14 = r12;
        gm_l2 = t14;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t16 = gm_l3;
            uint64_t t17 = gm_l2;
            uint8_t t18 = (t16 < t17);
            if (!t18) goto lb15;
            gm_b16_8 t19 = gm_l0;
            uint64_t t20 = gm_l3;
            uint8_t t21 = gm_f_digit_at_1oh5ota(t19, &(*(gm_t3072hbjtr5t6 *)((uint8_t *)a1)), t20);
            uint8_t t22 = ((t21 == ((uint8_t)48ULL)));
            if (!t22) goto lb15;
            {
                uint64_t t25 = gm_l3;
                uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                if (o27) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 231, 54);
                uint64_t t28 = r26;
                gm_l3 = t28;
            }
lc15:;
        }
lb15:;
    }
    {
        uint64_t t30 = gm_l3;
        uint64_t t31 = gm_l2;
        uint8_t t32 = ((t30 == t31));
        if (!t32) goto ie29;
        {
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 0, ((uint64_t)0ULL));
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 8, ((uint64_t)0ULL));
            GM_ST(int64_t, (uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 16, ((int64_t)0LL));
            goto gm_fdone;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        uint64_t t34 = gm_l2;
        gm_l4 = t34;
    }
    {
        for (;;) {
            gm_b16_8 t36 = gm_l0;
            uint64_t t37 = gm_l4;
            uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
            if (o39) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 234, 26);
            uint64_t t40 = r38;
            uint8_t t41 = gm_f_digit_at_1oh5ota(t36, &(*(gm_t3072hbjtr5t6 *)((uint8_t *)a1)), t40);
            uint8_t t42 = ((t41 == ((uint8_t)48ULL)));
            if (!t42) goto lb35;
            {
                uint64_t t45 = gm_l4;
                uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
                if (o47) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 234, 43);
                uint64_t t48 = r46;
                gm_l4 = t48;
            }
lc35:;
        }
lb35:;
    }
    {
        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t r51; bool o52 = __builtin_sub_overflow(t49, t50, &r51);
        if (o52) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 235, 17);
        uint64_t t53 = r51;
        int64_t t54 = (int64_t)t53;
        gm_l5 = t54;
    }
    {
        uint64_t t55 = gm_l3;
        uint64_t t56 = gm_l4;
        uint64_t t57 = gm_l3;
        uint64_t r58; bool o59 = __builtin_sub_overflow(t56, t57, &r58);
        if (o59) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 236, 37);
        uint64_t t60 = r58;
        int64_t t61 = gm_l5;
        int64_t r62; bool o63 = __builtin_sub_overflow(t61, ((int64_t)1LL), &r62);
        if (o63) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 236, 51);
        int64_t t64 = r62;
        uint64_t t65 = gm_l3;
        int64_t t66 = (int64_t)t65;
        int64_t r67; bool o68 = __builtin_sub_overflow(t64, t66, &r67);
        if (o68) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 236, 51);
        int64_t t69 = r67;
        int64_t t70 = GM_LD(int64_t, (((uint8_t *)a1) + 40));
        int64_t r71; bool o72 = __builtin_add_overflow(t69, t70, &r71);
        if (o72) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 236, 51);
        int64_t t73 = r71;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 0, t55);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 8, t60);
        GM_ST(int64_t, (uint8_t *)&(*(gm_tcs9tli4q9d6j *)gm_out) + 16, t73);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csv08vjo2a94sp gm_f_normalize_pcair89_c(gm_b16_8 a0, gm_csnt0cc832rgst a1) { gm_tcs9tli4q9d6j gm_r; gm_f_normalize_pcair89(&gm_r, a0, &a1); return GM_CONV(gm_csv08vjo2a94sp, gm_r); }
__attribute__((aligned(4))) void gm_f_parse_array_54l90b7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_parse_array_54l90b7 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    gm_t8vuock3jnv59 gm_l3; /* v */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 791u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
        uint8_t t6 = (t3 >= t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 16));
            gm_tekvgkj2hms37 t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)8ULL));
            GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t9);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t10);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 553, 5);
        uint64_t t16 = r14;
        GM_ST(uint64_t, (((uint8_t *)t11) + 16), t16);
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = gm_l0;
        gm_f_skip_ws_g70e352(t17);
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 16));
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 0) + 8);
        uint8_t t23 = (t20 < t22);
        if (!t23) goto ie18;
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 16));
        uint64_t t26 = (uintptr_t)t25;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srcge9itd5fie17, 556, 31);
        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 0)) + t26 * 1));
        uint8_t t30 = ((t29 == ((uint8_t)93ULL)));
        if (!t30) goto ie18;
        {
            void * t32 = gm_l0;
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 16));
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            if (o36) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 557, 9);
            uint64_t t37 = r35;
            GM_ST(uint64_t, (((uint8_t *)t32) + 16), t37);
        }
        {
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_t8vuock3jnv59 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)4ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t39);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb40;
            {
                void * t43 = gm_l0;
                gm_f_skip_ws_g70e352(t43);
            }
            {
                void * t44 = gm_l0;
                uint64_t t45 = gm_l1;
                uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                if (o47) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 562, 33);
                uint64_t t48 = r46;
                gm_te0t4fvim36sr t49;
                gm_f_parse_value_cgemdat((void *)&t49, (void *)NULL, cx, NULL, t44, t48);
                if (gm_unw.kind) {
                    goto gm_ll41;
                }
                if (((((uint8_t *)&t49))[0] != 0)) {
                    gm_tekvgkj2hms37 t50 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t49) + 8));
                    gm_te0t4fvim36sr t51;
                    GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
                    ((uint8_t *)&t51)[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t51 + 8), t50);
                    F.ret = t51;
                    goto gm_fret;
                }
                gm_l3 = (*(gm_t8vuock3jnv59 *)(((uint8_t *)&t49) + 8));
            }
            {
                void * t52 = (void *)((uint8_t *)&F.l2);
                gm_tftb1odc739t5 t53 = gm_f_push_0pgacsj_3(t52, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l3)));
                if (((((uint8_t *)&t53))[0] != 0)) {
                    gm_t9qkh0rsooc8d t54 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t53) + 1));
                    gm_tekvgkj2hms37 t55;
                    GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t55 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t55 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t55 + 12, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)9ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t55 + 8), t54);
                    gm_te0t4fvim36sr t56;
                    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t56 + 24, 0);
                    ((uint8_t *)&t56)[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t56 + 8), t55);
                    F.ret = t56;
                    goto gm_fret;
                }
                gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
            }
            {
                void * t58 = gm_l0;
                gm_f_skip_ws_g70e352(t58);
            }
            {
                void * t60 = gm_l0;
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 16));
                void * t62 = gm_l0;
                uint64_t t63 = GM_LD(uintptr_t, (((uint8_t *)t62) + 0) + 8);
                uint8_t t64 = (t61 >= t63);
                if (!t64) goto ie59;
                {
                    void * t66 = gm_l0;
                    uint64_t t67 = GM_LD(uintptr_t, (((uint8_t *)t66) + 0) + 8);
                    gm_tekvgkj2hms37 t68;
                    GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t68 + 8), t67);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t68);
                    goto gm_fdone;
                }
                goto ix59;
ie59:;
ix59:;
            }
            {
                void * t69 = gm_l0;
                uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 16));
                uint64_t t71 = (uintptr_t)t70;
                void * t72 = gm_l0;
                uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)t72) + 0) + 8);
                if (t71 >= (uintptr_t)(t73)) gm_trap_index(t71, t73, gm_srcge9itd5fie17, 566, 14);
                uint8_t t74 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t72) + 0)) + t71 * 1));
                gm_l4 = t74;
            }
            {
                uint8_t t76 = gm_l4;
                uint8_t t77 = ((t76 == ((uint8_t)44ULL)));
                if (!t77) goto ie75;
                {
                    void * t79 = gm_l0;
                    void * t80 = gm_l0;
                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)t80) + 16));
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    if (o83) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 568, 13);
                    uint64_t t84 = r82;
                    GM_ST(uint64_t, (((uint8_t *)t79) + 16), t84);
                }
                goto ix75;
ie75:;
                uint8_t t86 = gm_l4;
                uint8_t t87 = ((t86 == ((uint8_t)93ULL)));
                if (!t87) goto ie85;
                {
                    void * t89 = gm_l0;
                    void * t90 = gm_l0;
                    uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t90) + 16));
                    uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                    if (o93) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 570, 13);
                    uint64_t t94 = r92;
                    GM_ST(uint64_t, (((uint8_t *)t89) + 16), t94);
                }
                {
                    goto lb40;
                }
                goto ix85;
ie85:;
                {
                    void * t96 = gm_l0;
                    uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)t96) + 16));
                    gm_tekvgkj2hms37 t98;
                    GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t98 + 8), t97);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t98);
                    goto gm_fdone;
                }
ix85:;
ix75:;
            }
lc40:;
        }
gm_ll41:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
        }
        goto gm_lad;
lb40:;
    }
    {
        void * t99 = (void *)((uint8_t *)&F.l2);
        gm_tabb710d9mq2e t100;
        gm_f_into_slice_e14ea2s((void *)&t100, t99);
        if (((((uint8_t *)&t100))[0] != 0)) {
            gm_t9qkh0rsooc8d t101 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t100) + 8));
            gm_tekvgkj2hms37 t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t102 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t102 + 12, 0);
            GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)9ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102 + 8), t101);
            gm_te0t4fvim36sr t103;
            GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t103 + 24, 0);
            ((uint8_t *)&t103)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t103 + 8), t102);
            F.ret = t103;
            goto gm_fret;
        }
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
        gm_t8vuock3jnv59 t105;
        GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)4ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 8), t104);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
        GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t105);
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
inline __attribute__((aligned(4))) void gm_f_parse_document_0bpimpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_document_0bpimpc F;
    void * gm_l0; /* p */
    gm_t8vuock3jnv59 gm_l1; /* v */
    GM_STACK_CHECK(&F, 209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_skip_ws_g70e352(t2);
    }
    {
        void * t3 = gm_l0;
        gm_te0t4fvim36sr t4;
        gm_f_parse_value_cgemdat((void *)&t4, (void *)NULL, cx, NULL, t3, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_tekvgkj2hms37 t5 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t4) + 8));
            gm_te0t4fvim36sr t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = (*(gm_t8vuock3jnv59 *)(((uint8_t *)&t4) + 8));
    }
    {
        void * t7 = gm_l0;
        gm_f_skip_ws_g70e352(t7);
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 16));
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        uint8_t t13 = (t10 < t12);
        if (!t13) goto ie8;
        {
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
            gm_tekvgkj2hms37 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)7ULL));
            GM_ST(uint64_t, ((uint8_t *)&t17 + 8), t16);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t17);
            goto gm_fdone;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        const uint8_t *t18 = ((uint8_t *)&gm_l1);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t18, 24);
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
inline __attribute__((aligned(4))) void gm_f_parse_invslf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_parse_invslf8 F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* max_depth */
    gm_tgbgti82mf879 gm_l3; /* p */
    gm_te0t4fvim36sr gm_l4; /* res */
    gm_tekvgkj2hms37 gm_l5; /* e */
    gm_t8vuock3jnv59 gm_l6; /* root */
    GM_STACK_CHECK(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_new_jr5ejs0((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t2 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&gm_l3 + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&gm_l3 + 16, ((uint64_t)0ULL));
        uint64_t t3 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&gm_l3 + 24, t3);
    }
    {
        gm_t8vuock3jnv59 t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&gm_l4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l4 + 4, 0);
        ((uint8_t *)&gm_l4)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, (uint8_t *)&gm_l4 + 8, t4);
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l2);
        gm_t3mf58gv9kf3n t6 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_allocator_5m783ja(t5));
        gm_t3mf58gv9kf3n t7 = t6;
        gm_node wn8 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t7, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            void * t12 = (void *)((uint8_t *)&gm_l3);
            gm_f_parse_document_0bpimpc((void *)&(*(gm_te0t4fvim36sr *)((uint8_t *)&gm_l4)), (void *)NULL, (&wn8), NULL, t12);
            if (gm_unw.kind) {
                goto gm_ll10;
            }
        }
        F.w0 = GM_ZERO(gm_b0_1);
        goto gm_ls13;
gm_ll10:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we9; }
        goto gm_lad;
gm_ls13:;
we9:;
        gm_b0_1 t14 = F.w0;
    }
    {
        gm_te0t4fvim36sr t15 = gm_l4;
        gm_t8vuock3jnv59 t16;
        if (!((((uint8_t *)&t15))[0] != 0)) {
            t16 = (*(gm_t8vuock3jnv59 *)(((uint8_t *)&t15) + 8));
        } else {
            gm_tekvgkj2hms37 t17 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t15) + 8));
            gm_l5 = t17;
            {
                void * t19 = (void *)((uint8_t *)&F.l2);
                gm_f_deinit_sejdpqv(t19);
            }
            gm_tekvgkj2hms37 t20 = gm_l5;
            memset(&(*(gm_tit4ajsn3pv91 *)gm_out), 0, sizeof (*(gm_tit4ajsn3pv91 *)gm_out));
            ((uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 8, t20);
            goto gm_fdone;
        }
        gm_l6 = t16;
    }
    {
        gm_thshlh2q7ekfv t21;
        memcpy((uint8_t *)&t21 + 0, ((uint8_t *)&gm_l6), 24);
        memcpy((uint8_t *)&t21 + 24, ((uint8_t *)&F.l2), 48);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out))[0] = 0;
        GM_ST(gm_thshlh2q7ekfv, (uint8_t *)&(*(gm_tit4ajsn3pv91 *)gm_out) + 8, t21);
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
__attribute__((aligned(4))) void gm_f_parse_number_buivaka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_number_buivaka F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 1141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
        gm_l2 = t5;
    }
    {
        uint64_t t6 = gm_l2;
        gm_l3 = t6;
    }
    {
        uint64_t t8 = gm_l3;
        uint64_t t9 = (uintptr_t)t8;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srcge9itd5fie17, 377, 8);
        uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t9 * 1));
        uint8_t t12 = ((t11 == ((uint8_t)45ULL)));
        if (!t12) goto ie7;
        {
            uint64_t t14 = gm_l3;
            uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
            if (o16) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 377, 22);
            uint64_t t17 = r15;
            gm_l3 = t17;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t19 = gm_l3;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t21 = (t19 >= t20);
        if (!t21) goto ie18;
        {
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_tekvgkj2hms37 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t24 + 8), t23);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        uint64_t t26 = gm_l3;
        uint64_t t27 = (uintptr_t)t26;
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcge9itd5fie17, 379, 8);
        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 1));
        uint8_t t30 = ((t29 == ((uint8_t)48ULL)));
        if (!t30) goto ie25;
        {
            uint64_t t32 = gm_l3;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            if (o34) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 380, 9);
            uint64_t t35 = r33;
            gm_l3 = t35;
        }
        {
            uint64_t t37 = gm_l3;
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t39 = (t37 < t38);
            if (!t39) goto ie36;
            uint64_t t40 = gm_l3;
            uint64_t t41 = (uintptr_t)t40;
            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcge9itd5fie17, 381, 35);
            uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
            uint8_t t44 = gm_f_is_digit_k7levme(t43);
            if (!t44) goto ie36;
            {
                uint64_t t46 = gm_l3;
                gm_tekvgkj2hms37 t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t47 + 8), t46);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t47);
                goto gm_fdone;
            }
            goto ix36;
ie36:;
ix36:;
        }
        goto ix25;
ie25:;
        uint64_t t49 = gm_l3;
        uint64_t t50 = (uintptr_t)t49;
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcge9itd5fie17, 382, 15);
        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
        uint8_t t53 = (t52 >= ((uint8_t)49ULL));
        if (!t53) goto ie48;
        uint64_t t54 = gm_l3;
        uint64_t t55 = (uintptr_t)t54;
        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_srcge9itd5fie17, 382, 31);
        uint8_t t57 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t55 * 1));
        uint8_t t58 = (t57 <= ((uint8_t)57ULL));
        if (!t58) goto ie48;
        {
            for (;;) {
                uint64_t t61 = gm_l3;
                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t63 = (t61 < t62);
                if (!t63) goto lb60;
                uint64_t t64 = gm_l3;
                uint64_t t65 = (uintptr_t)t64;
                uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_srcge9itd5fie17, 383, 38);
                uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t65 * 1));
                uint8_t t68 = gm_f_is_digit_k7levme(t67);
                if (!t68) goto lb60;
                {
                    uint64_t t71 = gm_l3;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    if (o73) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 383, 46);
                    uint64_t t74 = r72;
                    gm_l3 = t74;
                }
lc60:;
            }
lb60:;
        }
        goto ix48;
ie48:;
        {
            uint64_t t76 = gm_l3;
            gm_tekvgkj2hms37 t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)2ULL));
            GM_ST(uint64_t, ((uint8_t *)&t77 + 8), t76);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t77);
            goto gm_fdone;
        }
ix48:;
ix25:;
    }
    {
        uint64_t t79 = gm_l3;
        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t81 = (t79 < t80);
        if (!t81) goto ie78;
        uint64_t t82 = gm_l3;
        uint64_t t83 = (uintptr_t)t82;
        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srcge9itd5fie17, 387, 22);
        uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t83 * 1));
        uint8_t t86 = ((t85 == ((uint8_t)46ULL)));
        if (!t86) goto ie78;
        {
            uint64_t t88 = gm_l3;
            uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
            if (o90) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 388, 9);
            uint64_t t91 = r89;
            gm_l3 = t91;
        }
        {
            uint64_t t93 = gm_l3;
            uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t95 = (t93 >= t94);
            if (!t95) goto ie92;
            {
                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                gm_tekvgkj2hms37 t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t98 + 8), t97);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t98);
                goto gm_fdone;
            }
            goto ix92;
ie92:;
ix92:;
        }
        {
            uint64_t t100 = gm_l3;
            uint64_t t101 = (uintptr_t)t100;
            uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srcge9itd5fie17, 390, 25);
            uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t101 * 1));
            uint8_t t104 = gm_f_is_digit_k7levme(t103);
            uint8_t t105 = (uint8_t)!t104;
            if (!t105) goto ie99;
            {
                uint64_t t107 = gm_l3;
                gm_tekvgkj2hms37 t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t108 + 8), t107);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t108);
                goto gm_fdone;
            }
            goto ix99;
ie99:;
ix99:;
        }
        {
            for (;;) {
                uint64_t t110 = gm_l3;
                uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t112 = (t110 < t111);
                if (!t112) goto lb109;
                uint64_t t113 = gm_l3;
                uint64_t t114 = (uintptr_t)t113;
                uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_srcge9itd5fie17, 391, 38);
                uint8_t t116 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t114 * 1));
                uint8_t t117 = gm_f_is_digit_k7levme(t116);
                if (!t117) goto lb109;
                {
                    uint64_t t120 = gm_l3;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    if (o122) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 391, 46);
                    uint64_t t123 = r121;
                    gm_l3 = t123;
                }
lc109:;
            }
lb109:;
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        uint64_t t125 = gm_l3;
        uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t127 = (t125 < t126);
        if (!t127) goto ie124;
        uint8_t t128 = 0;
        uint64_t t129 = gm_l3;
        uint64_t t130 = (uintptr_t)t129;
        uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t130 >= (uintptr_t)(t131)) gm_trap_index(t130, t131, gm_srcge9itd5fie17, 393, 23);
        uint8_t t132 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t130 * 1));
        uint8_t t133 = ((t132 == ((uint8_t)101ULL)));
        if (!t133) {
            uint64_t t134 = gm_l3;
            uint64_t t135 = (uintptr_t)t134;
            uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t135 >= (uintptr_t)(t136)) gm_trap_index(t135, t136, gm_srcge9itd5fie17, 393, 38);
            uint8_t t137 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t135 * 1));
            uint8_t t138 = ((t137 == ((uint8_t)69ULL)));
            t128 = t138;
        } else t128 = 1;
        if (!t128) goto ie124;
        {
            uint64_t t140 = gm_l3;
            uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
            if (o142) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 394, 9);
            uint64_t t143 = r141;
            gm_l3 = t143;
        }
        {
            uint64_t t145 = gm_l3;
            uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t147 = (t145 < t146);
            if (!t147) goto ie144;
            uint8_t t148 = 0;
            uint64_t t149 = gm_l3;
            uint64_t t150 = (uintptr_t)t149;
            uint64_t t151 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t150 >= (uintptr_t)(t151)) gm_trap_index(t150, t151, gm_srcge9itd5fie17, 395, 27);
            uint8_t t152 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t150 * 1));
            uint8_t t153 = ((t152 == ((uint8_t)43ULL)));
            if (!t153) {
                uint64_t t154 = gm_l3;
                uint64_t t155 = (uintptr_t)t154;
                uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t155 >= (uintptr_t)(t156)) gm_trap_index(t155, t156, gm_srcge9itd5fie17, 395, 42);
                uint8_t t157 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t155 * 1));
                uint8_t t158 = ((t157 == ((uint8_t)45ULL)));
                t148 = t158;
            } else t148 = 1;
            if (!t148) goto ie144;
            {
                uint64_t t160 = gm_l3;
                uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                if (o162) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 395, 57);
                uint64_t t163 = r161;
                gm_l3 = t163;
            }
            goto ix144;
ie144:;
ix144:;
        }
        {
            uint64_t t165 = gm_l3;
            uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t167 = (t165 >= t166);
            if (!t167) goto ie164;
            {
                uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                gm_tekvgkj2hms37 t170;
                GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)0ULL));
                GM_ST(uint64_t, ((uint8_t *)&t170 + 8), t169);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t170);
                goto gm_fdone;
            }
            goto ix164;
ie164:;
ix164:;
        }
        {
            uint64_t t172 = gm_l3;
            uint64_t t173 = (uintptr_t)t172;
            uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t173 >= (uintptr_t)(t174)) gm_trap_index(t173, t174, gm_srcge9itd5fie17, 397, 25);
            uint8_t t175 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t173 * 1));
            uint8_t t176 = gm_f_is_digit_k7levme(t175);
            uint8_t t177 = (uint8_t)!t176;
            if (!t177) goto ie171;
            {
                uint64_t t179 = gm_l3;
                gm_tekvgkj2hms37 t180;
                GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)2ULL));
                GM_ST(uint64_t, ((uint8_t *)&t180 + 8), t179);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 1;
                GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t180);
                goto gm_fdone;
            }
            goto ix171;
ie171:;
ix171:;
        }
        {
            for (;;) {
                uint64_t t182 = gm_l3;
                uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t184 = (t182 < t183);
                if (!t184) goto lb181;
                uint64_t t185 = gm_l3;
                uint64_t t186 = (uintptr_t)t185;
                uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_srcge9itd5fie17, 398, 38);
                uint8_t t188 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t186 * 1));
                uint8_t t189 = gm_f_is_digit_k7levme(t188);
                if (!t189) goto lb181;
                {
                    uint64_t t192 = gm_l3;
                    uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                    if (o194) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 398, 46);
                    uint64_t t195 = r193;
                    gm_l3 = t195;
                }
lc181:;
            }
lb181:;
        }
        goto ix124;
ie124:;
ix124:;
    }
    {
        void * t196 = gm_l0;
        uint64_t t197 = gm_l3;
        GM_ST(uint64_t, (((uint8_t *)t196) + 16), t197);
    }
    {
        uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t199 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t200 = gm_l2;
        uint64_t t201 = (uintptr_t)t200;
        uint64_t t202 = gm_l3;
        uint64_t t203 = (uintptr_t)t202;
        if (t201 > t203 || t203 > (uintptr_t)t198) gm_trap_slice(t201, t203, t198, gm_srcge9itd5fie17, 401, 34);
        gm_b16_8 t204;
        GM_ST(void *, (uint8_t *)&t204, (void *)(((uint8_t *)t199 + t201 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(t203 - t201));
        gm_tabb710d9mq2e t205;
        gm_f_dupe_g9iqpvn((void *)&t205, (void *)NULL, cx, NULL, t204, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdjk0ti7atv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t205))[0] != 0)) {
            gm_t9qkh0rsooc8d t206 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t205) + 8));
            gm_tekvgkj2hms37 t207;
            GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t207 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t207 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t207 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t207 + 12, 0);
            GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)9ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t207 + 8), t206);
            gm_t24hlg9v4qo9a t208;
            GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
            ((uint8_t *)&t208)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t208 + 8), t207);
            F.ret = t208;
            goto gm_fret;
        }
        gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&t205) + 8));
        gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
        gm_tnpifv20irnph t211;
        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 0), t210);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out))[0] = 0;
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_t24hlg9v4qo9a *)gm_out) + 8, t211);
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
__attribute__((aligned(4))) void gm_f_parse_object_4os6ms3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_parse_object_4os6ms3 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* k */
    gm_t8vuock3jnv59 gm_l4; /* v */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 1285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
        uint8_t t6 = (t3 >= t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 16));
            gm_tekvgkj2hms37 t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)8ULL));
            GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t9);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t10);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 581, 5);
        uint64_t t16 = r14;
        GM_ST(uint64_t, (((uint8_t *)t11) + 16), t16);
    }
    {
        gm_f_list_792g2go_2((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = gm_l0;
        gm_f_skip_ws_g70e352(t17);
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 16));
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 0) + 8);
        uint8_t t23 = (t20 < t22);
        if (!t23) goto ie18;
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 16));
        uint64_t t26 = (uintptr_t)t25;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srcge9itd5fie17, 584, 31);
        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 0)) + t26 * 1));
        uint8_t t30 = ((t29 == ((uint8_t)125ULL)));
        if (!t30) goto ie18;
        {
            void * t32 = gm_l0;
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 16));
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            if (o36) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 585, 9);
            uint64_t t37 = r35;
            GM_ST(uint64_t, (((uint8_t *)t32) + 16), t37);
        }
        {
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_t8vuock3jnv59 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t39);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb40;
            {
                void * t43 = gm_l0;
                gm_f_skip_ws_g70e352(t43);
            }
            {
                void * t45 = gm_l0;
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 16));
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uintptr_t, (((uint8_t *)t47) + 0) + 8);
                uint8_t t49 = (t46 >= t48);
                if (!t49) goto ie44;
                {
                    void * t51 = gm_l0;
                    uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)t51) + 0) + 8);
                    gm_tekvgkj2hms37 t53;
                    GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t53 + 8), t52);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t53);
                    goto gm_fdone;
                }
                goto ix44;
ie44:;
ix44:;
            }
            {
                void * t55 = gm_l0;
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
                uint64_t t57 = (uintptr_t)t56;
                void * t58 = gm_l0;
                uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)t58) + 0) + 8);
                if (t57 >= (uintptr_t)(t59)) gm_trap_index(t57, t59, gm_srcge9itd5fie17, 591, 12);
                uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t58) + 0)) + t57 * 1));
                uint8_t t61 = (!(t60 == ((uint8_t)34ULL)));
                if (!t61) goto ie54;
                {
                    void * t63 = gm_l0;
                    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)t63) + 16));
                    gm_tekvgkj2hms37 t65;
                    GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t65 + 8), t64);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t65);
                    goto gm_fdone;
                }
                goto ix54;
ie54:;
ix54:;
            }
            {
                void * t66 = gm_l0;
                gm_tfe4nnqq2l987 t67;
                gm_f_parse_string_o9jl88i((void *)&t67, (void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_ll41;
                }
                if (((((uint8_t *)&t67))[0] != 0)) {
                    gm_tekvgkj2hms37 t68 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t67) + 8));
                    gm_te0t4fvim36sr t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t69 + 24, 0);
                    ((uint8_t *)&t69)[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t69 + 8), t68);
                    F.ret = t69;
                    goto gm_fret;
                }
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                gm_l3 = t70;
            }
            {
                void * t71 = gm_l0;
                gm_f_skip_ws_g70e352(t71);
            }
            {
                void * t73 = gm_l0;
                uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 16));
                void * t75 = gm_l0;
                uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)t75) + 0) + 8);
                uint8_t t77 = (t74 >= t76);
                if (!t77) goto ie72;
                {
                    void * t79 = gm_l0;
                    uint64_t t80 = GM_LD(uintptr_t, (((uint8_t *)t79) + 0) + 8);
                    gm_tekvgkj2hms37 t81;
                    GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t81 + 8), t80);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t81);
                    goto gm_fdone;
                }
                goto ix72;
ie72:;
ix72:;
            }
            {
                void * t83 = gm_l0;
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)t83) + 16));
                uint64_t t85 = (uintptr_t)t84;
                void * t86 = gm_l0;
                uint64_t t87 = GM_LD(uintptr_t, (((uint8_t *)t86) + 0) + 8);
                if (t85 >= (uintptr_t)(t87)) gm_trap_index(t85, t87, gm_srcge9itd5fie17, 595, 12);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t86) + 0)) + t85 * 1));
                uint8_t t89 = (!(t88 == ((uint8_t)58ULL)));
                if (!t89) goto ie82;
                {
                    void * t91 = gm_l0;
                    uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)t91) + 16));
                    gm_tekvgkj2hms37 t93;
                    GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t93 + 8), t92);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t93);
                    goto gm_fdone;
                }
                goto ix82;
ie82:;
ix82:;
            }
            {
                void * t94 = gm_l0;
                void * t95 = gm_l0;
                uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)t95) + 16));
                uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                if (o98) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 596, 9);
                uint64_t t99 = r97;
                GM_ST(uint64_t, (((uint8_t *)t94) + 16), t99);
            }
            {
                void * t100 = gm_l0;
                gm_f_skip_ws_g70e352(t100);
            }
            {
                void * t101 = gm_l0;
                uint64_t t102 = gm_l1;
                uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                if (o104) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 598, 33);
                uint64_t t105 = r103;
                gm_te0t4fvim36sr t106;
                gm_f_parse_value_cgemdat((void *)&t106, (void *)NULL, cx, NULL, t101, t105);
                if (gm_unw.kind) {
                    goto gm_ll41;
                }
                if (((((uint8_t *)&t106))[0] != 0)) {
                    gm_tekvgkj2hms37 t107 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t106) + 8));
                    gm_te0t4fvim36sr t108;
                    GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t108 + 24, 0);
                    ((uint8_t *)&t108)[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t108 + 8), t107);
                    F.ret = t108;
                    goto gm_fret;
                }
                gm_l4 = (*(gm_t8vuock3jnv59 *)(((uint8_t *)&t106) + 8));
            }
            {
                void * t109 = (void *)((uint8_t *)&F.l2);
                gm_tfkvqluahdpno t110;
                gm_b16_8 t111 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t110 + 0, t111);
                memcpy((uint8_t *)&t110 + 16, ((uint8_t *)&gm_l4), 24);
                gm_tftb1odc739t5 t112 = gm_f_push_m55rf90_2(t109, &t110);
                if (((((uint8_t *)&t112))[0] != 0)) {
                    gm_t9qkh0rsooc8d t113 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t112) + 1));
                    gm_tekvgkj2hms37 t114;
                    GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t114 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t114 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t114 + 12, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)9ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t114 + 8), t113);
                    gm_te0t4fvim36sr t115;
                    GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t115 + 24, 0);
                    ((uint8_t *)&t115)[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t115 + 8), t114);
                    F.ret = t115;
                    goto gm_fret;
                }
                gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
            }
            {
                void * t117 = gm_l0;
                gm_f_skip_ws_g70e352(t117);
            }
            {
                void * t119 = gm_l0;
                uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)t119) + 16));
                void * t121 = gm_l0;
                uint64_t t122 = GM_LD(uintptr_t, (((uint8_t *)t121) + 0) + 8);
                uint8_t t123 = (t120 >= t122);
                if (!t123) goto ie118;
                {
                    void * t125 = gm_l0;
                    uint64_t t126 = GM_LD(uintptr_t, (((uint8_t *)t125) + 0) + 8);
                    gm_tekvgkj2hms37 t127;
                    GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t127 + 8), t126);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t127);
                    goto gm_fdone;
                }
                goto ix118;
ie118:;
ix118:;
            }
            {
                void * t128 = gm_l0;
                uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 16));
                uint64_t t130 = (uintptr_t)t129;
                void * t131 = gm_l0;
                uint64_t t132 = GM_LD(uintptr_t, (((uint8_t *)t131) + 0) + 8);
                if (t130 >= (uintptr_t)(t132)) gm_trap_index(t130, t132, gm_srcge9itd5fie17, 602, 14);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t131) + 0)) + t130 * 1));
                gm_l5 = t133;
            }
            {
                uint8_t t135 = gm_l5;
                uint8_t t136 = ((t135 == ((uint8_t)44ULL)));
                if (!t136) goto ie134;
                {
                    void * t138 = gm_l0;
                    void * t139 = gm_l0;
                    uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)t139) + 16));
                    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                    if (o142) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 604, 13);
                    uint64_t t143 = r141;
                    GM_ST(uint64_t, (((uint8_t *)t138) + 16), t143);
                }
                goto ix134;
ie134:;
                uint8_t t145 = gm_l5;
                uint8_t t146 = ((t145 == ((uint8_t)125ULL)));
                if (!t146) goto ie144;
                {
                    void * t148 = gm_l0;
                    void * t149 = gm_l0;
                    uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)t149) + 16));
                    uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                    if (o152) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 606, 13);
                    uint64_t t153 = r151;
                    GM_ST(uint64_t, (((uint8_t *)t148) + 16), t153);
                }
                {
                    goto lb40;
                }
                goto ix144;
ie144:;
                {
                    void * t155 = gm_l0;
                    uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)t155) + 16));
                    gm_tekvgkj2hms37 t157;
                    GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t157 + 8), t156);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t157);
                    goto gm_fdone;
                }
ix144:;
ix134:;
            }
lc40:;
        }
gm_ll41:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
        }
        goto gm_lad;
lb40:;
    }
    {
        void * t158 = (void *)((uint8_t *)&F.l2);
        gm_tabb710d9mq2e t159;
        gm_f_into_slice_hns5a32((void *)&t159, t158);
        if (((((uint8_t *)&t159))[0] != 0)) {
            gm_t9qkh0rsooc8d t160 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t159) + 8));
            gm_tekvgkj2hms37 t161;
            GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t161 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t161 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t161 + 12, 0);
            GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)9ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t161 + 8), t160);
            gm_te0t4fvim36sr t162;
            GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t162 + 24, 0);
            ((uint8_t *)&t162)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t162 + 8), t161);
            F.ret = t162;
            goto gm_fret;
        }
        gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t159) + 8));
        gm_t8vuock3jnv59 t164;
        GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)5ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t164 + 8), t163);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 0;
        GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t164);
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
inline __attribute__((aligned(4))) void gm_f_parse_string_o9jl88i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_string_o9jl88i F;
    void * gm_l0; /* p */
    gm_t2lqu8jsj511d gm_l1; /* r */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 442u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_empty_slice_ijtp9lo();
        gm_t1dp1jo1gr2pm t4;
        gm_f_read_string_b2p27kc((void *)&t4, t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_tekvgkj2hms37 t5 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t4) + 8));
            gm_tfe4nnqq2l987 t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_t2lqu8jsj511d t7 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&t4) + 8));
        gm_l1 = t7;
    }
    {
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
        if (!t10) goto ie8;
        {
            void * t12 = gm_l0;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
            if (o15) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 542, 17);
            uint64_t t16 = r14;
            GM_ST(uint64_t, (((uint8_t *)t12) + 16), t16);
        }
        {
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 8, t17);
            goto gm_fdone;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_tabb710d9mq2e t19;
        gm_f_make_2ip78bj((void *)&t19, (void *)NULL, cx, NULL, t18, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kfqhpvvf5lv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_t9qkh0rsooc8d t20 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t19) + 8));
            gm_tekvgkj2hms37 t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)9ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 8), t20);
            gm_tfe4nnqq2l987 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            ((uint8_t *)&t22)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t22 + 8), t21);
            F.ret = t22;
            goto gm_fret;
        }
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_l2 = t23;
    }
    {
        {
            void * t25 = gm_l0;
            gm_b16_8 t26 = gm_l2;
            gm_t1dp1jo1gr2pm t27;
            gm_f_read_string_b2p27kc((void *)&t27, t25, t26);
            if (((((uint8_t *)&t27))[0] != 0)) {
                gm_tekvgkj2hms37 t28 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t27) + 8));
                gm_tfe4nnqq2l987 t29;
                GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                ((uint8_t *)&t29)[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t29 + 8), t28);
                F.ret = t29;
                goto gm_fret;
            }
            gm_t2lqu8jsj511d t30 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&t27) + 8));
        }
    }
    {
        void * t31 = gm_l0;
        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
        if (o34) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 547, 13);
        uint64_t t35 = r33;
        GM_ST(uint64_t, (((uint8_t *)t31) + 16), t35);
    }
    {
        gm_b16_8 t36 = gm_l2;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tfe4nnqq2l987 *)gm_out) + 8, t37);
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
__attribute__((aligned(4))) void gm_f_parse_value_cgemdat(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_parse_value_cgemdat F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 1285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
        uint8_t t7 = (t4 >= t6);
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            gm_tekvgkj2hms37 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t11 + 8), t10);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcge9itd5fie17, 342, 10);
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 1));
        gm_l2 = t17;
    }
    {
        uint8_t t18 = gm_l2;
        gm_te0t4fvim36sr t19;
        uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t22 == ((uint8_t)110ULL))) goto mn21;
        {
            void * t24 = gm_l0;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(4));
            gm_tekh1vgsjf5ak t26;
            gm_f_literal_9ia3g6h((void *)&t26, t24, t25);
            if (((((uint8_t *)&t26))[0] != 0)) {
                gm_tekvgkj2hms37 t27 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t26) + 8));
                gm_te0t4fvim36sr t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t28 + 24, 0);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t28 + 8), t27);
                F.ret = t28;
                goto gm_fret;
            }
            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
        }
        gm_t8vuock3jnv59 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
        gm_t8vuock3jnv59 t31 = t30;
        gm_te0t4fvim36sr t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        ((uint8_t *)&t32)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t32 + 8), t31);
        t19 = t32;
        goto me20;
mn21:;
        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t34 == ((uint8_t)116ULL))) goto mn33;
        {
            void * t36 = gm_l0;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(4));
            gm_tekh1vgsjf5ak t38;
            gm_f_literal_9ia3g6h((void *)&t38, t36, t37);
            if (((((uint8_t *)&t38))[0] != 0)) {
                gm_tekvgkj2hms37 t39 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t38) + 8));
                gm_te0t4fvim36sr t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
                ((uint8_t *)&t40)[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t40 + 8), t39);
                F.ret = t40;
                goto gm_fret;
            }
            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 8));
        }
        gm_t8vuock3jnv59 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t42 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t42 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)1ULL));
        GM_ST(uint8_t, ((uint8_t *)&t42 + 8), ((uint8_t)1));
        gm_t8vuock3jnv59 t43 = t42;
        gm_te0t4fvim36sr t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
        ((uint8_t *)&t44)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t44 + 8), t43);
        t19 = t44;
        goto me20;
mn33:;
        uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t46 == ((uint8_t)102ULL))) goto mn45;
        {
            void * t48 = gm_l0;
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(5));
            gm_tekh1vgsjf5ak t50;
            gm_f_literal_9ia3g6h((void *)&t50, t48, t49);
            if (((((uint8_t *)&t50))[0] != 0)) {
                gm_tekvgkj2hms37 t51 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t50) + 8));
                gm_te0t4fvim36sr t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
                ((uint8_t *)&t52)[0] = 1;
                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t52 + 8), t51);
                F.ret = t52;
                goto gm_fret;
            }
            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 8));
        }
        gm_t8vuock3jnv59 t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t54 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t54 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
        GM_ST(uint8_t, ((uint8_t *)&t54 + 8), ((uint8_t)0));
        gm_t8vuock3jnv59 t55 = t54;
        gm_te0t4fvim36sr t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
        ((uint8_t *)&t56)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t56 + 8), t55);
        t19 = t56;
        goto me20;
mn45:;
        uint8_t t58 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t58 == ((uint8_t)34ULL))) goto mn57;
        void * t59 = gm_l0;
        gm_tfe4nnqq2l987 t60;
        gm_f_parse_string_o9jl88i((void *)&t60, (void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t60))[0] != 0)) {
            gm_tekvgkj2hms37 t61 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t60) + 8));
            gm_te0t4fvim36sr t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t62 + 24, 0);
            ((uint8_t *)&t62)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t62 + 8), t61);
            F.ret = t62;
            goto gm_fret;
        }
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
        gm_t8vuock3jnv59 t64;
        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)3ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 8), t63);
        gm_te0t4fvim36sr t65;
        GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
        ((uint8_t *)&t65)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t65 + 8), t64);
        t19 = t65;
        goto me20;
mn57:;
        uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t67 == ((uint8_t)91ULL))) goto mn66;
        void * t68 = gm_l0;
        uint64_t t69 = gm_l1;
        gm_te0t4fvim36sr t70;
        gm_f_parse_array_54l90b7((void *)&t70, (void *)NULL, cx, NULL, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t70))[0] != 0)) {
            gm_tekvgkj2hms37 t71 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t70) + 8));
            gm_te0t4fvim36sr t72;
            GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t72 + 24, 0);
            ((uint8_t *)&t72)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t72 + 8), t71);
            F.ret = t72;
            goto gm_fret;
        }
        gm_te0t4fvim36sr t73;
        GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
        ((uint8_t *)&t73)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t73 + 8), (*(gm_t8vuock3jnv59 *)(((uint8_t *)&t70) + 8)));
        t19 = t73;
        goto me20;
mn66:;
        uint8_t t75 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t75 == ((uint8_t)123ULL))) goto mn74;
        void * t76 = gm_l0;
        uint64_t t77 = gm_l1;
        gm_te0t4fvim36sr t78;
        gm_f_parse_object_4os6ms3((void *)&t78, (void *)NULL, cx, NULL, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t78))[0] != 0)) {
            gm_tekvgkj2hms37 t79 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t78) + 8));
            gm_te0t4fvim36sr t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t80 + 24, 0);
            ((uint8_t *)&t80)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t80 + 8), t79);
            F.ret = t80;
            goto gm_fret;
        }
        gm_te0t4fvim36sr t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
        ((uint8_t *)&t81)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t81 + 8), (*(gm_t8vuock3jnv59 *)(((uint8_t *)&t78) + 8)));
        t19 = t81;
        goto me20;
mn74:;
        uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t85 == ((uint8_t)45ULL))) goto pa84;
        goto po83;
pa84:;
        uint8_t t87 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if ((t87 < ((uint8_t)48ULL)) || (t87 > ((uint8_t)57ULL))) goto pa86;
        goto po83;
pa86:;
        goto mn82;
po83:;
        void * t88 = gm_l0;
        gm_t24hlg9v4qo9a t89;
        gm_f_parse_number_buivaka((void *)&t89, (void *)NULL, cx, NULL, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t89))[0] != 0)) {
            gm_tekvgkj2hms37 t90 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t89) + 8));
            gm_te0t4fvim36sr t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t91 + 24, 0);
            ((uint8_t *)&t91)[0] = 1;
            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t91 + 8), t90);
            F.ret = t91;
            goto gm_fret;
        }
        gm_tnpifv20irnph t92 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t89) + 8));
        gm_t8vuock3jnv59 t93;
        GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t93 + 8), t92);
        gm_te0t4fvim36sr t94;
        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
        ((uint8_t *)&t94)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t94 + 8), t93);
        t19 = t94;
        goto me20;
mn82:;
        void * t96 = gm_l0;
        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)t96) + 16));
        gm_tekvgkj2hms37 t98;
        GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)1ULL));
        GM_ST(uint64_t, ((uint8_t *)&t98 + 8), t97);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out))[0] = 1;
        GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_te0t4fvim36sr *)gm_out) + 8, t98);
        goto gm_fdone;
        goto me20;
mn95:;
        gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 343, 12);
me20:;
        uint8_t t99 = ((((uint8_t *)&t19))[0] != 0);
        F.ret = t19;
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
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_position_f7cepnk(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_position_f7cepnk F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* offset */
    uint64_t gm_l2; /* end */
    uint64_t gm_l3; /* line */
    uint64_t gm_l4; /* start */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 300u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t4 = (t2 < t3 ? t2 : t3);
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)1ULL);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t6 = gm_l2;
            gm_l5 = t6;
        }
        {
            for (;;) {
                uint64_t t8 = gm_l6;
                uint64_t t9 = gm_l5;
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l6;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l7 = t16;
                    {
                        uint64_t t19 = gm_l7;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcge9itd5fie17, 113, 12);
                        uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t20 * 1));
                        uint8_t t23 = ((t22 == ((uint8_t)10ULL)));
                        if (!t23) goto ie18;
                        {
                            uint64_t t25 = gm_l3;
                            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                            if (o27) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 114, 13);
                            uint64_t t28 = r26;
                            gm_l3 = t28;
                        }
                        {
                            uint64_t t29 = gm_l7;
                            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                            if (o31) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 115, 21);
                            uint64_t t32 = r30;
                            gm_l4 = t32;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 112, 5);
me14:;
                }
lc7:;
                {
                    uint64_t t34 = gm_l6;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l6 = t37;
                }
            }
lb7:;
        }
    }
    {
        uint64_t t38 = gm_l3;
        uint64_t t39 = gm_l2;
        uint64_t t40 = gm_l4;
        uint64_t r41; bool o42 = __builtin_sub_overflow(t39, t40, &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 118, 41);
        uint64_t t43 = r41;
        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
        if (o45) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 118, 41);
        uint64_t t46 = r44;
        gm_t2lqu8jsj511d t47;
        GM_ST(uint64_t, ((uint8_t *)&t47 + 0), t38);
        GM_ST(uint64_t, ((uint8_t *)&t47 + 8), t46);
        uint8_t t48 = 0;
        F.ret = t47;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) void gm_f_read_string_b2p27kc(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_string_b2p27kc F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* buf */
    gm_b16_8 gm_l2; /* t */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* n */
    uint8_t gm_l5; /* c */
    uint8_t gm_l6; /* e */
    uint8_t gm_l7; /* single */
    uint32_t gm_l8; /* cp */
    uint64_t gm_l9; /* used */
    uint32_t gm_l10; /* lo */
    uint64_t gm_l11; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 1289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 480, 10);
        uint64_t t8 = r6;
        gm_l3 = t8;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb9;
            {
                uint64_t t13 = gm_l3;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t15 = (t13 >= t14);
                if (!t15) goto ie12;
                {
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tekvgkj2hms37 t18;
                    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t18 + 8), t17);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t18);
                    goto gm_fdone;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                uint64_t t19 = gm_l3;
                uint64_t t20 = (uintptr_t)t19;
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcge9itd5fie17, 484, 14);
                uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 1));
                gm_l5 = t22;
            }
            {
                uint8_t t24 = gm_l5;
                uint8_t t25 = ((t24 == ((uint8_t)34ULL)));
                if (!t25) goto ie23;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t t28 = gm_l4;
                    gm_t2lqu8jsj511d t29;
                    GM_ST(uint64_t, ((uint8_t *)&t29 + 0), t27);
                    GM_ST(uint64_t, ((uint8_t *)&t29 + 8), t28);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 0;
                    GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t29);
                    goto gm_fdone;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                uint8_t t31 = gm_l5;
                uint8_t t32 = (t31 < ((uint8_t)32ULL));
                if (!t32) goto ie30;
                {
                    uint64_t t34 = gm_l3;
                    gm_tekvgkj2hms37 t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)5ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t35 + 8), t34);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                    GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t35);
                    goto gm_fdone;
                }
                goto ix30;
ie30:;
ix30:;
            }
            {
                uint8_t t37 = gm_l5;
                uint8_t t38 = ((t37 == ((uint8_t)92ULL)));
                if (!t38) goto ie36;
                {
                    uint64_t t41 = gm_l3;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    if (o43) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 488, 16);
                    uint64_t t44 = r42;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t46 = (t44 >= t45);
                    if (!t46) goto ie40;
                    {
                        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        gm_tekvgkj2hms37 t49;
                        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t49 + 8), t48);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                        GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t49);
                        goto gm_fdone;
                    }
                    goto ix40;
ie40:;
ix40:;
                }
                {
                    uint64_t t50 = gm_l3;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    if (o52) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 489, 20);
                    uint64_t t53 = r51;
                    uint64_t t54 = (uintptr_t)t53;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcge9itd5fie17, 489, 18);
                    uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t54 * 1));
                    gm_l6 = t56;
                }
                {
                    uint8_t t57 = gm_l6;
                    uint8_t t58;
                    uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t61 == ((uint8_t)34ULL))) goto mn60;
                    t58 = ((uint8_t)34ULL);
                    goto me59;
mn60:;
                    uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t63 == ((uint8_t)92ULL))) goto mn62;
                    t58 = ((uint8_t)92ULL);
                    goto me59;
mn62:;
                    uint8_t t65 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t65 == ((uint8_t)47ULL))) goto mn64;
                    t58 = ((uint8_t)47ULL);
                    goto me59;
mn64:;
                    uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t67 == ((uint8_t)98ULL))) goto mn66;
                    t58 = ((uint8_t)8ULL);
                    goto me59;
mn66:;
                    uint8_t t69 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t69 == ((uint8_t)102ULL))) goto mn68;
                    t58 = ((uint8_t)12ULL);
                    goto me59;
mn68:;
                    uint8_t t71 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t71 == ((uint8_t)110ULL))) goto mn70;
                    t58 = ((uint8_t)10ULL);
                    goto me59;
mn70:;
                    uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t73 == ((uint8_t)114ULL))) goto mn72;
                    t58 = ((uint8_t)13ULL);
                    goto me59;
mn72:;
                    uint8_t t75 = GM_LD(uint8_t, ((uint8_t *)&t57));
                    if (!(t75 == ((uint8_t)116ULL))) goto mn74;
                    t58 = ((uint8_t)9ULL);
                    goto me59;
mn74:;
                    t58 = ((uint8_t)0ULL);
                    goto me59;
mn76:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 490, 26);
me59:;
                    gm_l7 = t58;
                }
                {
                    uint8_t t78 = gm_l7;
                    uint8_t t79 = (!(t78 == ((uint8_t)0ULL)));
                    if (!t79) goto ie77;
                    {
                        uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        uint8_t t83 = (t82 > ((uint64_t)0ULL));
                        if (!t83) goto ie81;
                        {
                            uint64_t t85 = gm_l4;
                            uint64_t t86 = (uintptr_t)t85;
                            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcge9itd5fie17, 502, 34);
                            uint8_t t88 = gm_l7;
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1), t88);
                        }
                        goto ix81;
ie81:;
ix81:;
                    }
                    {
                        uint64_t t89 = gm_l4;
                        uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                        if (o91) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 503, 17);
                        uint64_t t92 = r90;
                        gm_l4 = t92;
                    }
                    {
                        uint64_t t93 = gm_l3;
                        uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)2ULL), &r94);
                        if (o95) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 504, 17);
                        uint64_t t96 = r94;
                        gm_l3 = t96;
                    }
                    goto ix77;
ie77:;
                    uint8_t t98 = gm_l6;
                    uint8_t t99 = ((t98 == ((uint8_t)117ULL)));
                    if (!t99) goto ie97;
                    {
                        void * t101 = gm_l0;
                        uint64_t t102 = gm_l3;
                        uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)2ULL), &r103);
                        if (o104) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 506, 35);
                        uint64_t t105 = r103;
                        gm_t9mtkhurfjls7 t106;
                        gm_f_hex4_23k9kdb((void *)&t106, t101, t105);
                        if (((((uint8_t *)&t106))[0] != 0)) {
                            gm_tekvgkj2hms37 t107 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t106) + 8));
                            gm_t1dp1jo1gr2pm t108;
                            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                            ((uint8_t *)&t108)[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t108 + 8), t107);
                            F.ret = t108;
                            goto gm_fret;
                        }
                        uint32_t t109 = GM_LD(uint32_t, (((uint8_t *)&t106) + 8));
                        gm_l8 = t109;
                    }
                    {
                        gm_l9 = ((uint64_t)6ULL);
                    }
                    {
                        uint32_t t111 = gm_l8;
                        uint8_t t112 = (t111 >= ((uint32_t)55296ULL));
                        if (!t112) goto ie110;
                        uint32_t t113 = gm_l8;
                        uint8_t t114 = (t113 <= ((uint32_t)56319ULL));
                        if (!t114) goto ie110;
                        {
                            uint8_t t117 = 0;
                            uint8_t t118 = 0;
                            uint64_t t119 = gm_l3;
                            uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)12ULL), &r120);
                            if (o121) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 510, 24);
                            uint64_t t122 = r120;
                            uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            uint8_t t124 = (t122 > t123);
                            if (!t124) {
                                uint64_t t125 = gm_l3;
                                uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)6ULL), &r126);
                                if (o127) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 510, 44);
                                uint64_t t128 = r126;
                                uint64_t t129 = (uintptr_t)t128;
                                uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t129 >= (uintptr_t)(t130)) gm_trap_index(t129, t130, gm_srcge9itd5fie17, 510, 42);
                                uint8_t t131 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t129 * 1));
                                uint8_t t132 = (!(t131 == ((uint8_t)92ULL)));
                                t118 = t132;
                            } else t118 = 1;
                            if (!t118) {
                                uint64_t t133 = gm_l3;
                                uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)7ULL), &r134);
                                if (o135) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 510, 68);
                                uint64_t t136 = r134;
                                uint64_t t137 = (uintptr_t)t136;
                                uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_srcge9itd5fie17, 510, 66);
                                uint8_t t139 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t137 * 1));
                                uint8_t t140 = (!(t139 == ((uint8_t)117ULL)));
                                t117 = t140;
                            } else t117 = 1;
                            if (!t117) goto ie116;
                            {
                                uint64_t t142 = gm_l3;
                                gm_tekvgkj2hms37 t143;
                                GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)4ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t143 + 8), t142);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t143);
                                goto gm_fdone;
                            }
                            goto ix116;
ie116:;
ix116:;
                        }
                        {
                            void * t144 = gm_l0;
                            uint64_t t145 = gm_l3;
                            uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)8ULL), &r146);
                            if (o147) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 513, 39);
                            uint64_t t148 = r146;
                            gm_t9mtkhurfjls7 t149;
                            gm_f_hex4_23k9kdb((void *)&t149, t144, t148);
                            if (((((uint8_t *)&t149))[0] != 0)) {
                                gm_tekvgkj2hms37 t150 = GM_LD(gm_tekvgkj2hms37, (((uint8_t *)&t149) + 8));
                                gm_t1dp1jo1gr2pm t151;
                                GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                                ((uint8_t *)&t151)[0] = 1;
                                GM_ST(gm_tekvgkj2hms37, ((uint8_t *)&t151 + 8), t150);
                                F.ret = t151;
                                goto gm_fret;
                            }
                            uint32_t t152 = GM_LD(uint32_t, (((uint8_t *)&t149) + 8));
                            gm_l10 = t152;
                        }
                        {
                            uint8_t t154 = 0;
                            uint32_t t155 = gm_l10;
                            uint8_t t156 = (t155 < ((uint32_t)56320ULL));
                            if (!t156) {
                                uint32_t t157 = gm_l10;
                                uint8_t t158 = (t157 > ((uint32_t)57343ULL));
                                t154 = t158;
                            } else t154 = 1;
                            if (!t154) goto ie153;
                            {
                                uint64_t t160 = gm_l3;
                                gm_tekvgkj2hms37 t161;
                                GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)4ULL));
                                GM_ST(uint64_t, ((uint8_t *)&t161 + 8), t160);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                                GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t161);
                                goto gm_fdone;
                            }
                            goto ix153;
ie153:;
ix153:;
                        }
                        {
                            uint32_t t162 = gm_l8;
                            uint32_t r163; bool o164 = __builtin_sub_overflow(t162, ((uint32_t)55296ULL), &r163);
                            if (o164) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 515, 38);
                            uint32_t t165 = r163;
                            unsigned __int128 n166 = (unsigned __int128)((uint64_t)10ULL);
                            if (n166 >= 32) gm_trap_shift((uint64_t)n166, "u32", gm_srcge9itd5fie17, 515, 37);
                            uint32_t r167 = (uint32_t)((uint32_t)t165 << (int)n166);
                            bool o168 = (uint32_t)(r167 >> (int)n166) != t165;
                            if (o168) gm_trap("integer overflow in `<<`", gm_srcge9itd5fie17, 515, 37);
                            uint32_t t169 = r167;
                            uint32_t r170; bool o171 = __builtin_add_overflow(((uint32_t)65536ULL), t169, &r170);
                            if (o171) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 515, 26);
                            uint32_t t172 = r170;
                            uint32_t t173 = gm_l10;
                            uint32_t r174; bool o175 = __builtin_sub_overflow(t173, ((uint32_t)56320ULL), &r174);
                            if (o175) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 515, 61);
                            uint32_t t176 = r174;
                            uint32_t r177; bool o178 = __builtin_add_overflow(t172, t176, &r177);
                            if (o178) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 515, 26);
                            uint32_t t179 = r177;
                            gm_l8 = t179;
                        }
                        {
                            gm_l9 = ((uint64_t)12ULL);
                        }
                        goto ix110;
ie110:;
                        uint32_t t181 = gm_l8;
                        uint8_t t182 = (t181 >= ((uint32_t)56320ULL));
                        if (!t182) goto ie180;
                        uint32_t t183 = gm_l8;
                        uint8_t t184 = (t183 <= ((uint32_t)57343ULL));
                        if (!t184) goto ie180;
                        {
                            uint64_t t186 = gm_l3;
                            gm_tekvgkj2hms37 t187;
                            GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)4ULL));
                            GM_ST(uint64_t, ((uint8_t *)&t187 + 8), t186);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                            GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t187);
                            goto gm_fdone;
                        }
                        goto ix180;
ie180:;
ix180:;
ix110:;
                    }
                    {
                        uint64_t t188 = gm_l4;
                        uint32_t t189 = gm_l8;
                        gm_b16_8 t190 = gm_l1;
                        uint64_t t191 = gm_l4;
                        uint64_t t192 = gm_f_encode_utf8_s8ajso1(t189, t190, t191);
                        uint64_t r193; bool o194 = __builtin_add_overflow(t188, t192, &r193);
                        if (o194) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 520, 17);
                        uint64_t t195 = r193;
                        gm_l4 = t195;
                    }
                    {
                        uint64_t t196 = gm_l3;
                        uint64_t t197 = gm_l9;
                        uint64_t r198; bool o199 = __builtin_add_overflow(t196, t197, &r198);
                        if (o199) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 521, 17);
                        uint64_t t200 = r198;
                        gm_l3 = t200;
                    }
                    goto ix97;
ie97:;
                    {
                        uint64_t t202 = gm_l3;
                        uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                        if (o204) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 523, 40);
                        uint64_t t205 = r203;
                        gm_tekvgkj2hms37 t206;
                        GM_ST(uint8_t, (uint8_t *)&t206 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)3ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t206 + 8), t205);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                        GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t206);
                        goto gm_fdone;
                    }
ix97:;
ix77:;
                }
                goto ix36;
ie36:;
                uint8_t t208 = gm_l5;
                uint8_t t209 = (t208 < ((uint8_t)128ULL));
                if (!t209) goto ie207;
                {
                    uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t213 = (t212 > ((uint64_t)0ULL));
                    if (!t213) goto ie211;
                    {
                        uint64_t t215 = gm_l4;
                        uint64_t t216 = (uintptr_t)t215;
                        uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t216 >= (uintptr_t)(t217)) gm_trap_index(t216, t217, gm_srcge9itd5fie17, 526, 30);
                        uint8_t t218 = gm_l5;
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t216 * 1), t218);
                    }
                    goto ix211;
ie211:;
ix211:;
                }
                {
                    uint64_t t219 = gm_l4;
                    uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                    if (o221) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 527, 13);
                    uint64_t t222 = r220;
                    gm_l4 = t222;
                }
                {
                    uint64_t t223 = gm_l3;
                    uint64_t r224; bool o225 = __builtin_add_overflow(t223, ((uint64_t)1ULL), &r224);
                    if (o225) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 528, 13);
                    uint64_t t226 = r224;
                    gm_l3 = t226;
                }
                goto ix207;
ie207:;
                {
                    gm_b16_8 t228 = gm_l2;
                    uint64_t t229 = gm_l3;
                    uint64_t t230 = gm_f_utf8_len_u8bogbn(t228, t229);
                    gm_l11 = t230;
                }
                {
                    uint64_t t232 = gm_l11;
                    uint8_t t233 = ((t232 == ((uint64_t)0ULL)));
                    if (!t233) goto ie231;
                    {
                        uint64_t t235 = gm_l3;
                        gm_tekvgkj2hms37 t236;
                        GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t236 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t236 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)6ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t236 + 8), t235);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out))[0] = 1;
                        GM_ST(gm_tekvgkj2hms37, (uint8_t *)&(*(gm_t1dp1jo1gr2pm *)gm_out) + 8, t236);
                        goto gm_fdone;
                    }
                    goto ix231;
ie231:;
ix231:;
                }
                {
                    uint64_t t238 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t239 = (t238 > ((uint64_t)0ULL));
                    if (!t239) goto ie237;
                    {
                        uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        void * t242 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                        uint64_t t243 = gm_l4;
                        uint64_t t244 = (uintptr_t)t243;
                        uint64_t t245 = gm_l4;
                        uint64_t t246 = gm_l11;
                        uint64_t r247; bool o248 = __builtin_add_overflow(t245, t246, &r247);
                        if (o248) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 532, 37);
                        uint64_t t249 = r247;
                        uint64_t t250 = (uintptr_t)t249;
                        if (t244 > t250 || t250 > (uintptr_t)t241) gm_trap_slice(t244, t250, t241, gm_srcge9itd5fie17, 532, 30);
                        gm_b16_8 t251;
                        GM_ST(void *, (uint8_t *)&t251, (void *)(((uint8_t *)t242 + t244 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(t250 - t244));
                        uint64_t t252 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        void * t253 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                        uint64_t t254 = gm_l3;
                        uint64_t t255 = (uintptr_t)t254;
                        uint64_t t256 = gm_l3;
                        uint64_t t257 = gm_l11;
                        uint64_t r258; bool o259 = __builtin_add_overflow(t256, t257, &r258);
                        if (o259) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 532, 59);
                        uint64_t t260 = r258;
                        uint64_t t261 = (uintptr_t)t260;
                        if (t255 > t261 || t261 > (uintptr_t)t252) gm_trap_slice(t255, t261, t252, gm_srcge9itd5fie17, 532, 54);
                        gm_b16_8 t262;
                        GM_ST(void *, (uint8_t *)&t262, (void *)(((uint8_t *)t253 + t255 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(t261 - t255));
                        gm_f_copy_from_f7j6p05(t251, t262);
                    }
                    goto ix237;
ie237:;
ix237:;
                }
                {
                    uint64_t t263 = gm_l4;
                    uint64_t t264 = gm_l11;
                    uint64_t r265; bool o266 = __builtin_add_overflow(t263, t264, &r265);
                    if (o266) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 533, 13);
                    uint64_t t267 = r265;
                    gm_l4 = t267;
                }
                {
                    uint64_t t268 = gm_l3;
                    uint64_t t269 = gm_l11;
                    uint64_t r270; bool o271 = __builtin_add_overflow(t268, t269, &r270);
                    if (o271) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 534, 13);
                    uint64_t t272 = r270;
                    gm_l3 = t272;
                }
ix207:;
ix36:;
            }
lc9:;
        }
lb9:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1dp1jo1gr2pm gm_f_read_string_b2p27kc_c(void * a0, gm_b16_8 a1) { gm_t1dp1jo1gr2pm gm_r; gm_f_read_string_b2p27kc(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_skip_ws_g70e352(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_ws_g70e352 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 16));
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
            uint8_t t7 = (t4 < t6);
            if (!t7) goto lb2;
            {
                void * t10 = gm_l0;
                uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 16));
                uint64_t t12 = (uintptr_t)t11;
                void * t13 = gm_l0;
                uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)t13) + 0) + 8);
                if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcge9itd5fie17, 312, 14);
                uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 0)) + t12 * 1));
                gm_l1 = t15;
            }
            {
                uint8_t t17 = 0;
                uint8_t t18 = 0;
                uint8_t t19 = 0;
                uint8_t t20 = gm_l1;
                uint8_t t21 = ((t20 == ((uint8_t)32ULL)));
                if (!t21) {
                    uint8_t t22 = gm_l1;
                    uint8_t t23 = ((t22 == ((uint8_t)9ULL)));
                    t19 = t23;
                } else t19 = 1;
                if (!t19) {
                    uint8_t t24 = gm_l1;
                    uint8_t t25 = ((t24 == ((uint8_t)10ULL)));
                    t18 = t25;
                } else t18 = 1;
                if (!t18) {
                    uint8_t t26 = gm_l1;
                    uint8_t t27 = ((t26 == ((uint8_t)13ULL)));
                    t17 = t27;
                } else t17 = 1;
                if (!t17) goto ie16;
                {
                    void * t29 = gm_l0;
                    void * t30 = gm_l0;
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 16));
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 313, 62);
                    uint64_t t34 = r32;
                    GM_ST(uint64_t, (((uint8_t *)t29) + 16), t34);
                }
                goto ix16;
ie16:;
                {
                    gm_b0_1 t36;
                    uint8_t t37 = 0;
                    F.ret = t36;
                    goto gm_fret;
                }
ix16:;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_split_25tpoqf(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_25tpoqf F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* neg */
    uint64_t gm_l3; /* int_start */
    uint64_t gm_l4; /* int_end */
    uint64_t gm_l5; /* frac_start */
    uint64_t gm_l6; /* frac_end */
    int64_t gm_l7; /* exp */
    uint8_t gm_l8; /* eneg */
    GM_STACK_CHECK_SHALLOW(&F, 636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srcge9itd5fie17, 175, 8);
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t3 * 1));
        uint8_t t6 = ((t5 == ((uint8_t)45ULL)));
        if (!t6) goto ie2;
        {
            gm_l2 = ((uint8_t)1);
        }
        {
            gm_l1 = ((uint64_t)1ULL);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        gm_l3 = t8;
    }
    {
        for (;;) {
            uint64_t t10 = gm_l1;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t12 = (t10 < t11);
            if (!t12) goto lb9;
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcge9itd5fie17, 180, 25);
            uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
            uint8_t t17 = (t16 >= ((uint8_t)48ULL));
            if (!t17) goto lb9;
            uint64_t t18 = gm_l1;
            uint64_t t19 = (uintptr_t)t18;
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcge9itd5fie17, 180, 41);
            uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t19 * 1));
            uint8_t t22 = (t21 <= ((uint8_t)57ULL));
            if (!t22) goto lb9;
            {
                uint64_t t25 = gm_l1;
                uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                if (o27) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 180, 55);
                uint64_t t28 = r26;
                gm_l1 = t28;
            }
lc9:;
        }
lb9:;
    }
    {
        uint64_t t29 = gm_l1;
        gm_l4 = t29;
    }
    {
        uint64_t t30 = gm_l1;
        gm_l5 = t30;
    }
    {
        uint64_t t31 = gm_l1;
        gm_l6 = t31;
    }
    {
        uint64_t t33 = gm_l1;
        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t35 = (t33 < t34);
        if (!t35) goto ie32;
        uint64_t t36 = gm_l1;
        uint64_t t37 = (uintptr_t)t36;
        uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srcge9itd5fie17, 184, 22);
        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t37 * 1));
        uint8_t t40 = ((t39 == ((uint8_t)46ULL)));
        if (!t40) goto ie32;
        {
            uint64_t t42 = gm_l1;
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 185, 9);
            uint64_t t45 = r43;
            gm_l1 = t45;
        }
        {
            uint64_t t46 = gm_l1;
            gm_l5 = t46;
        }
        {
            for (;;) {
                uint64_t t48 = gm_l1;
                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t50 = (t48 < t49);
                if (!t50) goto lb47;
                uint64_t t51 = gm_l1;
                uint64_t t52 = (uintptr_t)t51;
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srcge9itd5fie17, 187, 29);
                uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t52 * 1));
                uint8_t t55 = (t54 >= ((uint8_t)48ULL));
                if (!t55) goto lb47;
                uint64_t t56 = gm_l1;
                uint64_t t57 = (uintptr_t)t56;
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_srcge9itd5fie17, 187, 45);
                uint8_t t59 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t57 * 1));
                uint8_t t60 = (t59 <= ((uint8_t)57ULL));
                if (!t60) goto lb47;
                {
                    uint64_t t63 = gm_l1;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                    if (o65) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 187, 59);
                    uint64_t t66 = r64;
                    gm_l1 = t66;
                }
lc47:;
            }
lb47:;
        }
        {
            uint64_t t67 = gm_l1;
            gm_l6 = t67;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        gm_l7 = ((int64_t)0LL);
    }
    {
        uint64_t t69 = gm_l1;
        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t71 = (t69 < t70);
        if (!t71) goto ie68;
        {
            uint64_t t73 = gm_l1;
            uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
            if (o75) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 193, 9);
            uint64_t t76 = r74;
            gm_l1 = t76;
        }
        {
            gm_l8 = ((uint8_t)0);
        }
        {
            uint64_t t78 = gm_l1;
            uint64_t t79 = (uintptr_t)t78;
            uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_srcge9itd5fie17, 195, 12);
            uint8_t t81 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t79 * 1));
            uint8_t t82 = ((t81 == ((uint8_t)45ULL)));
            if (!t82) goto ie77;
            {
                gm_l8 = ((uint8_t)1);
            }
            {
                uint64_t t84 = gm_l1;
                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                if (o86) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 197, 13);
                uint64_t t87 = r85;
                gm_l1 = t87;
            }
            goto ix77;
ie77:;
            uint64_t t89 = gm_l1;
            uint64_t t90 = (uintptr_t)t89;
            uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t90 >= (uintptr_t)(t91)) gm_trap_index(t90, t91, gm_srcge9itd5fie17, 198, 19);
            uint8_t t92 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t90 * 1));
            uint8_t t93 = ((t92 == ((uint8_t)43ULL)));
            if (!t93) goto ie88;
            {
                uint64_t t95 = gm_l1;
                uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                if (o97) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 199, 13);
                uint64_t t98 = r96;
                gm_l1 = t98;
            }
            goto ix88;
ie88:;
ix88:;
ix77:;
        }
        {
            for (;;) {
                uint64_t t100 = gm_l1;
                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                uint8_t t102 = (t100 < t101);
                if (!t102) goto lb99;
                {
                    int64_t t106 = gm_l7;
                    uint8_t t107 = (t106 < ((int64_t)1000000000LL));
                    if (!t107) goto ie105;
                    {
                        int64_t t109 = gm_l7;
                        int64_t r110; bool o111 = __builtin_mul_overflow(t109, ((int64_t)10LL), &r110);
                        if (o111) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 202, 44);
                        int64_t t112 = r110;
                        uint64_t t113 = gm_l1;
                        uint64_t t114 = (uintptr_t)t113;
                        uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_srcge9itd5fie17, 202, 56);
                        uint8_t t116 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t114 * 1));
                        uint8_t r117; bool o118 = __builtin_sub_overflow(t116, ((uint8_t)48ULL), &r117);
                        if (o118) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 202, 56);
                        uint8_t t119 = r117;
                        int64_t t120 = (int64_t)t119;
                        int64_t r121; bool o122 = __builtin_add_overflow(t112, t120, &r121);
                        if (o122) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 202, 44);
                        int64_t t123 = r121;
                        gm_l7 = t123;
                    }
                    goto ix105;
ie105:;
ix105:;
                }
                {
                    uint64_t t124 = gm_l1;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    if (o126) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 203, 13);
                    uint64_t t127 = r125;
                    gm_l1 = t127;
                }
lc99:;
            }
lb99:;
        }
        {
            uint8_t t129 = gm_l8;
            if (!t129) goto ie128;
            {
                int64_t t131 = gm_l7;
                if (t131 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 205, 25);
                int64_t t132 = (int64_t)((uint64_t)0 - (uint64_t)t131);
                gm_l7 = t132;
            }
            goto ix128;
ie128:;
ix128:;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        memset((uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 1, 0, 7);
        uint8_t t133 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 0, t133);
        uint64_t t134 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 8, t134);
        uint64_t t135 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 16, t135);
        uint64_t t136 = gm_l5;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 24, t136);
        uint64_t t137 = gm_l6;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 32, t137);
        int64_t t138 = gm_l7;
        GM_ST(int64_t, (uint8_t *)&(*(gm_t3072hbjtr5t6 *)gm_out) + 40, t138);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csnt0cc832rgst gm_f_split_25tpoqf_c(gm_b16_8 a0) { gm_t3072hbjtr5t6 gm_r; gm_f_split_25tpoqf(&gm_r, a0); return GM_CONV(gm_csnt0cc832rgst, gm_r); }
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_d1vui09(gm_tnpifv20irnph a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_i64_d1vui09 F;
    gm_tnpifv20irnph gm_l0; /* n */
    gm_tcs9tli4q9d6j gm_l2; /* nm */
    int64_t gm_l3; /* frac_len */
    int64_t gm_l4; /* int_len */
    int64_t gm_l5; /* total */
    int64_t gm_l6; /* e */
    unsigned __int128 gm_l7; /* v */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 927u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
        gm_f_split_25tpoqf((void *)&F.l1, t2);
    }
    {
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
        gm_f_normalize_pcair89((void *)&gm_l2, t3, &(*(gm_t3072hbjtr5t6 *)((uint8_t *)&F.l1)));
    }
    {
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_t913bb735pvt5 t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t8), ((int64_t)0LL));
            ((uint8_t *)&t8)[8] = 1;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 32));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 24));
        uint64_t r12; bool o13 = __builtin_sub_overflow(t10, t11, &r12);
        if (o13) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 246, 18);
        uint64_t t14 = r12;
        int64_t t15 = (int64_t)t14;
        gm_l3 = t15;
    }
    {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 16));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
        uint64_t r18; bool o19 = __builtin_sub_overflow(t16, t17, &r18);
        if (o19) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 247, 17);
        uint64_t t20 = r18;
        int64_t t21 = (int64_t)t20;
        gm_l4 = t21;
    }
    {
        int64_t t22 = gm_l4;
        int64_t t23 = gm_l3;
        int64_t r24; bool o25 = __builtin_add_overflow(t22, t23, &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 248, 14);
        int64_t t26 = r24;
        gm_l5 = t26;
    }
    {
        int64_t t27 = GM_LD(int64_t, (((uint8_t *)&F.l1) + 40));
        int64_t t28 = gm_l3;
        int64_t r29; bool o30 = __builtin_sub_overflow(t27, t28, &r29);
        if (o30) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 249, 10);
        int64_t t31 = r29;
        int64_t t32 = gm_l5;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        int64_t t34 = (int64_t)t33;
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        int64_t t36 = (int64_t)t35;
        int64_t r37; bool o38 = __builtin_add_overflow(t34, t36, &r37);
        if (o38) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 249, 39);
        int64_t t39 = r37;
        int64_t r40; bool o41 = __builtin_sub_overflow(t32, t39, &r40);
        if (o41) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 249, 30);
        int64_t t42 = r40;
        int64_t r43; bool o44 = __builtin_add_overflow(t31, t42, &r43);
        if (o44) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 249, 10);
        int64_t t45 = r43;
        gm_l6 = t45;
    }
    {
        uint8_t t47 = 0;
        uint8_t t48 = 0;
        int64_t t49 = gm_l6;
        uint8_t t50 = (t49 < ((int64_t)0LL));
        if (!t50) {
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t52 = (t51 > ((uint64_t)19ULL));
            t48 = t52;
        } else t48 = 1;
        if (!t48) {
            int64_t t53 = gm_l6;
            uint8_t t54 = (t53 > ((int64_t)19LL));
            t47 = t54;
        } else t47 = 1;
        if (!t47) goto ie46;
        {
            gm_t913bb735pvt5 t56;
            memset(&t56, 0, sizeof t56);
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        gm_l7 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            gm_l8 = t59;
        }
        {
            for (;;) {
                uint64_t t61 = gm_l9;
                uint64_t t62 = gm_l8;
                uint8_t t63 = (t61 < t62);
                if (!t63) goto lb60;
                {
                    uint64_t t66 = gm_l9;
                    uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)&t66));
                    gm_l10 = t69;
                    {
                        unsigned __int128 t71 = gm_l7;
                        unsigned __int128 r72; bool o73 = __builtin_mul_overflow(t71, ((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)), &r72);
                        if (o73) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 252, 32);
                        unsigned __int128 t74 = r72;
                        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
                        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
                        uint64_t t77 = gm_l10;
                        uint64_t r78; bool o79 = __builtin_add_overflow(t76, t77, &r78);
                        if (o79) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 252, 62);
                        uint64_t t80 = r78;
                        uint8_t t81 = gm_f_digit_at_1oh5ota(t75, &(*(gm_t3072hbjtr5t6 *)((uint8_t *)&F.l1)), t80);
                        uint8_t r82; bool o83 = __builtin_sub_overflow(t81, ((uint8_t)48ULL), &r82);
                        if (o83) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 252, 42);
                        uint8_t t84 = r82;
                        unsigned __int128 t85 = (unsigned __int128)t84;
                        unsigned __int128 r86; bool o87 = __builtin_add_overflow(t74, t85, &r86);
                        if (o87) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 252, 32);
                        unsigned __int128 t88 = r86;
                        gm_l7 = t88;
                    }
                    goto me67;
mn68:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 252, 5);
me67:;
                }
lc60:;
                {
                    uint64_t t90 = gm_l9;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l9 = t93;
                }
            }
lb60:;
        }
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            int64_t t95 = gm_l6;
            uint64_t t96 = (uint64_t)t95;
            gm_l11 = t96;
        }
        {
            for (;;) {
                uint64_t t98 = gm_l12;
                uint64_t t99 = gm_l11;
                uint8_t t100 = (t98 < t99);
                if (!t100) goto lb97;
                {
                    uint64_t t103 = gm_l12;
                    {
                        unsigned __int128 t107 = gm_l7;
                        unsigned __int128 r108; bool o109 = __builtin_mul_overflow(t107, ((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)), &r108);
                        if (o109) gm_trap("integer overflow in `*`", gm_srcge9itd5fie17, 254, 13);
                        unsigned __int128 t110 = r108;
                        gm_l7 = t110;
                    }
                    {
                        unsigned __int128 t112 = gm_l7;
                        uint8_t t113 = (t112 > ((unsigned __int128)(((unsigned __int128)1u << 64) | 0u)));
                        if (!t113) goto ie111;
                        {
                            gm_t913bb735pvt5 t115;
                            memset(&t115, 0, sizeof t115);
                            uint8_t t116 = 0;
                            F.ret = t115;
                            goto gm_fret;
                        }
                        goto ix111;
ie111:;
ix111:;
                    }
                    goto me104;
mn105:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 253, 5);
me104:;
                }
lc97:;
                {
                    uint64_t t118 = gm_l12;
                    uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                    uint64_t t121 = r119;
                    gm_l12 = t121;
                }
            }
lb97:;
        }
    }
    {
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (!t123) goto ie122;
        {
            unsigned __int128 t126 = gm_l7;
            uint8_t t127 = (t126 > ((unsigned __int128)(((unsigned __int128)0u << 64) | 9223372036854775808u)));
            if (!t127) goto ie125;
            {
                gm_t913bb735pvt5 t129;
                memset(&t129, 0, sizeof t129);
                uint8_t t130 = 0;
                F.ret = t129;
                goto gm_fret;
            }
            goto ix125;
ie125:;
ix125:;
        }
        {
            unsigned __int128 t132 = gm_l7;
            uint8_t t133 = ((t132 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 9223372036854775808u))));
            if (!t133) goto ie131;
            {
                int64_t r135; bool o136 = __builtin_sub_overflow(((int64_t)-9223372036854775807LL), ((int64_t)1LL), &r135);
                if (o136) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 259, 46);
                int64_t t137 = r135;
                gm_t913bb735pvt5 t138;
                GM_ST(uint8_t, (uint8_t *)&t138 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t138 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t138 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t138), t137);
                ((uint8_t *)&t138)[8] = 1;
                uint8_t t139 = 0;
                F.ret = t138;
                goto gm_fret;
            }
            goto ix131;
ie131:;
ix131:;
        }
        {
            unsigned __int128 t140 = gm_l7;
            int64_t t141 = (int64_t)t140;
            if (t141 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcge9itd5fie17, 260, 16);
            int64_t t142 = (int64_t)((uint64_t)0 - (uint64_t)t141);
            gm_t913bb735pvt5 t143;
            GM_ST(uint8_t, (uint8_t *)&t143 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t143 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t143 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t143), t142);
            ((uint8_t *)&t143)[8] = 1;
            uint8_t t144 = 0;
            F.ret = t143;
            goto gm_fret;
        }
        goto ix122;
ie122:;
ix122:;
    }
    {
        unsigned __int128 t146 = gm_l7;
        uint8_t t147 = (t146 > ((unsigned __int128)(((unsigned __int128)0u << 64) | 9223372036854775807u)));
        if (!t147) goto ie145;
        {
            gm_t913bb735pvt5 t149;
            memset(&t149, 0, sizeof t149);
            uint8_t t150 = 0;
            F.ret = t149;
            goto gm_fret;
        }
        goto ix145;
ie145:;
ix145:;
    }
    {
        unsigned __int128 t151 = gm_l7;
        int64_t t152 = (int64_t)t151;
        gm_t913bb735pvt5 t153;
        GM_ST(uint8_t, (uint8_t *)&t153 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t153 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t153 + 12, 0);
        GM_ST(int64_t, ((uint8_t *)&t153), t152);
        ((uint8_t *)&t153)[8] = 1;
        uint8_t t154 = 0;
        F.ret = t153;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_utf8_len_u8bogbn(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_utf8_len_u8bogbn F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* c */
    uint64_t gm_l3; /* n */
    uint8_t gm_l4; /* lo */
    uint8_t gm_l5; /* hi */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srcge9itd5fie17, 426, 10);
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t3 * 1));
        gm_l2 = t5;
    }
    {
        uint8_t t7 = gm_l2;
        uint8_t t8 = (t7 < ((uint8_t)128ULL));
        if (!t8) goto ie6;
        {
            uint8_t t10 = 0;
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        gm_l4 = ((uint8_t)128ULL);
    }
    {
        gm_l5 = ((uint8_t)191ULL);
    }
    {
        uint8_t t12 = gm_l2;
        uint8_t t13 = (t12 >= ((uint8_t)194ULL));
        if (!t13) goto ie11;
        uint8_t t14 = gm_l2;
        uint8_t t15 = (t14 <= ((uint8_t)223ULL));
        if (!t15) goto ie11;
        {
            gm_l3 = ((uint64_t)2ULL);
        }
        goto ix11;
ie11:;
        uint8_t t18 = gm_l2;
        uint8_t t19 = (t18 >= ((uint8_t)224ULL));
        if (!t19) goto ie17;
        uint8_t t20 = gm_l2;
        uint8_t t21 = (t20 <= ((uint8_t)239ULL));
        if (!t21) goto ie17;
        {
            gm_l3 = ((uint64_t)3ULL);
        }
        {
            uint8_t t24 = gm_l2;
            uint8_t t25 = ((t24 == ((uint8_t)224ULL)));
            if (!t25) goto ie23;
            {
                gm_l4 = ((uint8_t)160ULL);
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            uint8_t t28 = gm_l2;
            uint8_t t29 = ((t28 == ((uint8_t)237ULL)));
            if (!t29) goto ie27;
            {
                gm_l5 = ((uint8_t)159ULL);
            }
            goto ix27;
ie27:;
ix27:;
        }
        goto ix17;
ie17:;
        uint8_t t32 = gm_l2;
        uint8_t t33 = (t32 >= ((uint8_t)240ULL));
        if (!t33) goto ie31;
        uint8_t t34 = gm_l2;
        uint8_t t35 = (t34 <= ((uint8_t)244ULL));
        if (!t35) goto ie31;
        {
            gm_l3 = ((uint64_t)4ULL);
        }
        {
            uint8_t t38 = gm_l2;
            uint8_t t39 = ((t38 == ((uint8_t)240ULL)));
            if (!t39) goto ie37;
            {
                gm_l4 = ((uint8_t)144ULL);
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            uint8_t t42 = gm_l2;
            uint8_t t43 = ((t42 == ((uint8_t)244ULL)));
            if (!t43) goto ie41;
            {
                gm_l5 = ((uint8_t)143ULL);
            }
            goto ix41;
ie41:;
ix41:;
        }
        goto ix31;
ie31:;
        {
            uint8_t t46 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
ix31:;
ix17:;
ix11:;
    }
    {
        uint64_t t48 = gm_l1;
        uint64_t t49 = gm_l3;
        uint64_t r50; bool o51 = __builtin_add_overflow(t48, t49, &r50);
        if (o51) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 444, 8);
        uint64_t t52 = r50;
        uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t54 = (t52 > t53);
        if (!t54) goto ie47;
        {
            uint8_t t56 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        uint8_t t58 = 0;
        uint64_t t59 = gm_l1;
        uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 445, 10);
        uint64_t t62 = r60;
        uint64_t t63 = (uintptr_t)t62;
        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t63 >= (uintptr_t)(t64)) gm_trap_index(t63, t64, gm_srcge9itd5fie17, 445, 8);
        uint8_t t65 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t63 * 1));
        uint8_t t66 = gm_l4;
        uint8_t t67 = (t65 < t66);
        if (!t67) {
            uint64_t t68 = gm_l1;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            if (o70) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 445, 27);
            uint64_t t71 = r69;
            uint64_t t72 = (uintptr_t)t71;
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcge9itd5fie17, 445, 25);
            uint8_t t74 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t72 * 1));
            uint8_t t75 = gm_l5;
            uint8_t t76 = (t74 > t75);
            t58 = t76;
        } else t58 = 1;
        if (!t58) goto ie57;
        {
            uint8_t t78 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        {
            gm_l7 = ((uint64_t)2ULL);
        }
        {
            uint64_t t80 = gm_l3;
            gm_l6 = t80;
        }
        {
            for (;;) {
                uint64_t t82 = gm_l7;
                uint64_t t83 = gm_l6;
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l7;
                    uint64_t t90 = GM_LD(uint64_t, ((uint8_t *)&t87));
                    gm_l8 = t90;
                    {
                        uint8_t t93 = 0;
                        uint64_t t94 = gm_l1;
                        uint64_t t95 = gm_l8;
                        uint64_t r96; bool o97 = __builtin_add_overflow(t94, t95, &r96);
                        if (o97) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 447, 14);
                        uint64_t t98 = r96;
                        uint64_t t99 = (uintptr_t)t98;
                        uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_srcge9itd5fie17, 447, 12);
                        uint8_t t101 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t99 * 1));
                        uint8_t t102 = (t101 < ((uint8_t)128ULL));
                        if (!t102) {
                            uint64_t t103 = gm_l1;
                            uint64_t t104 = gm_l8;
                            uint64_t r105; bool o106 = __builtin_add_overflow(t103, t104, &r105);
                            if (o106) gm_trap("integer overflow in `+`", gm_srcge9itd5fie17, 447, 33);
                            uint64_t t107 = r105;
                            uint64_t t108 = (uintptr_t)t107;
                            uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srcge9itd5fie17, 447, 31);
                            uint8_t t110 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t108 * 1));
                            uint8_t t111 = (t110 > ((uint8_t)191ULL));
                            t93 = t111;
                        } else t93 = 1;
                        if (!t93) goto ie92;
                        {
                            uint8_t t113 = 0;
                            F.ret = ((uint64_t)0ULL);
                            goto gm_fret;
                        }
                        goto ix92;
ie92:;
ix92:;
                    }
                    goto me88;
mn89:;
                    gm_trap("no match arm matched the scrutinee", gm_srcge9itd5fie17, 446, 5);
me88:;
                }
lc81:;
                {
                    uint64_t t115 = gm_l7;
                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                    uint64_t t118 = r116;
                    gm_l7 = t118;
                }
            }
lb81:;
        }
    }
    {
        uint64_t t119 = gm_l3;
        uint8_t t120 = 0;
        F.ret = t119;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

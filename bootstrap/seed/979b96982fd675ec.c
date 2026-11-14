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
typedef struct gm_cs3cuou245tr5f { _Bool f0; _Bool f1; _Bool f2; } gm_cs3cuou245tr5f;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { void * f0; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tiacef8jroje2;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tndsf0a4f4lj0;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v1; } f1; } gm_tt7ek1a8bopcl;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v1; } f1; } gm_ta8qa3lgiolvh;
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_b16_8 f0; gm_tv2uf4leil5hq f1; } gm_taiafe1jasg4m;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiacef8jroje2) == 40 && _Alignof(gm_tiacef8jroje2) == 8 && offsetof(gm_tiacef8jroje2, f0) == 0 && offsetof(gm_tiacef8jroje2, f1) == 8 && offsetof(gm_tiacef8jroje2, f2) == 24, "layout of `MatchExpr`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tndsf0a4f4lj0 v1; } f1; } gm_tjqj6atpbrqje;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tndsf0a4f4lj0) == 48 && _Alignof(gm_tndsf0a4f4lj0) == 8 && offsetof(gm_tndsf0a4f4lj0, f0) == 0 && offsetof(gm_tndsf0a4f4lj0, f1) == 16 && offsetof(gm_tndsf0a4f4lj0, f2) == 24 && offsetof(gm_tndsf0a4f4lj0, f3) == 32, "layout of `Arm`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v1; } f1; } gm_toc4mrkn2e7o9;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tt7ek1a8bopcl) == 24 && _Alignof(gm_tt7ek1a8bopcl) == 8 && offsetof(gm_tt7ek1a8bopcl, f0) == 0 && offsetof(gm_tt7ek1a8bopcl, f1) == 8, "layout of `Abort![]Attribute`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { gm_t1s75i08qtptd f0; gm_b16_8 f1; void * f2; gm_t79rbdv4rafe5 f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_t0l6vh8eupt26;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; } gm_t41cj6gvba3r8;
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { gm_b16_8 f0; gm_tjsp7bhdn2e8j f1; uint8_t p2[5]; void * f2; } gm_t4d1qrgh0vnq1;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
typedef struct { gm_tjsp7bhdn2e8j f0; uint8_t f1; } gm_t6k157sild47t;
_Static_assert(sizeof(gm_ta8qa3lgiolvh) == 72 && _Alignof(gm_ta8qa3lgiolvh) == 8 && offsetof(gm_ta8qa3lgiolvh, f0) == 0 && offsetof(gm_ta8qa3lgiolvh, f1) == 8, "layout of `Abort!Binding`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_taiafe1jasg4m) == 80 && _Alignof(gm_taiafe1jasg4m) == 8 && offsetof(gm_taiafe1jasg4m, f0) == 0 && offsetof(gm_taiafe1jasg4m, f1) == 16, "layout of `(attrs: []Attribute, binding: Binding)`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjqj6atpbrqje) == 56 && _Alignof(gm_tjqj6atpbrqje) == 8 && offsetof(gm_tjqj6atpbrqje, f0) == 0 && offsetof(gm_tjqj6atpbrqje, f1) == 8, "layout of `Abort!Arm`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_toc4mrkn2e7o9) == 48 && _Alignof(gm_toc4mrkn2e7o9) == 8 && offsetof(gm_toc4mrkn2e7o9, f0) == 0 && offsetof(gm_toc4mrkn2e7o9, f1) == 8, "layout of `Abort!Block`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_ttja7ii61nila;
typedef struct { gm_tapdr2ag17fh7 f0; uint8_t f1; uint8_t pe[7]; } gm_ttrm2ubqha4nn;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t0l6vh8eupt26) == 120 && _Alignof(gm_t0l6vh8eupt26) == 8 && offsetof(gm_t0l6vh8eupt26, f0) == 0 && offsetof(gm_t0l6vh8eupt26, f1) == 40 && offsetof(gm_t0l6vh8eupt26, f2) == 56 && offsetof(gm_t0l6vh8eupt26, f3) == 64 && offsetof(gm_t0l6vh8eupt26, f4) == 104, "layout of `ForExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t41cj6gvba3r8) == 96 && _Alignof(gm_t41cj6gvba3r8) == 8 && offsetof(gm_t41cj6gvba3r8, f0) == 0 && offsetof(gm_t41cj6gvba3r8, f1) == 32 && offsetof(gm_t41cj6gvba3r8, f2) == 64, "layout of `(TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t4d1qrgh0vnq1) == 32 && _Alignof(gm_t4d1qrgh0vnq1) == 8 && offsetof(gm_t4d1qrgh0vnq1, f0) == 0 && offsetof(gm_t4d1qrgh0vnq1, f1) == 16 && offsetof(gm_t4d1qrgh0vnq1, f2) == 24, "layout of `(places: []Expr, op: AssignOp, value: ^Expr)`");
_Static_assert(sizeof(gm_t6k157sild47t) == 4 && _Alignof(gm_t6k157sild47t) == 1 && offsetof(gm_t6k157sild47t, f0) == 0 && offsetof(gm_t6k157sild47t, f1) == 3, "layout of `?AssignOp`");
typedef struct { gm_ttja7ii61nila f0; uint8_t f1; uint8_t pe[7]; } gm_t9t5bnrf784qu;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { void * f0; gm_t79rbdv4rafe5 f1; gm_ttrm2ubqha4nn f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tt2g4r14evejs;
_Static_assert(sizeof(gm_ttja7ii61nila) == 48 && _Alignof(gm_ttja7ii61nila) == 8 && offsetof(gm_ttja7ii61nila, f0) == 0 && offsetof(gm_ttja7ii61nila, f1) == 8, "layout of `IfElse`");
_Static_assert(sizeof(gm_ttrm2ubqha4nn) == 56 && _Alignof(gm_ttrm2ubqha4nn) == 8 && offsetof(gm_ttrm2ubqha4nn, f0) == 0 && offsetof(gm_ttrm2ubqha4nn, f1) == 48, "layout of `?WhenElse`");
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; void * f2; } gm_t15nkjiutlvf3;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tt2g4r14evejs v1; } f1; } gm_t5q7g12jsm3oo;
typedef struct { gm_b16_8 f0; gm_t9t5bnrf784qu f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t99g4dkn8rrfb;
_Static_assert(sizeof(gm_t9t5bnrf784qu) == 56 && _Alignof(gm_t9t5bnrf784qu) == 8 && offsetof(gm_t9t5bnrf784qu, f0) == 0 && offsetof(gm_t9t5bnrf784qu, f1) == 48, "layout of `?IfElse`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_t0d02142k4tu8 f0; uint8_t f1; uint8_t pe[7]; } gm_toqf51949ppls;
_Static_assert(sizeof(gm_tt2g4r14evejs) == 120 && _Alignof(gm_tt2g4r14evejs) == 8 && offsetof(gm_tt2g4r14evejs, f0) == 0 && offsetof(gm_tt2g4r14evejs, f1) == 8 && offsetof(gm_tt2g4r14evejs, f2) == 48 && offsetof(gm_tt2g4r14evejs, f3) == 104, "layout of `WhenStmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0d02142k4tu8 v1; } f1; } gm_ttlipffho4ijl;
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
_Static_assert(sizeof(gm_t15nkjiutlvf3) == 80 && _Alignof(gm_t15nkjiutlvf3) == 8 && offsetof(gm_t15nkjiutlvf3, f0) == 0 && offsetof(gm_t15nkjiutlvf3, f1) == 64 && offsetof(gm_t15nkjiutlvf3, f2) == 72, "layout of `(pat: Pattern, value: ^Expr, fallback: ^Expr)`");
_Static_assert(sizeof(gm_t5q7g12jsm3oo) == 128 && _Alignof(gm_t5q7g12jsm3oo) == 8 && offsetof(gm_t5q7g12jsm3oo, f0) == 0 && offsetof(gm_t5q7g12jsm3oo, f1) == 8, "layout of `Abort!WhenStmt`");
_Static_assert(sizeof(gm_t99g4dkn8rrfb) == 88 && _Alignof(gm_t99g4dkn8rrfb) == 8 && offsetof(gm_t99g4dkn8rrfb, f0) == 0 && offsetof(gm_t99g4dkn8rrfb, f1) == 16 && offsetof(gm_t99g4dkn8rrfb, f2) == 72, "layout of `IfExpr`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_taiafe1jasg4m v0; gm_t4d1qrgh0vnq1 v1; void * v2; gm_tgtub0v94d5u5 v3; void * v4; gm_t43d6nbdfap6p v5; void * v6; gm_t15nkjiutlvf3 v7; gm_t79rbdv4rafe5 v8; } f1; } gm_tem8p0t66u1ce;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
_Static_assert(sizeof(gm_toqf51949ppls) == 72 && _Alignof(gm_toqf51949ppls) == 8 && offsetof(gm_toqf51949ppls, f0) == 0 && offsetof(gm_toqf51949ppls, f1) == 64, "layout of `?Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
_Static_assert(sizeof(gm_ttlipffho4ijl) == 72 && _Alignof(gm_ttlipffho4ijl) == 8 && offsetof(gm_ttlipffho4ijl, f0) == 0 && offsetof(gm_ttlipffho4ijl, f1) == 8, "layout of `Abort!Pattern`");
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
typedef struct { gm_tem8p0t66u1ce f0; uint8_t f1; uint8_t pe[7]; } gm_t5821lde3dhck;
_Static_assert(sizeof(gm_tem8p0t66u1ce) == 120 && _Alignof(gm_tem8p0t66u1ce) == 8 && offsetof(gm_tem8p0t66u1ce, f0) == 0 && offsetof(gm_tem8p0t66u1ce, f1) == 8, "layout of `StmtKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tem8p0t66u1ce v1; } f1; } gm_thm5bu4f6031r;
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tem8p0t66u1ce f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to88ae1b67aev;
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tq5j0547p8452 v1; } f1; } gm_tt06c5njno8oo;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t5821lde3dhck) == 128 && _Alignof(gm_t5821lde3dhck) == 8 && offsetof(gm_t5821lde3dhck, f0) == 0 && offsetof(gm_t5821lde3dhck, f1) == 120, "layout of `?StmtKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t5821lde3dhck v1; } f1; } gm_taeav3snkvh6h;
_Static_assert(sizeof(gm_thm5bu4f6031r) == 128 && _Alignof(gm_thm5bu4f6031r) == 8 && offsetof(gm_thm5bu4f6031r, f0) == 0 && offsetof(gm_thm5bu4f6031r, f1) == 8, "layout of `Abort!StmtKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_to88ae1b67aev v1; } f1; } gm_tmgpf942vp8ci;
_Static_assert(sizeof(gm_to88ae1b67aev) == 144 && _Alignof(gm_to88ae1b67aev) == 8 && offsetof(gm_to88ae1b67aev, f0) == 0 && offsetof(gm_to88ae1b67aev, f1) == 8 && offsetof(gm_to88ae1b67aev, f2) == 128, "layout of `Stmt`");
_Static_assert(sizeof(gm_tt06c5njno8oo) == 88 && _Alignof(gm_tt06c5njno8oo) == 8 && offsetof(gm_tt06c5njno8oo, f0) == 0 && offsetof(gm_tt06c5njno8oo, f1) == 8, "layout of `Abort!CondPart`");
_Static_assert(sizeof(gm_taeav3snkvh6h) == 136 && _Alignof(gm_taeav3snkvh6h) == 8 && offsetof(gm_taeav3snkvh6h, f0) == 0 && offsetof(gm_taeav3snkvh6h, f1) == 8, "layout of `Abort!?StmtKind`");
_Static_assert(sizeof(gm_tmgpf942vp8ci) == 152 && _Alignof(gm_tmgpf942vp8ci) == 8 && offsetof(gm_tmgpf942vp8ci, f0) == 0 && offsetof(gm_tmgpf942vp8ci, f1) == 8, "layout of `Abort!Stmt`");
struct gm_fr_assign_op_0s1u8tb {
    gm_frame h;
    gm_t6k157sild47t ret;
};
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_kw_p0r3sr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_word_18dnh5f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binding_ahead_cjedtav {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_eat_kw_tvv0v3e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_3v2gjd4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_65q32rn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7t4k5u0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8ckfo47 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ojer7i1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_expr_dhdi86k {
    gm_frame h;
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3cgmi89 {
    gm_frame h;
    gm_t0d02142k4tu8 l0; /* pat */
    gm_ttlipffho4ijl ret;
};
struct gm_fr_fn_lr1jr0i {
    gm_frame h;
    gm_to88ae1b67aev l1; /* s */
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_fn_q64l5t2 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_fn_tc4108s {
    gm_frame h;
    gm_t0d02142k4tu8 l0; /* pat */
    gm_ttlipffho4ijl ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_58nr42v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fvvkq82 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jfj8aoh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mho6jfm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_taqq58r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lo_fp82gt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_mk_expr_53qj7nd {
    gm_frame h;
    gm_t2gtbh39ptekd l1; /* k */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_next_id_a7upaf4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_nth_c0r2nvs {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_nth_is_3fgqmcb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_nth_is_kw_4i9ruj5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_parse_arm_h86all4 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* pats */
    gm_tc3oht4ksth9d l4; /* body */
    gm_tjqj6atpbrqje ret;
};
struct gm_fr_parse_arms_24fl9da {
    gm_frame h;
    void * l0; /* p */
    gm_trqfvmjtt4sij l1; /* arms */
    gm_b16_8 ret;
};
struct gm_fr_parse_binding_ojaae7v {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* names */
    gm_ta8qa3lgiolvh ret;
};
struct gm_fr_parse_block_body_1juv37t {
    gm_frame h;
    void * l0; /* p */
    gm_trqfvmjtt4sij l1; /* stmts */
    gm_b16_8 ret;
};
struct gm_fr_parse_block_tpg3f1i {
    gm_frame h;
    gm_toc4mrkn2e7o9 ret;
};
struct gm_fr_parse_capture_body_fgf89k2 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_cond_6sk4htm {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_tq5j0547p8452 l5; /* part */
    gm_t4jn8fl3lmk5k ret;
};
struct gm_fr_parse_cond_part_5vg3hu1 {
    gm_frame h;
    void * l0; /* p */
    gm_toqf51949ppls l1; /* bind */
    gm_t0d02142k4tu8 l2; /* pat */
    gm_tt06c5njno8oo ret;
};
struct gm_fr_parse_cond_parts_ohqkl5k {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_parse_defer_body_log60it {
    gm_frame h;
    gm_tem8p0t66u1ce l2; /* k */
    gm_tmgpf942vp8ci ret;
};
struct gm_fr_parse_expr_or_assign_btc70ah {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* places */
    gm_thm5bu4f6031r ret;
};
struct gm_fr_parse_for_q8vk3da {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_trqfvmjtt4sij l4; /* pats */
    gm_t79rbdv4rafe5 l6; /* body */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_if_shktfou {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* branches */
    gm_t9t5bnrf784qu l4; /* else_ */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_labeled_block_s1cpmi0 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_toc4mrkn2e7o9 ret;
};
struct gm_fr_parse_let_else_e2n01r7 {
    gm_frame h;
    void * l0; /* p */
    gm_t0d02142k4tu8 l1; /* pat */
    gm_tc3oht4ksth9d l2; /* value */
    gm_tioi3jh9v22g5 l3; /* e */
    gm_taeav3snkvh6h ret;
};
struct gm_fr_parse_match_1lcgsrh {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_name_stmt_nb6mtgu {
    gm_frame h;
    gm_tem8p0t66u1ce l2; /* k */
    gm_thm5bu4f6031r ret;
};
struct gm_fr_parse_rhs_9susfpi {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* first */
    gm_trqfvmjtt4sij l3; /* elems */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_stmt_40aalpm {
    gm_frame h;
    gm_tem8p0t66u1ce l2; /* k */
    gm_tmgpf942vp8ci ret;
};
struct gm_fr_parse_stmt_kind_tji0utd {
    gm_frame h;
    gm_to88ae1b67aev l3; /* body */
    gm_tem8p0t66u1ce l4; /* k */
    gm_thm5bu4f6031r ret;
};
struct gm_fr_parse_when_stmt_a9ra0gh {
    gm_frame h;
    gm_t79rbdv4rafe5 l3; /* then */
    gm_ttrm2ubqha4nn l4; /* else_ */
    gm_t5q7g12jsm3oo ret;
};
struct gm_fr_parse_while_gvp1mcm {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_t79rbdv4rafe5 l3; /* body */
    gm_thm5bu4f6031r ret;
};
struct gm_fr_parse_with_hlmj8pt {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_trqfvmjtt4sij l4; /* handlers */
    gm_t79rbdv4rafe5 l6; /* body */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_reject_assign_after_cond_utj4j69 {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_report_djvi2hf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_t6k157sild47t gm_f_assign_op_0s1u8tb(const void * a0);
__attribute__((aligned(4))) gm_t6k157sild47t gm_f_assign_op_0s1u8tb_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_binding_ahead_cjedtav(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_binds_ahead_e3p74o2(void * a0);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_kw_tvv0v3e(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7t4k5u0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ojer7i1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj(void);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_kw_7kbmp0i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expect_terminator_f7kp9sk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_fn_3cgmi89(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_lr1jr0i(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_q64l5t2(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_fn_tc4108s(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jfj8aoh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_taqq58r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_members_oq7gs8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_closure a2);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_0oj2e9g(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_5bcttcf(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_7hdc0s2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_lrokjci(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_qlsqbft(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ulodmu9(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_kw_4i9ruj5(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_arm_h86all4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_arms_24fl9da(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_attrs_imvfdlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_binding_ojaae7v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_block_body_1juv37t(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_tpg3f1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cond_6sk4htm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cond_part_5vg3hu1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_parse_cond_parts_ohqkl5k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_parse_defer_body_log60it(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_or_assign_btc70ah(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_for_q8vk3da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_if_shktfou(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_labeled_block_s1cpmi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_let_else_e2n01r7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_match_1lcgsrh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_name_stmt_nb6mtgu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_pattern_mggn9o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_rhs_9susfpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_stmt_40aalpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_stmt_kind_tji0utd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_when_stmt_a9ra0gh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_while_gvp1mcm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_with_hlmj8pt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0ie7e7i(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nnonkr8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5rtia7(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_recover_to_stmt_end_freq9f3(void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_reject_assign_after_cond_utj4j69(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_speculate_j6q99ri(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_k0d09vor0rn[40] __attribute__((aligned(8)));
extern uint8_t gm_k3u24nfalvr[40] __attribute__((aligned(8)));
extern uint8_t gm_k52ekgkvp6f[40] __attribute__((aligned(8)));
extern uint8_t gm_k9o9iqv20c0[40] __attribute__((aligned(8)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern uint8_t gm_kefd26qglb8[40] __attribute__((aligned(8)));
extern uint8_t gm_ki0a6rpk4hh[40] __attribute__((aligned(8)));
extern uint8_t gm_kli6567itlv[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s2fp7iapio2lo[11];
extern const uint8_t gm_s5dpb04jti9nb[17];
extern const uint8_t gm_s688e8m4pgcda[6];
extern const uint8_t gm_s7rk1k9jrjscv[81];
extern const uint8_t gm_s8m5q4i8uoe5j[6];
extern const uint8_t gm_s9akpbuv78p3m[109];
extern const uint8_t gm_scnb11g2ghrd0[35];
extern const uint8_t gm_sd48v0fh5kl37[24];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_shs874j97ior1[14];
extern const uint8_t gm_sitbu884cbr46[96];
extern const uint8_t gm_sj486prdcn2go[14];
extern const uint8_t gm_sk2sae8qmnmbb[31];
extern const uint8_t gm_sl8fmedd9kk64[34];
extern const uint8_t gm_snemlnnsbdb13[46];
extern const uint8_t gm_so7em1q8clnib[8];
extern const uint8_t gm_sp69n8gsguhfh[77];
extern const uint8_t gm_sp796mgrgp4mj[45];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcp33bek143l58[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_ssce05hnjd595[32];
extern const uint8_t gm_stna0ubmss8p6[48];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_54906rm F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    gm_t9qkh0rsooc8d t5 = t4;
    uint8_t t6 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t3) + 8), ((uint8_t *)&t5))));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_kw_p0r3sr9 F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    gm_t9qkh0rsooc8d t5 = t4;
    uint8_t t6 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t3) + 8), ((uint8_t *)&t5))));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_word_18dnh5f F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* word */
    gm_b16_8 gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_nth_c0r2nvs((void *)&t2, t1, ((uint64_t)0ULL));
    gm_t3fcbuehh3bs2 t3 = t2;
    uint8_t t4;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t7 != ((uint8_t)0ULL)) goto mn6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
    gm_l2 = t8;
    gm_b16_8 t9 = gm_l2;
    gm_b16_8 t10 = gm_l1;
    gm_b16_8 t11 = t9;
    gm_b16_8 t12 = t10;
    uint8_t t13 = (gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
    t4 = t13;
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 170, 58);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_box_j4urmed F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpnk3k16op)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t2 = t1;
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcuiab49hlgv7g, 452, 52);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eat_kw_tvv0v3e(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_kw_tvv0v3e F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_kw_p0r3sr9(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t10 = gm_l0;
            gm_tm4pc0emhrv1g t11;
            gm_f_bump_q2i40re((void *)&t11, t10);
        }
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_qmock9g F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t10 = gm_l0;
            gm_tm4pc0emhrv1g t11;
            gm_f_bump_q2i40re((void *)&t11, t10);
        }
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_65q32rn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7t4k5u0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7t4k5u0 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8ckfo47 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_expr_dhdi86k F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t1 = gm_l0;
    gm_t2gtbh39ptekd t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)50ULL));
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)gm_out), t1, &t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_fail_e5kqpus F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_t4gut2c9fj1kt t4;
    gm_f_error_s6hn871((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t5 = gm_f_fail_diag_u0q4kp3(t1, &t4);
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
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_k239kpg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_t3fcbuehh3bs2 t3 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)t2) + 0));
    F.ret = t3;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_rb20u32 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 132));
        uint32_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 434, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 132), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_58nr42v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4();
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
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fvvkq82 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8ckfo47();
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
inline __attribute__((aligned(4))) void gm_f_list_jfj8aoh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jfj8aoh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_7t4k5u0();
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
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mho6jfm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_65q32rn();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb();
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
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo_fp82gt4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t1));
    gm_tl874t2atnlh3 t3 = t2;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&t3) + 4));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mk_expr_53qj7nd F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
    void * t1 = gm_l0;
    uint32_t t2 = gm_f_next_id_a7upaf4(t1);
    gm_t2gtbh39ptekd t3 = F.l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, t2);
    GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, t3);
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_id_a7upaf4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 56));
        uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)t3));
        gm_l1 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 56));
            void * t8 = (void *)((uint8_t *)t7);
            gm_l2 = t8;
        }
        {
            void * t9 = gm_l2;
            void * t10 = gm_l2;
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)t10));
            uint32_t r12; bool o13 = __builtin_add_overflow(t11, ((uint32_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 443, 5);
            uint32_t t14 = r12;
            GM_ST(uint32_t, ((uint8_t *)t9), t14);
        }
    }
    {
        uint32_t t15 = gm_l1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_c0r2nvs F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    uint64_t t6 = gm_l1;
    uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 126, 70);
    uint64_t t9 = r7;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 8) + 8);
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 126, 85);
    uint64_t t14 = r12;
    uint64_t t15 = (t9 < t14 ? t9 : t14);
    uint64_t t16 = (uintptr_t)t15;
    gm_b16_8 t17 = t3;
    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
    if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcuiab49hlgv7g, 126, 62);
    gm_t3fcbuehh3bs2 t19 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t17)) + t16 * 48) + 0));
    F.ret = t19;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_3fgqmcb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_t3fcbuehh3bs2 t3;
    gm_f_nth_c0r2nvs((void *)&t3, t1, t2);
    gm_t3fcbuehh3bs2 t4 = t3;
    gm_t9qkh0rsooc8d t5 = gm_l2;
    gm_t9qkh0rsooc8d t6 = t5;
    uint8_t t7 = (((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t4) + 8), ((uint8_t *)&t6))));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_kw_4i9ruj5(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_kw_4i9ruj5 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_t3fcbuehh3bs2 t3;
    gm_f_nth_c0r2nvs((void *)&t3, t1, t2);
    gm_t3fcbuehh3bs2 t4 = t3;
    gm_t9qkh0rsooc8d t5 = gm_l2;
    gm_t9qkh0rsooc8d t6 = t5;
    uint8_t t7 = (((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t4) + 8), ((uint8_t *)&t6))));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_arms_24fl9da(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_arms_24fl9da F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_58nr42v((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = F.l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8m5q4i8uoe5j));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_closure t4 = ((gm_closure){(void *)gm_f_fn_q64l5t2, (void *)&F.h, cx});
        gm_b0_1 t5 = gm_f_members_oq7gs8i((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t7 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_block_body_1juv37t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_block_body_1juv37t F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_ir0h3tc((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = F.l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s688e8m4pgcda));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_closure t4 = ((gm_closure){(void *)gm_f_fn_lr1jr0i, (void *)&F.h, cx});
        gm_b0_1 t5 = gm_f_members_oq7gs8i((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t7 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_capture_body_fgf89k2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)65ULL));
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_f_parse_block_expr_0j6b78q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t7);
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
        void * t8 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
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
            gm_th2lqbo77pvsc t8;
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
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_b16_8 t29 = gm_l2;
        gm_th2lqbo77pvsc t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_djvi2hf F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 253, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 136), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 64));
        gm_tl874t2atnlh3 t10 = gm_l1;
        gm_b16_8 t11 = gm_l2;
        gm_b0_1 t12 = gm_f_report_eardf07((void *)NULL, cx, NULL, t9, t10, t11);
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
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_restr_p85qqmv F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK_SHALLOW(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_toh0ootodhjue, a1);
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)t2) + 128));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_toh0ootodhjue t5 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t4) + 128), t5);
    }
    {
        gm_toh0ootodhjue t6 = gm_l2;
        uint8_t t7 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
gm_fprop:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_bm4ll8d F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 32));
    F.ret = t3;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_from_1sqap0o F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_l0;
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 80));
    uint32_t t6 = gm_l1;
    uint32_t t7 = (t5 > t6 ? t5 : t6);
    gm_tl874t2atnlh3 t8;
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 8), t7);
    F.ret = t8;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tok_ai6sc12 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
    uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
    if (o9) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 122, 73);
    uint64_t t10 = r8;
    uint64_t t11 = (t5 < t10 ? t5 : t10);
    uint64_t t12 = (uintptr_t)t11;
    gm_b16_8 t13 = t3;
    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
    if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcuiab49hlgv7g, 122, 56);
    void * t15 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t12 * 48);
    F.ret = t15;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t6k157sild47t gm_f_assign_op_0s1u8tb(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_assign_op_0s1u8tb F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t6k157sild47t t1;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; default: goto ma22; }
ma3:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t26 != ((uint8_t)8ULL)) goto mn25;
    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t27 != ((uint8_t)22ULL)) goto mn25;
    gm_tjsp7bhdn2e8j t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
    GM_ST(uint8_t, (uint8_t *)&t28 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
    gm_t6k157sild47t t29;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t29), t28);
    ((uint8_t *)&t29)[3] = 1;
    t1 = t29;
    goto me2;
mn25:;
ma4:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t31 != ((uint8_t)8ULL)) goto mn30;
    uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t32 != ((uint8_t)23ULL)) goto mn30;
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 1), t33);
    gm_tjsp7bhdn2e8j t35;
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 1), t34);
    gm_t6k157sild47t t36;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t36), t35);
    ((uint8_t *)&t36)[3] = 1;
    t1 = t36;
    goto me2;
mn30:;
ma5:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t38 != ((uint8_t)8ULL)) goto mn37;
    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t39 != ((uint8_t)24ULL)) goto mn37;
    gm_t9qkh0rsooc8d t40;
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 1), t40);
    gm_tjsp7bhdn2e8j t42;
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t42 + 1), t41);
    gm_t6k157sild47t t43;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t43), t42);
    ((uint8_t *)&t43)[3] = 1;
    t1 = t43;
    goto me2;
mn37:;
ma6:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t45 != ((uint8_t)8ULL)) goto mn44;
    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t46 != ((uint8_t)25ULL)) goto mn44;
    gm_t9qkh0rsooc8d t47;
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t48;
    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 1), t47);
    gm_tjsp7bhdn2e8j t49;
    GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t49 + 1), t48);
    gm_t6k157sild47t t50;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t50), t49);
    ((uint8_t *)&t50)[3] = 1;
    t1 = t50;
    goto me2;
mn44:;
ma7:;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t52 != ((uint8_t)8ULL)) goto mn51;
    uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t53 != ((uint8_t)26ULL)) goto mn51;
    gm_ts6oqppjh0ba3 t54;
    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)4ULL));
    gm_tjsp7bhdn2e8j t55;
    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t55 + 1), t54);
    gm_t6k157sild47t t56;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t56), t55);
    ((uint8_t *)&t56)[3] = 1;
    t1 = t56;
    goto me2;
mn51:;
ma8:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t58 != ((uint8_t)8ULL)) goto mn57;
    uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t59 != ((uint8_t)27ULL)) goto mn57;
    gm_ts6oqppjh0ba3 t60;
    GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)5ULL));
    gm_tjsp7bhdn2e8j t61;
    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t61 + 1), t60);
    gm_t6k157sild47t t62;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t62), t61);
    ((uint8_t *)&t62)[3] = 1;
    t1 = t62;
    goto me2;
mn57:;
ma9:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t64 != ((uint8_t)8ULL)) goto mn63;
    uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t65 != ((uint8_t)28ULL)) goto mn63;
    gm_t9qkh0rsooc8d t66;
    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t67;
    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t67 + 1), t66);
    gm_tjsp7bhdn2e8j t68;
    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t68 + 1), t67);
    gm_t6k157sild47t t69;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t69), t68);
    ((uint8_t *)&t69)[3] = 1;
    t1 = t69;
    goto me2;
mn63:;
ma10:;
    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t71 != ((uint8_t)8ULL)) goto mn70;
    uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t72 != ((uint8_t)29ULL)) goto mn70;
    gm_ts6oqppjh0ba3 t73;
    GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)6ULL));
    gm_tjsp7bhdn2e8j t74;
    GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t74 + 1), t73);
    gm_t6k157sild47t t75;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t75), t74);
    ((uint8_t *)&t75)[3] = 1;
    t1 = t75;
    goto me2;
mn70:;
ma11:;
    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t77 != ((uint8_t)8ULL)) goto mn76;
    uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t78 != ((uint8_t)30ULL)) goto mn76;
    gm_ts6oqppjh0ba3 t79;
    GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)7ULL));
    gm_tjsp7bhdn2e8j t80;
    GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t80 + 1), t79);
    gm_t6k157sild47t t81;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t81), t80);
    ((uint8_t *)&t81)[3] = 1;
    t1 = t81;
    goto me2;
mn76:;
ma12:;
    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t83 != ((uint8_t)8ULL)) goto mn82;
    uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t84 != ((uint8_t)31ULL)) goto mn82;
    gm_ts6oqppjh0ba3 t85;
    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)8ULL));
    gm_tjsp7bhdn2e8j t86;
    GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t86 + 1), t85);
    gm_t6k157sild47t t87;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t87), t86);
    ((uint8_t *)&t87)[3] = 1;
    t1 = t87;
    goto me2;
mn82:;
ma13:;
    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t89 != ((uint8_t)8ULL)) goto mn88;
    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t90 != ((uint8_t)32ULL)) goto mn88;
    gm_ts6oqppjh0ba3 t91;
    GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)9ULL));
    gm_tjsp7bhdn2e8j t92;
    GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t92 + 1), t91);
    gm_t6k157sild47t t93;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t93), t92);
    ((uint8_t *)&t93)[3] = 1;
    t1 = t93;
    goto me2;
mn88:;
ma14:;
    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t95 != ((uint8_t)8ULL)) goto mn94;
    uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t96 != ((uint8_t)33ULL)) goto mn94;
    gm_t9qkh0rsooc8d t97;
    GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t98;
    GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 1), t97);
    gm_tjsp7bhdn2e8j t99;
    GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t99 + 1), t98);
    gm_t6k157sild47t t100;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t100), t99);
    ((uint8_t *)&t100)[3] = 1;
    t1 = t100;
    goto me2;
mn94:;
ma15:;
    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t102 != ((uint8_t)8ULL)) goto mn101;
    uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t103 != ((uint8_t)34ULL)) goto mn101;
    gm_t9qkh0rsooc8d t104;
    GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t105;
    GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t105 + 1), t104);
    gm_tjsp7bhdn2e8j t106;
    GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t106 + 1), t105);
    gm_t6k157sild47t t107;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t107), t106);
    ((uint8_t *)&t107)[3] = 1;
    t1 = t107;
    goto me2;
mn101:;
ma16:;
    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t109 != ((uint8_t)8ULL)) goto mn108;
    uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t110 != ((uint8_t)35ULL)) goto mn108;
    gm_t9qkh0rsooc8d t111;
    GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t112;
    GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t112 + 1), t111);
    gm_tjsp7bhdn2e8j t113;
    GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t113 + 1), t112);
    gm_t6k157sild47t t114;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t114), t113);
    ((uint8_t *)&t114)[3] = 1;
    t1 = t114;
    goto me2;
mn108:;
ma17:;
    uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t116 != ((uint8_t)8ULL)) goto mn115;
    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t117 != ((uint8_t)36ULL)) goto mn115;
    gm_t9qkh0rsooc8d t118;
    GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t119;
    GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t119 + 1), t118);
    gm_tjsp7bhdn2e8j t120;
    GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t120 + 1), t119);
    gm_t6k157sild47t t121;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t121), t120);
    ((uint8_t *)&t121)[3] = 1;
    t1 = t121;
    goto me2;
mn115:;
ma18:;
    uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t123 != ((uint8_t)8ULL)) goto mn122;
    uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t124 != ((uint8_t)37ULL)) goto mn122;
    gm_t9qkh0rsooc8d t125;
    GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t126;
    GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t126 + 1), t125);
    gm_tjsp7bhdn2e8j t127;
    GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t127 + 1), t126);
    gm_t6k157sild47t t128;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t128), t127);
    ((uint8_t *)&t128)[3] = 1;
    t1 = t128;
    goto me2;
mn122:;
ma19:;
    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t130 != ((uint8_t)8ULL)) goto mn129;
    uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t131 != ((uint8_t)38ULL)) goto mn129;
    gm_t9qkh0rsooc8d t132;
    GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t133;
    GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t133 + 1), t132);
    gm_tjsp7bhdn2e8j t134;
    GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t134 + 1), t133);
    gm_t6k157sild47t t135;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t135), t134);
    ((uint8_t *)&t135)[3] = 1;
    t1 = t135;
    goto me2;
mn129:;
ma20:;
    uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t137 != ((uint8_t)8ULL)) goto mn136;
    uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t138 != ((uint8_t)39ULL)) goto mn136;
    gm_t9qkh0rsooc8d t139;
    GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t140;
    GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t140 + 1), t139);
    gm_tjsp7bhdn2e8j t141;
    GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t141 + 1), t140);
    gm_t6k157sild47t t142;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t142), t141);
    ((uint8_t *)&t142)[3] = 1;
    t1 = t142;
    goto me2;
mn136:;
ma21:;
    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t144 != ((uint8_t)8ULL)) goto mn143;
    uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t145 != ((uint8_t)40ULL)) goto mn143;
    gm_t9qkh0rsooc8d t146;
    GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t147;
    GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t147 + 1), t146);
    gm_tjsp7bhdn2e8j t148;
    GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)1ULL));
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t148 + 1), t147);
    gm_t6k157sild47t t149;
    GM_ST(gm_tjsp7bhdn2e8j, ((uint8_t *)&t149), t148);
    ((uint8_t *)&t149)[3] = 1;
    t1 = t149;
    goto me2;
mn143:;
ma22:;
    gm_t6k157sild47t t151;
    memset(&t151, 0, sizeof t151);
    t1 = t151;
    goto me2;
mn150:;
ma23:;
    gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 4, 58);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t6k157sild47t gm_f_assign_op_0s1u8tb_c(gm_t3fcbuehh3bs2 a0) { return gm_f_assign_op_0s1u8tb(&a0); }
__attribute__((aligned(4))) uint8_t gm_f_binding_ahead_cjedtav(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binding_ahead_cjedtav F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 866u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_nth_is_name_oqhoobd(t3, ((uint64_t)0ULL));
        uint8_t t5 = (uint8_t)!t4;
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
        void * t9 = gm_l0;
        gm_t3fcbuehh3bs2 t10;
        gm_f_nth_c0r2nvs((void *)&t10, t9, ((uint64_t)1ULL));
        gm_t41cj6gvba3r8 t11;
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)54ULL));
        gm_t3fcbuehh3bs2 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 0, t13);
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)55ULL));
        gm_t3fcbuehh3bs2 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 8), t14);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 32, t15);
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)56ULL));
        gm_t3fcbuehh3bs2 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 8), t16);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 64, t17);
        gm_t41cj6gvba3r8 t18 = t11;
        uint8_t t19 = 0;
        gm_t3fcbuehh3bs2 t20 = t10;
        if (gm_eqt7ftn1841u(((uint8_t *)&t18) + 0, ((uint8_t *)&t20))) t19 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t18) + 32, ((uint8_t *)&t20))) t19 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t18) + 64, ((uint8_t *)&t20))) t19 = 1;
        uint8_t t21 = (uint8_t)(t19);
        if (!t21) goto ie8;
        {
            uint8_t t23 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t25 = gm_l0;
        gm_t9qkh0rsooc8d t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)57ULL));
        uint8_t t27 = gm_f_nth_is_3fgqmcb(t25, ((uint64_t)1ULL), t26);
        uint8_t t28 = (uint8_t)!t27;
        if (!t28) goto ie24;
        {
            uint8_t t30 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            void * t32 = gm_l0;
            uint64_t t33 = gm_l1;
            uint8_t t34 = gm_f_nth_is_name_oqhoobd(t32, t33);
            if (!t34) goto lb31;
            void * t35 = gm_l0;
            uint64_t t36 = gm_l1;
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_srcp33bek143l58, 138, 41);
            uint64_t t39 = r37;
            gm_t9qkh0rsooc8d t40;
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)57ULL));
            uint8_t t41 = gm_f_nth_is_3fgqmcb(t35, t39, t40);
            if (!t41) goto lb31;
            {
                uint64_t t44 = gm_l1;
                uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)2ULL), &r45);
                if (o46) gm_trap("integer overflow in `+`", gm_srcp33bek143l58, 138, 58);
                uint64_t t47 = r45;
                gm_l1 = t47;
            }
lc31:;
        }
lb31:;
    }
    {
        uint8_t t48 = 0;
        void * t49 = gm_l0;
        uint64_t t50 = gm_l1;
        uint8_t t51 = gm_f_nth_is_name_oqhoobd(t49, t50);
        if (t51) {
            void * t52 = gm_l0;
            uint64_t t53 = gm_l1;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            if (o55) gm_trap("integer overflow in `+`", gm_srcp33bek143l58, 139, 39);
            uint64_t t56 = r54;
            gm_t3fcbuehh3bs2 t57;
            gm_f_nth_c0r2nvs((void *)&t57, t52, t56);
            gm_t41cj6gvba3r8 t58;
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)54ULL));
            gm_t3fcbuehh3bs2 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t60 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t60 + 8), t59);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t58 + 0, t60);
            gm_t9qkh0rsooc8d t61;
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)55ULL));
            gm_t3fcbuehh3bs2 t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t62 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t62 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t62 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t62 + 8), t61);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t58 + 32, t62);
            gm_t9qkh0rsooc8d t63;
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)56ULL));
            gm_t3fcbuehh3bs2 t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t64 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 8), t63);
            GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t58 + 64, t64);
            gm_t41cj6gvba3r8 t65 = t58;
            uint8_t t66 = 0;
            gm_t3fcbuehh3bs2 t67 = t57;
            if (gm_eqt7ftn1841u(((uint8_t *)&t65) + 0, ((uint8_t *)&t67))) t66 = 1;
            if (gm_eqt7ftn1841u(((uint8_t *)&t65) + 32, ((uint8_t *)&t67))) t66 = 1;
            if (gm_eqt7ftn1841u(((uint8_t *)&t65) + 64, ((uint8_t *)&t67))) t66 = 1;
            uint8_t t68 = (uint8_t)(t66);
            t48 = t68;
        } else t48 = 0;
        uint8_t t69 = 0;
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_fn_3cgmi89(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_3cgmi89 F;
    GM_STACK_CHECK(&F, 657u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_let_else_e2n01r7 *)((&F.h)->up))->l0));
        gm_ttlipffho4ijl t3;
        gm_f_parse_pattern_mggn9o4((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttlipffho4ijl t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_ttlipffho4ijl t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_t0d02142k4tu8 t7 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t4) + 8));
        F.l0 = t7;
    }
    {
        void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_let_else_e2n01r7 *)((&F.h)->up))->l0));
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)55ULL));
        uint8_t t11 = gm_f_at_54906rm(t9, t10);
        if (!t11) goto ie8;
        uint8_t t12 = 0;
        uint8_t t13;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&F.l0) + 8) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn15;
        t13 = ((uint8_t)1);
        goto me14;
mn15:;
        t13 = ((uint8_t)0);
        goto me14;
mn17:;
        gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 116, 36);
me14:;
        if (!t13) {
            uint8_t t18 = ((GM_LD(uint8_t, (((uint8_t *)&F.l0) + 8) + 0) == ((uint8_t)0ULL)));
            t12 = t18;
        } else t12 = 1;
        uint8_t t19 = (uint8_t)!t12;
        if (!t19) goto ie8;
        {
            gm_t0d02142k4tu8 t21 = F.l0;
            gm_ttlipffho4ijl t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            ((uint8_t *)&t22)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t22 + 8), t21);
            gm_ttlipffho4ijl t23 = t22;
            uint8_t t24 = ((((uint8_t *)&t23))[0] != 0);
            F.ret = t22;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_b0_1 t25;
        gm_ttlipffho4ijl t26;
        memset(&t26, 0, sizeof t26);
        ((uint8_t *)&t26)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t26 + 8), t25);
        gm_ttlipffho4ijl t27 = t26;
        uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
        F.ret = t26;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_lr1jr0i(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_lr1jr0i F;
    uint32_t gm_l0; /* start */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK(&F, 973u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l0));
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l0 = t3;
    }
    {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l0));
        gm_tmgpf942vp8ci t6;
        gm_f_parse_stmt_40aalpm((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmgpf942vp8ci t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) goto ie4;
        gm_to88ae1b67aev t8 = GM_LD(gm_to88ae1b67aev, (((uint8_t *)&t7) + 8));
        F.l1 = t8;
        {
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l1);
            gm_tftb1odc739t5 t11 = gm_f_push_d6c76ud(t10, &(*(gm_to88ae1b67aev *)((uint8_t *)&F.l1)));
            gm_tftb1odc739t5 t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcp33bek143l58, 46, 13);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        }
        {
            void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l0));
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_shs874j97ior1));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(13));
            gm_b0_1 t16 = gm_f_expect_terminator_f7kp9sk((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix4;
ie4:;
        {
            void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l0));
            gm_f_recover_to_stmt_end_freq9f3(t18);
        }
        {
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l0));
            uint32_t t20 = gm_l0;
            gm_tl874t2atnlh3 t21 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t19, t20));
            gm_l2 = t21;
        }
        {
            void * t22 = (void *)((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l1);
            gm_to88ae1b67aev t23;
            memset((uint8_t *)&t23 + 4, 0, 4);
            memset((uint8_t *)&t23 + 140, 0, 4);
            void * t24 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l0));
            uint32_t t25 = gm_f_next_id_a7upaf4(t24);
            GM_ST(uint32_t, (uint8_t *)&t23 + 0, t25);
            void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_block_body_1juv37t *)((&F.h)->up))->l0));
            gm_tl874t2atnlh3 t27 = gm_l2;
            gm_tc3oht4ksth9d t28;
            gm_f_error_expr_dhdi86k((void *)&t28, t26, GM_CONV(gm_cseahvg8v7p0vs, t27));
            void * t29 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tem8p0t66u1ce t30;
            memset(&t30, 0, sizeof t30);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)2ULL));
            GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
            GM_ST(gm_tem8p0t66u1ce, (uint8_t *)&t23 + 8, t30);
            gm_tl874t2atnlh3 t31 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t23 + 128, t31);
            gm_tftb1odc739t5 t32 = gm_f_push_d6c76ud(t22, &t23);
            gm_tftb1odc739t5 t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcp33bek143l58, 51, 13);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
ix4:;
    }
    {
        gm_b0_1 t35;
        gm_t9qkh0rsooc8d t36;
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t36 + 1), t35);
        gm_t9qkh0rsooc8d t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fn_q64l5t2(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_q64l5t2 F;
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = (void *)((uint8_t *)&((struct gm_fr_parse_arms_24fl9da *)((&F.h)->up))->l1);
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_arms_24fl9da *)((&F.h)->up))->l0));
        gm_tjqj6atpbrqje t4;
        gm_f_parse_arm_h86all4((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tjqj6atpbrqje t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_t9qkh0rsooc8d t7;
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 1), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tndsf0a4f4lj0 t8 = GM_LD(gm_tndsf0a4f4lj0, (((uint8_t *)&t5) + 8));
        gm_tftb1odc739t5 t9 = gm_f_push_bpovr2k(t2, &t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcp33bek143l58, 380, 9);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_arms_24fl9da *)((&F.h)->up))->l0));
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sj486prdcn2go));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(13));
        gm_b0_1 t14 = gm_f_expect_terminator_f7kp9sk((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t15;
        gm_t9qkh0rsooc8d t16;
        ((uint8_t *)&t16)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t16 + 1), t15);
        gm_t9qkh0rsooc8d t17 = t16;
        uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
        F.ret = t16;
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
inline __attribute__((aligned(4))) void gm_f_fn_tc4108s(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_tc4108s F;
    GM_STACK_CHECK(&F, 652u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_cond_part_5vg3hu1 *)((&F.h)->up))->l0));
        gm_ttlipffho4ijl t3;
        gm_f_parse_pattern_mggn9o4((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttlipffho4ijl t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_ttlipffho4ijl t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_t0d02142k4tu8 t7 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t4) + 8));
        F.l0 = t7;
    }
    {
        void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_cond_part_5vg3hu1 *)((&F.h)->up))->l0));
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)55ULL));
        uint8_t t11 = gm_f_at_54906rm(t9, t10);
        if (!t11) goto ie8;
        {
            gm_t0d02142k4tu8 t13 = F.l0;
            gm_ttlipffho4ijl t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t14 + 8), t13);
            gm_ttlipffho4ijl t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_b0_1 t17;
        gm_ttlipffho4ijl t18;
        memset(&t18, 0, sizeof t18);
        ((uint8_t *)&t18)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
        gm_ttlipffho4ijl t19 = t18;
        uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
        F.ret = t18;
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
__attribute__((aligned(4))) void gm_f_parse_arm_h86all4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_arm_h86all4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l3; /* guard */
    GM_STACK_CHECK(&F, 2266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        gm_f_list_jfj8aoh((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l2);
        void * t5 = gm_l0;
        gm_ttlipffho4ijl t6;
        gm_f_parse_pattern_mggn9o4((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttlipffho4ijl t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 8));
            gm_tjqj6atpbrqje t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        gm_t0d02142k4tu8 t10 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t7) + 8));
        gm_tftb1odc739t5 t11 = gm_f_push_nnonkr8(t4, &t10);
        gm_tftb1odc739t5 t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcp33bek143l58, 391, 5);
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
    }
    {
        for (;;) {
            void * t15 = gm_l0;
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)57ULL));
            uint8_t t17 = gm_f_eat_qmock9g(t15, t16);
            if (!t17) goto lb14;
            {
                void * t20 = (void *)((uint8_t *)&F.l2);
                void * t21 = gm_l0;
                gm_ttlipffho4ijl t22;
                gm_f_parse_pattern_mggn9o4((void *)&t22, (void *)NULL, cx, NULL, t21);
                if (gm_unw.kind) {
                    goto gm_ll18;
                }
                gm_ttlipffho4ijl t23 = t22;
                if (((((uint8_t *)&t23))[0] != 0)) {
                    gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
                    gm_tjqj6atpbrqje t25;
                    memset(&t25, 0, sizeof t25);
                    ((uint8_t *)&t25)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), t24);
                    F.ret = t25;
                    goto gm_fret;
                }
                gm_t0d02142k4tu8 t26 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t23) + 8));
                gm_tftb1odc739t5 t27 = gm_f_push_nnonkr8(t20, &t26);
                gm_tftb1odc739t5 t28 = t27;
                if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcp33bek143l58, 392, 27);
                gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
            }
lc14:;
        }
gm_ll18:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
        }
        goto gm_lad;
lb14:;
    }
    {
        gm_l3 = ((void *)0);
    }
    {
        void * t31 = gm_l0;
        gm_t9qkh0rsooc8d t32;
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)13ULL));
        uint8_t t33 = gm_f_eat_kw_tvv0v3e(t31, t32);
        if (!t33) goto ie30;
        {
            void * t35 = gm_l0;
            gm_to1cssm8dfiv5 t36;
            gm_f_parse_expr_bkb2n6e((void *)&t36, (void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t37 = t36;
            if (((((uint8_t *)&t37))[0] != 0)) {
                gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 8));
                gm_tjqj6atpbrqje t39;
                memset(&t39, 0, sizeof t39);
                ((uint8_t *)&t39)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), t38);
                F.ret = t39;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t40 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t37) + 8));
            void * t41 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t41;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        {
            void * t43 = gm_l0;
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)59ULL));
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_scnb11g2ghrd0));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(34));
            gm_t0u0j6voh3loe t46 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t43, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t47 = t46;
            if (((((uint8_t *)&t47))[0] != 0)) {
                gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 4));
                gm_tjqj6atpbrqje t49;
                memset(&t49, 0, sizeof t49);
                ((uint8_t *)&t49)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t49 + 8), t48);
                F.ret = t49;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t47) + 4));
        }
    }
    {
        gm_to1cssm8dfiv5 t51;
        void * t53 = gm_l0;
        gm_t9qkh0rsooc8d t54;
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)65ULL));
        uint8_t t55 = gm_f_at_54906rm(t53, t54);
        if (!t55) goto ie52;
        void * t57 = gm_l0;
        gm_to1cssm8dfiv5 t58;
        gm_f_parse_block_expr_0j6b78q((void *)&t58, (void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t59 = t58;
        t51 = t59;
        goto ix52;
ie52:;
        void * t61 = gm_l0;
        gm_to1cssm8dfiv5 t62;
        gm_f_parse_expr_bkb2n6e((void *)&t62, (void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t63 = t62;
        t51 = t63;
ix52:;
        gm_to1cssm8dfiv5 t64 = t51;
        if (((((uint8_t *)&t64))[0] != 0)) {
            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 8));
            gm_tjqj6atpbrqje t66;
            memset(&t66, 0, sizeof t66);
            ((uint8_t *)&t66)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t66 + 8), t65);
            F.ret = t66;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t67 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t64) + 8));
        F.l4 = t67;
    }
    {
        void * t69 = gm_l0;
        gm_t3fcbuehh3bs2 t70;
        gm_f_kind_k239kpg((void *)&t70, t69);
        gm_t6k157sild47t t71 = gm_f_assign_op_0s1u8tb(&t70);
        gm_t6k157sild47t t72 = t71;
        uint8_t t73;
        if (((((uint8_t *)&t72))[3] != 0)) goto mn75;
        t73 = ((uint8_t)0);
        goto me74;
mn75:;
        t73 = ((uint8_t)1);
        goto me74;
mn76:;
        gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 398, 8);
me74:;
        if (!t73) goto ie68;
        {
            void * t78 = gm_l0;
            void * t79 = gm_l0;
            gm_tl874t2atnlh3 t80 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t79));
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sp69n8gsguhfh));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(76));
            gm_b0_1 t82 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t78, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tjqj6atpbrqje t83;
            memset(&t83, 0, sizeof t83);
            ((uint8_t *)&t83)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t83 + 8), t82);
            gm_tjqj6atpbrqje t84 = t83;
            uint8_t t85 = ((((uint8_t *)&t84))[0] != 0);
            F.ret = t83;
            goto gm_fret;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        gm_tndsf0a4f4lj0 t86;
        memset((uint8_t *)&t86 + 44, 0, 4);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t86 + 0, t87);
        void * t88 = gm_l3;
        GM_ST(void *, (uint8_t *)&t86 + 16, t88);
        void * t89 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t86 + 24, t89);
        void * t90 = gm_l0;
        uint32_t t91 = gm_l1;
        gm_tl874t2atnlh3 t92 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t90, t91));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t86 + 32, t92);
        gm_tjqj6atpbrqje t93;
        GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
        ((uint8_t *)&t93)[0] = 0;
        GM_ST(gm_tndsf0a4f4lj0, ((uint8_t *)&t93 + 8), t86);
        gm_tjqj6atpbrqje t94 = t93;
        uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
        F.ret = t93;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_parse_arms_24fl9da(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_binding_ojaae7v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_binding_ojaae7v F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l3; /* ty */
    gm_t9qkh0rsooc8d gm_l4; /* kind */
    void * gm_l5; /* init */
    GM_STACK_CHECK(&F, 1906u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        gm_f_list_mho6jfm((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l2);
        void * t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s1pvh8pfrs3ir));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
        gm_t4jn8fl3lmk5k t7;
        gm_f_parse_name_figjvbe((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 8));
            gm_ta8qa3lgiolvh t10;
            memset(&t10, 0, sizeof t10);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t10 + 8), t9);
            F.ret = t10;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t8) + 8));
        gm_tftb1odc739t5 t12 = gm_f_push_s5rtia7(t4, &t11);
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcp33bek143l58, 146, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        for (;;) {
            void * t16 = gm_l0;
            gm_t9qkh0rsooc8d t17;
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)57ULL));
            uint8_t t18 = gm_f_eat_qmock9g(t16, t17);
            if (!t18) goto lb15;
            {
                void * t21 = (void *)((uint8_t *)&F.l2);
                void * t22 = gm_l0;
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s1pvh8pfrs3ir));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(6));
                gm_t4jn8fl3lmk5k t24;
                gm_f_parse_name_figjvbe((void *)&t24, (void *)NULL, cx, NULL, t22, t23);
                if (gm_unw.kind) {
                    goto gm_ll19;
                }
                gm_t4jn8fl3lmk5k t25 = t24;
                if (((((uint8_t *)&t25))[0] != 0)) {
                    gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 8));
                    gm_ta8qa3lgiolvh t27;
                    memset(&t27, 0, sizeof t27);
                    ((uint8_t *)&t27)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), t26);
                    F.ret = t27;
                    goto gm_fret;
                }
                gm_t08olq97rscn3 t28 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t25) + 8));
                gm_tftb1odc739t5 t29 = gm_f_push_s5rtia7(t21, &t28);
                gm_tftb1odc739t5 t30 = t29;
                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcp33bek143l58, 147, 27);
                gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
            }
lc15:;
        }
gm_ll19:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
        }
        goto gm_lad;
lb15:;
    }
    {
        gm_l3 = ((void *)0);
    }
    {
        gm_t9qkh0rsooc8d t32;
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
        gm_l4 = t32;
    }
    {
        void * t34 = gm_l0;
        gm_t9qkh0rsooc8d t35;
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)54ULL));
        uint8_t t36 = gm_f_eat_qmock9g(t34, t35);
        if (!t36) goto ie33;
        {
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            gm_l4 = t38;
        }
        goto ix33;
ie33:;
        void * t40 = gm_l0;
        gm_t9qkh0rsooc8d t41;
        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)55ULL));
        uint8_t t42 = gm_f_eat_qmock9g(t40, t41);
        if (!t42) goto ie39;
        {
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
            gm_l4 = t44;
        }
        goto ix39;
ie39:;
        void * t46 = gm_l0;
        gm_t9qkh0rsooc8d t47;
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)56ULL));
        uint8_t t48 = gm_f_eat_qmock9g(t46, t47);
        if (!t48) goto ie45;
        {
            void * t50 = gm_l0;
            gm_to1cssm8dfiv5 t51;
            gm_f_parse_type_2m93dtq((void *)&t51, (void *)NULL, cx, NULL, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t52 = t51;
            if (((((uint8_t *)&t52))[0] != 0)) {
                gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 8));
                gm_ta8qa3lgiolvh t54;
                memset(&t54, 0, sizeof t54);
                ((uint8_t *)&t54)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t54 + 8), t53);
                F.ret = t54;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t55 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t52) + 8));
            void * t56 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t56;
        }
        {
            void * t58 = gm_l0;
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)56ULL));
            uint8_t t60 = gm_f_eat_qmock9g(t58, t59);
            if (!t60) goto ie57;
            {
                gm_t9qkh0rsooc8d t62;
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
                gm_l4 = t62;
            }
            goto ix57;
ie57:;
            void * t64 = gm_l0;
            gm_t9qkh0rsooc8d t65;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)22ULL));
            uint8_t t66 = gm_f_eat_qmock9g(t64, t65);
            if (!t66) goto ie63;
            {
                gm_t9qkh0rsooc8d t68;
                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)1ULL));
                gm_l4 = t68;
            }
            goto ix63;
ie63:;
            {
                void * t70 = gm_l0;
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sp796mgrgp4mj));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(44));
                gm_b0_1 t72 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t70, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ta8qa3lgiolvh t73;
                memset(&t73, 0, sizeof t73);
                ((uint8_t *)&t73)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t73 + 8), t72);
                gm_ta8qa3lgiolvh t74 = t73;
                uint8_t t75 = ((((uint8_t *)&t74))[0] != 0);
                F.ret = t73;
                goto gm_fret;
            }
ix63:;
ix57:;
        }
        goto ix45;
ie45:;
        {
            void * t77 = gm_l0;
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sk2sae8qmnmbb));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(30));
            gm_b0_1 t79 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ta8qa3lgiolvh t80;
            memset(&t80, 0, sizeof t80);
            ((uint8_t *)&t80)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t80 + 8), t79);
            gm_ta8qa3lgiolvh t81 = t80;
            uint8_t t82 = ((((uint8_t *)&t81))[0] != 0);
            F.ret = t80;
            goto gm_fret;
        }
ix45:;
ix39:;
ix33:;
    }
    {
        void * t83 = gm_l0;
        uint64_t t84 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        gm_to1cssm8dfiv5 t85;
        gm_f_parse_rhs_9susfpi((void *)&t85, (void *)NULL, cx, NULL, t83, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t86 = t85;
        if (((((uint8_t *)&t86))[0] != 0)) {
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 8));
            gm_ta8qa3lgiolvh t88;
            memset(&t88, 0, sizeof t88);
            ((uint8_t *)&t88)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t88 + 8), t87);
            F.ret = t88;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t89 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t86) + 8));
        void * t90 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t90;
    }
    {
        gm_tv2uf4leil5hq t91;
        memset((uint8_t *)&t91 + 4, 0, 4);
        memset((uint8_t *)&t91 + 33, 0, 7);
        memset((uint8_t *)&t91 + 60, 0, 4);
        void * t92 = gm_l0;
        uint32_t t93 = gm_f_next_id_a7upaf4(t92);
        GM_ST(uint32_t, (uint8_t *)&t91 + 0, t93);
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t91 + 8, t94);
        void * t95 = gm_l3;
        GM_ST(void *, (uint8_t *)&t91 + 24, t95);
        gm_t9qkh0rsooc8d t96 = gm_l4;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t91 + 32, t96);
        void * t97 = gm_l5;
        GM_ST(void *, (uint8_t *)&t91 + 40, t97);
        void * t98 = gm_l0;
        uint32_t t99 = gm_l1;
        gm_tl874t2atnlh3 t100 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t98, t99));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t91 + 48, t100);
        gm_ta8qa3lgiolvh t101;
        GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
        ((uint8_t *)&t101)[0] = 0;
        GM_ST(gm_tv2uf4leil5hq, ((uint8_t *)&t101 + 8), t91);
        gm_ta8qa3lgiolvh t102 = t101;
        uint8_t t103 = ((((uint8_t *)&t102))[0] != 0);
        F.ret = t101;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_parse_block_body_1juv37t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_block_tpg3f1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_block_tpg3f1i F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l2; /* stmts */
    GM_STACK_CHECK(&F, 436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_t9qkh0rsooc8d t6;
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)65ULL));
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s5dpb04jti9nb));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(16));
            gm_t0u0j6voh3loe t8 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t5, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 4));
                gm_toc4mrkn2e7o9 t11;
                memset(&t11, 0, sizeof t11);
                ((uint8_t *)&t11)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
                F.ret = t11;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t9) + 4));
        }
    }
    {
        void * t13 = gm_l0;
        gm_t9qkh0rsooc8d t14 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
            gm_toc4mrkn2e7o9 t17;
            memset(&t17, 0, sizeof t17);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), t16);
            F.ret = t17;
            goto gm_fret;
        }
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        void * t19 = gm_l0;
        gm_b16_8 t20 = gm_f_parse_block_body_1juv37t((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t20;
    }
    {
        void * t21 = gm_l0;
        gm_f_leave_rb20u32(t21);
    }
    {
        gm_t79rbdv4rafe5 t22;
        memset((uint8_t *)&t22 + 4, 0, 4);
        memset((uint8_t *)&t22 + 36, 0, 4);
        void * t23 = gm_l0;
        uint32_t t24 = gm_f_next_id_a7upaf4(t23);
        GM_ST(uint32_t, (uint8_t *)&t22 + 0, t24);
        gm_b16_8 t25 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 8, t25);
        void * t26 = gm_l0;
        uint32_t t27 = gm_l1;
        gm_tl874t2atnlh3 t28 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t26, t27));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t22 + 24, t28);
        gm_toc4mrkn2e7o9 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        ((uint8_t *)&t29)[0] = 0;
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t29 + 8), t22);
        gm_toc4mrkn2e7o9 t30 = t29;
        uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
        F.ret = t29;
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
__attribute__((aligned(4))) void gm_f_parse_cond_6sk4htm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_cond_6sk4htm F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l6; /* x */
    void * gm_l7; /* value */
    void * gm_l8; /* e */
    GM_STACK_CHECK(&F, 542u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        gm_f_list_v5d1o9b((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)((uint8_t *)&F.l2);
        gm_t9qkh0rsooc8d t6 = gm_f_parse_cond_parts_ohqkl5k((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
            gm_t4jn8fl3lmk5k t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t13 = (t12 > ((uint64_t)1ULL));
        if (!t13) goto ie11;
        {
            {
                gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                gm_l3 = t16;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t18 = gm_l4;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t20 = (t18 < t19);
                    if (!t20) goto lb17;
                    {
                        uint64_t t23 = gm_l4;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_tq5j0547p8452 t28 = GM_LD(gm_tq5j0547p8452, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t24 * 80));
                        F.l5 = t28;
                        {
                            void * t30;
                            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            if (t33 != ((uint8_t)1ULL)) goto mn32;
                            void * t34 = GM_LD(void *, (((uint8_t *)&F.l5) + 8));
                            gm_l6 = t34;
                            void * t35 = gm_l6;
                            t30 = t35;
                            goto me31;
mn32:;
                            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                            if (t37 != ((uint8_t)0ULL)) goto mn36;
                            void * t38 = GM_LD(void *, ((((uint8_t *)&F.l5) + 8) + 64));
                            gm_l7 = t38;
                            void * t39 = gm_l7;
                            t30 = t39;
                            goto me31;
mn36:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 232, 18);
me31:;
                            gm_l8 = t30;
                        }
                        {
                            void * t41 = gm_l8;
                            uint8_t t42;
                            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                            if (t45 != ((uint8_t)13ULL)) goto mn44;
                            uint8_t t46 = GM_LD(uint8_t, ((((((uint8_t *)t41) + 8) + 8) + 0) + 0));
                            if (t46 != ((uint8_t)21ULL)) goto mn44;
                            t42 = ((uint8_t)1);
                            goto me43;
mn44:;
                            t42 = ((uint8_t)0);
                            goto me43;
mn47:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 236, 16);
me43:;
                            if (!t42) goto ie40;
                            {
                                void * t49 = gm_l0;
                                void * t50 = gm_l8;
                                gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 72));
                                gm_b16_8 t52;
                                GM_ST(void *, (uint8_t *)&t52, (void *)(gm_snemlnnsbdb13));
                                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(45));
                                gm_b0_1 t53 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t49, t51, t52);
                                if (gm_unw.kind) {
                                    goto gm_ll21;
                                }
                            }
                            goto ix40;
ie40:;
ix40:;
                        }
                        goto me26;
mn27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 231, 9);
me26:;
                    }
lc17:;
                    {
                        uint64_t t55 = gm_l4;
                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                        uint64_t t58 = r56;
                        gm_l4 = t58;
                    }
                }
gm_ll21:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
                }
                goto gm_lad;
lb17:;
            }
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_t08olq97rscn3 t59;
        memset((uint8_t *)&t59 + 28, 0, 4);
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
        void * t61 = gm_l0;
        uint32_t t62 = gm_l1;
        gm_tl874t2atnlh3 t63 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t61, t62));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t59 + 16, t63);
        gm_t4jn8fl3lmk5k t64;
        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        ((uint8_t *)&t64)[0] = 0;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t64 + 8), t59);
        gm_t4jn8fl3lmk5k t65 = t64;
        uint8_t t66 = ((((uint8_t *)&t65))[0] != 0);
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
inline __attribute__((aligned(4))) void gm_f_parse_cond_part_5vg3hu1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_cond_part_5vg3hu1 F;
    GM_STACK_CHECK(&F, 1587u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_toqf51949ppls t2;
        memset(&t2, 0, sizeof t2);
        F.l1 = t2;
    }
    {
        void * t4 = F.l0;
        uint8_t t5 = gm_f_binds_ahead_e3p74o2(t4);
        if (!t5) goto ie3;
        {
            void * t7 = F.l0;
            gm_closure t8 = ((gm_closure){(void *)gm_f_fn_tc4108s, (void *)&F.h, cx});
            gm_f_speculate_j6q99ri((void *)&(*(gm_toqf51949ppls *)((uint8_t *)&F.l1)), (void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        if (!((((uint8_t *)&F.l1))[64] != 0)) goto ie9;
        gm_t0d02142k4tu8 t10 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l1));
        F.l2 = t10;
        {
            {
                void * t13 = F.l0;
                gm_tm4pc0emhrv1g t14;
                gm_f_bump_q2i40re((void *)&t14, t13);
            }
        }
        {
            gm_tuqaojgkqdcop t15;
            memcpy((uint8_t *)&t15 + 0, ((uint8_t *)&F.l2), 64);
            void * t16 = F.l0;
            gm_to1cssm8dfiv5 t17;
            gm_f_parse_expr_bkb2n6e((void *)&t17, (void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) {
                gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 8));
                gm_tt06c5njno8oo t20;
                memset(&t20, 0, sizeof t20);
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t20 + 8), t19);
                F.ret = t20;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t21 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t18) + 8));
            void * t22 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t15 + 64, t22);
            gm_tq5j0547p8452 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tuqaojgkqdcop, ((uint8_t *)&t23 + 8), t15);
            gm_tt06c5njno8oo t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            ((uint8_t *)&t24)[0] = 0;
            GM_ST(gm_tq5j0547p8452, ((uint8_t *)&t24 + 8), t23);
            gm_tt06c5njno8oo t25 = t24;
            uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t27 = F.l0;
        gm_to1cssm8dfiv5 t28;
        gm_f_parse_expr_bkb2n6e((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) {
            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 8));
            gm_tt06c5njno8oo t31;
            memset(&t31, 0, sizeof t31);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), t30);
            F.ret = t31;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t32 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t29) + 8));
        void * t33 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq5j0547p8452 t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1ULL));
        GM_ST(void *, ((uint8_t *)&t34 + 8), t33);
        gm_tt06c5njno8oo t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        ((uint8_t *)&t35)[0] = 0;
        GM_ST(gm_tq5j0547p8452, ((uint8_t *)&t35 + 8), t34);
        gm_tt06c5njno8oo t36 = t35;
        uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
        F.ret = t35;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_parse_cond_parts_ohqkl5k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_parse_cond_parts_ohqkl5k F;
    void * gm_l0; /* p */
    void * gm_l1; /* parts */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK(&F, 359u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l2 = t4;
    }
    {
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb8;
            {
                void * t11 = gm_l1;
                void * t12 = gm_l0;
                gm_tt06c5njno8oo t13;
                gm_f_parse_cond_part_5vg3hu1((void *)&t13, (void *)NULL, cx, NULL, t12);
                if (gm_unw.kind) {
                    goto gm_ll9;
                }
                gm_tt06c5njno8oo t14 = t13;
                if (((((uint8_t *)&t14))[0] != 0)) {
                    gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 8));
                    gm_t9qkh0rsooc8d t16;
                    ((uint8_t *)&t16)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t16 + 1), t15);
                    F.ret = t16;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll9;
                }
                gm_tq5j0547p8452 t17 = GM_LD(gm_tq5j0547p8452, (((uint8_t *)&t14) + 8));
                gm_tftb1odc739t5 t18 = gm_f_push_eb7kbhp(t11, &t17);
                gm_tftb1odc739t5 t19 = t18;
                if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srcp33bek143l58, 246, 9);
                gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
            }
            {
                void * t22 = gm_l0;
                gm_t9qkh0rsooc8d t23;
                GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
                uint8_t t24 = gm_f_eat_kw_tvv0v3e(t22, t23);
                uint8_t t25 = (uint8_t)!t24;
                if (!t25) goto ie21;
                {
                    goto lb8;
                }
                goto ix21;
ie21:;
ix21:;
            }
lc8:;
        }
gm_ll9:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_ld1_1;
lb8:;
    }
    {
        gm_b0_1 t27;
        gm_t9qkh0rsooc8d t28;
        ((uint8_t *)&t28)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t28 + 1), t27);
        gm_t9qkh0rsooc8d t29 = t28;
        uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
        F.ret = t28;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t30;
        goto gm_ld1_1;
    }
    {
        {
            void * t32 = gm_l0;
            gm_toh0ootodhjue t33 = gm_l2;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t32) + 128), t33);
        }
    }
    goto gm_ls34;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t36 = gm_l0;
            gm_toh0ootodhjue t37 = gm_l2;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t36) + 128), t37);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls34:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_parse_defer_body_log60it(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_defer_body_log60it F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 1831u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        gm_tem8p0t66u1ce t4;
        void * t6 = gm_l0;
        gm_t9qkh0rsooc8d t7;
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)65ULL));
        uint8_t t8 = gm_f_at_54906rm(t6, t7);
        if (!t8) goto ie5;
        void * t10 = gm_l0;
        gm_toc4mrkn2e7o9 t11;
        gm_f_parse_block_tpg3f1i((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 8));
            gm_tmgpf942vp8ci t14;
            memset(&t14, 0, sizeof t14);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 8), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t15 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t12) + 8));
        gm_tem8p0t66u1ce t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t16 + 8), t15);
        gm_tem8p0t66u1ce t17 = t16;
        t4 = t17;
        goto ix5;
ie5:;
        void * t19 = gm_l0;
        gm_thm5bu4f6031r t20;
        gm_f_parse_expr_or_assign_btc70ah((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thm5bu4f6031r t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) {
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 8));
            gm_tmgpf942vp8ci t23;
            memset(&t23, 0, sizeof t23);
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
            F.ret = t23;
            goto gm_fret;
        }
        gm_tem8p0t66u1ce t24 = GM_LD(gm_tem8p0t66u1ce, (((uint8_t *)&t21) + 8));
        gm_tem8p0t66u1ce t25 = t24;
        t4 = t25;
ix5:;
        F.l2 = t4;
    }
    {
        gm_to88ae1b67aev t26;
        memset((uint8_t *)&t26 + 4, 0, 4);
        memset((uint8_t *)&t26 + 140, 0, 4);
        void * t27 = gm_l0;
        uint32_t t28 = gm_f_next_id_a7upaf4(t27);
        GM_ST(uint32_t, (uint8_t *)&t26 + 0, t28);
        memcpy((uint8_t *)&t26 + 8, ((uint8_t *)&F.l2), 120);
        void * t29 = gm_l0;
        uint32_t t30 = gm_l1;
        gm_tl874t2atnlh3 t31 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t29, t30));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t26 + 128, t31);
        gm_tmgpf942vp8ci t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        ((uint8_t *)&t32)[0] = 0;
        GM_ST(gm_to88ae1b67aev, ((uint8_t *)&t32 + 8), t26);
        gm_tmgpf942vp8ci t33 = t32;
        uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
        F.ret = t32;
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
__attribute__((aligned(4))) void gm_f_parse_expr_or_assign_btc70ah(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_expr_or_assign_btc70ah F;
    void * gm_l0; /* p */
    gm_tjsp7bhdn2e8j gm_l2; /* op */
    void * gm_l3; /* value */
    GM_STACK_CHECK(&F, 2551u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_to1cssm8dfiv5 t4;
        gm_f_parse_expr_bkb2n6e((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_thm5bu4f6031r t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t8 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t5) + 8));
        gm_tftb1odc739t5 t9 = gm_f_push_d943vgp(t2, &t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcp33bek143l58, 185, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        for (;;) {
            void * t13 = gm_l0;
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)57ULL));
            uint8_t t15 = gm_f_eat_qmock9g(t13, t14);
            if (!t15) goto lb12;
            {
                void * t18 = (void *)((uint8_t *)&F.l1);
                void * t19 = gm_l0;
                gm_to1cssm8dfiv5 t20;
                gm_f_parse_expr_bkb2n6e((void *)&t20, (void *)NULL, cx, NULL, t19);
                if (gm_unw.kind) {
                    goto gm_ll16;
                }
                gm_to1cssm8dfiv5 t21 = t20;
                if (((((uint8_t *)&t21))[0] != 0)) {
                    gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 8));
                    gm_thm5bu4f6031r t23;
                    memset(&t23, 0, sizeof t23);
                    ((uint8_t *)&t23)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
                    F.ret = t23;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t24 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t21) + 8));
                gm_tftb1odc739t5 t25 = gm_f_push_d943vgp(t18, &t24);
                gm_tftb1odc739t5 t26 = t25;
                if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcp33bek143l58, 186, 27);
                gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
            }
lc12:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
        }
        goto gm_lad;
lb12:;
    }
    {
        void * t29 = gm_l0;
        gm_t3fcbuehh3bs2 t30;
        gm_f_kind_k239kpg((void *)&t30, t29);
        gm_t6k157sild47t t31 = gm_f_assign_op_0s1u8tb(&t30);
        gm_t6k157sild47t t32 = t31;
        if (!((((uint8_t *)&t32))[3] != 0)) goto ie28;
        gm_tjsp7bhdn2e8j t33 = GM_LD(gm_tjsp7bhdn2e8j, ((uint8_t *)&t32));
        gm_l2 = t33;
        {
            {
                void * t36 = gm_l0;
                gm_tm4pc0emhrv1g t37;
                gm_f_bump_q2i40re((void *)&t37, t36);
            }
        }
        {
            void * t38 = gm_l0;
            uint64_t t39 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
            gm_to1cssm8dfiv5 t40;
            gm_f_parse_rhs_9susfpi((void *)&t40, (void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t41 = t40;
            if (((((uint8_t *)&t41))[0] != 0)) {
                gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 8));
                gm_thm5bu4f6031r t43;
                memset(&t43, 0, sizeof t43);
                ((uint8_t *)&t43)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t43 + 8), t42);
                F.ret = t43;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t44 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t41) + 8));
            void * t45 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t45;
        }
        {
            gm_t4d1qrgh0vnq1 t46;
            memset((uint8_t *)&t46 + 19, 0, 5);
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
            gm_tjsp7bhdn2e8j t48 = gm_l2;
            GM_ST(gm_tjsp7bhdn2e8j, (uint8_t *)&t46 + 16, t48);
            void * t49 = gm_l3;
            GM_ST(void *, (uint8_t *)&t46 + 24, t49);
            gm_tem8p0t66u1ce t50;
            memset(&t50, 0, sizeof t50);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t4d1qrgh0vnq1, ((uint8_t *)&t50 + 8), t46);
            gm_thm5bu4f6031r t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            ((uint8_t *)&t51)[0] = 0;
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t51 + 8), t50);
            gm_thm5bu4f6031r t52 = t51;
            uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
            F.ret = t51;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        uint64_t t55 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
        uint8_t t56 = (t55 > ((uint64_t)1ULL));
        if (!t56) goto ie54;
        {
            void * t58 = gm_l0;
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_stna0ubmss8p6));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(47));
            gm_b0_1 t60 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thm5bu4f6031r t61;
            memset(&t61, 0, sizeof t61);
            ((uint8_t *)&t61)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t61 + 8), t60);
            gm_thm5bu4f6031r t62 = t61;
            uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
            F.ret = t61;
            goto gm_fret;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        uint64_t t64 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t65 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
        if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcp33bek143l58, 193, 22);
        void * t66 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 0)) + t64 * 88)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tem8p0t66u1ce t67;
        memset(&t67, 0, sizeof t67);
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
        GM_ST(void *, ((uint8_t *)&t67 + 8), t66);
        gm_thm5bu4f6031r t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
        ((uint8_t *)&t68)[0] = 0;
        GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t68 + 8), t67);
        gm_thm5bu4f6031r t69 = t68;
        uint8_t t70 = ((((uint8_t *)&t69))[0] != 0);
        F.ret = t68;
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
__attribute__((aligned(4))) void gm_f_parse_for_q8vk3da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_for_q8vk3da F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l2; /* l */
    uint32_t gm_l3; /* start */
    void * gm_l5; /* iter */
    gm_tl874t2atnlh3 gm_l7; /* whole */
    GM_STACK_CHECK(&F, 2220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2;
        if (!((((uint8_t *)&F.l1))[32] != 0)) goto ie3;
        gm_t08olq97rscn3 t4 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l1));
        gm_l2 = t4;
        uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 4));
        uint32_t t7 = t6;
        t2 = t7;
        goto ix3;
ie3:;
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        uint32_t t11 = t10;
        t2 = t11;
ix3:;
        gm_l3 = t2;
    }
    {
        {
            void * t13 = gm_l0;
            gm_tm4pc0emhrv1g t14;
            gm_f_bump_q2i40re((void *)&t14, t13);
        }
    }
    {
        gm_f_list_jfj8aoh((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = (void *)((uint8_t *)&F.l4);
        void * t16 = gm_l0;
        gm_ttlipffho4ijl t17;
        gm_f_parse_pattern_mggn9o4((void *)&t17, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttlipffho4ijl t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) {
            gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 8));
            gm_to1cssm8dfiv5 t20;
            memset(&t20, 0, sizeof t20);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t20 + 8), t19);
            F.ret = t20;
            goto gm_fret;
        }
        gm_t0d02142k4tu8 t21 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t18) + 8));
        gm_tftb1odc739t5 t22 = gm_f_push_nnonkr8(t15, &t21);
        gm_tftb1odc739t5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcp33bek143l58, 337, 5);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
    }
    {
        for (;;) {
            void * t26 = gm_l0;
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)57ULL));
            uint8_t t28 = gm_f_eat_qmock9g(t26, t27);
            if (!t28) goto lb25;
            {
                void * t31 = (void *)((uint8_t *)&F.l4);
                void * t32 = gm_l0;
                gm_ttlipffho4ijl t33;
                gm_f_parse_pattern_mggn9o4((void *)&t33, (void *)NULL, cx, NULL, t32);
                if (gm_unw.kind) {
                    goto gm_ll29;
                }
                gm_ttlipffho4ijl t34 = t33;
                if (((((uint8_t *)&t34))[0] != 0)) {
                    gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 8));
                    gm_to1cssm8dfiv5 t36;
                    memset(&t36, 0, sizeof t36);
                    ((uint8_t *)&t36)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t36 + 8), t35);
                    F.ret = t36;
                    goto gm_fret;
                }
                gm_t0d02142k4tu8 t37 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t34) + 8));
                gm_tftb1odc739t5 t38 = gm_f_push_nnonkr8(t31, &t37);
                gm_tftb1odc739t5 t39 = t38;
                if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcp33bek143l58, 338, 27);
                gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
            }
lc25:;
        }
gm_ll29:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
        }
        goto gm_lad;
lb25:;
    }
    {
        {
            void * t42 = gm_l0;
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)15ULL));
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_ssce05hnjd595));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(31));
            gm_t0u0j6voh3loe t45 = gm_f_expect_kw_7kbmp0i((void *)NULL, cx, NULL, t42, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) {
                gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 4));
                gm_to1cssm8dfiv5 t48;
                memset(&t48, 0, sizeof t48);
                ((uint8_t *)&t48)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t48 + 8), t47);
                F.ret = t48;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t46) + 4));
        }
    }
    {
        void * t50 = gm_l0;
        gm_to1cssm8dfiv5 t51;
        gm_f_parse_head_expr_6sofdhn((void *)&t51, (void *)NULL, cx, NULL, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t52 = t51;
        if (((((uint8_t *)&t52))[0] != 0)) {
            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 8));
            gm_to1cssm8dfiv5 t54;
            memset(&t54, 0, sizeof t54);
            ((uint8_t *)&t54)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t54 + 8), t53);
            F.ret = t54;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t55 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t52) + 8));
        void * t56 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t56;
    }
    {
        void * t57 = gm_l0;
        gm_toc4mrkn2e7o9 t58;
        gm_f_parse_labeled_block_s1cpmi0((void *)&t58, (void *)NULL, cx, NULL, t57, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t59 = t58;
        if (((((uint8_t *)&t59))[0] != 0)) {
            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 8));
            gm_to1cssm8dfiv5 t61;
            memset(&t61, 0, sizeof t61);
            ((uint8_t *)&t61)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t61 + 8), t60);
            F.ret = t61;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t62 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t59) + 8));
        F.l6 = t62;
    }
    {
        void * t63 = gm_l0;
        uint32_t t64 = gm_l3;
        gm_tl874t2atnlh3 t65 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t63, t64));
        gm_l7 = t65;
    }
    {
        void * t66 = gm_l0;
        gm_t0l6vh8eupt26 t67;
        memset((uint8_t *)&t67 + 116, 0, 4);
        memcpy((uint8_t *)&t67 + 0, ((uint8_t *)&F.l1), 40);
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t67 + 40, t68);
        void * t69 = gm_l5;
        GM_ST(void *, (uint8_t *)&t67 + 56, t69);
        memcpy((uint8_t *)&t67 + 64, ((uint8_t *)&F.l6), 40);
        gm_tl874t2atnlh3 t70 = gm_l7;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t67 + 104, t70);
        gm_tq7s98ihonkju t71 = gm_f_new_5bcttcf((void *)NULL, cx, NULL, &t67, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k52ekgkvp6f)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t72 = t71;
        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcp33bek143l58, 343, 27);
        void * t73 = GM_LD(void *, (((uint8_t *)&t72) + 8));
        gm_t2gtbh39ptekd t74;
        memset(&t74, 0, sizeof t74);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)43ULL));
        GM_ST(void *, ((uint8_t *)&t74 + 8), t73);
        gm_tl874t2atnlh3 t75 = gm_l7;
        gm_tc3oht4ksth9d t76;
        gm_f_mk_expr_53qj7nd((void *)&t76, t66, &t74, GM_CONV(gm_cseahvg8v7p0vs, t75));
        gm_to1cssm8dfiv5 t77;
        GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
        ((uint8_t *)&t77)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t77 + 8), t76);
        gm_to1cssm8dfiv5 t78 = t77;
        uint8_t t79 = ((((uint8_t *)&t78))[0] != 0);
        F.ret = t77;
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
__attribute__((aligned(4))) void gm_f_parse_if_shktfou(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_if_shktfou F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_t08olq97rscn3 gm_l3; /* cond */
    gm_t08olq97rscn3 gm_l5; /* c */
    gm_t08olq97rscn3 gm_l6; /* name */
    gm_tl874t2atnlh3 gm_l7; /* whole */
    GM_STACK_CHECK(&F, 3522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_tm4pc0emhrv1g t6;
            gm_f_bump_q2i40re((void *)&t6, t5);
        }
    }
    {
        gm_f_list_taqq58r((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        gm_t4jn8fl3lmk5k t8;
        gm_f_parse_cond_6sk4htm((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 8));
            gm_to1cssm8dfiv5 t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t12 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t9) + 8));
        gm_l3 = t12;
    }
    {
        void * t13 = gm_l0;
        gm_t9qkh0rsooc8d t14 = gm_f_reject_assign_after_cond_utj4j69((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
            gm_to1cssm8dfiv5 t17;
            memset(&t17, 0, sizeof t17);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), t16);
            F.ret = t17;
            goto gm_fret;
        }
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        void * t19 = (void *)((uint8_t *)&F.l2);
        gm_tcuumjkg5eni6 t20;
        memcpy((uint8_t *)&t20 + 0, ((uint8_t *)&gm_l3), 32);
        void * t21 = gm_l0;
        gm_toc4mrkn2e7o9 t22;
        gm_f_parse_block_tpg3f1i((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) {
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
            gm_to1cssm8dfiv5 t25;
            memset(&t25, 0, sizeof t25);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), t24);
            F.ret = t25;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t26 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t23) + 8));
        GM_ST(gm_t79rbdv4rafe5, (uint8_t *)&t20 + 32, t26);
        gm_tftb1odc739t5 t27 = gm_f_push_0ie7e7i(t19, &t20);
        gm_tftb1odc739t5 t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcp33bek143l58, 285, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        gm_t9t5bnrf784qu t30;
        memset(&t30, 0, sizeof t30);
        F.l4 = t30;
    }
    {
        for (;;) {
            void * t32 = gm_l0;
            gm_t9qkh0rsooc8d t33;
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)7ULL));
            uint8_t t34 = gm_f_at_kw_p0r3sr9(t32, t33);
            if (!t34) goto lb31;
            {
                void * t38 = gm_l0;
                gm_t9qkh0rsooc8d t39;
                GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)13ULL));
                uint8_t t40 = gm_f_nth_is_kw_4i9ruj5(t38, ((uint64_t)1ULL), t39);
                if (!t40) goto ie37;
                {
                    {
                        void * t43 = gm_l0;
                        gm_tm4pc0emhrv1g t44;
                        gm_f_bump_q2i40re((void *)&t44, t43);
                    }
                }
                {
                    {
                        void * t46 = gm_l0;
                        gm_tm4pc0emhrv1g t47;
                        gm_f_bump_q2i40re((void *)&t47, t46);
                    }
                }
                {
                    void * t48 = gm_l0;
                    gm_t4jn8fl3lmk5k t49;
                    gm_f_parse_cond_6sk4htm((void *)&t49, (void *)NULL, cx, NULL, t48);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    gm_t4jn8fl3lmk5k t50 = t49;
                    if (((((uint8_t *)&t50))[0] != 0)) {
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 8));
                        gm_to1cssm8dfiv5 t52;
                        memset(&t52, 0, sizeof t52);
                        ((uint8_t *)&t52)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t52 + 8), t51);
                        F.ret = t52;
                        goto gm_fret;
                    }
                    gm_t08olq97rscn3 t53 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t50) + 8));
                    gm_l5 = t53;
                }
                {
                    void * t54 = gm_l0;
                    gm_t9qkh0rsooc8d t55 = gm_f_reject_assign_after_cond_utj4j69((void *)NULL, cx, NULL, t54);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    gm_t9qkh0rsooc8d t56 = t55;
                    if (((((uint8_t *)&t56))[0] != 0)) {
                        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                        gm_to1cssm8dfiv5 t58;
                        memset(&t58, 0, sizeof t58);
                        ((uint8_t *)&t58)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t58 + 8), t57);
                        F.ret = t58;
                        goto gm_fret;
                    }
                    gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
                }
                {
                    void * t60 = (void *)((uint8_t *)&F.l2);
                    gm_tcuumjkg5eni6 t61;
                    memcpy((uint8_t *)&t61 + 0, ((uint8_t *)&gm_l5), 32);
                    void * t62 = gm_l0;
                    gm_toc4mrkn2e7o9 t63;
                    gm_f_parse_block_tpg3f1i((void *)&t63, (void *)NULL, cx, NULL, t62);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    gm_toc4mrkn2e7o9 t64 = t63;
                    if (((((uint8_t *)&t64))[0] != 0)) {
                        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 8));
                        gm_to1cssm8dfiv5 t66;
                        memset(&t66, 0, sizeof t66);
                        ((uint8_t *)&t66)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t66 + 8), t65);
                        F.ret = t66;
                        goto gm_fret;
                    }
                    gm_t79rbdv4rafe5 t67 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t64) + 8));
                    GM_ST(gm_t79rbdv4rafe5, (uint8_t *)&t61 + 32, t67);
                    gm_tftb1odc739t5 t68 = gm_f_push_0ie7e7i(t60, &t61);
                    gm_tftb1odc739t5 t69 = t68;
                    if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srcp33bek143l58, 293, 13);
                    gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
                }
                goto ix37;
ie37:;
                void * t72 = gm_l0;
                gm_t9qkh0rsooc8d t73;
                GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)65ULL));
                uint8_t t74 = gm_f_nth_is_3fgqmcb(t72, ((uint64_t)1ULL), t73);
                if (!t74) goto ie71;
                {
                    {
                        void * t77 = gm_l0;
                        gm_tm4pc0emhrv1g t78;
                        gm_f_bump_q2i40re((void *)&t78, t77);
                    }
                }
                {
                    void * t79 = gm_l0;
                    gm_toc4mrkn2e7o9 t80;
                    gm_f_parse_block_tpg3f1i((void *)&t80, (void *)NULL, cx, NULL, t79);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    gm_toc4mrkn2e7o9 t81 = t80;
                    if (((((uint8_t *)&t81))[0] != 0)) {
                        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 8));
                        gm_to1cssm8dfiv5 t83;
                        memset(&t83, 0, sizeof t83);
                        ((uint8_t *)&t83)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t83 + 8), t82);
                        F.ret = t83;
                        goto gm_fret;
                    }
                    gm_t79rbdv4rafe5 t84 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t81) + 8));
                    gm_ttja7ii61nila t85;
                    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t85 + 8), t84);
                    gm_t9t5bnrf784qu t86;
                    GM_ST(uint8_t, (uint8_t *)&t86 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t86 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t86 + 52, 0);
                    GM_ST(gm_ttja7ii61nila, ((uint8_t *)&t86), t85);
                    ((uint8_t *)&t86)[48] = 1;
                    F.l4 = t86;
                }
                {
                    goto lb31;
                }
                goto ix71;
ie71:;
                void * t88 = gm_l0;
                uint8_t t89 = gm_f_nth_is_name_oqhoobd(t88, ((uint64_t)1ULL));
                if (!t89) goto ie87;
                void * t90 = gm_l0;
                gm_t9qkh0rsooc8d t91;
                GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)59ULL));
                uint8_t t92 = gm_f_nth_is_3fgqmcb(t90, ((uint64_t)2ULL), t91);
                if (!t92) goto ie87;
                {
                    {
                        void * t95 = gm_l0;
                        gm_tm4pc0emhrv1g t96;
                        gm_f_bump_q2i40re((void *)&t96, t95);
                    }
                }
                {
                    void * t97 = gm_l0;
                    gm_b16_8 t98;
                    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s1pvh8pfrs3ir));
                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(6));
                    gm_t4jn8fl3lmk5k t99;
                    gm_f_parse_name_figjvbe((void *)&t99, (void *)NULL, cx, NULL, t97, t98);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    gm_t4jn8fl3lmk5k t100 = t99;
                    if (((((uint8_t *)&t100))[0] != 0)) {
                        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 8));
                        gm_to1cssm8dfiv5 t102;
                        memset(&t102, 0, sizeof t102);
                        ((uint8_t *)&t102)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t102 + 8), t101);
                        F.ret = t102;
                        goto gm_fret;
                    }
                    gm_t08olq97rscn3 t103 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t100) + 8));
                    gm_l6 = t103;
                }
                {
                    {
                        void * t105 = gm_l0;
                        gm_tm4pc0emhrv1g t106;
                        gm_f_bump_q2i40re((void *)&t106, t105);
                    }
                }
                {
                    gm_ts686ckv8ehm2 t107;
                    memcpy((uint8_t *)&t107 + 0, ((uint8_t *)&gm_l6), 32);
                    void * t108 = gm_l0;
                    gm_to1cssm8dfiv5 t109;
                    gm_f_parse_capture_body_fgf89k2((void *)&t109, (void *)NULL, cx, NULL, t108);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    gm_to1cssm8dfiv5 t110 = t109;
                    if (((((uint8_t *)&t110))[0] != 0)) {
                        gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 8));
                        gm_to1cssm8dfiv5 t112;
                        memset(&t112, 0, sizeof t112);
                        ((uint8_t *)&t112)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t112 + 8), t111);
                        F.ret = t112;
                        goto gm_fret;
                    }
                    gm_tc3oht4ksth9d t113 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t110) + 8));
                    void * t114 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t113);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    GM_ST(void *, (uint8_t *)&t107 + 32, t114);
                    gm_ttja7ii61nila t115;
                    GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_ts686ckv8ehm2, ((uint8_t *)&t115 + 8), t107);
                    gm_t9t5bnrf784qu t116;
                    GM_ST(uint8_t, (uint8_t *)&t116 + 49, 0);
                    GM_ST(uint16_t, (uint8_t *)&t116 + 50, 0);
                    GM_ST(uint32_t, (uint8_t *)&t116 + 52, 0);
                    GM_ST(gm_ttja7ii61nila, ((uint8_t *)&t116), t115);
                    ((uint8_t *)&t116)[48] = 1;
                    F.l4 = t116;
                }
                {
                    goto lb31;
                }
                goto ix87;
ie87:;
                {
                    goto lb31;
                }
ix87:;
ix71:;
ix37:;
            }
lc31:;
        }
gm_ll35:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
        }
        goto gm_lad;
lb31:;
    }
    {
        void * t118 = gm_l0;
        uint32_t t119 = gm_l1;
        gm_tl874t2atnlh3 t120 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t118, t119));
        gm_l7 = t120;
    }
    {
        void * t121 = gm_l0;
        gm_t99g4dkn8rrfb t122;
        memset((uint8_t *)&t122 + 84, 0, 4);
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t122 + 0, t123);
        memcpy((uint8_t *)&t122 + 16, ((uint8_t *)&F.l4), 56);
        gm_tl874t2atnlh3 t124 = gm_l7;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t122 + 72, t124);
        gm_tq7s98ihonkju t125 = gm_f_new_ulodmu9((void *)NULL, cx, NULL, &t122, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3u24nfalvr)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t126 = t125;
        if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 8, gm_srcp33bek143l58, 310, 26);
        void * t127 = GM_LD(void *, (((uint8_t *)&t126) + 8));
        gm_t2gtbh39ptekd t128;
        memset(&t128, 0, sizeof t128);
        GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)40ULL));
        GM_ST(void *, ((uint8_t *)&t128 + 8), t127);
        gm_tl874t2atnlh3 t129 = gm_l7;
        gm_tc3oht4ksth9d t130;
        gm_f_mk_expr_53qj7nd((void *)&t130, t121, &t128, GM_CONV(gm_cseahvg8v7p0vs, t129));
        gm_to1cssm8dfiv5 t131;
        GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
        ((uint8_t *)&t131)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t131 + 8), t130);
        gm_to1cssm8dfiv5 t132 = t131;
        uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
        F.ret = t131;
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
inline __attribute__((aligned(4))) void gm_f_parse_labeled_block_s1cpmi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_labeled_block_s1cpmi0 F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l2; /* l */
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t1s75i08qtptd t2 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)a1));
        gm_t1s75i08qtptd t3 = t2;
        gm_t08olq97rscn3 t4;
        if (((((uint8_t *)&t3))[32] != 0)) {
            gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            void * t6 = gm_l0;
            gm_f_parse_block_tpg3f1i((void *)&(*(gm_toc4mrkn2e7o9 *)gm_out), (void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l2 = t4;
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 88);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_tftb1odc739t5 t10 = gm_f_push_3ef746b(t8, t9);
        gm_tftb1odc739t5 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcp33bek143l58, 316, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
    }
    {
    }
    {
        void * t17 = gm_l0;
        gm_f_parse_block_tpg3f1i((void *)&F.ret, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t18 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t18;
        goto gm_ld1_1;
    }
    {
        {
            void * t20 = gm_l0;
            void * t21 = (void *)(((uint8_t *)t20) + 88);
            gm_th2lqbo77pvsc t22;
            gm_f_pop_vr08sii((void *)&t22, t21);
        }
    }
    goto gm_ls23;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t25 = gm_l0;
            void * t26 = (void *)(((uint8_t *)t25) + 88);
            gm_th2lqbo77pvsc t27;
            gm_f_pop_vr08sii((void *)&t27, t26);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls23:;
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
inline __attribute__((aligned(4))) void gm_f_parse_let_else_e2n01r7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_let_else_e2n01r7 F;
    gm_t08olq97rscn3 gm_l4; /* c */
    GM_STACK_CHECK(&F, 3464u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        void * t3 = F.l0;
        uint8_t t4 = gm_f_binds_ahead_e3p74o2(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            gm_t5821lde3dhck t7;
            memset(&t7, 0, sizeof t7);
            gm_taeav3snkvh6h t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 0;
            GM_ST(gm_t5821lde3dhck, ((uint8_t *)&t8 + 8), t7);
            gm_taeav3snkvh6h t9 = t8;
            uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = F.l0;
        gm_closure t12 = ((gm_closure){(void *)gm_f_fn_3cgmi89, (void *)&F.h, cx});
        gm_toqf51949ppls t13;
        gm_f_speculate_j6q99ri((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toqf51949ppls t14 = t13;
        gm_t0d02142k4tu8 t15;
        if (((((uint8_t *)&t14))[64] != 0)) {
            gm_t0d02142k4tu8 t16 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            gm_t5821lde3dhck t17;
            memset(&t17, 0, sizeof t17);
            gm_taeav3snkvh6h t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(gm_t5821lde3dhck, ((uint8_t *)&t18 + 8), t17);
            gm_taeav3snkvh6h t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        F.l1 = t15;
    }
    {
        {
            void * t22 = F.l0;
            gm_tm4pc0emhrv1g t23;
            gm_f_bump_q2i40re((void *)&t23, t22);
        }
    }
    {
        void * t24 = F.l0;
        gm_to1cssm8dfiv5 t25;
        gm_f_parse_expr_bkb2n6e((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) {
            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
            gm_taeav3snkvh6h t28;
            memset(&t28, 0, sizeof t28);
            ((uint8_t *)&t28)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), t27);
            F.ret = t28;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t29 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t26) + 8));
        F.l2 = t29;
    }
    {
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 8) + 0));
        if (t31 != ((uint8_t)14ULL)) goto ie30;
        gm_tioi3jh9v22g5 t32 = GM_LD(gm_tioi3jh9v22g5, ((((uint8_t *)&F.l2) + 8) + 8));
        F.l3 = t32;
        {
            if (!(((((uint8_t *)&F.l3) + 8))[32] != 0)) goto ie34;
            gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l3) + 8));
            gm_l4 = t35;
            {
                void * t37 = F.l0;
                gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sitbu884cbr46));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(95));
                gm_b0_1 t40 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t37, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t41 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tem8p0t66u1ce t42;
                memset(&t42, 0, sizeof t42);
                GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)2ULL));
                GM_ST(void *, ((uint8_t *)&t42 + 8), t41);
                gm_t5821lde3dhck t43;
                GM_ST(uint8_t, (uint8_t *)&t43 + 121, 0);
                GM_ST(uint16_t, (uint8_t *)&t43 + 122, 0);
                GM_ST(uint32_t, (uint8_t *)&t43 + 124, 0);
                GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t43), t42);
                ((uint8_t *)&t43)[120] = 1;
                gm_taeav3snkvh6h t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 0;
                GM_ST(gm_t5821lde3dhck, ((uint8_t *)&t44 + 8), t43);
                gm_taeav3snkvh6h t45 = t44;
                uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
                F.ret = t44;
                goto gm_fret;
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            gm_t15nkjiutlvf3 t47;
            memcpy((uint8_t *)&t47 + 0, ((uint8_t *)&F.l1), 64);
            void * t48 = GM_LD(void *, (((uint8_t *)&F.l3) + 0));
            GM_ST(void *, (uint8_t *)&t47 + 64, t48);
            void * t49 = GM_LD(void *, (((uint8_t *)&F.l3) + 48));
            GM_ST(void *, (uint8_t *)&t47 + 72, t49);
            gm_tem8p0t66u1ce t50;
            memset(&t50, 0, sizeof t50);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)7ULL));
            GM_ST(gm_t15nkjiutlvf3, ((uint8_t *)&t50 + 8), t47);
            gm_t5821lde3dhck t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 121, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 122, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 124, 0);
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t51), t50);
            ((uint8_t *)&t51)[120] = 1;
            gm_taeav3snkvh6h t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            ((uint8_t *)&t52)[0] = 0;
            GM_ST(gm_t5821lde3dhck, ((uint8_t *)&t52 + 8), t51);
            gm_taeav3snkvh6h t53 = t52;
            uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
            F.ret = t52;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t55 = F.l0;
        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 48));
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s9akpbuv78p3m));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(108));
        gm_b0_1 t58 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t55, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t59 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tem8p0t66u1ce t60;
        memset(&t60, 0, sizeof t60);
        GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)2ULL));
        GM_ST(void *, ((uint8_t *)&t60 + 8), t59);
        gm_t5821lde3dhck t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 121, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 122, 0);
        GM_ST(uint32_t, (uint8_t *)&t61 + 124, 0);
        GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t61), t60);
        ((uint8_t *)&t61)[120] = 1;
        gm_taeav3snkvh6h t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        ((uint8_t *)&t62)[0] = 0;
        GM_ST(gm_t5821lde3dhck, ((uint8_t *)&t62 + 8), t61);
        gm_taeav3snkvh6h t63 = t62;
        uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
        F.ret = t62;
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
inline __attribute__((aligned(4))) void gm_f_parse_match_1lcgsrh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_match_1lcgsrh F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l3; /* arms */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    GM_STACK_CHECK(&F, 1316u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_tm4pc0emhrv1g t6;
            gm_f_bump_q2i40re((void *)&t6, t5);
        }
    }
    {
        void * t7 = gm_l0;
        gm_to1cssm8dfiv5 t8;
        gm_f_parse_head_expr_6sofdhn((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 8));
            gm_to1cssm8dfiv5 t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t9) + 8));
        void * t13 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t13;
    }
    {
        {
            void * t15 = gm_l0;
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)65ULL));
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sd48v0fh5kl37));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(23));
            gm_t0u0j6voh3loe t18 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t19 = t18;
            if (((((uint8_t *)&t19))[0] != 0)) {
                gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 4));
                gm_to1cssm8dfiv5 t21;
                memset(&t21, 0, sizeof t21);
                ((uint8_t *)&t21)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t21 + 8), t20);
                F.ret = t21;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t19) + 4));
        }
    }
    {
        void * t23 = gm_l0;
        gm_t9qkh0rsooc8d t24 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) {
            gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
            gm_to1cssm8dfiv5 t27;
            memset(&t27, 0, sizeof t27);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), t26);
            F.ret = t27;
            goto gm_fret;
        }
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
    }
    {
        void * t29 = gm_l0;
        gm_b16_8 t30 = gm_f_parse_arms_24fl9da((void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t30;
    }
    {
        void * t31 = gm_l0;
        gm_f_leave_rb20u32(t31);
    }
    {
        void * t32 = gm_l0;
        uint32_t t33 = gm_l1;
        gm_tl874t2atnlh3 t34 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t32, t33));
        gm_l4 = t34;
    }
    {
        void * t35 = gm_l0;
        gm_tiacef8jroje2 t36;
        memset((uint8_t *)&t36 + 36, 0, 4);
        void * t37 = gm_l2;
        GM_ST(void *, (uint8_t *)&t36 + 0, t37);
        gm_b16_8 t38 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 8, t38);
        gm_tl874t2atnlh3 t39 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t36 + 24, t39);
        gm_tq7s98ihonkju t40 = gm_f_new_lrokjci((void *)NULL, cx, NULL, &t36, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0d09vor0rn)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srcp33bek143l58, 373, 29);
        void * t42 = GM_LD(void *, (((uint8_t *)&t41) + 8));
        gm_t2gtbh39ptekd t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)41ULL));
        GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
        gm_tl874t2atnlh3 t44 = gm_l4;
        gm_tc3oht4ksth9d t45;
        gm_f_mk_expr_53qj7nd((void *)&t45, t35, &t43, GM_CONV(gm_cseahvg8v7p0vs, t44));
        gm_to1cssm8dfiv5 t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        ((uint8_t *)&t46)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t46 + 8), t45);
        gm_to1cssm8dfiv5 t47 = t46;
        uint8_t t48 = ((((uint8_t *)&t47))[0] != 0);
        F.ret = t46;
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
__attribute__((aligned(4))) void gm_f_parse_name_stmt_nb6mtgu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_name_stmt_nb6mtgu F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l1; /* label */
    GM_STACK_CHECK(&F, 4650u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t3fcbuehh3bs2 t4;
        gm_f_kind_k239kpg((void *)&t4, t3);
        gm_t3fcbuehh3bs2 t5 = t4;
        uint8_t t6;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 98, 8);
me7:;
        if (!t6) goto ie2;
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)56ULL));
        uint8_t t13 = gm_f_nth_is_3fgqmcb(t11, ((uint64_t)1ULL), t12);
        if (!t13) goto ie2;
        void * t14 = gm_l0;
        gm_t3fcbuehh3bs2 t15;
        gm_f_nth_c0r2nvs((void *)&t15, t14, ((uint64_t)2ULL));
        gm_t41cj6gvba3r8 t16;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)12ULL));
        gm_t3fcbuehh3bs2 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t18 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 8), t17);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t16 + 0, t18);
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)30ULL));
        gm_t3fcbuehh3bs2 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t20 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t20 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t20 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 8), t19);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t16 + 32, t20);
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)31ULL));
        gm_t3fcbuehh3bs2 t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 8), t21);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t16 + 64, t22);
        gm_t41cj6gvba3r8 t23 = t16;
        uint8_t t24 = 0;
        gm_t3fcbuehh3bs2 t25 = t15;
        if (gm_eqt7ftn1841u(((uint8_t *)&t23) + 0, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t23) + 32, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t23) + 64, ((uint8_t *)&t25))) t24 = 1;
        uint8_t t26 = (uint8_t)(t24);
        if (!t26) goto ie2;
        {
            void * t28 = gm_l0;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_so7em1q8clnib));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(7));
            gm_t4jn8fl3lmk5k t30;
            gm_f_parse_ident_n2c6b75((void *)&t30, (void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4jn8fl3lmk5k t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) {
                gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
                gm_thm5bu4f6031r t33;
                memset(&t33, 0, sizeof t33);
                ((uint8_t *)&t33)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t33 + 8), t32);
                F.ret = t33;
                goto gm_fret;
            }
            gm_t08olq97rscn3 t34 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t31) + 8));
            gm_l1 = t34;
        }
        {
            {
                void * t36 = gm_l0;
                gm_tm4pc0emhrv1g t37;
                gm_f_bump_q2i40re((void *)&t37, t36);
            }
        }
        {
            void * t39 = gm_l0;
            gm_t9qkh0rsooc8d t40;
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)30ULL));
            uint8_t t41 = gm_f_at_kw_p0r3sr9(t39, t40);
            if (!t41) goto ie38;
            {
                void * t43 = gm_l0;
                gm_t08olq97rscn3 t44 = gm_l1;
                gm_t1s75i08qtptd t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 36, 0);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t45), t44);
                ((uint8_t *)&t45)[32] = 1;
                gm_f_parse_while_gvp1mcm((void *)&(*(gm_thm5bu4f6031r *)gm_out), (void *)NULL, cx, NULL, t43, &t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix38;
ie38:;
ix38:;
        }
        {
            void * t47 = gm_l0;
            gm_t9qkh0rsooc8d t48;
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)12ULL));
            uint8_t t49 = gm_f_at_kw_p0r3sr9(t47, t48);
            if (!t49) goto ie46;
            {
                void * t51 = gm_l0;
                gm_t08olq97rscn3 t52 = gm_l1;
                gm_t1s75i08qtptd t53;
                GM_ST(uint8_t, (uint8_t *)&t53 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t53 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t53 + 36, 0);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t53), t52);
                ((uint8_t *)&t53)[32] = 1;
                gm_to1cssm8dfiv5 t54;
                gm_f_parse_for_q8vk3da((void *)&t54, (void *)NULL, cx, NULL, t51, &t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to1cssm8dfiv5 t55 = t54;
                if (((((uint8_t *)&t55))[0] != 0)) {
                    gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 8));
                    gm_thm5bu4f6031r t57;
                    memset(&t57, 0, sizeof t57);
                    ((uint8_t *)&t57)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t57 + 8), t56);
                    F.ret = t57;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t58 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t55) + 8));
                void * t59 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tem8p0t66u1ce t60;
                memset(&t60, 0, sizeof t60);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)2ULL));
                GM_ST(void *, ((uint8_t *)&t60 + 8), t59);
                gm_thm5bu4f6031r t61;
                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                ((uint8_t *)&t61)[0] = 0;
                GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t61 + 8), t60);
                gm_thm5bu4f6031r t62 = t61;
                uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
                F.ret = t61;
                goto gm_fret;
            }
            goto ix46;
ie46:;
ix46:;
        }
        {
            void * t64 = gm_l0;
            gm_t08olq97rscn3 t65 = gm_l1;
            gm_t1s75i08qtptd t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t66), t65);
            ((uint8_t *)&t66)[32] = 1;
            gm_to1cssm8dfiv5 t67;
            gm_f_parse_with_hlmj8pt((void *)&t67, (void *)NULL, cx, NULL, t64, &t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t68 = t67;
            if (((((uint8_t *)&t68))[0] != 0)) {
                gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 8));
                gm_thm5bu4f6031r t70;
                memset(&t70, 0, sizeof t70);
                ((uint8_t *)&t70)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t70 + 8), t69);
                F.ret = t70;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t71 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t68) + 8));
            void * t72 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tem8p0t66u1ce t73;
            memset(&t73, 0, sizeof t73);
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)2ULL));
            GM_ST(void *, ((uint8_t *)&t73 + 8), t72);
            gm_thm5bu4f6031r t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            ((uint8_t *)&t74)[0] = 0;
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t74 + 8), t73);
            gm_thm5bu4f6031r t75 = t74;
            uint8_t t76 = ((((uint8_t *)&t75))[0] != 0);
            F.ret = t74;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t78 = gm_l0;
        uint8_t t79 = gm_f_binding_ahead_cjedtav(t78);
        if (!t79) goto ie77;
        {
            gm_taiafe1jasg4m t81;
            gm_b16_8 t82 = gm_f_empty_slice_atlcs4j();
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 0, t82);
            void * t83 = gm_l0;
            gm_ta8qa3lgiolvh t84;
            gm_f_parse_binding_ojaae7v((void *)&t84, (void *)NULL, cx, NULL, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ta8qa3lgiolvh t85 = t84;
            if (((((uint8_t *)&t85))[0] != 0)) {
                gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 8));
                gm_thm5bu4f6031r t87;
                memset(&t87, 0, sizeof t87);
                ((uint8_t *)&t87)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t87 + 8), t86);
                F.ret = t87;
                goto gm_fret;
            }
            gm_tv2uf4leil5hq t88 = GM_LD(gm_tv2uf4leil5hq, (((uint8_t *)&t85) + 8));
            GM_ST(gm_tv2uf4leil5hq, (uint8_t *)&t81 + 16, t88);
            gm_tem8p0t66u1ce t89;
            memset(&t89, 0, sizeof t89);
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)0ULL));
            GM_ST(gm_taiafe1jasg4m, ((uint8_t *)&t89 + 8), t81);
            gm_thm5bu4f6031r t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            ((uint8_t *)&t90)[0] = 0;
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t90 + 8), t89);
            gm_thm5bu4f6031r t91 = t90;
            uint8_t t92 = ((((uint8_t *)&t91))[0] != 0);
            F.ret = t90;
            goto gm_fret;
        }
        goto ix77;
ie77:;
ix77:;
    }
    {
        void * t94 = gm_l0;
        gm_taeav3snkvh6h t95;
        gm_f_parse_let_else_e2n01r7((void *)&t95, (void *)NULL, cx, NULL, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_taeav3snkvh6h t96 = t95;
        if (((((uint8_t *)&t96))[0] != 0)) {
            gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 8));
            gm_thm5bu4f6031r t98;
            memset(&t98, 0, sizeof t98);
            ((uint8_t *)&t98)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t98 + 8), t97);
            F.ret = t98;
            goto gm_fret;
        }
        gm_t5821lde3dhck t99 = GM_LD(gm_t5821lde3dhck, (((uint8_t *)&t96) + 8));
        gm_t5821lde3dhck t100 = t99;
        if (!((((uint8_t *)&t100))[120] != 0)) goto ie93;
        gm_tem8p0t66u1ce t101 = GM_LD(gm_tem8p0t66u1ce, ((uint8_t *)&t100));
        F.l2 = t101;
        {
            gm_tem8p0t66u1ce t103 = F.l2;
            gm_thm5bu4f6031r t104;
            GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
            ((uint8_t *)&t104)[0] = 0;
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t104 + 8), t103);
            gm_thm5bu4f6031r t105 = t104;
            uint8_t t106 = ((((uint8_t *)&t105))[0] != 0);
            F.ret = t104;
            goto gm_fret;
        }
        goto ix93;
ie93:;
ix93:;
    }
    {
        void * t107 = gm_l0;
        gm_f_parse_expr_or_assign_btc70ah((void *)&(*(gm_thm5bu4f6031r *)gm_out), (void *)NULL, cx, NULL, t107);
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
inline __attribute__((aligned(4))) void gm_f_parse_rhs_9susfpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_parse_rhs_9susfpi F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* targets */
    gm_tl874t2atnlh3 gm_l4; /* last */
    GM_STACK_CHECK(&F, 1933u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_expr_bkb2n6e((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l2 = t7;
    }
    {
        uint8_t t9 = 0;
        uint64_t t10 = gm_l1;
        uint8_t t11 = (t10 < ((uint64_t)2ULL));
        if (!t11) {
            void * t12 = gm_l0;
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)57ULL));
            uint8_t t14 = gm_f_at_54906rm(t12, t13);
            uint8_t t15 = (uint8_t)!t14;
            t9 = t15;
        } else t9 = 1;
        if (!t9) goto ie8;
        {
            gm_tc3oht4ksth9d t17 = F.l2;
            gm_to1cssm8dfiv5 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t18 + 8), t17);
            gm_to1cssm8dfiv5 t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_f_list_fvvkq82((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l3);
        gm_tv1o1j44ii4db t22;
        memset((uint8_t *)&t22 + 129, 0, 7);
        gm_t1s75i08qtptd t23;
        memset(&t23, 0, sizeof t23);
        GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t22 + 0, t23);
        memcpy((uint8_t *)&t22 + 40, ((uint8_t *)&F.l2), 88);
        GM_ST(uint8_t, (uint8_t *)&t22 + 128, ((uint8_t)0));
        gm_tftb1odc739t5 t24 = gm_f_push_l8mrumu(t21, &t22);
        gm_tftb1odc739t5 t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcp33bek143l58, 176, 5);
        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
    }
    {
        for (;;) {
            void * t28 = gm_l0;
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)57ULL));
            uint8_t t30 = gm_f_eat_qmock9g(t28, t29);
            if (!t30) goto lb27;
            {
                void * t33 = (void *)((uint8_t *)&F.l3);
                gm_tv1o1j44ii4db t34;
                memset((uint8_t *)&t34 + 129, 0, 7);
                gm_t1s75i08qtptd t35;
                memset(&t35, 0, sizeof t35);
                GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t34 + 0, t35);
                void * t36 = gm_l0;
                gm_to1cssm8dfiv5 t37;
                gm_f_parse_expr_bkb2n6e((void *)&t37, (void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_ll31;
                }
                gm_to1cssm8dfiv5 t38 = t37;
                if (((((uint8_t *)&t38))[0] != 0)) {
                    gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 8));
                    gm_to1cssm8dfiv5 t40;
                    memset(&t40, 0, sizeof t40);
                    ((uint8_t *)&t40)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t40 + 8), t39);
                    F.ret = t40;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t41 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t38) + 8));
                GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&t34 + 40, t41);
                GM_ST(uint8_t, (uint8_t *)&t34 + 128, ((uint8_t)0));
                gm_tftb1odc739t5 t42 = gm_f_push_l8mrumu(t33, &t34);
                gm_tftb1odc739t5 t43 = t42;
                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcp33bek143l58, 177, 27);
                gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
            }
lc27:;
        }
gm_ll31:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb27; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc27; }
        }
        goto gm_lad;
lb27:;
    }
    {
        uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
        if (o47) gm_trap("integer overflow in `-`", gm_srcp33bek143l58, 178, 25);
        uint64_t t48 = r46;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srcp33bek143l58, 178, 13);
        gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t49 * 136) + 40) + 72));
        gm_l4 = t51;
    }
    {
        void * t52 = gm_l0;
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_t2gtbh39ptekd t54;
        memset(&t54, 0, sizeof t54);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t54 + 8), t53);
        gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_tl874t2atnlh3 t56 = gm_l4;
        gm_tl874t2atnlh3 t57 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t55), GM_CONV(gm_cseahvg8v7p0vs, t56)));
        gm_tc3oht4ksth9d t58;
        gm_f_mk_expr_53qj7nd((void *)&t58, t52, &t54, GM_CONV(gm_cseahvg8v7p0vs, t57));
        gm_to1cssm8dfiv5 t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
        ((uint8_t *)&t59)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t59 + 8), t58);
        gm_to1cssm8dfiv5 t60 = t59;
        uint8_t t61 = ((((uint8_t *)&t60))[0] != 0);
        F.ret = t59;
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
inline __attribute__((aligned(4))) void gm_f_parse_stmt_40aalpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_stmt_40aalpm F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 1045u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_thm5bu4f6031r t5;
        gm_f_parse_stmt_kind_tji0utd((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thm5bu4f6031r t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_tmgpf942vp8ci t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tem8p0t66u1ce t9 = GM_LD(gm_tem8p0t66u1ce, (((uint8_t *)&t6) + 8));
        F.l2 = t9;
    }
    {
        gm_to88ae1b67aev t10;
        memset((uint8_t *)&t10 + 4, 0, 4);
        memset((uint8_t *)&t10 + 140, 0, 4);
        void * t11 = gm_l0;
        uint32_t t12 = gm_f_next_id_a7upaf4(t11);
        GM_ST(uint32_t, (uint8_t *)&t10 + 0, t12);
        memcpy((uint8_t *)&t10 + 8, ((uint8_t *)&F.l2), 120);
        void * t13 = gm_l0;
        uint32_t t14 = gm_l1;
        gm_tl874t2atnlh3 t15 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t13, t14));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t10 + 128, t15);
        gm_tmgpf942vp8ci t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        ((uint8_t *)&t16)[0] = 0;
        GM_ST(gm_to88ae1b67aev, ((uint8_t *)&t16 + 8), t10);
        gm_tmgpf942vp8ci t17 = t16;
        uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
        F.ret = t16;
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
__attribute__((aligned(4))) void gm_f_parse_stmt_kind_tji0utd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_stmt_kind_tji0utd F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* attrs */
    uint8_t gm_l2; /* is_err */
    GM_STACK_CHECK(&F, 7010u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t3fcbuehh3bs2 t3;
        gm_f_kind_k239kpg((void *)&t3, t2);
        gm_t3fcbuehh3bs2 t4 = t3;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; default: goto ma15; }
ma6:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t19 != ((uint8_t)7ULL)) goto mn18;
        {
            void * t21 = gm_l0;
            gm_tt7ek1a8bopcl t22;
            gm_f_parse_attrs_imvfdlq((void *)&t22, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tt7ek1a8bopcl t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) {
                gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
                gm_thm5bu4f6031r t25;
                memset(&t25, 0, sizeof t25);
                ((uint8_t *)&t25)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), t24);
                F.ret = t25;
                goto gm_fret;
            }
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_l1 = t26;
        }
        {
            void * t28 = gm_l0;
            uint8_t t29 = gm_f_binding_ahead_cjedtav(t28);
            uint8_t t30 = (uint8_t)!t29;
            if (!t30) goto ie27;
            {
                void * t32 = gm_l0;
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sl8fmedd9kk64));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(33));
                gm_b0_1 t34 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t32, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_thm5bu4f6031r t35;
                memset(&t35, 0, sizeof t35);
                ((uint8_t *)&t35)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t35 + 8), t34);
                gm_thm5bu4f6031r t36 = t35;
                uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
                F.ret = t35;
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            gm_taiafe1jasg4m t38;
            gm_b16_8 t39 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t39);
            void * t40 = gm_l0;
            gm_ta8qa3lgiolvh t41;
            gm_f_parse_binding_ojaae7v((void *)&t41, (void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ta8qa3lgiolvh t42 = t41;
            if (((((uint8_t *)&t42))[0] != 0)) {
                gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 8));
                gm_thm5bu4f6031r t44;
                memset(&t44, 0, sizeof t44);
                ((uint8_t *)&t44)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t44 + 8), t43);
                F.ret = t44;
                goto gm_fret;
            }
            gm_tv2uf4leil5hq t45 = GM_LD(gm_tv2uf4leil5hq, (((uint8_t *)&t42) + 8));
            GM_ST(gm_tv2uf4leil5hq, (uint8_t *)&t38 + 16, t45);
            gm_tem8p0t66u1ce t46;
            memset(&t46, 0, sizeof t46);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)0ULL));
            GM_ST(gm_taiafe1jasg4m, ((uint8_t *)&t46 + 8), t38);
            gm_thm5bu4f6031r t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            ((uint8_t *)&t47)[0] = 0;
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t47 + 8), t46);
            gm_thm5bu4f6031r t48 = t47;
            uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
            F.ret = t47;
            goto gm_fret;
        }
        goto me5;
mn18:;
ma7:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t53 != ((uint8_t)2ULL)) goto pa52;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t54 != ((uint8_t)5ULL)) goto pa52;
        goto po51;
pa52:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t56 != ((uint8_t)2ULL)) goto pa55;
        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t57 != ((uint8_t)8ULL)) goto pa55;
        goto po51;
pa55:;
        goto mn50;
po51:;
        {
            void * t59 = gm_l0;
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)8ULL));
            uint8_t t61 = gm_f_at_kw_p0r3sr9(t59, t60);
            gm_l2 = t61;
        }
        {
            {
                void * t63 = gm_l0;
                gm_tm4pc0emhrv1g t64;
                gm_f_bump_q2i40re((void *)&t64, t63);
            }
        }
        {
            void * t65 = gm_l0;
            gm_tmgpf942vp8ci t66;
            gm_f_parse_defer_body_log60it((void *)&t66, (void *)NULL, cx, NULL, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tmgpf942vp8ci t67 = t66;
            if (((((uint8_t *)&t67))[0] != 0)) {
                gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 8));
                gm_thm5bu4f6031r t69;
                memset(&t69, 0, sizeof t69);
                ((uint8_t *)&t69)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t69 + 8), t68);
                F.ret = t69;
                goto gm_fret;
            }
            gm_to88ae1b67aev t70 = GM_LD(gm_to88ae1b67aev, (((uint8_t *)&t67) + 8));
            F.l3 = t70;
        }
        {
            uint8_t t71 = gm_l2;
            gm_tq7s98ihonkju t72 = gm_f_new_0oj2e9g((void *)NULL, cx, NULL, &(*(gm_to88ae1b67aev *)((uint8_t *)&F.l3)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k9o9iqv20c0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t73 = t72;
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srcp33bek143l58, 77, 49);
            void * t74 = GM_LD(void *, (((uint8_t *)&t73) + 8));
            gm_tgtub0v94d5u5 t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), t71);
            GM_ST(void *, ((uint8_t *)&t75 + 8), t74);
            gm_tem8p0t66u1ce t76;
            memset(&t76, 0, sizeof t76);
            GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)3ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t76 + 8), t75);
            gm_thm5bu4f6031r t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            ((uint8_t *)&t77)[0] = 0;
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t77 + 8), t76);
            gm_thm5bu4f6031r t78 = t77;
            uint8_t t79 = ((((uint8_t *)&t78))[0] != 0);
            F.ret = t77;
            goto gm_fret;
        }
        goto me5;
mn50:;
ma8:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t81 != ((uint8_t)2ULL)) goto mn80;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t82 != ((uint8_t)29ULL)) goto mn80;
        void * t83 = gm_l0;
        gm_t5q7g12jsm3oo t84;
        gm_f_parse_when_stmt_a9ra0gh((void *)&t84, (void *)NULL, cx, NULL, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5q7g12jsm3oo t85 = t84;
        if (((((uint8_t *)&t85))[0] != 0)) {
            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 8));
            gm_thm5bu4f6031r t87;
            memset(&t87, 0, sizeof t87);
            ((uint8_t *)&t87)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t87 + 8), t86);
            F.ret = t87;
            goto gm_fret;
        }
        gm_tt2g4r14evejs t88 = GM_LD(gm_tt2g4r14evejs, (((uint8_t *)&t85) + 8));
        gm_tq7s98ihonkju t89 = gm_f_new_qlsqbft((void *)NULL, cx, NULL, &t88, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kefd26qglb8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t90 = t89;
        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_srcp33bek143l58, 79, 41);
        void * t91 = GM_LD(void *, (((uint8_t *)&t90) + 8));
        gm_tem8p0t66u1ce t92;
        memset(&t92, 0, sizeof t92);
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t92 + 8), t91);
        gm_thm5bu4f6031r t93;
        GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
        ((uint8_t *)&t93)[0] = 0;
        GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t93 + 8), t92);
        gm_thm5bu4f6031r t94 = t93;
        uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
        F.ret = t93;
        goto gm_fret;
        goto me5;
mn80:;
ma9:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t97 != ((uint8_t)2ULL)) goto mn96;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t98 != ((uint8_t)30ULL)) goto mn96;
        void * t99 = gm_l0;
        gm_t1s75i08qtptd t100;
        memset(&t100, 0, sizeof t100);
        gm_f_parse_while_gvp1mcm((void *)&(*(gm_thm5bu4f6031r *)gm_out), (void *)NULL, cx, NULL, t99, &t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn96:;
ma10:;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t102 != ((uint8_t)2ULL)) goto mn101;
        uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t103 != ((uint8_t)2ULL)) goto mn101;
        {
            {
                void * t106 = gm_l0;
                gm_tm4pc0emhrv1g t107;
                gm_f_bump_q2i40re((void *)&t107, t106);
            }
        }
        {
            void * t108 = gm_l0;
            gm_to1cssm8dfiv5 t109;
            gm_f_parse_expr_bkb2n6e((void *)&t109, (void *)NULL, cx, NULL, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t110 = t109;
            if (((((uint8_t *)&t110))[0] != 0)) {
                gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 8));
                gm_thm5bu4f6031r t112;
                memset(&t112, 0, sizeof t112);
                ((uint8_t *)&t112)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t112 + 8), t111);
                F.ret = t112;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t113 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t110) + 8));
            void * t114 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t113);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tem8p0t66u1ce t115;
            memset(&t115, 0, sizeof t115);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)6ULL));
            GM_ST(void *, ((uint8_t *)&t115 + 8), t114);
            gm_thm5bu4f6031r t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
            ((uint8_t *)&t116)[0] = 0;
            GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t116 + 8), t115);
            gm_thm5bu4f6031r t117 = t116;
            uint8_t t118 = ((((uint8_t *)&t117))[0] != 0);
            F.ret = t116;
            goto gm_fret;
        }
        goto me5;
mn101:;
ma11:;
        uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t120 != ((uint8_t)8ULL)) goto mn119;
        uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t121 != ((uint8_t)65ULL)) goto mn119;
        void * t122 = gm_l0;
        gm_toc4mrkn2e7o9 t123;
        gm_f_parse_block_tpg3f1i((void *)&t123, (void *)NULL, cx, NULL, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t124 = t123;
        if (((((uint8_t *)&t124))[0] != 0)) {
            gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 8));
            gm_thm5bu4f6031r t126;
            memset(&t126, 0, sizeof t126);
            ((uint8_t *)&t126)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t126 + 8), t125);
            F.ret = t126;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t127 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t124) + 8));
        gm_tem8p0t66u1ce t128;
        memset(&t128, 0, sizeof t128);
        GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t128 + 8), t127);
        gm_thm5bu4f6031r t129;
        GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
        ((uint8_t *)&t129)[0] = 0;
        GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t129 + 8), t128);
        gm_thm5bu4f6031r t130 = t129;
        uint8_t t131 = ((((uint8_t *)&t130))[0] != 0);
        F.ret = t129;
        goto gm_fret;
        goto me5;
mn119:;
ma12:;
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t135 != ((uint8_t)0ULL)) goto pa134;
        goto po133;
pa134:;
        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t137 != ((uint8_t)1ULL)) goto pa136;
        goto po133;
pa136:;
        goto mn132;
po133:;
        void * t138 = gm_l0;
        gm_f_parse_name_stmt_nb6mtgu((void *)&(*(gm_thm5bu4f6031r *)gm_out), (void *)NULL, cx, NULL, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn132:;
ma13:;
        uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t140 != ((uint8_t)2ULL)) goto mn139;
        void * t141 = gm_l0;
        uint8_t t142 = gm_f_binding_ahead_cjedtav(t141);
        if (!t142) goto mn139;
        gm_taiafe1jasg4m t143;
        gm_b16_8 t144 = gm_f_empty_slice_atlcs4j();
        GM_ST(gm_b16_8, (uint8_t *)&t143 + 0, t144);
        void * t145 = gm_l0;
        gm_ta8qa3lgiolvh t146;
        gm_f_parse_binding_ojaae7v((void *)&t146, (void *)NULL, cx, NULL, t145);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ta8qa3lgiolvh t147 = t146;
        if (((((uint8_t *)&t147))[0] != 0)) {
            gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 8));
            gm_thm5bu4f6031r t149;
            memset(&t149, 0, sizeof t149);
            ((uint8_t *)&t149)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t149 + 8), t148);
            F.ret = t149;
            goto gm_fret;
        }
        gm_tv2uf4leil5hq t150 = GM_LD(gm_tv2uf4leil5hq, (((uint8_t *)&t147) + 8));
        GM_ST(gm_tv2uf4leil5hq, (uint8_t *)&t143 + 16, t150);
        gm_tem8p0t66u1ce t151;
        memset(&t151, 0, sizeof t151);
        GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)0ULL));
        GM_ST(gm_taiafe1jasg4m, ((uint8_t *)&t151 + 8), t143);
        gm_thm5bu4f6031r t152;
        GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
        ((uint8_t *)&t152)[0] = 0;
        GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t152 + 8), t151);
        gm_thm5bu4f6031r t153 = t152;
        uint8_t t154 = ((((uint8_t *)&t153))[0] != 0);
        F.ret = t152;
        goto gm_fret;
        goto me5;
mn139:;
ma14:;
        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t158 != ((uint8_t)8ULL)) goto pa157;
        uint8_t t159 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t159 != ((uint8_t)58ULL)) goto pa157;
        goto po156;
pa157:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t161 != ((uint8_t)8ULL)) goto pa160;
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t162 != ((uint8_t)46ULL)) goto pa160;
        goto po156;
pa160:;
        uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t164 != ((uint8_t)8ULL)) goto pa163;
        uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t165 != ((uint8_t)61ULL)) goto pa163;
        goto po156;
pa163:;
        uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t167 != ((uint8_t)8ULL)) goto pa166;
        uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        if (t168 != ((uint8_t)1ULL)) goto pa166;
        goto po156;
pa166:;
        uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t170 != ((uint8_t)3ULL)) goto pa169;
        goto po156;
pa169:;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t172 != ((uint8_t)5ULL)) goto pa171;
        goto po156;
pa171:;
        goto mn155;
po156:;
        {
            void * t175 = gm_l0;
            gm_taeav3snkvh6h t176;
            gm_f_parse_let_else_e2n01r7((void *)&t176, (void *)NULL, cx, NULL, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_taeav3snkvh6h t177 = t176;
            if (((((uint8_t *)&t177))[0] != 0)) {
                gm_b0_1 t178 = GM_LD(gm_b0_1, (((uint8_t *)&t177) + 8));
                gm_thm5bu4f6031r t179;
                memset(&t179, 0, sizeof t179);
                ((uint8_t *)&t179)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t179 + 8), t178);
                F.ret = t179;
                goto gm_fret;
            }
            gm_t5821lde3dhck t180 = GM_LD(gm_t5821lde3dhck, (((uint8_t *)&t177) + 8));
            gm_t5821lde3dhck t181 = t180;
            if (!((((uint8_t *)&t181))[120] != 0)) goto ie174;
            gm_tem8p0t66u1ce t182 = GM_LD(gm_tem8p0t66u1ce, ((uint8_t *)&t181));
            F.l4 = t182;
            {
                gm_tem8p0t66u1ce t184 = F.l4;
                gm_thm5bu4f6031r t185;
                GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t185 + 4, 0);
                ((uint8_t *)&t185)[0] = 0;
                GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t185 + 8), t184);
                gm_thm5bu4f6031r t186 = t185;
                uint8_t t187 = ((((uint8_t *)&t186))[0] != 0);
                F.ret = t185;
                goto gm_fret;
            }
            goto ix174;
ie174:;
ix174:;
        }
        {
            void * t188 = gm_l0;
            gm_f_parse_expr_or_assign_btc70ah((void *)&(*(gm_thm5bu4f6031r *)gm_out), (void *)NULL, cx, NULL, t188);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn155:;
ma15:;
        void * t190 = gm_l0;
        gm_f_parse_expr_or_assign_btc70ah((void *)&(*(gm_thm5bu4f6031r *)gm_out), (void *)NULL, cx, NULL, t190);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me5;
mn189:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srcp33bek143l58, 67, 5);
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
inline __attribute__((aligned(4))) void gm_f_parse_when_stmt_a9ra0gh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_when_stmt_a9ra0gh F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* cond */
    GM_STACK_CHECK(&F, 2301u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_tm4pc0emhrv1g t6;
            gm_f_bump_q2i40re((void *)&t6, t5);
        }
    }
    {
        void * t7 = gm_l0;
        gm_to1cssm8dfiv5 t8;
        gm_f_parse_head_expr_6sofdhn((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 8));
            gm_t5q7g12jsm3oo t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t9) + 8));
        void * t13 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t13;
    }
    {
        void * t14 = gm_l0;
        gm_toc4mrkn2e7o9 t15;
        gm_f_parse_block_tpg3f1i((void *)&t15, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
            gm_t5q7g12jsm3oo t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t19 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t16) + 8));
        F.l3 = t19;
    }
    {
        gm_ttrm2ubqha4nn t20;
        memset(&t20, 0, sizeof t20);
        F.l4 = t20;
    }
    {
        void * t22 = gm_l0;
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)7ULL));
        uint8_t t24 = gm_f_eat_kw_tvv0v3e(t22, t23);
        if (!t24) goto ie21;
        {
            void * t27 = gm_l0;
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)29ULL));
            uint8_t t29 = gm_f_at_kw_p0r3sr9(t27, t28);
            if (!t29) goto ie26;
            {
                void * t31 = gm_l0;
                gm_t5q7g12jsm3oo t32;
                gm_f_parse_when_stmt_a9ra0gh((void *)&t32, (void *)NULL, cx, NULL, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5q7g12jsm3oo t33 = t32;
                if (((((uint8_t *)&t33))[0] != 0)) {
                    gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 8));
                    gm_t5q7g12jsm3oo t35;
                    memset(&t35, 0, sizeof t35);
                    ((uint8_t *)&t35)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t35 + 8), t34);
                    F.ret = t35;
                    goto gm_fret;
                }
                gm_tt2g4r14evejs t36 = GM_LD(gm_tt2g4r14evejs, (((uint8_t *)&t33) + 8));
                gm_tq7s98ihonkju t37 = gm_f_new_qlsqbft((void *)NULL, cx, NULL, &t36, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kli6567itlv)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t38 = t37;
                if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_srcp33bek143l58, 213, 39);
                void * t39 = GM_LD(void *, (((uint8_t *)&t38) + 8));
                gm_tapdr2ag17fh7 t40;
                memset(&t40, 0, sizeof t40);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)1ULL));
                GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
                gm_ttrm2ubqha4nn t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 52, 0);
                GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t41), t40);
                ((uint8_t *)&t41)[48] = 1;
                F.l4 = t41;
            }
            goto ix26;
ie26:;
            {
                void * t43 = gm_l0;
                gm_toc4mrkn2e7o9 t44;
                gm_f_parse_block_tpg3f1i((void *)&t44, (void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_toc4mrkn2e7o9 t45 = t44;
                if (((((uint8_t *)&t45))[0] != 0)) {
                    gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 8));
                    gm_t5q7g12jsm3oo t47;
                    memset(&t47, 0, sizeof t47);
                    ((uint8_t *)&t47)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t47 + 8), t46);
                    F.ret = t47;
                    goto gm_fret;
                }
                gm_t79rbdv4rafe5 t48 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t45) + 8));
                gm_tapdr2ag17fh7 t49;
                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t49 + 8), t48);
                gm_ttrm2ubqha4nn t50;
                GM_ST(uint8_t, (uint8_t *)&t50 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 52, 0);
                GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t50), t49);
                ((uint8_t *)&t50)[48] = 1;
                F.l4 = t50;
            }
ix26:;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_tt2g4r14evejs t51;
        memset((uint8_t *)&t51 + 116, 0, 4);
        void * t52 = gm_l2;
        GM_ST(void *, (uint8_t *)&t51 + 0, t52);
        memcpy((uint8_t *)&t51 + 8, ((uint8_t *)&F.l3), 40);
        memcpy((uint8_t *)&t51 + 48, ((uint8_t *)&F.l4), 56);
        void * t53 = gm_l0;
        uint32_t t54 = gm_l1;
        gm_tl874t2atnlh3 t55 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t53, t54));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t51 + 104, t55);
        gm_t5q7g12jsm3oo t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
        ((uint8_t *)&t56)[0] = 0;
        GM_ST(gm_tt2g4r14evejs, ((uint8_t *)&t56 + 8), t51);
        gm_t5q7g12jsm3oo t57 = t56;
        uint8_t t58 = ((((uint8_t *)&t57))[0] != 0);
        F.ret = t56;
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
inline __attribute__((aligned(4))) void gm_f_parse_while_gvp1mcm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_while_gvp1mcm F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l2; /* cond */
    GM_STACK_CHECK(&F, 1243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        {
            void * t3 = gm_l0;
            gm_tm4pc0emhrv1g t4;
            gm_f_bump_q2i40re((void *)&t4, t3);
        }
    }
    {
        void * t5 = gm_l0;
        gm_t4jn8fl3lmk5k t6;
        gm_f_parse_cond_6sk4htm((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 8));
            gm_thm5bu4f6031r t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t10 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t7) + 8));
        gm_l2 = t10;
    }
    {
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12 = gm_f_reject_assign_after_cond_utj4j69((void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
            gm_thm5bu4f6031r t15;
            memset(&t15, 0, sizeof t15);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        void * t17 = gm_l0;
        gm_toc4mrkn2e7o9 t18;
        gm_f_parse_labeled_block_s1cpmi0((void *)&t18, (void *)NULL, cx, NULL, t17, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 8));
            gm_thm5bu4f6031r t21;
            memset(&t21, 0, sizeof t21);
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t21 + 8), t20);
            F.ret = t21;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t22 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t19) + 8));
        F.l3 = t22;
    }
    {
        gm_t43d6nbdfap6p t23;
        memcpy((uint8_t *)&t23 + 0, ((uint8_t *)&F.l1), 40);
        memcpy((uint8_t *)&t23 + 40, ((uint8_t *)&gm_l2), 32);
        memcpy((uint8_t *)&t23 + 72, ((uint8_t *)&F.l3), 40);
        gm_tem8p0t66u1ce t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)5ULL));
        GM_ST(gm_t43d6nbdfap6p, ((uint8_t *)&t24 + 8), t23);
        gm_thm5bu4f6031r t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        ((uint8_t *)&t25)[0] = 0;
        GM_ST(gm_tem8p0t66u1ce, ((uint8_t *)&t25 + 8), t24);
        gm_thm5bu4f6031r t26 = t25;
        uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
        F.ret = t25;
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
__attribute__((aligned(4))) void gm_f_parse_with_hlmj8pt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_with_hlmj8pt F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l2; /* l */
    uint32_t gm_l3; /* start */
    void * gm_l5; /* on */
    gm_tl874t2atnlh3 gm_l7; /* whole */
    GM_STACK_CHECK(&F, 2280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2;
        if (!((((uint8_t *)&F.l1))[32] != 0)) goto ie3;
        gm_t08olq97rscn3 t4 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&F.l1));
        gm_l2 = t4;
        uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 4));
        uint32_t t7 = t6;
        t2 = t7;
        goto ix3;
ie3:;
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        uint32_t t11 = t10;
        t2 = t11;
ix3:;
        gm_l3 = t2;
    }
    {
        {
            void * t13 = gm_l0;
            gm_tm4pc0emhrv1g t14;
            gm_f_bump_q2i40re((void *)&t14, t13);
        }
    }
    {
        gm_f_list_vp1djv1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = (void *)((uint8_t *)&F.l4);
        void * t16 = gm_l0;
        gm_to1cssm8dfiv5 t17;
        gm_f_parse_head_expr_6sofdhn((void *)&t17, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) {
            gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 8));
            gm_to1cssm8dfiv5 t20;
            memset(&t20, 0, sizeof t20);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t20 + 8), t19);
            F.ret = t20;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t21 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t18) + 8));
        gm_tftb1odc739t5 t22 = gm_f_push_d943vgp(t15, &t21);
        gm_tftb1odc739t5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcp33bek143l58, 351, 5);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
    }
    {
        for (;;) {
            void * t26 = gm_l0;
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)57ULL));
            uint8_t t28 = gm_f_eat_qmock9g(t26, t27);
            if (!t28) goto lb25;
            {
                void * t31 = (void *)((uint8_t *)&F.l4);
                void * t32 = gm_l0;
                gm_to1cssm8dfiv5 t33;
                gm_f_parse_head_expr_6sofdhn((void *)&t33, (void *)NULL, cx, NULL, t32);
                if (gm_unw.kind) {
                    goto gm_ll29;
                }
                gm_to1cssm8dfiv5 t34 = t33;
                if (((((uint8_t *)&t34))[0] != 0)) {
                    gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 8));
                    gm_to1cssm8dfiv5 t36;
                    memset(&t36, 0, sizeof t36);
                    ((uint8_t *)&t36)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t36 + 8), t35);
                    F.ret = t36;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t37 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t34) + 8));
                gm_tftb1odc739t5 t38 = gm_f_push_d943vgp(t31, &t37);
                gm_tftb1odc739t5 t39 = t38;
                if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcp33bek143l58, 352, 27);
                gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
            }
lc25:;
        }
gm_ll29:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
        }
        goto gm_lad;
lb25:;
    }
    {
        gm_l5 = ((void *)0);
    }
    {
        void * t42 = gm_l0;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sdl8mgtvebfcs));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
        uint8_t t44 = gm_f_at_word_18dnh5f(t42, t43);
        if (!t44) goto ie41;
        {
            {
                void * t47 = gm_l0;
                gm_tm4pc0emhrv1g t48;
                gm_f_bump_q2i40re((void *)&t48, t47);
            }
        }
        {
            void * t49 = gm_l0;
            gm_to1cssm8dfiv5 t50;
            gm_f_parse_head_expr_6sofdhn((void *)&t50, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t51 = t50;
            if (((((uint8_t *)&t51))[0] != 0)) {
                gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 8));
                gm_to1cssm8dfiv5 t53;
                memset(&t53, 0, sizeof t53);
                ((uint8_t *)&t53)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t53 + 8), t52);
                F.ret = t53;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t54 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t51) + 8));
            void * t55 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t55;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t56 = gm_l0;
        gm_toc4mrkn2e7o9 t57;
        gm_f_parse_labeled_block_s1cpmi0((void *)&t57, (void *)NULL, cx, NULL, t56, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) {
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 8));
            gm_to1cssm8dfiv5 t60;
            memset(&t60, 0, sizeof t60);
            ((uint8_t *)&t60)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t60 + 8), t59);
            F.ret = t60;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t61 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t58) + 8));
        F.l6 = t61;
    }
    {
        void * t62 = gm_l0;
        uint32_t t63 = gm_l3;
        gm_tl874t2atnlh3 t64 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t62, t63));
        gm_l7 = t64;
    }
    {
        void * t65 = gm_l0;
        gm_t0l6vh8eupt26 t66;
        memset((uint8_t *)&t66 + 116, 0, 4);
        memcpy((uint8_t *)&t66 + 0, ((uint8_t *)&F.l1), 40);
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t66 + 40, t67);
        void * t68 = gm_l5;
        GM_ST(void *, (uint8_t *)&t66 + 56, t68);
        memcpy((uint8_t *)&t66 + 64, ((uint8_t *)&F.l6), 40);
        gm_tl874t2atnlh3 t69 = gm_l7;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t66 + 104, t69);
        gm_tq7s98ihonkju t70 = gm_f_new_7hdc0s2((void *)NULL, cx, NULL, &t66, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ki0a6rpk4hh)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t71 = t70;
        if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srcp33bek143l58, 360, 28);
        void * t72 = GM_LD(void *, (((uint8_t *)&t71) + 8));
        gm_t2gtbh39ptekd t73;
        memset(&t73, 0, sizeof t73);
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)42ULL));
        GM_ST(void *, ((uint8_t *)&t73 + 8), t72);
        gm_tl874t2atnlh3 t74 = gm_l7;
        gm_tc3oht4ksth9d t75;
        gm_f_mk_expr_53qj7nd((void *)&t75, t65, &t73, GM_CONV(gm_cseahvg8v7p0vs, t74));
        gm_to1cssm8dfiv5 t76;
        GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
        ((uint8_t *)&t76)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t76 + 8), t75);
        gm_to1cssm8dfiv5 t77 = t76;
        uint8_t t78 = ((((uint8_t *)&t77))[0] != 0);
        F.ret = t76;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_reject_assign_after_cond_utj4j69(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_reject_assign_after_cond_utj4j69 F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    GM_STACK_CHECK(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)22ULL));
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            gm_b0_1 t8;
            gm_t9qkh0rsooc8d t9;
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t9 + 1), t8);
            gm_t9qkh0rsooc8d t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_tl874t2atnlh3 t13 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t12));
        gm_l1 = t13;
    }
    {
        gm_tl874t2atnlh3 t14 = gm_l1;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s7rk1k9jrjscv));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(80));
        gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t16 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2fp7iapio2lo));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(10));
        gm_tl874t2atnlh3 t18 = gm_l1;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sgpdmvc9s0d2p));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
        gm_b0_1 t20 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t16, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l0;
        gm_b0_1 t22 = gm_f_fail_diag_u0q4kp3(t21, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l2)));
        gm_t9qkh0rsooc8d t23;
        ((uint8_t *)&t23)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t23 + 1), t22);
        gm_t9qkh0rsooc8d t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
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

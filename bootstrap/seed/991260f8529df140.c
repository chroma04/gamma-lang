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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_t8er5olt969ip;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_b16_8 f11; } gm_th4d8o29g5urm;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { uint64_t f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tkb8hpcqf70t1;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_tjcj4j2gbh6c8 f0; uint8_t f1; uint8_t pe[1]; } gm_tp8mv052i18rg;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8er5olt969ip) == 28 && _Alignof(gm_t8er5olt969ip) == 4 && offsetof(gm_t8er5olt969ip, f0) == 0 && offsetof(gm_t8er5olt969ip, f1) == 4 && offsetof(gm_t8er5olt969ip, f2) == 8 && offsetof(gm_t8er5olt969ip, f3) == 12 && offsetof(gm_t8er5olt969ip, f4) == 16, "layout of `Deferred`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th4d8o29g5urm) == 192 && _Alignof(gm_th4d8o29g5urm) == 8 && offsetof(gm_th4d8o29g5urm, f0) == 0 && offsetof(gm_th4d8o29g5urm, f1) == 16 && offsetof(gm_th4d8o29g5urm, f2) == 32 && offsetof(gm_th4d8o29g5urm, f3) == 48 && offsetof(gm_th4d8o29g5urm, f4) == 64 && offsetof(gm_th4d8o29g5urm, f5) == 80 && offsetof(gm_th4d8o29g5urm, f6) == 96 && offsetof(gm_th4d8o29g5urm, f7) == 112 && offsetof(gm_th4d8o29g5urm, f8) == 128 && offsetof(gm_th4d8o29g5urm, f9) == 144 && offsetof(gm_th4d8o29g5urm, f10) == 160 && offsetof(gm_th4d8o29g5urm, f11) == 176, "layout of `(string, string, string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkb8hpcqf70t1) == 24 && _Alignof(gm_tkb8hpcqf70t1) == 8 && offsetof(gm_tkb8hpcqf70t1, f0) == 0 && offsetof(gm_tkb8hpcqf70t1, f1) == 8, "layout of `NamedDecl`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
typedef struct { gm_tkb8hpcqf70t1 f0; uint8_t f1; uint8_t pe[7]; } gm_toblvhepnjqkh;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tp8mv052i18rg) == 6 && _Alignof(gm_tp8mv052i18rg) == 2 && offsetof(gm_tp8mv052i18rg, f0) == 0 && offsetof(gm_tp8mv052i18rg, f1) == 4, "layout of `?(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_toblvhepnjqkh) == 32 && _Alignof(gm_toblvhepnjqkh) == 8 && offsetof(gm_toblvhepnjqkh, f0) == 0 && offsetof(gm_toblvhepnjqkh, f1) == 24, "layout of `?NamedDecl`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
typedef struct { gm_t428ue2u3hf5g f0; uint8_t f1; uint8_t pe[7]; } gm_trgmlgt2gmmjs;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_trgmlgt2gmmjs) == 40 && _Alignof(gm_trgmlgt2gmmjs) == 8 && offsetof(gm_trgmlgt2gmmjs, f0) == 0 && offsetof(gm_trgmlgt2gmmjs, f1) == 32, "layout of `?ConstKey`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
typedef struct { gm_tb07pp6njg0bk f0; uint8_t f1; uint8_t pe[7]; } gm_tk1gen3hgkm51;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
_Static_assert(sizeof(gm_tk1gen3hgkm51) == 232 && _Alignof(gm_tk1gen3hgkm51) == 8 && offsetof(gm_tk1gen3hgkm51, f0) == 0 && offsetof(gm_tk1gen3hgkm51, f1) == 224, "layout of `?LocalConst`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_array_len_eielif2 {
    gm_frame h;
    gm_t0ialelq3ql1k l6; /* v */
    gm_tbci5ospk5nnn l7; /* i */
    gm_t5e6kkuq21nig ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_type_64h714c {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_calls_on_value_q0pa6h5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_len_exprs_m8s6m0h {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* names */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_component_type_q9ge5lo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_comptime_value_l5orc93 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_key_of_q36m6nm {
    gm_frame h;
    gm_t5vbkdq81qfs7 l9; /* r */
    gm_t0ialelq3ql1k l10; /* v */
    gm_tbci5ospk5nnn l11; /* i */
    gm_trqfvmjtt4sij l13; /* bytes */
    gm_trgmlgt2gmmjs ret;
};
struct gm_fr_construction_call_ktqtrpo {
    gm_frame h;
    void * ret;
};
struct gm_fr_decl_is_type_oh0cpn9 {
    gm_frame h;
    gm_t6qooom8qa2do l3; /* v */
    gm_tv2uf4leil5hq l5; /* b */
    uint8_t ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_1l2vi9a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_enum_array_type_0sic6t0 {
    gm_frame h;
    gm_tckdobunaulee l8; /* v */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fn_ref_7alnaqh {
    gm_frame h;
    gm_tddtjfggdat0d l6; /* sig */
    gm_tddtjfggdat0d l17; /* have */
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_ty_qaih7ts {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_generic_decl_oir9hm6 {
    gm_frame h;
    gm_toblvhepnjqkh ret;
};
struct gm_fr_generic_type_args_5b8bvov {
    gm_frame h;
    gm_t6art7bprjiqb l5; /* info */
    gm_trqfvmjtt4sij l8; /* given */
    gm_t7te2tdpebbt4 l14; /* a */
    gm_t0mfir12leg2g l16; /* ep */
    gm_tc3oht4ksth9d l17; /* x */
    gm_tc3oht4ksth9d l19; /* x */
    gm_trqfvmjtt4sij l25; /* out */
    gm_t0mfir12leg2g l30; /* ep */
    gm_tc3oht4ksth9d l32; /* g */
    gm_tc3oht4ksth9d l34; /* ex */
    gm_trqfvmjtt4sij l37; /* map */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_hoists_13k2a2s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_type_name_a77a0kf {
    gm_frame h;
    gm_tp8mv052i18rg ret;
};
struct gm_fr_is_builtin_type_name_v5ttoka {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_oversized_int_name_nr25haf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_path_qa34i5j {
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
struct gm_fr_len_param_names_cilm68f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_len_params_bound_f7mjvn9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_length_is_type_soi53ah {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9tqbks7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
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
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_const_idpokjh {
    gm_frame h;
    gm_tk1gen3hgkm51 ret;
};
struct gm_fr_local_fn_aji8qnd {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_looks_like_type_ho5qv1k {
    gm_frame h;
    gm_tv1o1j44ii4db l11; /* el */
    uint8_t ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_names_error_oet8t6b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_names_type_rjdgib9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_prefix_operand_3n5jchh {
    gm_frame h;
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_down_hge098p {
    gm_frame h;
    gm_t2gtbh39ptekd l21; /* kind */
    void * ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pushes_down_0djaflp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_type_jjodbei {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_resolve_type_unchecked_psme7dn {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_trqfvmjtt4sij l13; /* edits */
    gm_trqfvmjtt4sij l18; /* ts */
    gm_tv1o1j44ii4db l21; /* el */
    gm_trqfvmjtt4sij l22; /* names */
    gm_tv1o1j44ii4db l25; /* el */
    gm_tqqhoivahirr0 l35; /* f */
    gm_tqqhoivahirr0 l53; /* f */
    gm_tkacitepg5fk0 l82; /* a */
    gm_tet9guhev5r4u l84; /* i */
    gm_tc3oht4ksth9d l86; /* x */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_syntax_is_type_pe6ugv2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_syntax_module_vho1001 {
    gm_frame h;
    gm_t6qooom8qa2do l9; /* v */
    gm_tv2uf4leil5hq l13; /* b */
    gm_tf5srh4mp4b3i ret;
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
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_type_param_3av4r8i {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_value_param_tof1h6t {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_with_operand_ajbkgld {
    gm_frame h;
    gm_t2gtbh39ptekd l10; /* kind */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_callconv_fn_ptr_l9kavc3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_6nmb1bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bnsf58k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bunqlnl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c3isg16(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_il5kbp4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l0aimgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o2s8o9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o5o5km2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oq3rvor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pplcoh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qmamljh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rd9dflp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sv91363(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uhvct7c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_um7orcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_up1v1ae(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_afk8dfq(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k0n7eot(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_untyped_if_plain_lbrp24p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3n1bpabq7f[40] __attribute__((aligned(8)));
extern uint8_t gm_k6r9cr7441u[40] __attribute__((aligned(8)));
extern uint8_t gm_k8b8p636a5m[40] __attribute__((aligned(8)));
extern uint8_t gm_kijoa5b5euq[40] __attribute__((aligned(8)));
extern uint8_t gm_klh5uk0bvu4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s2328gjukbu28[16];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s33hvuvcdjs1b[11];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9rkpo2ljmui9[50];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfpopfqbubj89[35];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgrpmaenv85i1[73];
extern const uint8_t gm_sgva3apqq9jo2[68];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_sjdn5gs7lu39i[57];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_sldikqa9i70sm[52];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm7179tmr0nhi[27];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_sm9funvq1222p[44];
extern const uint8_t gm_smi7k9v6u7cmo[11];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_ssj1a8k9nspq4[82];
extern const uint8_t gm_stevggi5tvq7c[18];
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src6lm8esmv16p1, 176, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
        void * t11 = gm_l3;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_l2;
        uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    void * t5 = gm_l4;
    gm_t88rcucg1b7uk t6 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t1, t2, t3, t4, t5, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&t1));
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1l2vi9a F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 96)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_hoists_13k2a2s(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        void * t10 = gm_f_prefix_operand_3n5jchh(t9);
        void * t11 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
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
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_list_9tqbks7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9tqbks7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1l2vi9a();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_name_rbnop5l F;
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
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 256) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 290, 61);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 256) + 0)) + t3 * 16));
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        gm_tep2drsl9408b t4;
        memset((uint8_t *)&t4 + 20, 0, 4);
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_t88rcucg1b7uk t6 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_hv483nm(t3, &t4);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcsffver18si54, 63, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 89u);
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
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
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
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        unsigned __int128 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_tf5srh4mp4b3i t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        gm_l1 = t3;
    }
    {
        unsigned __int128 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8;
        uint64_t t9 = (uint64_t)t7;
        gm_tf5srh4mp4b3i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[8] = 1;
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
        uint8_t t12 = 0;
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)6ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 176u);
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
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma3; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    t1 = ((uint8_t)1);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_same_package_vmps24r(t12, t13, t14);
    t1 = t15;
    goto me2;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l3;
    uint8_t t20 = ((t18 == t19));
    t1 = t20;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 168, 103);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_array_len_eielif2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint32_t gm_l4; /* pid */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint64_t gm_l8; /* n */
    void * gm_l9; /* hook */
    GM_STACK_CHECK(&F, 1224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l3 = t5;
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_t9805qtonbsag t10 = gm_f_value_param_tof1h6t(t8, t9);
            if (!((((uint8_t *)&t10))[4] != 0)) goto ie7;
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&t10));
            gm_l4 = t11;
            {
                uint32_t t14 = gm_l4;
                uint64_t t15 = (uint64_t)t14;
                uint64_t t16 = (uintptr_t)t15;
                void * t17 = gm_l0;
                uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1128) + 0) + 8);
                if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcsffver18si54, 634, 26);
                uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1128) + 0)) + t16 * 48) + 0) + 0));
                if (t19 != ((uint8_t)2ULL)) goto ie13;
                gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1128) + 0)) + t16 * 48) + 0) + 4));
                gm_l5 = t20;
                {
                    void * t23 = gm_l0;
                    void * t24 = (void *)(((uint8_t *)t23) + 8);
                    gm_t88rcucg1b7uk t25 = gm_l5;
                    uint8_t t26 = gm_f_is_integer_8d4f9ek(t24, GM_CONV(gm_csguankko4lmhd, t25));
                    uint8_t t27 = (uint8_t)!t26;
                    if (!t27) goto ie22;
                    {
                        void * t29 = gm_l0;
                        void * t30 = gm_l2;
                        gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                        gm_tnpifv20irnph t33;
                        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                        gm_tabb710d9mq2e t34;
                        gm_f_format_bunqlnl((void *)&t34, (void *)NULL, cx, NULL, t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcsffver18si54, 636, 33);
                        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                        gm_b0_1 t37 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t29, t31, t36);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_t5e6kkuq21nig t38;
                        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t38 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)2ULL));
                        uint8_t t39 = 0;
                        F.ret = t38;
                        goto gm_fret;
                    }
                    goto ix22;
ie22:;
ix22:;
                }
                {
                    void * t40 = gm_l0;
                    void * t41 = (void *)(((uint8_t *)t40) + 8);
                    uint32_t t42 = gm_l4;
                    gm_t88rcucg1b7uk t43 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t41, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5e6kkuq21nig t44;
                    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44 + 8), t43);
                    uint8_t t45 = 0;
                    F.ret = t44;
                    goto gm_fret;
                }
                goto ix13;
ie13:;
                {
                    gm_t5e6kkuq21nig t47;
                    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t47 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)2ULL));
                    uint8_t t48 = 0;
                    F.ret = t47;
                    goto gm_fret;
                }
ix13:;
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
        void * t49 = gm_l0;
        uint64_t t50 = gm_l1;
        void * t51 = gm_l2;
        gm_tkv1vap90vht2 t52;
        gm_f_try_untyped_fj0h8i5((void *)&t52, (void *)NULL, cx, NULL, t49, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t52))[208] != 0)) goto mn54;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
        if (t55 != ((uint8_t)0ULL)) goto mn54;
        F.l6 = (*(gm_t0ialelq3ql1k *)(((uint8_t *)&t52) + 8));
        {
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t58 != ((uint8_t)0ULL)) goto ie57;
            gm_tbci5ospk5nnn t59 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 8));
            F.l7 = t59;
            {
                gm_tf5srh4mp4b3i t62 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (!((((uint8_t *)&t62))[8] != 0)) goto ie61;
                uint64_t t63 = GM_LD(uint64_t, ((uint8_t *)&t62));
                gm_l8 = t63;
                {
                    uint64_t t65 = gm_l8;
                    gm_t5e6kkuq21nig t66;
                    GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t66 + 8), t65);
                    uint8_t t67 = 0;
                    F.ret = t66;
                    goto gm_fret;
                }
                goto ix61;
ie61:;
ix61:;
            }
            {
                void * t68 = gm_l0;
                void * t69 = gm_l2;
                gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72));
                gm_b16_8 t71 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t72;
                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
                gm_tabb710d9mq2e t73;
                gm_f_format_uhvct7c((void *)&t73, (void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srcsffver18si54, 646, 33);
                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_b0_1 t76 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t68, t70, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t5e6kkuq21nig t77;
                GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)2ULL));
                uint8_t t78 = 0;
                F.ret = t77;
                goto gm_fret;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t79 = gm_l0;
            void * t80 = gm_l2;
            gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t80) + 72));
            void * t82 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t83 = gm_f_describe_bm129vh(t82);
            gm_tnpifv20irnph t84;
            GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
            gm_tabb710d9mq2e t85;
            gm_f_format_up1v1ae((void *)&t85, (void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srcsffver18si54, 649, 29);
            gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
            gm_b0_1 t88 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t79, t81, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t5e6kkuq21nig t89;
            GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)2ULL));
            uint8_t t90 = 0;
            F.ret = t89;
            goto gm_fret;
        }
        goto me53;
mn54:;
        if (!((((uint8_t *)&t52))[208] != 0)) goto mn91;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
        if (t92 != ((uint8_t)1ULL)) goto mn91;
        gm_t5e6kkuq21nig t93;
        GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t93 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)2ULL));
        uint8_t t94 = 0;
        F.ret = t93;
        goto gm_fret;
        goto me53;
mn91:;
        if (((((uint8_t *)&t52))[208] != 0)) goto mn95;
        {
            void * t97 = gm_l0;
            void * t98 = GM_LD(void *, (((uint8_t *)t97) + 2176));
            void * t99;
            if ((GM_LD(void *, ((uint8_t *)&t98)) != 0)) {
                void * t100 = GM_LD(void *, (((uint8_t *)&t98) + 0));
                t99 = t100;
            } else {
                {
                    void * t102 = gm_l0;
                    void * t103 = gm_l2;
                    gm_tl874t2atnlh3 t104 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t103) + 72));
                    gm_b16_8 t105;
                    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_s9rkpo2ljmui9));
                    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(49));
                    gm_b0_1 t106 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t102, t104, t105);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_t5e6kkuq21nig t107;
                GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t107 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)2ULL));
                uint8_t t108 = 0;
                F.ret = t107;
                goto gm_fret;
            }
            gm_l9 = t99;
        }
        {
            void * t109 = gm_l9;
            void * t110 = gm_l0;
            uint64_t t111 = gm_l1;
            void * t112 = gm_l2;
            void * t113 = t109;
            gm_t5e6kkuq21nig t114;
            if (GM_PTR_TAGGED(t113)) {
                t114 = ((gm_t5e6kkuq21nig (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t113))(t110, t111, t112);
            } else {
                t114 = ((gm_t5e6kkuq21nig (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t113)(NULL, cx, NULL, t110, t111, t112);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t115 = 0;
            F.ret = t114;
            goto gm_fret;
        }
        goto me53;
mn95:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 642, 5);
me53:;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_builtin_type_64h714c F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tjcj4j2gbh6c8 gm_l2; /* i */
    GM_STACK_CHECK(&F, 755u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l1;
        gm_tp8mv052i18rg t4 = gm_f_int_type_name_a77a0kf(t3);
        if (!((((uint8_t *)&t4))[4] != 0)) goto ie2;
        gm_tjcj4j2gbh6c8 t5 = GM_LD(gm_tjcj4j2gbh6c8, ((uint8_t *)&t4));
        gm_l2 = t5;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint16_t t10 = GM_LD(uint16_t, (((uint8_t *)&gm_l2) + 2));
            gm_t88rcucg1b7uk t11 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[4] = 1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tkn42u7na3t81 t14;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s6groq37ram3h, 5) == 0)) goto mn16;
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 8);
        gm_t88rcucg1b7uk t19 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[4] = 1;
        t14 = t20;
        goto me15;
mn16:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s0i3i86nphpsh, 5) == 0)) goto mn21;
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 8);
        gm_t53at80pfdus3 t24;
        memset(&t24, 0, sizeof t24);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
        gm_t88rcucg1b7uk t25 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t23, &t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[4] = 1;
        t14 = t26;
        goto me15;
mn21:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_shnig5tqtdedi, 3) == 0)) goto mn27;
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 8);
        gm_t53at80pfdus3 t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t30 + 8), ((uint16_t)16ULL));
        gm_t88rcucg1b7uk t31 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t29, &t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32), t31);
        ((uint8_t *)&t32)[4] = 1;
        t14 = t32;
        goto me15;
mn27:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sm9e81djuncnu, 3) == 0)) goto mn33;
        void * t34 = gm_l0;
        void * t35 = (void *)(((uint8_t *)t34) + 8);
        gm_t53at80pfdus3 t36;
        memset(&t36, 0, sizeof t36);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t36 + 8), ((uint16_t)32ULL));
        gm_t88rcucg1b7uk t37 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t35, &t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38), t37);
        ((uint8_t *)&t38)[4] = 1;
        t14 = t38;
        goto me15;
mn33:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sqqajgod2bb4o, 3) == 0)) goto mn39;
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_t53at80pfdus3 t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t42 + 8), ((uint16_t)64ULL));
        gm_t88rcucg1b7uk t43 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t41, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t44 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44), t43);
        ((uint8_t *)&t44)[4] = 1;
        t14 = t44;
        goto me15;
mn39:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s929lennfd3g6, 4) == 0)) goto mn45;
        void * t46 = gm_l0;
        void * t47 = (void *)(((uint8_t *)t46) + 8);
        gm_t88rcucg1b7uk t48 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t49), t48);
        ((uint8_t *)&t49)[4] = 1;
        t14 = t49;
        goto me15;
mn45:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s3cv29r83pgn4, 5) == 0)) goto mn50;
        void * t51 = gm_l0;
        void * t52 = (void *)(((uint8_t *)t51) + 8);
        gm_t88rcucg1b7uk t53 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t54), t53);
        ((uint8_t *)&t54)[4] = 1;
        t14 = t54;
        goto me15;
mn50:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s2737i1iq57nf, 4) == 0)) goto mn55;
        void * t56 = gm_l0;
        void * t57 = (void *)(((uint8_t *)t56) + 8);
        gm_t88rcucg1b7uk t58 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t59), t58);
        ((uint8_t *)&t59)[4] = 1;
        t14 = t59;
        goto me15;
mn55:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_slolcr6ikvg33, 6) == 0)) goto mn60;
        void * t61 = gm_l0;
        void * t62 = (void *)(((uint8_t *)t61) + 8);
        gm_t53at80pfdus3 t63;
        memset(&t63, 0, sizeof t63);
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t64 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t62, &t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t65;
        GM_ST(uint8_t, (uint8_t *)&t65 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t65 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t65), t64);
        ((uint8_t *)&t65)[4] = 1;
        t14 = t65;
        goto me15;
mn60:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s0iqiubfm7e7e, 6) == 0)) goto mn66;
        void * t67 = gm_l0;
        void * t68 = (void *)(((uint8_t *)t67) + 8);
        gm_t88rcucg1b7uk t69 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t70), t69);
        ((uint8_t *)&t70)[4] = 1;
        t14 = t70;
        goto me15;
mn66:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s47llp68ptjgu, 7) == 0)) goto mn71;
        void * t72 = gm_l0;
        void * t73 = (void *)(((uint8_t *)t72) + 8);
        void * t74 = gm_l0;
        void * t75 = (void *)(((uint8_t *)t74) + 8);
        gm_t88rcucg1b7uk t76 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t77 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t73, ((uint8_t)1), t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t78 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t78), t77);
        ((uint8_t *)&t78)[4] = 1;
        t14 = t78;
        goto me15;
mn71:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sflee3fpg0btv, 4) == 0)) goto mn79;
        void * t80 = gm_l0;
        void * t81 = (void *)(((uint8_t *)t80) + 8);
        gm_t88rcucg1b7uk t82 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t81, ((uint8_t)0), ((uint16_t)32ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t83), t82);
        ((uint8_t *)&t83)[4] = 1;
        t14 = t83;
        goto me15;
mn79:;
        gm_tkn42u7na3t81 t85;
        memset(&t85, 0, sizeof t85);
        t14 = t85;
        goto me15;
mn84:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 31, 12);
me15:;
        uint8_t t86 = 0;
        F.ret = t14;
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
inline __attribute__((aligned(4))) uint8_t gm_f_calls_on_value_q0pa6h5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_calls_on_value_q0pa6h5 F;
    void * gm_l0; /* callee */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma4; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)22ULL)) goto mn9;
    void * t11 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l1 = t11;
    void * t12 = gm_l1;
    uint8_t t13 = gm_f_is_path_qa34i5j(t12);
    uint8_t t14 = (uint8_t)!t13;
    t2 = t14;
    goto me3;
mn9:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)20ULL)) goto mn15;
    t2 = ((uint8_t)1);
    goto me3;
mn15:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn17:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 270, 58);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2) {
    struct gm_fr_check_len_exprs_m8s6m0h F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* map */
    gm_toceffb697p0u gm_l2; /* at */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t2i8ne5ibd7v4 gm_l6; /* le */
    gm_tl874t2atnlh3 gm_l9; /* a */
    GM_STACK_CHECK(&F, 594u);
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
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 2192) + 0) + 8);
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
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 2192) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcsffver18si54, 668, 15);
                        gm_t2i8ne5ibd7v4 t20 = GM_LD(gm_t2i8ne5ibd7v4, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 2192) + 0)) + t17 * 16));
                        gm_l6 = t20;
                    }
                    {
                        uint8_t t22 = 0;
                        void * t23 = gm_l0;
                        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                        gm_b16_8 t25 = gm_l1;
                        uint8_t t26 = gm_f_len_params_bound_f7mjvn9(t23, GM_CONV(gm_csguankko4lmhd, t24), t25);
                        uint8_t t27 = (uint8_t)!t26;
                        if (!t27) {
                            void * t28 = gm_l0;
                            void * t29 = (void *)(((uint8_t *)t28) + 8);
                            void * t30 = gm_l0;
                            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                            gm_b16_8 t32 = gm_l1;
                            gm_t88rcucg1b7uk t33 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t30, t31, t32);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            uint8_t t34 = gm_f_is_error_8k27gd1(t29, GM_CONV(gm_csguankko4lmhd, t33));
                            uint8_t t35 = (uint8_t)!t34;
                            t22 = t35;
                        } else t22 = 1;
                        if (!t22) goto ie21;
                        {
                            goto lc5;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t37 = gm_l0;
                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                        gm_b16_8 t39 = gm_l1;
                        void * t40 = (void *)((uint8_t *)&F.l7);
                        gm_b0_1 t41 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t37, t38, t39, t40);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
                        gm_t6j6smj7qoctu t43;
                        void * t44 = gm_l0;
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 0));
                        gm_b16_8 t46 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t44, t45);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t43 + 0, t46);
                        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                        gm_b16_8 t49;
                        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(2));
                        gm_tabb710d9mq2e t50;
                        gm_f_join_sfknljb((void *)&t50, (void *)NULL, cx, NULL, t48, t49);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcsffver18si54, 672, 129);
                        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t43 + 16, t51);
                        gm_tabb710d9mq2e t52;
                        gm_f_format_rd9dflp((void *)&t52, (void *)NULL, cx, NULL, &t43);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_srcsffver18si54, 672, 34);
                        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                        gm_f_error_s6hn871((void *)&F.l8, (void *)NULL, cx, NULL, t42, t54);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        if (!((((uint8_t *)&gm_l2))[12] != 0)) goto ie55;
                        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l2));
                        gm_l9 = t56;
                        {
                            void * t58 = (void *)((uint8_t *)&F.l8);
                            gm_tl874t2atnlh3 t59 = gm_l9;
                            gm_b16_8 t60;
                            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_stevggi5tvq7c));
                            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(17));
                            gm_f_add_label_50fj7vv(t58, GM_CONV(gm_cseahvg8v7p0vs, t59), t60);
                        }
                        goto ix55;
ie55:;
ix55:;
                    }
                    {
                        void * t61 = gm_l0;
                        gm_b0_1 t62 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t61, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 667, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t64 = gm_l4;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l4 = t67;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_component_type_q9ge5lo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_component_type_q9ge5lo F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* text */
    GM_STACK_CHECK(&F, 339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_t88rcucg1b7uk t9 = gm_l3;
        gm_t53at80pfdus3 t10;
        gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
        uint8_t t11;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t14 != ((uint8_t)15ULL)) goto mn13;
        t11 = ((uint8_t)1);
        goto me12;
mn13:;
        t11 = ((uint8_t)0);
        goto me12;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 546, 8);
me12:;
        if (!t11) goto ie6;
        {
            void * t17 = gm_l0;
            gm_t88rcucg1b7uk t18 = gm_l3;
            gm_b16_8 t19 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t19;
        }
        {
            void * t20 = gm_l0;
            void * t21 = gm_l2;
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
            gm_t6j6smj7qoctu t23;
            gm_b16_8 t24 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t24);
            gm_b16_8 t25 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t25);
            gm_tabb710d9mq2e t26;
            gm_f_format_um7orcc((void *)&t26, (void *)NULL, cx, NULL, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcsffver18si54, 548, 25);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_b0_1 t29 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t20, t22, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_t88rcucg1b7uk t30 = gm_l3;
        uint8_t t31 = 0;
        F.ret = t30;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_comptime_value_l5orc93 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* e */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint32_t gm_l5; /* pid */
    gm_t88rcucg1b7uk gm_l6; /* have */
    gm_t428ue2u3hf5g gm_l7; /* key */
    void * gm_l8; /* hook */
    uint32_t gm_l9; /* id */
    void * gm_l10; /* cell */
    gm_t428ue2u3hf5g gm_l11; /* key */
    GM_STACK_CHECK(&F, 1283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l3;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l4 = t5;
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_t9805qtonbsag t10 = gm_f_value_param_tof1h6t(t8, t9);
            if (!((((uint8_t *)&t10))[4] != 0)) goto ie7;
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&t10));
            gm_l5 = t11;
            {
                uint32_t t14 = gm_l5;
                uint64_t t15 = (uint64_t)t14;
                uint64_t t16 = (uintptr_t)t15;
                void * t17 = gm_l0;
                uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1128) + 0) + 8);
                if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcsffver18si54, 800, 29);
                uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1128) + 0)) + t16 * 48) + 0) + 0));
                if (t19 != ((uint8_t)2ULL)) goto ie13;
                gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1128) + 0)) + t16 * 48) + 0) + 4));
                gm_l6 = t20;
                {
                    gm_t88rcucg1b7uk t23 = gm_l6;
                    gm_t88rcucg1b7uk t24 = gm_l2;
                    uint8_t t25 = (!gm_eq9m6mvtmgde(((uint8_t *)&t23), ((uint8_t *)&t24)));
                    if (!t25) goto ie22;
                    gm_t88rcucg1b7uk t26 = gm_l2;
                    void * t27 = gm_l0;
                    void * t28 = (void *)(((uint8_t *)t27) + 8);
                    gm_t88rcucg1b7uk t29 = gm_l6;
                    gm_t88rcucg1b7uk t30 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t28, t29);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t31 = (!gm_eq9m6mvtmgde(((uint8_t *)&t26), ((uint8_t *)&t30)));
                    if (!t31) goto ie22;
                    {
                        void * t33 = gm_l0;
                        void * t34 = gm_l3;
                        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
                        gm_tfeab279c16rr t36;
                        void * t37 = gm_l0;
                        gm_t88rcucg1b7uk t38 = gm_l2;
                        gm_b16_8 t39 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t39);
                        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t40);
                        void * t41 = gm_l0;
                        gm_t88rcucg1b7uk t42 = gm_l6;
                        gm_b16_8 t43 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t36 + 32, t43);
                        gm_tabb710d9mq2e t44;
                        gm_f_format_c3isg16((void *)&t44, (void *)NULL, cx, NULL, &t36);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srcsffver18si54, 802, 33);
                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                        gm_b0_1 t47 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t33, t35, t46);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_tkn42u7na3t81 t48;
                        memset(&t48, 0, sizeof t48);
                        uint8_t t49 = 0;
                        F.ret = t48;
                        goto gm_fret;
                    }
                    goto ix22;
ie22:;
ix22:;
                }
                {
                    void * t50 = gm_l0;
                    void * t51 = (void *)(((uint8_t *)t50) + 8);
                    uint32_t t52 = gm_l5;
                    gm_t88rcucg1b7uk t53 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tkn42u7na3t81 t54;
                    GM_ST(uint8_t, (uint8_t *)&t54 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t54 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t54), t53);
                    ((uint8_t *)&t54)[4] = 1;
                    uint8_t t55 = 0;
                    F.ret = t54;
                    goto gm_fret;
                }
                goto ix13;
ie13:;
                {
                    gm_tkn42u7na3t81 t57;
                    memset(&t57, 0, sizeof t57);
                    uint8_t t58 = 0;
                    F.ret = t57;
                    goto gm_fret;
                }
ix13:;
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
        void * t60 = gm_l0;
        uint64_t t61 = gm_l1;
        void * t62 = gm_l3;
        gm_t88rcucg1b7uk t63 = gm_l2;
        gm_trgmlgt2gmmjs t64;
        gm_f_const_key_of_q36m6nm((void *)&t64, (void *)NULL, cx, NULL, t60, t61, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t64))[32] != 0)) goto ie59;
        gm_l7 = (*(gm_t428ue2u3hf5g *)(((uint8_t *)&t64) + 0));
        {
            void * t66 = gm_l0;
            void * t67 = (void *)(((uint8_t *)t66) + 8);
            gm_t428ue2u3hf5g t68 = gm_l7;
            gm_t53at80pfdus3 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t69 + 8), t68);
            gm_t88rcucg1b7uk t70 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t67, &t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t71), t70);
            ((uint8_t *)&t71)[4] = 1;
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t74 = gm_l0;
        void * t75 = GM_LD(void *, (((uint8_t *)t74) + 2168));
        if (!(GM_LD(void *, ((uint8_t *)&t75)) != 0)) goto ie73;
        void * t76 = GM_LD(void *, ((uint8_t *)&t75));
        gm_l8 = t76;
        void * t77 = gm_l0;
        void * t78 = (void *)(((uint8_t *)t77) + 8);
        gm_t88rcucg1b7uk t79 = gm_l2;
        uint8_t t80 = gm_f_has_param_u9t0efs(t78, GM_CONV(gm_csguankko4lmhd, t79));
        uint8_t t81 = (uint8_t)!t80;
        if (!t81) goto ie73;
        {
            void * t83 = gm_l8;
            void * t84 = gm_l0;
            uint64_t t85 = gm_l1;
            void * t86 = gm_l3;
            gm_t88rcucg1b7uk t87 = gm_l2;
            gm_tkn42u7na3t81 t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t88), t87);
            ((uint8_t *)&t88)[4] = 1;
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s33hvuvcdjs1b));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(10));
            void * t90 = gm_l3;
            gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 72));
            void * t92 = t83;
            gm_t9805qtonbsag t93;
            if (GM_PTR_TAGGED(t92)) {
                t93 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t92))(t84, t85, t86, t88, t89, GM_CONV(gm_cseahvg8v7p0vs, t91));
            } else {
                t93 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t92)(NULL, cx, NULL, t84, t85, t86, t88, t89, t91);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t94;
            if (((((uint8_t *)&t93))[4] != 0)) {
                uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&t93) + 0));
                t94 = t95;
            } else {
                gm_tkn42u7na3t81 t96;
                memset(&t96, 0, sizeof t96);
                uint8_t t97 = 0;
                F.ret = t96;
                goto gm_fret;
            }
            gm_l9 = t94;
        }
        {
            uint32_t t98 = gm_l9;
            uint64_t t99 = (uint64_t)t98;
            uint64_t t100 = (uintptr_t)t99;
            void * t101 = gm_l0;
            uint64_t t102 = GM_LD(uintptr_t, ((((uint8_t *)t101) + 1968) + 0) + 8);
            if (t100 >= (uintptr_t)(t102)) gm_trap_index(t100, t102, gm_srcsffver18si54, 811, 17);
            void * t103 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t101) + 1968) + 0)) + t100 * 88) + 80));
            void * t104;
            if ((GM_LD(void *, ((uint8_t *)&t103)) != 0)) {
                void * t105 = GM_LD(void *, (((uint8_t *)&t103) + 0));
                t104 = t105;
            } else {
                gm_tkn42u7na3t81 t106;
                memset(&t106, 0, sizeof t106);
                uint8_t t107 = 0;
                F.ret = t106;
                goto gm_fret;
            }
            gm_l10 = t104;
        }
        {
            void * t108 = gm_l0;
            void * t109 = gm_l10;
            gm_t88rcucg1b7uk t110 = gm_l2;
            void * t111 = gm_l3;
            gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t111) + 72));
            gm_trgmlgt2gmmjs t113;
            gm_f_value_key_vki574i((void *)&t113, (void *)NULL, cx, NULL, t108, &(*(gm_t6evimt84lndq *)(((uint8_t *)t109) + 0)), t110, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t428ue2u3hf5g t114;
            if (((((uint8_t *)&t113))[32] != 0)) {
                t114 = (*(gm_t428ue2u3hf5g *)(((uint8_t *)&t113) + 0));
            } else {
                gm_tkn42u7na3t81 t115;
                memset(&t115, 0, sizeof t115);
                uint8_t t116 = 0;
                F.ret = t115;
                goto gm_fret;
            }
            gm_l11 = t114;
        }
        {
            void * t117 = gm_l0;
            void * t118 = (void *)(((uint8_t *)t117) + 8);
            gm_t428ue2u3hf5g t119 = gm_l11;
            gm_t53at80pfdus3 t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t120 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t120 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t120 + 8), t119);
            gm_t88rcucg1b7uk t121 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t118, &t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t122;
            GM_ST(uint8_t, (uint8_t *)&t122 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t122 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t122), t121);
            ((uint8_t *)&t122)[4] = 1;
            uint8_t t123 = 0;
            F.ret = t122;
            goto gm_fret;
        }
        goto ix73;
ie73:;
ix73:;
    }
    {
        void * t124 = gm_l0;
        void * t125 = gm_l3;
        gm_tl874t2atnlh3 t126 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t125) + 72));
        gm_b16_8 t127;
        GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sm7179tmr0nhi));
        GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(26));
        gm_b0_1 t128 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t124, t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tkn42u7na3t81 t129;
        memset(&t129, 0, sizeof t129);
        uint8_t t130 = 0;
        F.ret = t129;
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
__attribute__((aligned(4))) void gm_f_const_key_of_q36m6nm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_const_key_of_q36m6nm F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    uint32_t gm_l5; /* f */
    void * gm_l6; /* x */
    void * gm_l7; /* inner */
    gm_tibupt4dmcqai gm_l8; /* lit */
    uint8_t gm_l12; /* b */
    GM_STACK_CHECK(&F, 1793u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l2;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 8) + 0) == ((uint8_t)4ULL)));
        if (!t4) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l3;
            gm_t53at80pfdus3 t10;
            gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
            if (t11 != ((uint8_t)13ULL)) goto ie6;
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 8));
            gm_l4 = t12;
            void * t13 = gm_l0;
            gm_t88rcucg1b7uk t14 = gm_l4;
            uint8_t t15 = gm_f_is_nullable_pointer_nkie06s(t13, GM_CONV(gm_csguankko4lmhd, t14));
            if (!t15) goto ie6;
            {
                gm_t428ue2u3hf5g t17;
                GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                GM_ST(gm_t428ue2u3hf5g, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 0, t17);
                goto gm_fdone;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            gm_trgmlgt2gmmjs t18;
            memset(&t18, 0, sizeof t18);
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = gm_l1;
        void * t23 = gm_l2;
        gm_t88rcucg1b7uk t24 = gm_l3;
        gm_t9805qtonbsag t25 = gm_f_fn_ref_7alnaqh((void *)NULL, cx, NULL, t21, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t25))[4] != 0)) goto ie20;
        uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&t25));
        gm_l5 = t26;
        {
            gm_tiilmfd16v3fc t28;
            memset((uint8_t *)&t28 + 4, 0, 4);
            uint32_t t29 = gm_l5;
            GM_ST(uint32_t, (uint8_t *)&t28 + 0, t29);
            uint32_t t30 = gm_l5;
            uint64_t t31 = (uint64_t)t30;
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 344) + 0) + 8);
            if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srcsffver18si54, 826, 53);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 344) + 0)) + t32 * 136) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 8, t35);
            gm_t428ue2u3hf5g t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t36 + 8), t28);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
            ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
            GM_ST(gm_t428ue2u3hf5g, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 0, t36);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 8);
        gm_t88rcucg1b7uk t40 = gm_l3;
        uint8_t t41 = gm_f_has_param_u9t0efs(t39, GM_CONV(gm_csguankko4lmhd, t40));
        if (!t41) goto ie37;
        {
            gm_trgmlgt2gmmjs t43;
            memset(&t43, 0, sizeof t43);
            uint8_t t44 = 0;
            F.ret = t43;
            goto gm_fret;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        void * t45 = gm_l2;
        gm_l6 = t45;
    }
    {
        for (;;) {
            void * t47 = gm_l6;
            uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t47) + 8) + 0));
            if (t48 != ((uint8_t)9ULL)) goto lb46;
            void * t49 = GM_LD(void *, ((((uint8_t *)t47) + 8) + 8));
            gm_l7 = t49;
            {
                void * t52 = gm_l7;
                gm_l6 = t52;
            }
lc46:;
        }
lb46:;
    }
    {
        void * t54 = gm_l6;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t54) + 8) + 0));
        if (t55 != ((uint8_t)2ULL)) goto ie53;
        gm_tibupt4dmcqai t56 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t54) + 8) + 8));
        gm_l8 = t56;
        uint8_t t57 = (!(GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 16) + 0) == ((uint8_t)2ULL)));
        if (!t57) goto ie53;
        gm_t88rcucg1b7uk t58 = gm_l3;
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        gm_t88rcucg1b7uk t61 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t62 = (gm_eq9m6mvtmgde(((uint8_t *)&t58), ((uint8_t *)&t61)));
        if (!t62) goto ie53;
        {
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_t428ue2u3hf5g t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)3ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
            ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
            GM_ST(gm_t428ue2u3hf5g, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 0, t66);
            goto gm_fdone;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        void * t67 = gm_l0;
        uint64_t t68 = gm_l1;
        void * t69 = gm_l2;
        gm_tkv1vap90vht2 t70;
        gm_f_untyped_if_plain_lbrp24p((void *)&t70, (void *)NULL, cx, NULL, t67, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5vbkdq81qfs7 t71;
        if (((((uint8_t *)&t70))[208] != 0)) {
            t71 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t70) + 0));
        } else {
            gm_trgmlgt2gmmjs t72;
            memset(&t72, 0, sizeof t72);
            uint8_t t73 = 0;
            F.ret = t72;
            goto gm_fret;
        }
        F.l9 = t71;
    }
    {
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
        if (t75 != ((uint8_t)0ULL)) goto ie74;
        gm_t0ialelq3ql1k t76 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l9) + 8));
        F.l10 = t76;
        {
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t83) { case ((uint8_t)0ULL): goto ma79; case ((uint8_t)1ULL): goto ma81; case ((uint8_t)2ULL): goto ma81; case ((uint8_t)3ULL): goto ma80; case ((uint8_t)4ULL): goto ma81; default: goto ma81; }
ma79:;
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t85 != ((uint8_t)0ULL)) goto mn84;
            gm_tbci5ospk5nnn t86 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l10) + 8));
            F.l11 = t86;
            {
                uint8_t t89 = 0;
                void * t90 = gm_l0;
                void * t91 = (void *)(((uint8_t *)t90) + 8);
                gm_t88rcucg1b7uk t92 = gm_l3;
                uint8_t t93 = gm_f_is_integer_8d4f9ek(t91, GM_CONV(gm_csguankko4lmhd, t92));
                uint8_t t94 = (uint8_t)!t93;
                if (!t94) {
                    void * t95 = gm_l0;
                    gm_t88rcucg1b7uk t96 = gm_l3;
                    uint8_t t97 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t95, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), t96);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t98 = (uint8_t)!t97;
                    t89 = t98;
                } else t89 = 1;
                if (!t89) goto ie88;
                {
                    gm_trgmlgt2gmmjs t100;
                    memset(&t100, 0, sizeof t100);
                    uint8_t t101 = 0;
                    F.ret = t100;
                    goto gm_fret;
                }
                goto ix88;
ie88:;
ix88:;
            }
            {
                void * t102 = gm_l0;
                gm_t88rcucg1b7uk t103 = gm_l3;
                gm_t428ue2u3hf5g t104;
                gm_f_int_key_dmcraup((void *)&t104, (void *)NULL, cx, NULL, t102, t103, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                GM_ST(gm_t428ue2u3hf5g, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 0, t104);
                goto gm_fdone;
            }
            goto me78;
mn84:;
ma80:;
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t106 != ((uint8_t)3ULL)) goto mn105;
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 8));
            gm_l12 = t107;
            {
                void * t110 = gm_l0;
                void * t111 = (void *)(((uint8_t *)t110) + 8);
                gm_t88rcucg1b7uk t112 = gm_l3;
                gm_t53at80pfdus3 t113;
                gm_f_kind_jcnt4hm((void *)&t113, t111, GM_CONV(gm_csguankko4lmhd, t112));
                uint8_t t114 = (!(GM_LD(uint8_t, ((uint8_t *)&t113) + 0) == ((uint8_t)4ULL)));
                if (!t114) goto ie109;
                {
                    gm_trgmlgt2gmmjs t116;
                    memset(&t116, 0, sizeof t116);
                    uint8_t t117 = 0;
                    F.ret = t116;
                    goto gm_fret;
                }
                goto ix109;
ie109:;
ix109:;
            }
            {
                gm_f_list_uvfu1k1((void *)&F.l13, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t118 = (void *)((uint8_t *)&F.l13);
                uint8_t t119;
                uint8_t t121 = gm_l12;
                if (!t121) goto ie120;
                uint8_t t123 = ((uint8_t)1ULL);
                t119 = t123;
                goto ix120;
ie120:;
                uint8_t t125 = ((uint8_t)0ULL);
                t119 = t125;
ix120:;
                gm_tftb1odc739t5 t126 = gm_f_push_t83rg2m(t118, t119);
                if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srcsffver18si54, 842, 13);
                gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
            }
            {
                gm_tcmkompbl09il t128;
                memset((uint8_t *)&t128 + 4, 0, 4);
                gm_t88rcucg1b7uk t129 = gm_l3;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t128 + 0, t129);
                gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
                GM_ST(gm_b16_8, (uint8_t *)&t128 + 8, t131);
                gm_t428ue2u3hf5g t132;
                GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t132 + 8), t128);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 36, 0);
                ((uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out))[32] = 1;
                GM_ST(gm_t428ue2u3hf5g, (uint8_t *)&(*(gm_trgmlgt2gmmjs *)gm_out) + 0, t132);
                goto gm_fdone;
            }
            goto me78;
mn105:;
ma81:;
            gm_trgmlgt2gmmjs t134;
            memset(&t134, 0, sizeof t134);
            uint8_t t135 = 0;
            F.ret = t134;
            goto gm_fret;
            goto me78;
mn133:;
ma82:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 834, 5);
me78:;
        }
        goto ix74;
ie74:;
        {
            gm_trgmlgt2gmmjs t137;
            memset(&t137, 0, sizeof t137);
            uint8_t t138 = 0;
            F.ret = t137;
            goto gm_fret;
        }
ix74:;
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
inline __attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_construction_call_ktqtrpo F;
    void * gm_l0; /* e */
    void * gm_l1; /* callee */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    void * gm_l6; /* inner */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)19ULL)) goto mn11;
        void * t13 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t13;
        void * t14;
        void * t16 = gm_l1;
        uint8_t t17 = gm_f_calls_on_value_q0pa6h5(t16);
        if (!t17) goto ie15;
        void * t19 = ((void *)0);
        t14 = t19;
        goto ix15;
ie15:;
        void * t21 = gm_l0;
        void * t22 = t21;
        t14 = t22;
ix15:;
        uint8_t t23 = 0;
        F.ret = t14;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)28ULL)) goto mn24;
        gm_t3mf58gv9kf3n t26 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t26;
        void * t27 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t28 = gm_f_construction_call_ktqtrpo(t27);
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
        goto me3;
mn24:;
ma6:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)24ULL)) goto mn30;
        void * t32 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t32;
        void * t33 = gm_l3;
        void * t34 = gm_f_construction_call_ktqtrpo(t33);
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me3;
mn30:;
ma7:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)25ULL)) goto pa38;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t40;
        goto po37;
pa38:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t42 != ((uint8_t)26ULL)) goto pa41;
        void * t43 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t43;
        goto po37;
pa41:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t45 != ((uint8_t)27ULL)) goto pa44;
        void * t46 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t46;
        goto po37;
pa44:;
        goto mn36;
po37:;
        void * t47 = gm_l4;
        void * t48 = gm_f_construction_call_ktqtrpo(t47);
        uint8_t t49 = 0;
        F.ret = t48;
        goto gm_fret;
        goto me3;
mn36:;
ma8:;
        uint8_t t51 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me3;
mn50:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 258, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_decl_is_type_oh0cpn9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2) {
    struct gm_fr_decl_is_type_oh0cpn9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint32_t gm_l2; /* depth */
    void * gm_l4; /* decl */
    GM_STACK_CHECK(&F, 454u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint64_t t4 = gm_l1;
        gm_tagrkbqh054h7 t5;
        gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)2ULL)) goto ie2;
        F.l3 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t5) + 8));
        {
            uint8_t t8;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; default: goto ma12; }
ma10:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t16 != ((uint8_t)0ULL)) goto mn15;
            t8 = ((uint8_t)1);
            goto me9;
mn15:;
ma11:;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t18 != ((uint8_t)1ULL)) goto mn17;
            t8 = ((uint8_t)1);
            goto me9;
mn17:;
ma12:;
            t8 = ((uint8_t)0);
            goto me9;
mn19:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 144, 40);
me9:;
            uint8_t t20 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t22 = gm_l2;
        uint8_t t23 = (t22 > ((uint32_t)16ULL));
        if (!t23) goto ie21;
        {
            uint8_t t25 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        uint64_t t26 = gm_l1;
        uint64_t t27 = (uintptr_t)t26;
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        uint64_t t30 = GM_LD(uintptr_t, ((((uint8_t *)t29) + 240) + 0) + 8);
        if (t27 >= (uintptr_t)(t30)) gm_trap_index(t27, t30, gm_srcsffver18si54, 146, 14);
        void * t31 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t29) + 240) + 0)) + t27 * 216);
        gm_l4 = t31;
    }
    {
        void * t33 = gm_l4;
        uint8_t t34 = GM_LD(uint8_t, (((((uint8_t *)t33) + 40) + 64) + 0));
        if (t34 != ((uint8_t)0ULL)) goto ie32;
        gm_tv2uf4leil5hq t35 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t33) + 40) + 64) + 8));
        F.l5 = t35;
        {
            uint8_t t38 = 0;
            void * t39 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
            uint8_t t40;
            if ((GM_LD(void *, ((uint8_t *)&t39)) != 0)) goto mn42;
            t40 = ((uint8_t)0);
            goto me41;
mn42:;
            t40 = ((uint8_t)1);
            goto me41;
mn43:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 148, 8);
me41:;
            if (!t40) {
                uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 8) + 8);
                uint8_t t45 = (!(t44 == ((uint64_t)1ULL)));
                t38 = t45;
            } else t38 = 1;
            if (!t38) goto ie37;
            {
                uint8_t t47 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            void * t48 = gm_l0;
            void * t49 = gm_l4;
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t49) + 32));
            void * t51 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
            uint32_t t52 = gm_l2;
            uint8_t t53 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t48, t50, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
        }
        goto ix32;
ie32:;
        {
            uint8_t t56 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix32:;
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
extern __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_enum_array_type_0sic6t0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_enum_array_type_0sic6t0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* index */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* u */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 988u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        uint8_t t5 = 0;
        void * t6 = gm_l4;
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint8_t t8 = gm_f_is_error_8k27gd1(t6, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) {
            void * t9 = gm_l4;
            gm_t88rcucg1b7uk t10 = gm_l2;
            uint8_t t11 = gm_f_is_error_8k27gd1(t9, GM_CONV(gm_csguankko4lmhd, t10));
            t5 = t11;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            void * t13 = gm_l4;
            gm_t88rcucg1b7uk t14 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_t53at80pfdus3 t19;
        gm_f_kind_jcnt4hm((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t20;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t23 != ((uint8_t)19ULL)) goto mn22;
        t20 = ((uint8_t)1);
        goto me21;
mn22:;
        t20 = ((uint8_t)0);
        goto me21;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 603, 8);
me21:;
        if (!t20) goto ie16;
        {
            void * t26 = gm_l0;
            gm_tl874t2atnlh3 t27 = gm_l3;
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l1;
            gm_b16_8 t30 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t31;
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
            gm_tabb710d9mq2e t32;
            gm_f_format_o2s8o9q((void *)&t32, (void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcsffver18si54, 604, 23);
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_b0_1 t35 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t26, t27, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t36 = gm_l4;
            gm_t88rcucg1b7uk t37 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t39 = gm_l0;
        gm_t88rcucg1b7uk t40 = gm_l1;
        gm_tdlpv0q69lov1 t41;
        gm_f_union_variants_a7khaqp((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
        gm_tep2drsl9408b t42;
        if (((((uint8_t *)&t41))[24] != 0)) {
            t42 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t41) + 0));
        } else {
            {
                void * t44 = gm_l0;
                gm_tl874t2atnlh3 t45 = gm_l3;
                void * t46 = gm_l0;
                gm_t88rcucg1b7uk t47 = gm_l1;
                gm_b16_8 t48 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t46, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t49;
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
                gm_tabb710d9mq2e t50;
                gm_f_format_qmamljh((void *)&t50, (void *)NULL, cx, NULL, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcsffver18si54, 608, 23);
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                gm_b0_1 t53 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t44, t45, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t54 = gm_l4;
            gm_t88rcucg1b7uk t55 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        gm_l5 = t42;
    }
    {
        {
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_l6 = t58;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t60 = gm_l7;
                uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t62 = (t60 < t61);
                if (!t62) goto lb59;
                {
                    uint64_t t65 = gm_l7;
                    uint64_t t66 = (uintptr_t)t65;
                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tckdobunaulee t70 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t66 * 72));
                    F.l8 = t70;
                    {
                        uint8_t t73;
                        if ((((((uint8_t *)&F.l8) + 16))[4] != 0)) goto mn75;
                        t73 = ((uint8_t)0);
                        goto me74;
mn75:;
                        t73 = ((uint8_t)1);
                        goto me74;
mn76:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 612, 12);
me74:;
                        if (!t73) goto ie72;
                        {
                            void * t78 = gm_l0;
                            gm_tl874t2atnlh3 t79 = gm_l3;
                            gm_t6j6smj7qoctu t80;
                            void * t81 = gm_l0;
                            gm_t88rcucg1b7uk t82 = gm_l1;
                            gm_b16_8 t83 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t81, t82);
                            if (gm_unw.kind) {
                                goto gm_ll63;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t80 + 0, t83);
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t80 + 16, t84);
                            gm_tabb710d9mq2e t85;
                            gm_f_format_il5kbp4((void *)&t85, (void *)NULL, cx, NULL, &t80);
                            if (gm_unw.kind) {
                                goto gm_ll63;
                            }
                            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srcsffver18si54, 613, 27);
                            gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
                            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                            gm_b0_1 t88 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t78, t79, t87);
                            if (gm_unw.kind) {
                                goto gm_ll63;
                            }
                        }
                        {
                            void * t89 = gm_l4;
                            gm_t88rcucg1b7uk t90 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t89);
                            if (gm_unw.kind) {
                                goto gm_ll63;
                            }
                            uint8_t t91 = 0;
                            F.ret = t90;
                            goto gm_fret;
                        }
                        goto ix72;
ie72:;
ix72:;
                    }
                    goto me68;
mn69:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 611, 5);
me68:;
                }
lc59:;
                {
                    uint64_t t93 = gm_l7;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l7 = t96;
                }
            }
gm_ll63:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
            }
            goto gm_lad;
lb59:;
        }
    }
    {
        void * t97 = gm_l4;
        gm_t8nckeqfenl8a t98;
        memset((uint8_t *)&t98 + 20, 0, 4);
        uint64_t t99 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
        uint64_t t100 = (uint64_t)t99;
        GM_ST(uint64_t, (uint8_t *)&t98 + 0, t100);
        gm_t88rcucg1b7uk t101 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t98 + 8, t101);
        gm_t88rcucg1b7uk t102 = gm_l1;
        gm_tnnjeerolos0a t103;
        GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t103 + 4), t102);
        GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t98 + 12, t103);
        gm_t53at80pfdus3 t104;
        GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t104 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t104 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t104 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)11ULL));
        GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t104 + 8), t98);
        gm_t88rcucg1b7uk t105 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t97, &t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t106 = 0;
        F.ret = t105;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_ref_7alnaqh(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_fn_ref_7alnaqh F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_t88rcucg1b7uk gm_l4; /* want */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint32_t gm_l10; /* g */
    uint32_t gm_l11; /* g */
    void * gm_l12; /* base */
    gm_t08olq97rscn3 gm_l13; /* name */
    uint64_t gm_l14; /* m */
    uint32_t gm_l15; /* g */
    void * gm_l16; /* info */
    GM_STACK_CHECK(&F, 1566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)13ULL)) goto ie3;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l5 = t9;
        {
            gm_t88rcucg1b7uk t11 = gm_l5;
            gm_l4 = t11;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        gm_t88rcucg1b7uk t15 = gm_l4;
        gm_t53at80pfdus3 t16;
        gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t17 != ((uint8_t)16ULL)) goto ie12;
        F.l6 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t16) + 8));
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            gm_l8 = ((uint32_t)0ULL);
        }
        {
            void * t19 = gm_l2;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            switch (t25) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma23; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma23; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma21; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma23; case ((uint8_t)10ULL): goto ma23; case ((uint8_t)11ULL): goto ma23; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma23; case ((uint8_t)15ULL): goto ma23; case ((uint8_t)16ULL): goto ma23; case ((uint8_t)17ULL): goto ma23; case ((uint8_t)18ULL): goto ma23; case ((uint8_t)19ULL): goto ma23; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma23; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma23; case ((uint8_t)24ULL): goto ma23; case ((uint8_t)25ULL): goto ma23; case ((uint8_t)26ULL): goto ma23; case ((uint8_t)27ULL): goto ma23; case ((uint8_t)28ULL): goto ma23; case ((uint8_t)29ULL): goto ma23; case ((uint8_t)30ULL): goto ma23; case ((uint8_t)31ULL): goto ma23; case ((uint8_t)32ULL): goto ma23; case ((uint8_t)33ULL): goto ma23; case ((uint8_t)34ULL): goto ma23; case ((uint8_t)35ULL): goto ma23; case ((uint8_t)36ULL): goto ma23; case ((uint8_t)37ULL): goto ma23; case ((uint8_t)38ULL): goto ma23; case ((uint8_t)39ULL): goto ma23; case ((uint8_t)40ULL): goto ma23; case ((uint8_t)41ULL): goto ma23; case ((uint8_t)42ULL): goto ma23; case ((uint8_t)43ULL): goto ma23; case ((uint8_t)44ULL): goto ma23; case ((uint8_t)45ULL): goto ma23; case ((uint8_t)46ULL): goto ma23; case ((uint8_t)47ULL): goto ma23; case ((uint8_t)48ULL): goto ma23; case ((uint8_t)49ULL): goto ma23; case ((uint8_t)50ULL): goto ma23; default: goto ma23; }
ma21:;
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            if (t27 != ((uint8_t)6ULL)) goto mn26;
            gm_t08olq97rscn3 t28 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t19) + 8) + 8));
            gm_l9 = t28;
            {
                void * t31 = gm_l0;
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                gm_t9805qtonbsag t33 = gm_f_local_fn_aji8qnd(t31, t32);
                if (!((((uint8_t *)&t33))[4] != 0)) goto ie30;
                uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)&t33));
                gm_l10 = t34;
                {
                    uint32_t t36 = gm_l10;
                    gm_l8 = t36;
                }
                goto ix30;
ie30:;
                {
                    void * t38 = gm_l0;
                    uint64_t t39 = gm_l1;
                    gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                    gm_tf5srh4mp4b3i t41 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t38, t39, t40);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t42;
                    if (((((uint8_t *)&t41))[8] != 0)) {
                        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&t41) + 0));
                        t42 = t43;
                    } else {
                        gm_t9805qtonbsag t44;
                        memset(&t44, 0, sizeof t44);
                        uint8_t t45 = 0;
                        F.ret = t44;
                        goto gm_fret;
                    }
                    gm_l7 = t42;
                }
                {
                    void * t47 = gm_l0;
                    uint64_t t48 = gm_l7;
                    gm_t6qooom8qa2do t49;
                    gm_f_resolve_decl_pus042v((void *)&t49, (void *)NULL, cx, NULL, t47, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                    if (t50 != ((uint8_t)5ULL)) goto ie46;
                    uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&t49) + 8));
                    gm_l11 = t51;
                    {
                        uint32_t t53 = gm_l11;
                        gm_l8 = t53;
                    }
                    goto ix46;
ie46:;
                    {
                        gm_t9805qtonbsag t55;
                        memset(&t55, 0, sizeof t55);
                        uint8_t t56 = 0;
                        F.ret = t55;
                        goto gm_fret;
                    }
ix46:;
                }
ix30:;
            }
            goto me20;
mn26:;
ma22:;
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            if (t58 != ((uint8_t)22ULL)) goto mn57;
            void * t59 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 0));
            gm_l12 = t59;
            gm_t08olq97rscn3 t60 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t19) + 8) + 8) + 8));
            gm_l13 = t60;
            {
                void * t62 = gm_l0;
                uint64_t t63 = gm_l1;
                void * t64 = gm_l12;
                gm_tf5srh4mp4b3i t65 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t62, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t66;
                if (((((uint8_t *)&t65))[8] != 0)) {
                    uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&t65) + 0));
                    t66 = t67;
                } else {
                    gm_t9805qtonbsag t68;
                    memset(&t68, 0, sizeof t68);
                    uint8_t t69 = 0;
                    F.ret = t68;
                    goto gm_fret;
                }
                gm_l14 = t66;
            }
            {
                void * t70 = gm_l0;
                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
                uint64_t t72 = gm_l14;
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                gm_tf5srh4mp4b3i t74 = gm_f_lookup_59juqoo(t71, t72, t73);
                uint64_t t75;
                if (((((uint8_t *)&t74))[8] != 0)) {
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&t74) + 0));
                    t75 = t76;
                } else {
                    gm_t9805qtonbsag t77;
                    memset(&t77, 0, sizeof t77);
                    uint8_t t78 = 0;
                    F.ret = t77;
                    goto gm_fret;
                }
                gm_l7 = t75;
            }
            {
                void * t80 = gm_l0;
                uint64_t t81 = gm_l7;
                uint64_t t82 = gm_l1;
                uint8_t t83 = gm_f_decl_visible_bd168ti(t80, t81, t82);
                uint8_t t84 = (uint8_t)!t83;
                if (!t84) goto ie79;
                {
                    gm_t9805qtonbsag t86;
                    memset(&t86, 0, sizeof t86);
                    uint8_t t87 = 0;
                    F.ret = t86;
                    goto gm_fret;
                }
                goto ix79;
ie79:;
ix79:;
            }
            {
                void * t89 = gm_l0;
                uint64_t t90 = gm_l7;
                gm_t6qooom8qa2do t91;
                gm_f_resolve_decl_pus042v((void *)&t91, (void *)NULL, cx, NULL, t89, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
                if (t92 != ((uint8_t)5ULL)) goto ie88;
                uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&t91) + 8));
                gm_l15 = t93;
                {
                    uint32_t t95 = gm_l15;
                    gm_l8 = t95;
                }
                goto ix88;
ie88:;
                {
                    gm_t9805qtonbsag t97;
                    memset(&t97, 0, sizeof t97);
                    uint8_t t98 = 0;
                    F.ret = t97;
                    goto gm_fret;
                }
ix88:;
            }
            goto me20;
mn57:;
ma23:;
            gm_t9805qtonbsag t100;
            memset(&t100, 0, sizeof t100);
            uint8_t t101 = 0;
            F.ret = t100;
            goto gm_fret;
            goto me20;
mn99:;
ma24:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 857, 5);
me20:;
        }
        {
            uint32_t t102 = gm_l8;
            uint64_t t103 = (uint64_t)t102;
            uint64_t t104 = (uintptr_t)t103;
            void * t105 = gm_l0;
            uint64_t t106 = GM_LD(uintptr_t, ((((uint8_t *)t105) + 344) + 0) + 8);
            if (t104 >= (uintptr_t)(t106)) gm_trap_index(t104, t106, gm_srcsffver18si54, 876, 14);
            void * t107 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t105) + 344) + 0)) + t104 * 136);
            gm_l16 = t107;
        }
        {
            void * t109 = gm_l16;
            uint64_t t110 = GM_LD(uintptr_t, (((uint8_t *)t109) + 80) + 8);
            uint8_t t111 = (t110 > ((uint64_t)0ULL));
            if (!t111) goto ie108;
            {
                gm_t9805qtonbsag t113;
                memset(&t113, 0, sizeof t113);
                uint8_t t114 = 0;
                F.ret = t113;
                goto gm_fret;
            }
            goto ix108;
ie108:;
ix108:;
        }
        {
            void * t115 = gm_l0;
            void * t116 = gm_l16;
            gm_f_fn_ty_qaih7ts((void *)&F.l17, (void *)NULL, cx, NULL, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t118 = gm_l0;
            void * t119 = (void *)(((uint8_t *)t118) + 8);
            gm_tddtjfggdat0d t120 = F.l17;
            gm_t53at80pfdus3 t121;
            GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t121 + 8), t120);
            gm_t88rcucg1b7uk t122 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t119, &t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t123 = gm_l0;
            void * t124 = (void *)(((uint8_t *)t123) + 8);
            gm_tddtjfggdat0d t125 = F.l6;
            gm_t53at80pfdus3 t126;
            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t126 + 8), t125);
            gm_t88rcucg1b7uk t127 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t124, &t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t128 = (!gm_eq9m6mvtmgde(((uint8_t *)&t122), ((uint8_t *)&t127)));
            if (!t128) goto ie117;
            {
                gm_t9805qtonbsag t130;
                memset(&t130, 0, sizeof t130);
                uint8_t t131 = 0;
                F.ret = t130;
                goto gm_fret;
            }
            goto ix117;
ie117:;
ix117:;
        }
        {
            uint32_t t132 = gm_l8;
            gm_t9805qtonbsag t133;
            GM_ST(uint8_t, (uint8_t *)&t133 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t133 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t133), t132);
            ((uint8_t *)&t133)[4] = 1;
            uint8_t t134 = 0;
            F.ret = t133;
            goto gm_fret;
        }
        goto ix12;
ie12:;
        {
            gm_t9805qtonbsag t136;
            memset(&t136, 0, sizeof t136);
            uint8_t t137 = 0;
            F.ret = t136;
            goto gm_fret;
        }
ix12:;
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
inline __attribute__((aligned(4))) void gm_f_fn_ty_qaih7ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_ty_qaih7ts F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_nof7vfj((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 40));
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
                    gm_t5v0c8ia6a5mo t16 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 56));
                    F.l5 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l2);
                        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        gm_tftb1odc739t5 t20 = gm_f_push_k3l9nic(t18, GM_CONV(gm_csguankko4lmhd, t19));
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcsffver18si54, 538, 25);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 538, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l4;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l4 = t26;
                }
            }
lb5:;
        }
    }
    {
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0, 4);
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0, 7);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        void * t29 = gm_l1;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 56));
        void * t31 = gm_l1;
        gm_tnpifv20irnph t32 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t31) + 64));
        void * t33 = gm_l1;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t33) + 128));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0, t28);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16, t30);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24, t32);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40, t34);
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
__attribute__((aligned(4))) void gm_f_generic_decl_oir9hm6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3) {
    struct gm_fr_generic_decl_oir9hm6 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* callee */
    uint8_t gm_l3; /* report */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l9; /* d */
    uint64_t gm_l10; /* d */
    GM_STACK_CHECK(&F, 841u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)6ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t11;
        {
            void * t13 = gm_l0;
            uint64_t t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t16 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t17;
            if (((((uint8_t *)&t16))[8] != 0)) {
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                t17 = t18;
            } else {
                gm_toblvhepnjqkh t19;
                memset(&t19, 0, sizeof t19);
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            gm_l5 = t17;
        }
        {
            gm_tkb8hpcqf70t1 t21;
            memset((uint8_t *)&t21 + 20, 0, 4);
            uint64_t t22 = gm_l5;
            GM_ST(uint64_t, (uint8_t *)&t21 + 0, t22);
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t21 + 8, t23);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 28, 0);
            ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out))[24] = 1;
            GM_ST(gm_tkb8hpcqf70t1, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 0, t21);
            goto gm_fdone;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)22ULL)) goto mn24;
        void * t26 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t26;
        gm_t08olq97rscn3 t27 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t27;
        {
            void * t29 = gm_l0;
            uint64_t t30 = gm_l1;
            void * t31 = gm_l6;
            gm_tf5srh4mp4b3i t32 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t29, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t33;
            if (((((uint8_t *)&t32))[8] != 0)) {
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t32) + 0));
                t33 = t34;
            } else {
                gm_toblvhepnjqkh t35;
                memset(&t35, 0, sizeof t35);
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            gm_l8 = t33;
        }
        {
            uint8_t t38 = gm_l3;
            if (!t38) goto ie37;
            {
                void * t40 = gm_l0;
                uint64_t t41 = gm_l8;
                uint64_t t42 = gm_l1;
                gm_tf5srh4mp4b3i t43 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t40, t41, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t44;
                if (((((uint8_t *)&t43))[8] != 0)) {
                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
                    t44 = t45;
                } else {
                    gm_toblvhepnjqkh t46;
                    memset(&t46, 0, sizeof t46);
                    uint8_t t47 = 0;
                    F.ret = t46;
                    goto gm_fret;
                }
                gm_l9 = t44;
            }
            {
                gm_tkb8hpcqf70t1 t48;
                memset((uint8_t *)&t48 + 20, 0, 4);
                uint64_t t49 = gm_l9;
                GM_ST(uint64_t, (uint8_t *)&t48 + 0, t49);
                gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t48 + 8, t50);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 28, 0);
                ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out))[24] = 1;
                GM_ST(gm_tkb8hpcqf70t1, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 0, t48);
                goto gm_fdone;
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            void * t51 = gm_l0;
            void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
            uint64_t t53 = gm_l8;
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t55 = gm_f_lookup_59juqoo(t52, t53, t54);
            uint64_t t56;
            if (((((uint8_t *)&t55))[8] != 0)) {
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&t55) + 0));
                t56 = t57;
            } else {
                gm_toblvhepnjqkh t58;
                memset(&t58, 0, sizeof t58);
                uint8_t t59 = 0;
                F.ret = t58;
                goto gm_fret;
            }
            gm_l10 = t56;
        }
        {
            void * t61 = gm_l0;
            uint64_t t62 = gm_l10;
            uint64_t t63 = gm_l1;
            uint8_t t64 = gm_f_decl_visible_bd168ti(t61, t62, t63);
            uint8_t t65 = (uint8_t)!t64;
            if (!t65) goto ie60;
            {
                gm_toblvhepnjqkh t67;
                memset(&t67, 0, sizeof t67);
                uint8_t t68 = 0;
                F.ret = t67;
                goto gm_fret;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            void * t69 = gm_l0;
            uint64_t t70 = gm_l10;
            uint64_t t71 = gm_l1;
            gm_b0_1 t72 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t69, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tkb8hpcqf70t1 t73;
            memset((uint8_t *)&t73 + 20, 0, 4);
            uint64_t t74 = gm_l10;
            GM_ST(uint64_t, (uint8_t *)&t73 + 0, t74);
            gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t73 + 8, t75);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 28, 0);
            ((uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out))[24] = 1;
            GM_ST(gm_tkb8hpcqf70t1, (uint8_t *)&(*(gm_toblvhepnjqkh *)gm_out) + 0, t73);
            goto gm_fdone;
        }
        goto me3;
mn24:;
ma6:;
        gm_toblvhepnjqkh t77;
        memset(&t77, 0, sizeof t77);
        uint8_t t78 = 0;
        F.ret = t77;
        goto gm_fret;
        goto me3;
mn76:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 562, 5);
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
__attribute__((aligned(4))) void gm_f_generic_type_args_5b8bvov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_type_args_5b8bvov F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* template */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* params */
    gm_b16_8 gm_l7; /* name */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* next */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t08olq97rscn3 gm_l18; /* n */
    gm_tf5srh4mp4b3i gm_l20; /* found */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* j */
    uint32_t gm_l24; /* p */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    uint32_t gm_l29; /* pid */
    uint64_t gm_l31; /* m */
    void * gm_l33; /* d */
    void * gm_l35; /* e */
    gm_t88rcucg1b7uk gm_l36; /* vt */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* j */
    uint32_t gm_l41; /* q */
    gm_t88rcucg1b7uk gm_l42; /* t */
    GM_STACK_CHECK(&F, 3774u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l2;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcsffver18si54, 715, 13);
        gm_t6art7bprjiqb t7 = GM_LD(gm_t6art7bprjiqb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184));
        F.l5 = t7;
    }
    {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 120));
        gm_l6 = t8;
    }
    {
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_l7 = t9;
    }
    {
        gm_f_list_9tqbks7((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t11 = gm_l6;
            gm_l9 = t11;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l10;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l10;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t19 * 4));
                    {
                        void * t25 = (void *)((uint8_t *)&F.l8);
                        gm_t0mfir12leg2g t26;
                        memset(&t26, 0, sizeof t26);
                        gm_tftb1odc739t5 t27 = gm_f_push_k0n7eot(t25, &t26);
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcsffver18si54, 720, 23);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 720, 5);
me22:;
                }
lc12:;
                {
                    uint64_t t30 = gm_l10;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l10 = t33;
                }
            }
lb12:;
        }
    }
    {
        gm_l11 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t35 = gm_l3;
            gm_l12 = t35;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t37 = gm_l13;
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t39 = (t37 < t38);
                if (!t39) goto lb36;
                {
                    uint64_t t42 = gm_l13;
                    uint64_t t43 = (uintptr_t)t42;
                    uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_t7te2tdpebbt4 t47 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t43 * 144));
                    F.l14 = t47;
                    {
                        gm_l15 = ((uint64_t)0ULL);
                    }
                    {
                        gm_t0mfir12leg2g t49;
                        memset(&t49, 0, sizeof t49);
                        F.l16 = t49;
                    }
                    {
                        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        switch (t55) { case ((uint8_t)0ULL): goto ma51; case ((uint8_t)1ULL): goto ma52; case ((uint8_t)2ULL): goto ma53; default: goto ma54; }
ma51:;
                        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        if (t57 != ((uint8_t)0ULL)) goto mn56;
                        gm_tc3oht4ksth9d t58 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l14) + 0) + 8));
                        F.l17 = t58;
                        {
                            uint64_t t60 = gm_l11;
                            gm_l15 = t60;
                        }
                        {
                            uint64_t t61 = gm_l11;
                            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                            if (o63) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 728, 17);
                            uint64_t t64 = r62;
                            gm_l11 = t64;
                        }
                        {
                            const uint8_t *t65 = ((uint8_t *)&F.l17);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 92, 0);
                            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)))[88] = 1;
                            memmove((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 0, t65, 88);
                        }
                        goto me50;
mn56:;
ma52:;
                        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        if (t67 != ((uint8_t)1ULL)) goto mn66;
                        gm_t08olq97rscn3 t68 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l14) + 0) + 8) + 0));
                        gm_l18 = t68;
                        gm_tc3oht4ksth9d t69 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l14) + 0) + 8) + 32));
                        F.l19 = t69;
                        {
                            gm_tf5srh4mp4b3i t71;
                            memset(&t71, 0, sizeof t71);
                            gm_l20 = t71;
                        }
                        {
                            {
                                gm_b16_8 t73 = gm_l6;
                                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                                gm_l21 = t74;
                            }
                            {
                                gm_l22 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t76 = gm_l22;
                                    uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                    uint8_t t78 = (t76 < t77);
                                    if (!t78) goto lb75;
                                    {
                                        uint64_t t81 = gm_l22;
                                        uint64_t t82 = gm_l22;
                                        uint64_t t83 = (uintptr_t)t82;
                                        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                        uint32_t t85 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t83 * 4));
                                        gm_tn68el8d14vku t86;
                                        GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t86 + 0), t81);
                                        GM_ST(uint32_t, ((uint8_t *)&t86 + 8), t85);
                                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&t86) + 0));
                                        gm_l23 = t89;
                                        uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&t86) + 8));
                                        gm_l24 = t90;
                                        {
                                            void * t93 = gm_l0;
                                            void * t94 = (void *)(((uint8_t *)t93) + 8);
                                            uint32_t t95 = gm_l24;
                                            gm_b16_8 t96 = gm_f_param_name_rbnop5l(t94, t95);
                                            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                            uint8_t t98 = (gm_eq0iqiubfm7e(((uint8_t *)&t96), ((uint8_t *)&t97)));
                                            if (!t98) goto ie92;
                                            {
                                                uint64_t t100 = gm_l23;
                                                gm_tf5srh4mp4b3i t101;
                                                GM_ST(uint8_t, (uint8_t *)&t101 + 9, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t101 + 10, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t101 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t101), t100);
                                                ((uint8_t *)&t101)[8] = 1;
                                                gm_l20 = t101;
                                            }
                                            {
                                                goto lb75;
                                            }
                                            goto ix92;
ie92:;
ix92:;
                                        }
                                        goto me87;
mn88:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 733, 17);
me87:;
                                    }
lc75:;
                                    {
                                        uint64_t t103 = gm_l22;
                                        uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                                        uint64_t t106 = r104;
                                        gm_l22 = t106;
                                    }
                                }
lb75:;
                            }
                        }
                        {
                            gm_tf5srh4mp4b3i t107 = gm_l20;
                            uint64_t t108;
                            if (((((uint8_t *)&t107))[8] != 0)) {
                                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&t107) + 0));
                                t108 = t109;
                            } else {
                                {
                                    void * t111 = gm_l0;
                                    gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l18) + 16));
                                    gm_t6j6smj7qoctu t113;
                                    gm_b16_8 t114 = gm_l7;
                                    GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t114);
                                    gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t115);
                                    gm_tabb710d9mq2e t116;
                                    gm_f_format_bnsf58k((void *)&t116, (void *)NULL, cx, NULL, &t113);
                                    if (gm_unw.kind) {
                                        goto gm_ll40;
                                    }
                                    if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_srcsffver18si54, 740, 37);
                                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                                    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                                    gm_b0_1 t119 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t111, t112, t118);
                                    if (gm_unw.kind) {
                                        goto gm_ll40;
                                    }
                                }
                                gm_th2lqbo77pvsc t120;
                                memset(&t120, 0, sizeof t120);
                                uint8_t t121 = 0;
                                F.ret = t120;
                                goto gm_fret;
                            }
                            gm_l15 = t108;
                        }
                        {
                            const uint8_t *t122 = ((uint8_t *)&F.l19);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 92, 0);
                            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)))[88] = 1;
                            memmove((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l16)) + 0, t122, 88);
                        }
                        goto me50;
mn66:;
ma53:;
                        uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)&F.l14) + 0) + 0));
                        if (t124 != ((uint8_t)2ULL)) goto mn123;
                        {
                            void * t126 = gm_l0;
                            gm_tl874t2atnlh3 t127 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                            gm_b16_8 t128;
                            GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sldikqa9i70sm));
                            GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(51));
                            gm_b0_1 t129 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t126, t127, t128);
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t130;
                            memset(&t130, 0, sizeof t130);
                            uint8_t t131 = 0;
                            F.ret = t130;
                            goto gm_fret;
                        }
                        goto me50;
mn123:;
ma54:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 725, 9);
me50:;
                    }
                    {
                        uint64_t t133 = gm_l15;
                        uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t135 = (t133 >= t134);
                        if (!t135) goto ie132;
                        {
                            void * t137 = gm_l0;
                            gm_tl874t2atnlh3 t138 = gm_l4;
                            gm_t3877pk7lv748 t139;
                            gm_b16_8 t140 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t140);
                            uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            GM_ST(uint64_t, (uint8_t *)&t139 + 16, t141);
                            gm_tabb710d9mq2e t142;
                            gm_f_format_n6tblha((void *)&t142, (void *)NULL, cx, NULL, &t139);
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 8, gm_srcsffver18si54, 751, 27);
                            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
                            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                            gm_b0_1 t145 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t137, t138, t144);
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t146;
                            memset(&t146, 0, sizeof t146);
                            uint8_t t147 = 0;
                            F.ret = t146;
                            goto gm_fret;
                        }
                        goto ix132;
ie132:;
ix132:;
                    }
                    {
                        uint64_t t149 = gm_l15;
                        uint64_t t150 = (uintptr_t)t149;
                        uint64_t t151 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        if (t150 >= (uintptr_t)(t151)) gm_trap_index(t150, t151, gm_srcsffver18si54, 754, 12);
                        uint8_t t152;
                        if (((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t150 * 96))[88] != 0)) goto mn154;
                        t152 = ((uint8_t)0);
                        goto me153;
mn154:;
                        t152 = ((uint8_t)1);
                        goto me153;
mn155:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 754, 12);
me153:;
                        if (!t152) goto ie148;
                        {
                            void * t157 = gm_l0;
                            gm_tl874t2atnlh3 t158 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l14) + 128));
                            void * t159 = gm_l0;
                            void * t160 = (void *)(((uint8_t *)t159) + 8);
                            uint64_t t161 = gm_l15;
                            uint64_t t162 = (uintptr_t)t161;
                            uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t162 >= (uintptr_t)(t163)) gm_trap_index(t162, t163, gm_srcsffver18si54, 755, 94);
                            uint32_t t164 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t162 * 4));
                            gm_b16_8 t165 = gm_f_param_name_rbnop5l(t160, t164);
                            gm_tnpifv20irnph t166;
                            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t165);
                            gm_tabb710d9mq2e t167;
                            gm_f_format_nbjjmv4((void *)&t167, (void *)NULL, cx, NULL, t166);
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                            if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 8, gm_srcsffver18si54, 755, 29);
                            gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&t167) + 8));
                            gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                            gm_b0_1 t170 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t157, t158, t169);
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t171;
                            memset(&t171, 0, sizeof t171);
                            uint8_t t172 = 0;
                            F.ret = t171;
                            goto gm_fret;
                        }
                        goto ix148;
ie148:;
ix148:;
                    }
                    {
                        uint64_t t173 = gm_l15;
                        uint64_t t174 = (uintptr_t)t173;
                        uint64_t t175 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        if (t174 >= (uintptr_t)(t175)) gm_trap_index(t174, t175, gm_srcsffver18si54, 758, 9);
                        gm_t0mfir12leg2g t176 = F.l16;
                        GM_ST(gm_t0mfir12leg2g, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t174 * 96), t176);
                    }
                    goto me45;
mn46:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 722, 5);
me45:;
                }
lc36:;
                {
                    uint64_t t178 = gm_l13;
                    uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                    uint64_t t181 = r179;
                    gm_l13 = t181;
                }
            }
gm_ll40:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
            }
            goto gm_lad;
lb36:;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t183 = gm_l6;
            gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
            gm_l26 = t184;
        }
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t186 = gm_l27;
                uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                uint8_t t188 = (t186 < t187);
                if (!t188) goto lb185;
                {
                    uint64_t t191 = gm_l27;
                    uint64_t t192 = gm_l27;
                    uint64_t t193 = (uintptr_t)t192;
                    uint64_t t194 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                    uint32_t t195 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t193 * 4));
                    gm_tn68el8d14vku t196;
                    GM_ST(uint32_t, (uint8_t *)&t196 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t196 + 0), t191);
                    GM_ST(uint32_t, ((uint8_t *)&t196 + 8), t195);
                    uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&t196) + 0));
                    gm_l28 = t199;
                    uint32_t t200 = GM_LD(uint32_t, (((uint8_t *)&t196) + 8));
                    gm_l29 = t200;
                    {
                        gm_t0mfir12leg2g t202;
                        memset(&t202, 0, sizeof t202);
                        F.l30 = t202;
                    }
                    {
                        uint64_t t203 = gm_l1;
                        gm_l31 = t203;
                    }
                    {
                        uint64_t t205 = gm_l28;
                        uint64_t t206 = (uintptr_t)t205;
                        uint64_t t207 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        if (t206 >= (uintptr_t)(t207)) gm_trap_index(t206, t207, gm_srcsffver18si54, 764, 17);
                        if (!((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t206 * 96))[88] != 0)) goto ie204;
                        gm_tc3oht4ksth9d t208 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t206 * 96));
                        F.l32 = t208;
                        {
                            const uint8_t *t210 = ((uint8_t *)&F.l32);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 92, 0);
                            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)))[88] = 1;
                            memmove((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 0, t210, 88);
                        }
                        goto ix204;
ie204:;
                        uint64_t t212 = gm_l28;
                        uint64_t t213 = (uintptr_t)t212;
                        uint64_t t214 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 136) + 8);
                        if (t213 >= (uintptr_t)(t214)) gm_trap_index(t213, t214, gm_srcsffver18si54, 766, 24);
                        void * t215 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 136)) + t213 * 8));
                        if (!(GM_LD(void *, ((uint8_t *)&t215)) != 0)) goto ie211;
                        void * t216 = GM_LD(void *, ((uint8_t *)&t215));
                        gm_l33 = t216;
                        {
                            void * t218 = gm_l33;
                            const uint8_t *t219 = ((uint8_t *)t218);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 89, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 90, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 92, 0);
                            ((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)))[88] = 1;
                            memmove((uint8_t *)&(*(gm_t0mfir12leg2g *)((uint8_t *)&F.l30)) + 0, t219, 88);
                        }
                        {
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 16));
                            gm_l31 = t220;
                        }
                        goto ix211;
ie211:;
                        uint32_t t222 = gm_l29;
                        uint64_t t223 = (uint64_t)t222;
                        uint64_t t224 = (uintptr_t)t223;
                        void * t225 = gm_l0;
                        uint64_t t226 = GM_LD(uintptr_t, ((((uint8_t *)t225) + 1128) + 0) + 8);
                        if (t224 >= (uintptr_t)(t226)) gm_trap_index(t224, t226, gm_srcsffver18si54, 770, 19);
                        uint8_t t227 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t225) + 1128) + 0)) + t224 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                        if (!t227) goto ie221;
                        {
                            void * t229 = (void *)((uint8_t *)&F.l25);
                            void * t230 = gm_l0;
                            void * t231 = (void *)(((uint8_t *)t230) + 8);
                            gm_tnpifv20irnph t232 = gm_f_empty_effects_r0iv3pv();
                            gm_t53at80pfdus3 t233;
                            memset(&t233, 0, sizeof t233);
                            GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)24ULL));
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t233 + 8), t232);
                            gm_t88rcucg1b7uk t234 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t231, &t233);
                            if (gm_unw.kind) {
                                goto gm_ll189;
                            }
                            gm_tftb1odc739t5 t235 = gm_f_push_k3l9nic(t229, GM_CONV(gm_csguankko4lmhd, t234));
                            if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 1, gm_srcsffver18si54, 772, 13);
                            gm_b0_1 t236 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 1));
                        }
                        {
                            goto lc185;
                        }
                        goto ix221;
ie221:;
                        {
                            void * t238 = gm_l0;
                            gm_tl874t2atnlh3 t239 = gm_l4;
                            gm_t6j6smj7qoctu t240;
                            void * t241 = gm_l0;
                            void * t242 = (void *)(((uint8_t *)t241) + 8);
                            uint32_t t243 = gm_l29;
                            gm_b16_8 t244 = gm_f_param_name_rbnop5l(t242, t243);
                            GM_ST(gm_b16_8, (uint8_t *)&t240 + 0, t244);
                            gm_b16_8 t245 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t240 + 16, t245);
                            gm_tabb710d9mq2e t246;
                            gm_f_format_l0aimgq((void *)&t246, (void *)NULL, cx, NULL, &t240);
                            if (gm_unw.kind) {
                                goto gm_ll189;
                            }
                            if (((((uint8_t *)&t246))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t246) + 8, gm_srcsffver18si54, 775, 27);
                            gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&t246) + 8));
                            gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)&t247));
                            gm_b0_1 t249 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t238, t239, t248);
                            if (gm_unw.kind) {
                                goto gm_ll189;
                            }
                        }
                        {
                            gm_th2lqbo77pvsc t250;
                            memset(&t250, 0, sizeof t250);
                            uint8_t t251 = 0;
                            F.ret = t250;
                            goto gm_fret;
                        }
ix221:;
ix211:;
ix204:;
                    }
                    {
                        gm_t0mfir12leg2g t252 = F.l30;
                        gm_tc3oht4ksth9d t253;
                        if (((((uint8_t *)&t252))[88] != 0)) {
                            t253 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t252) + 0));
                        } else {
                            gm_th2lqbo77pvsc t254;
                            memset(&t254, 0, sizeof t254);
                            uint8_t t255 = 0;
                            F.ret = t254;
                            goto gm_fret;
                        }
                        F.l34 = t253;
                    }
                    {
                        void * t256 = (void *)((uint8_t *)&F.l34);
                        gm_l35 = t256;
                    }
                    {
                        uint32_t t257 = gm_l29;
                        uint64_t t258 = (uint64_t)t257;
                        uint64_t t259 = (uintptr_t)t258;
                        void * t260 = gm_l0;
                        uint64_t t261 = GM_LD(uintptr_t, ((((uint8_t *)t260) + 1128) + 0) + 8);
                        if (t259 >= (uintptr_t)(t261)) gm_trap_index(t259, t261, gm_srcsffver18si54, 780, 20);
                        gm_t88rcucg1b7uk t262;
                        uint8_t t268 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 1128) + 0)) + t259 * 48) + 0) + 0));
                        switch (t268) { case ((uint8_t)0ULL): goto ma266; case ((uint8_t)1ULL): goto ma265; case ((uint8_t)2ULL): goto ma264; default: goto ma267; }
ma264:;
                        uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 1128) + 0)) + t259 * 48) + 0) + 0));
                        if (t270 != ((uint8_t)2ULL)) goto mn269;
                        gm_t88rcucg1b7uk t271 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 1128) + 0)) + t259 * 48) + 0) + 4));
                        gm_l36 = t271;
                        {
                            gm_f_list_oalq03d((void *)&F.l37, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll189;
                            }
                        }
                        {
                            {
                                uint64_t t274 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                void * t275 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                                uint64_t t276 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t277 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                                uint64_t t278 = (uintptr_t)t277;
                                if (t276 > t278 || t278 > (uintptr_t)t274) gm_trap_slice(t276, t278, t274, gm_srcsffver18si54, 783, 39);
                                gm_b16_8 t279;
                                GM_ST(void *, (uint8_t *)&t279, (void *)(((uint8_t *)t275 + t276 * 4)));
                                GM_ST(uintptr_t, (uint8_t *)&t279 + 8, (uintptr_t)(t278 - t276));
                                gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
                                gm_l38 = t280;
                            }
                            {
                                gm_l39 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t282 = gm_l39;
                                    uint64_t t283 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                    uint8_t t284 = (t282 < t283);
                                    if (!t284) goto lb281;
                                    {
                                        uint64_t t287 = gm_l39;
                                        uint64_t t288 = gm_l39;
                                        uint64_t t289 = (uintptr_t)t288;
                                        uint64_t t290 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                        uint32_t t291 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t289 * 4));
                                        gm_tn68el8d14vku t292;
                                        GM_ST(uint32_t, (uint8_t *)&t292 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t292 + 0), t287);
                                        GM_ST(uint32_t, ((uint8_t *)&t292 + 8), t291);
                                        uint64_t t295 = GM_LD(uint64_t, (((uint8_t *)&t292) + 0));
                                        gm_l40 = t295;
                                        uint32_t t296 = GM_LD(uint32_t, (((uint8_t *)&t292) + 8));
                                        gm_l41 = t296;
                                        {
                                            void * t298 = (void *)((uint8_t *)&F.l37);
                                            uint32_t t299 = gm_l41;
                                            uint64_t t300 = gm_l40;
                                            uint64_t t301 = (uintptr_t)t300;
                                            uint64_t t302 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                                            if (t301 >= (uintptr_t)(t302)) gm_trap_index(t301, t302, gm_srcsffver18si54, 783, 96);
                                            gm_t88rcucg1b7uk t303 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t301 * 4));
                                            gm_tplpbqf3duvn3 t304;
                                            GM_ST(uint32_t, ((uint8_t *)&t304 + 0), t299);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t304 + 4), t303);
                                            gm_tftb1odc739t5 t305 = gm_f_push_h69n5fi(t298, GM_CONV(gm_cs7e9ppsd384bj, t304));
                                            if (((((uint8_t *)&t305))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t305) + 1, gm_srcsffver18si54, 783, 67);
                                            gm_b0_1 t306 = GM_LD(gm_b0_1, (((uint8_t *)&t305) + 1));
                                        }
                                        goto me293;
mn294:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 783, 17);
me293:;
                                    }
lc281:;
                                    {
                                        uint64_t t308 = gm_l39;
                                        uint64_t r309; bool o310 = __builtin_add_overflow(t308, ((uint64_t)1ULL), &r309);
                                        uint64_t t311 = r309;
                                        gm_l39 = t311;
                                    }
                                }
lb281:;
                            }
                        }
                        void * t312 = gm_l0;
                        uint64_t t313 = gm_l31;
                        void * t314 = gm_l0;
                        gm_t88rcucg1b7uk t315 = gm_l36;
                        gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                        gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
                        gm_t88rcucg1b7uk t318 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t314, t315, t317);
                        if (gm_unw.kind) {
                            goto gm_ll189;
                        }
                        void * t319 = gm_l35;
                        gm_tkn42u7na3t81 t320 = gm_f_comptime_value_l5orc93((void *)NULL, cx, NULL, t312, t313, t318, t319);
                        if (gm_unw.kind) {
                            goto gm_ll189;
                        }
                        gm_t88rcucg1b7uk t321;
                        if (((((uint8_t *)&t320))[4] != 0)) {
                            gm_t88rcucg1b7uk t322 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t320) + 0));
                            t321 = t322;
                        } else {
                            gm_th2lqbo77pvsc t323;
                            memset(&t323, 0, sizeof t323);
                            uint8_t t324 = 0;
                            F.ret = t323;
                            goto gm_fret;
                        }
                        gm_t88rcucg1b7uk t325 = t321;
                        t262 = t325;
                        goto me263;
mn269:;
ma265:;
                        uint8_t t327 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 1128) + 0)) + t259 * 48) + 0) + 0));
                        if (t327 != ((uint8_t)1ULL)) goto mn326;
                        void * t328 = gm_l0;
                        uint64_t t329 = gm_l31;
                        void * t330 = gm_l35;
                        gm_t88rcucg1b7uk t331 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t328, t329, t330);
                        if (gm_unw.kind) {
                            goto gm_ll189;
                        }
                        t262 = t331;
                        goto me263;
mn326:;
ma266:;
                        uint8_t t333 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 1128) + 0)) + t259 * 48) + 0) + 0));
                        if (t333 != ((uint8_t)0ULL)) goto mn332;
                        void * t334 = gm_l0;
                        uint64_t t335 = gm_l31;
                        void * t336 = gm_l35;
                        gm_t88rcucg1b7uk t337 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t334, t335, t336);
                        if (gm_unw.kind) {
                            goto gm_ll189;
                        }
                        t262 = t337;
                        goto me263;
mn332:;
ma267:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 780, 14);
me263:;
                        gm_l42 = t262;
                    }
                    {
                        void * t339 = gm_l0;
                        void * t340 = (void *)(((uint8_t *)t339) + 8);
                        gm_t88rcucg1b7uk t341 = gm_l42;
                        uint8_t t342 = gm_f_is_error_8k27gd1(t340, GM_CONV(gm_csguankko4lmhd, t341));
                        if (!t342) goto ie338;
                        {
                            gm_th2lqbo77pvsc t344;
                            memset(&t344, 0, sizeof t344);
                            uint8_t t345 = 0;
                            F.ret = t344;
                            goto gm_fret;
                        }
                        goto ix338;
ie338:;
ix338:;
                    }
                    {
                        void * t346 = (void *)((uint8_t *)&F.l25);
                        gm_t88rcucg1b7uk t347 = gm_l42;
                        gm_tftb1odc739t5 t348 = gm_f_push_k3l9nic(t346, GM_CONV(gm_csguankko4lmhd, t347));
                        if (((((uint8_t *)&t348))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t348) + 1, gm_srcsffver18si54, 790, 9);
                        gm_b0_1 t349 = GM_LD(gm_b0_1, (((uint8_t *)&t348) + 1));
                    }
                    goto me197;
mn198:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 761, 5);
me197:;
                }
lc185:;
                {
                    uint64_t t351 = gm_l27;
                    uint64_t r352; bool o353 = __builtin_add_overflow(t351, ((uint64_t)1ULL), &r352);
                    uint64_t t354 = r352;
                    gm_l27 = t354;
                }
            }
gm_ll189:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb185; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc185; }
            }
            goto gm_lad;
lb185:;
        }
    }
    {
        gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t355);
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
extern __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hoists_13k2a2s F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* x */
    void * gm_l3; /* inner */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma5:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)28ULL)) goto mn10;
        gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t12;
        void * t13 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
        t3 = t13;
        goto me4;
mn10:;
ma6:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)24ULL)) goto mn14;
        void * t16 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t16;
        void * t17 = gm_l2;
        t3 = t17;
        goto me4;
mn14:;
ma7:;
        uint8_t t19 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me4;
mn18:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 285, 14);
me4:;
        gm_l3 = t3;
    }
    {
        void * t21 = gm_l3;
        void * t22 = gm_f_construction_call_ktqtrpo(t21);
        uint8_t t23;
        if ((GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto mn25;
        t23 = ((uint8_t)0);
        goto me24;
mn25:;
        t23 = ((uint8_t)1);
        goto me24;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 290, 8);
me24:;
        if (!t23) goto ie20;
        {
            uint8_t t28 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_f_prefix_operand_3n5jchh(t29);
        uint8_t t31 = gm_f_pushes_down_0djaflp(t30);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tp8mv052i18rg gm_f_int_type_name_a77a0kf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_type_name_a77a0kf F;
    gm_b16_8 gm_l0; /* name */
    uint32_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 320u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t7 = (t6 < ((uint64_t)2ULL));
        if (!t7) {
            uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcsffver18si54, 5, 24);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
            gm_tqs5p4n2tq19v t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)117ULL));
            GM_ST(uint8_t, ((uint8_t *)&t11 + 1), ((uint8_t)105ULL));
            uint8_t t12 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t11) + 0, ((uint8_t *)&t10))) t12 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t11) + 1, ((uint8_t *)&t10))) t12 = 1;
            uint8_t t13 = (uint8_t)(!t12);
            t5 = t13;
        } else t5 = 1;
        if (!t5) {
            uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcsffver18si54, 5, 53);
            uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
            uint8_t t17 = ((t16 == ((uint8_t)48ULL)));
            t4 = t17;
        } else t4 = 1;
        if (!t4) {
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t19 = (t18 > ((uint64_t)4ULL));
            t3 = t19;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_tp8mv052i18rg t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l1 = ((uint32_t)0ULL);
    }
    {
        {
            uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t26 = (uintptr_t)((uint64_t)1ULL);
            if (t26 > (uintptr_t)t24 || (uintptr_t)t24 > (uintptr_t)t24) gm_trap_slice(t26, (uintptr_t)t24, t24, gm_srcsffver18si54, 7, 14);
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t25 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)((uintptr_t)t24 - t26));
            gm_l2 = t27;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t29 = gm_l3;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t31 = (t29 < t30);
                if (!t31) goto lb28;
                {
                    uint64_t t34 = gm_l3;
                    uint64_t t35 = (uintptr_t)t34;
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t35 * 1));
                    uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)&t37));
                    gm_l4 = t40;
                    {
                        {
                            uint8_t t44 = gm_l4;
                            gm_l5 = t44;
                        }
                        uint8_t t45 = 0;
                        uint8_t t46 = gm_l5;
                        uint8_t t47 = (((uint8_t)48ULL) <= t46);
                        if (t47) {
                            uint8_t t48 = gm_l5;
                            uint8_t t49 = (t48 <= ((uint8_t)57ULL));
                            t45 = t49;
                        } else t45 = 0;
                        uint8_t t50 = (uint8_t)!t45;
                        uint8_t t51 = t50;
                        if (!t51) goto ie42;
                        {
                            gm_tp8mv052i18rg t53;
                            memset(&t53, 0, sizeof t53);
                            uint8_t t54 = 0;
                            F.ret = t53;
                            goto gm_fret;
                        }
                        goto ix42;
ie42:;
ix42:;
                    }
                    {
                        uint32_t t55 = gm_l1;
                        uint32_t r56; bool o57 = __builtin_mul_overflow(t55, ((uint32_t)10ULL), &r56);
                        if (o57) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 9, 16);
                        uint32_t t58 = r56;
                        uint8_t t59 = gm_l4;
                        uint8_t r60; bool o61 = __builtin_sub_overflow(t59, ((uint8_t)48ULL), &r60);
                        if (o61) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 9, 32);
                        uint8_t t62 = r60;
                        uint32_t t63 = (uint32_t)t62;
                        uint32_t r64; bool o65 = __builtin_add_overflow(t58, t63, &r64);
                        if (o65) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 9, 16);
                        uint32_t t66 = r64;
                        gm_l1 = t66;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 7, 5);
me38:;
                }
lc28:;
                {
                    uint64_t t68 = gm_l3;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l3 = t71;
                }
            }
lb28:;
        }
    }
    {
        {
            uint32_t t74 = gm_l1;
            gm_l6 = t74;
        }
        uint8_t t75 = 0;
        uint32_t t76 = gm_l6;
        uint8_t t77 = (((uint32_t)1ULL) <= t76);
        if (t77) {
            uint32_t t78 = gm_l6;
            uint8_t t79 = (t78 <= ((uint32_t)128ULL));
            t75 = t79;
        } else t75 = 0;
        uint8_t t80 = (uint8_t)!t75;
        uint8_t t81 = t80;
        if (!t81) goto ie72;
        {
            gm_tp8mv052i18rg t83;
            memset(&t83, 0, sizeof t83);
            uint8_t t84 = 0;
            F.ret = t83;
            goto gm_fret;
        }
        goto ix72;
ie72:;
ix72:;
    }
    {
        uint64_t t85 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_srcsffver18si54, 12, 21);
        uint8_t t87 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t85 * 1));
        uint8_t t88 = ((t87 == ((uint8_t)105ULL)));
        uint32_t t89 = gm_l1;
        uint16_t t90 = (uint16_t)t89;
        gm_tjcj4j2gbh6c8 t91;
        GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t91 + 0), t88);
        GM_ST(uint16_t, ((uint8_t *)&t91 + 2), t90);
        gm_tp8mv052i18rg t92;
        GM_ST(uint8_t, (uint8_t *)&t92 + 5, 0);
        GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t92), t91);
        ((uint8_t *)&t92)[4] = 1;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_builtin_type_name_v5ttoka(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_builtin_type_name_v5ttoka F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 445u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        gm_b16_8 t3 = gm_l0;
        gm_tp8mv052i18rg t4 = gm_f_int_type_name_a77a0kf(t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[4] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 49, 12);
me6:;
        if (!t5) {
            gm_b16_8 t9 = gm_l0;
            gm_th4d8o29g5urm t10;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s6groq37ram3h));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s0i3i86nphpsh));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t12);
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_shnig5tqtdedi));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 32, t13);
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sm9e81djuncnu));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 48, t14);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sqqajgod2bb4o));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 64, t15);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s929lennfd3g6));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 80, t16);
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s3cv29r83pgn4));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 96, t17);
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s2737i1iq57nf));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 112, t18);
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_slolcr6ikvg33));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 128, t19);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s0iqiubfm7e7e));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 144, t20);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s47llp68ptjgu));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 160, t21);
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sflee3fpg0btv));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 176, t22);
            uint8_t t23 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 0, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 16, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 32, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 48, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 64, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 80, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 96, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 112, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 128, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 144, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 160, ((uint8_t *)&t9))) t23 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 176, ((uint8_t *)&t9))) t23 = 1;
            uint8_t t24 = (uint8_t)(t23);
            t2 = t24;
        } else t2 = 1;
        uint8_t t25 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_oversized_int_name_nr25haf(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_oversized_int_name_nr25haf F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* bits */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 365u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t6 = (t5 < ((uint64_t)2ULL));
        if (!t6) {
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcsffver18si54, 17, 24);
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t7 * 1));
            gm_tqs5p4n2tq19v t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)117ULL));
            GM_ST(uint8_t, ((uint8_t *)&t10 + 1), ((uint8_t)105ULL));
            uint8_t t11 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t10) + 0, ((uint8_t *)&t9))) t11 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t10) + 1, ((uint8_t *)&t9))) t11 = 1;
            uint8_t t12 = (uint8_t)(!t11);
            t4 = t12;
        } else t4 = 1;
        if (!t4) {
            uint64_t t13 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcsffver18si54, 17, 53);
            uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t13 * 1));
            uint8_t t16 = ((t15 == ((uint8_t)48ULL)));
            t3 = t16;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t18 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t21 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
            if (t22 > (uintptr_t)t20 || (uintptr_t)t20 > (uintptr_t)t20) gm_trap_slice(t22, (uintptr_t)t20, t20, gm_srcsffver18si54, 18, 14);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t21 + t22 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)((uintptr_t)t20 - t22));
            gm_l1 = t23;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l2;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l2;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t31 * 1));
                    uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t33));
                    gm_l3 = t36;
                    {
                        {
                            uint8_t t40 = gm_l3;
                            gm_l4 = t40;
                        }
                        uint8_t t41 = 0;
                        uint8_t t42 = gm_l4;
                        uint8_t t43 = (((uint8_t)48ULL) <= t42);
                        if (t43) {
                            uint8_t t44 = gm_l4;
                            uint8_t t45 = (t44 <= ((uint8_t)57ULL));
                            t41 = t45;
                        } else t41 = 0;
                        uint8_t t46 = (uint8_t)!t41;
                        uint8_t t47 = t46;
                        if (!t47) goto ie38;
                        {
                            uint8_t t49 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 18, 5);
me34:;
                }
lc24:;
                {
                    uint64_t t51 = gm_l2;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l2 = t54;
                }
            }
lb24:;
        }
    }
    {
        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t57 = (t56 > ((uint64_t)4ULL));
        if (!t57) goto ie55;
        {
            uint8_t t59 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        gm_l5 = ((uint32_t)0ULL);
    }
    {
        {
            uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t62 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t63 = (uintptr_t)((uint64_t)1ULL);
            if (t63 > (uintptr_t)t61 || (uintptr_t)t61 > (uintptr_t)t61) gm_trap_slice(t63, (uintptr_t)t61, t61, gm_srcsffver18si54, 23, 14);
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t62 + t63 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)((uintptr_t)t61 - t63));
            gm_l6 = t64;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t66 = gm_l7;
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t68 = (t66 < t67);
                if (!t68) goto lb65;
                {
                    uint64_t t71 = gm_l7;
                    uint64_t t72 = (uintptr_t)t71;
                    uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t74 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t72 * 1));
                    uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)&t74));
                    gm_l8 = t77;
                    {
                        uint32_t t79 = gm_l5;
                        uint32_t r80; bool o81 = __builtin_mul_overflow(t79, ((uint32_t)10ULL), &r80);
                        if (o81) gm_trap("integer overflow in `*`", gm_srcsffver18si54, 23, 33);
                        uint32_t t82 = r80;
                        uint8_t t83 = gm_l8;
                        uint8_t r84; bool o85 = __builtin_sub_overflow(t83, ((uint8_t)48ULL), &r84);
                        if (o85) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 23, 49);
                        uint8_t t86 = r84;
                        uint32_t t87 = (uint32_t)t86;
                        uint32_t r88; bool o89 = __builtin_add_overflow(t82, t87, &r88);
                        if (o89) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 23, 33);
                        uint32_t t90 = r88;
                        gm_l5 = t90;
                    }
                    goto me75;
mn76:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 23, 5);
me75:;
                }
lc65:;
                {
                    uint64_t t92 = gm_l7;
                    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                    uint64_t t95 = r93;
                    gm_l7 = t95;
                }
            }
lb65:;
        }
    }
    {
        uint32_t t96 = gm_l5;
        uint8_t t97 = (t96 > ((uint32_t)128ULL));
        uint8_t t98 = 0;
        F.ret = t97;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_path_qa34i5j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_path_qa34i5j F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t10 != ((uint8_t)6ULL)) goto mn9;
    t2 = ((uint8_t)1);
    goto me3;
mn9:;
ma5:;
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t12 != ((uint8_t)22ULL)) goto mn11;
    void * t13 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l1 = t13;
    void * t14 = gm_l1;
    uint8_t t15 = gm_f_is_path_qa34i5j(t14);
    t2 = t15;
    goto me3;
mn11:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn16:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 276, 46);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4_c(void * a0, gm_td7pkamr64l0c a1) { gm_f_leave_module_scope_4hp5mc4(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_len_param_names_cilm68f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_len_param_names_cilm68f F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    void * gm_l3; /* out */
    uint32_t gm_l4; /* p */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l7; /* m */
    gm_b16_8 gm_l8; /* n */
    gm_t88rcucg1b7uk gm_l9; /* a */
    gm_t88rcucg1b7uk gm_l10; /* b */
    GM_STACK_CHECK(&F, 569u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t13 != ((uint8_t)19ULL)) goto mn12;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l4 = t14;
        {
            gm_b16_8 t16 = gm_l2;
            gm_l5 = t16;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l6;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tplpbqf3duvn3 t28 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t24 * 8));
                    gm_l7 = t28;
                    {
                        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
                        uint32_t t32 = gm_l4;
                        uint8_t t33 = ((t31 == t32));
                        if (!t33) goto ie30;
                        {
                            gm_t6j6smj7qoctu t35;
                            void * t36 = gm_l0;
                            void * t37 = (void *)(((uint8_t *)t36) + 8);
                            uint32_t t38 = gm_l4;
                            gm_b16_8 t39 = gm_f_param_name_rbnop5l(t37, t38);
                            GM_ST(gm_b16_8, (uint8_t *)&t35 + 0, t39);
                            void * t40 = gm_l0;
                            gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 4));
                            gm_b16_8 t42 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t40, t41);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t35 + 16, t42);
                            gm_tabb710d9mq2e t43;
                            gm_f_format_vom8db6((void *)&t43, (void *)NULL, cx, NULL, &t35);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcsffver18si54, 700, 29);
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                            gm_l8 = t45;
                        }
                        {
                            gm_b16_8 t47 = gm_l8;
                            void * t48 = gm_l3;
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 0));
                            uint8_t t50 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t49) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t49))) + i * 16, ((uint8_t *)&t47))) { t50 = 1; break; }
                            uint8_t t51 = (uint8_t)(!t50);
                            if (!t51) goto ie46;
                            {
                                void * t53 = gm_l3;
                                gm_b16_8 t54 = gm_l8;
                                gm_tftb1odc739t5 t55 = gm_f_push_3ef746b(t53, t54);
                                if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcsffver18si54, 701, 41);
                                gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                            }
                            goto ix46;
ie46:;
ix46:;
                        }
                        goto ix30;
ie30:;
ix30:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 698, 22);
me26:;
                }
lc17:;
                {
                    uint64_t t58 = gm_l6;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l6 = t61;
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
        goto me6;
mn12:;
ma8:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t63 != ((uint8_t)22ULL)) goto mn62;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l9 = t64;
        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
        gm_l10 = t65;
        {
            void * t67 = gm_l0;
            gm_t88rcucg1b7uk t68 = gm_l9;
            gm_b16_8 t69 = gm_l2;
            void * t70 = gm_l3;
            gm_b0_1 t71 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t67, t68, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t72 = gm_l0;
            gm_t88rcucg1b7uk t73 = gm_l10;
            gm_b16_8 t74 = gm_l2;
            void * t75 = gm_l3;
            gm_b0_1 t76 = gm_f_len_param_names_cilm68f((void *)NULL, cx, NULL, t72, t73, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me6;
mn62:;
ma9:;
        goto me6;
mn77:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 697, 5);
me6:;
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
__attribute__((aligned(4))) uint8_t gm_f_len_params_bound_f7mjvn9(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_len_params_bound_f7mjvn9 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    uint32_t gm_l3; /* p */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l6; /* m */
    gm_t428ue2u3hf5g gm_l7; /* k */
    gm_t88rcucg1b7uk gm_l8; /* a */
    gm_t88rcucg1b7uk gm_l9; /* b */
    GM_STACK_CHECK(&F, 445u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma7:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t13 != ((uint8_t)19ULL)) goto mn12;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l3 = t14;
        {
            {
                gm_b16_8 t17 = gm_l2;
                gm_l4 = t17;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t19 = gm_l5;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t21 = (t19 < t20);
                    if (!t21) goto lb18;
                    {
                        uint64_t t24 = gm_l5;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tplpbqf3duvn3 t29 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t25 * 8));
                        gm_l6 = t29;
                        {
                            uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                            uint32_t t33 = gm_l3;
                            uint8_t t34 = ((t32 == t33));
                            if (!t34) goto ie31;
                            {
                                void * t37 = gm_l0;
                                void * t38 = (void *)(((uint8_t *)t37) + 8);
                                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 4));
                                gm_t53at80pfdus3 t40;
                                gm_f_kind_jcnt4hm((void *)&t40, t38, GM_CONV(gm_csguankko4lmhd, t39));
                                uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
                                if (t41 != ((uint8_t)21ULL)) goto ie36;
                                gm_l7 = (*(gm_t428ue2u3hf5g *)(((uint8_t *)&t40) + 8));
                                {
                                    gm_tf5srh4mp4b3i t43 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l7)));
                                    uint8_t t44;
                                    if (((((uint8_t *)&t43))[8] != 0)) goto mn46;
                                    t44 = ((uint8_t)0);
                                    goto me45;
mn46:;
                                    t44 = ((uint8_t)1);
                                    goto me45;
mn47:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 684, 63);
me45:;
                                    uint8_t t48 = 0;
                                    F.ret = t44;
                                    goto gm_fret;
                                }
                                goto ix36;
ie36:;
ix36:;
                            }
                            {
                                uint8_t t49 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix31;
ie31:;
ix31:;
                        }
                        goto me27;
mn28:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 682, 13);
me27:;
                    }
lc18:;
                    {
                        uint64_t t51 = gm_l5;
                        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                        uint64_t t54 = r52;
                        gm_l5 = t54;
                    }
                }
lb18:;
            }
        }
        {
            uint8_t t55 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me6;
mn12:;
ma8:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t57 != ((uint8_t)22ULL)) goto mn56;
        gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l8 = t58;
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
        gm_l9 = t59;
        uint8_t t60 = 0;
        void * t61 = gm_l0;
        gm_t88rcucg1b7uk t62 = gm_l8;
        gm_b16_8 t63 = gm_l2;
        uint8_t t64 = gm_f_len_params_bound_f7mjvn9(t61, GM_CONV(gm_csguankko4lmhd, t62), t63);
        if (t64) {
            void * t65 = gm_l0;
            gm_t88rcucg1b7uk t66 = gm_l9;
            gm_b16_8 t67 = gm_l2;
            uint8_t t68 = gm_f_len_params_bound_f7mjvn9(t65, GM_CONV(gm_csguankko4lmhd, t66), t67);
            t60 = t68;
        } else t60 = 0;
        uint8_t t69 = 0;
        F.ret = t60;
        goto gm_fret;
        goto me6;
mn56:;
ma9:;
        uint8_t t71 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me6;
mn70:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 680, 5);
me6:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_length_is_type_soi53ah(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_length_is_type_soi53ah F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* len */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)6ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t11;
        uint8_t t12 = 0;
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_t9805qtonbsag t15 = gm_f_value_param_tof1h6t(t13, t14);
        uint8_t t16;
        if (((((uint8_t *)&t15))[4] != 0)) goto mn18;
        t16 = ((uint8_t)1);
        goto me17;
mn18:;
        t16 = ((uint8_t)0);
        goto me17;
mn19:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 593, 30);
me17:;
        if (t16) {
            void * t20 = gm_l0;
            uint64_t t21 = gm_l1;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint8_t t23 = gm_f_names_type_rjdgib9((void *)NULL, cx, NULL, t20, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t12 = t23;
        } else t12 = 0;
        uint8_t t24 = 0;
        F.ret = t12;
        goto gm_fret;
        goto me3;
mn9:;
ma5:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)22ULL)) goto mn25;
        void * t27 = gm_l0;
        uint64_t t28 = gm_l1;
        void * t29 = gm_l2;
        uint8_t t30 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t27, t28, t29, ((uint32_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
        goto me3;
mn25:;
ma6:;
        uint8_t t33 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn32:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 592, 5);
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
inline __attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_const_idpokjh F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 411u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1568) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 109, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1568) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 110, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1568) + 0)) + t15 * 224) + 0));
                gm_b16_8 t19 = gm_l1;
                uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (!t20) goto ie13;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t t23 = (uintptr_t)t22;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 1568) + 0) + 8);
                    if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcsffver18si54, 110, 58);
                    const uint8_t *t26 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 1568) + 0)) + t23 * 224);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out) + 225, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out) + 226, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out) + 228, 0);
                    ((uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out))[224] = 1;
                    memmove((uint8_t *)&(*(gm_tk1gen3hgkm51 *)gm_out) + 0, t26, 224);
                    goto gm_fdone;
                }
                goto ix13;
ie13:;
ix13:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_tk1gen3hgkm51 t27;
        memset(&t27, 0, sizeof t27);
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tk1gen3hgkm51 gm_f_local_const_idpokjh_c(void * a0, gm_b16_8 a1) { gm_tk1gen3hgkm51 gm_r; gm_f_local_const_idpokjh(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_fn_aji8qnd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_local_fn_aji8qnd F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1608) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 119, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1608) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 120, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1608) + 0)) + t15 * 24) + 0));
                gm_b16_8 t19 = gm_l1;
                uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (!t20) goto ie13;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t t23 = (uintptr_t)t22;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 1608) + 0) + 8);
                    if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcsffver18si54, 120, 55);
                    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 1608) + 0)) + t23 * 24) + 16));
                    gm_t9805qtonbsag t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t27), t26);
                    ((uint8_t *)&t27)[4] = 1;
                    uint8_t t28 = 0;
                    F.ret = t27;
                    goto gm_fret;
                }
                goto ix13;
ie13:;
ix13:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_t9805qtonbsag t29;
        memset(&t29, 0, sizeof t29);
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_looks_like_type_ho5qv1k F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    void * gm_l5; /* inner */
    gm_t3mf58gv9kf3n gm_l6; /* a */
    void * gm_l7; /* inner */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l12; /* lhs */
    void * gm_l13; /* callee */
    gm_tkb8hpcqf70t1 gm_l14; /* g */
    void * gm_l15; /* inner */
    GM_STACK_CHECK(&F, 1351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma13; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma13; case ((uint8_t)45ULL): goto ma13; case ((uint8_t)46ULL): goto ma13; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma13; case ((uint8_t)50ULL): goto ma13; default: goto ma13; }
ma4:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)6ULL)) goto mn16;
        gm_t08olq97rscn3 t18 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t18;
        {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l1;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tf5srh4mp4b3i t24 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t24))[8] != 0)) goto ie20;
            uint64_t t25 = GM_LD(uint64_t, ((uint8_t *)&t24));
            gm_l4 = t25;
            {
                void * t27 = gm_l0;
                uint64_t t28 = gm_l4;
                gm_t6qooom8qa2do t29;
                gm_f_resolve_decl_pus042v((void *)&t29, (void *)NULL, cx, NULL, t27, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t30;
                uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
                if (t33 != ((uint8_t)0ULL)) goto mn32;
                t30 = ((uint8_t)1);
                goto me31;
mn32:;
                t30 = ((uint8_t)0);
                goto me31;
mn34:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 221, 68);
me31:;
                uint8_t t35 = 0;
                F.ret = t30;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint8_t t37 = gm_f_is_builtin_type_name_v5ttoka(t36);
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto me3;
mn16:;
ma5:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)9ULL)) goto mn39;
        void * t41 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t41;
        void * t42 = gm_l0;
        uint64_t t43 = gm_l1;
        void * t44 = gm_l5;
        uint8_t t45 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t42, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
        goto me3;
mn39:;
ma6:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)28ULL)) goto mn47;
        gm_t3mf58gv9kf3n t49 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t49;
        {
            void * t52 = gm_l2;
            uint8_t t53 = gm_f_hoists_13k2a2s(t52);
            if (!t53) goto ie51;
            {
                uint8_t t55 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix51;
ie51:;
ix51:;
        }
        {
            void * t57 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
            void * t58 = gm_f_construction_call_ktqtrpo(t57);
            uint8_t t59;
            if ((GM_LD(void *, ((uint8_t *)&t58)) != 0)) goto mn61;
            t59 = ((uint8_t)0);
            goto me60;
mn61:;
            t59 = ((uint8_t)1);
            goto me60;
mn62:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 230, 16);
me60:;
            if (!t59) goto ie56;
            {
                void * t64 = gm_l0;
                uint64_t t65 = gm_l1;
                void * t66 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                uint8_t t67 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t64, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t68 = 0;
                F.ret = t67;
                goto gm_fret;
            }
            goto ix56;
ie56:;
ix56:;
        }
        {
            uint8_t t69 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn47:;
ma7:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t71 != ((uint8_t)24ULL)) goto mn70;
        void * t72 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t72;
        {
            void * t75 = gm_l2;
            uint8_t t76 = gm_f_hoists_13k2a2s(t75);
            if (!t76) goto ie74;
            {
                uint8_t t78 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix74;
ie74:;
ix74:;
        }
        {
            void * t80 = gm_l7;
            void * t81 = gm_f_construction_call_ktqtrpo(t80);
            uint8_t t82;
            if ((GM_LD(void *, ((uint8_t *)&t81)) != 0)) goto mn84;
            t82 = ((uint8_t)0);
            goto me83;
mn84:;
            t82 = ((uint8_t)1);
            goto me83;
mn85:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 235, 16);
me83:;
            if (!t82) goto ie79;
            {
                void * t87 = gm_l0;
                uint64_t t88 = gm_l1;
                void * t89 = gm_l7;
                uint8_t t90 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t87, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t91 = 0;
                F.ret = t90;
                goto gm_fret;
            }
            goto ix79;
ie79:;
ix79:;
        }
        {
            uint8_t t92 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn70:;
ma8:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t94 != ((uint8_t)10ULL)) goto mn93;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t95;
        {
            {
                gm_b16_8 t98 = gm_l8;
                gm_l9 = t98;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t100 = gm_l10;
                    uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t102 = (t100 < t101);
                    if (!t102) goto lb99;
                    {
                        uint64_t t105 = gm_l10;
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tv1o1j44ii4db t110 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t106 * 136));
                        F.l11 = t110;
                        {
                            uint8_t t113 = 0;
                            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 128));
                            if (!t114) {
                                void * t115 = gm_l0;
                                uint64_t t116 = gm_l1;
                                void * t117 = (void *)(((uint8_t *)&F.l11) + 40);
                                uint8_t t118 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t115, t116, t117);
                                if (gm_unw.kind) {
                                    goto gm_ll103;
                                }
                                uint8_t t119 = (uint8_t)!t118;
                                t113 = t119;
                            } else t113 = 1;
                            if (!t113) goto ie112;
                            {
                                uint8_t t121 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix112;
ie112:;
ix112:;
                        }
                        goto me108;
mn109:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 239, 13);
me108:;
                    }
lc99:;
                    {
                        uint64_t t123 = gm_l10;
                        uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                        uint64_t t126 = r124;
                        gm_l10 = t126;
                    }
                }
gm_ll103:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb99; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc99; }
                }
                goto gm_lad;
lb99:;
            }
        }
        {
            uint8_t t127 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn93:;
ma9:;
        uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t131 != ((uint8_t)25ULL)) goto pa130;
        goto po129;
pa130:;
        uint8_t t133 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t133 != ((uint8_t)26ULL)) goto pa132;
        goto po129;
pa132:;
        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t135 != ((uint8_t)27ULL)) goto pa134;
        goto po129;
pa134:;
        uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t137 != ((uint8_t)29ULL)) goto pa136;
        goto po129;
pa136:;
        uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t139 != ((uint8_t)30ULL)) goto pa138;
        goto po129;
pa138:;
        uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t141 != ((uint8_t)31ULL)) goto pa140;
        goto po129;
pa140:;
        uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t143 != ((uint8_t)32ULL)) goto pa142;
        goto po129;
pa142:;
        uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t145 != ((uint8_t)33ULL)) goto pa144;
        goto po129;
pa144:;
        goto mn128;
po129:;
        uint8_t t146 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn128:;
ma10:;
        uint8_t t148 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t148 != ((uint8_t)17ULL)) goto mn147;
        void * t149 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l12 = t149;
        void * t150 = gm_l0;
        uint64_t t151 = gm_l1;
        void * t152 = gm_l12;
        uint8_t t153 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t150, t151, t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t154 = 0;
        F.ret = t153;
        goto gm_fret;
        goto me3;
mn147:;
ma11:;
        uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t156 != ((uint8_t)19ULL)) goto mn155;
        void * t157 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l13 = t157;
        {
            void * t159 = gm_l0;
            uint64_t t160 = gm_l1;
            void * t161 = gm_l13;
            gm_toblvhepnjqkh t162;
            gm_f_generic_decl_oir9hm6((void *)&t162, (void *)NULL, cx, NULL, t159, t160, t161, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkb8hpcqf70t1 t163;
            if (((((uint8_t *)&t162))[24] != 0)) {
                t163 = (*(gm_tkb8hpcqf70t1 *)(((uint8_t *)&t162) + 0));
            } else {
                uint8_t t164 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l14 = t163;
        }
        {
            void * t165 = gm_l0;
            uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 0));
            gm_t6qooom8qa2do t167;
            gm_f_resolve_decl_pus042v((void *)&t167, (void *)NULL, cx, NULL, t165, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t168;
            uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&t167) + 0));
            if (t171 != ((uint8_t)1ULL)) goto mn170;
            t168 = ((uint8_t)1);
            goto me169;
mn170:;
            t168 = ((uint8_t)0);
            goto me169;
mn172:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 248, 20);
me169:;
            uint8_t t173 = 0;
            F.ret = t168;
            goto gm_fret;
        }
        goto me3;
mn155:;
ma12:;
        uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t175 != ((uint8_t)35ULL)) goto mn174;
        void * t176 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l15 = t176;
        void * t177 = gm_l0;
        uint64_t t178 = gm_l1;
        void * t179 = gm_l15;
        uint8_t t180 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t177, t178, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t181 = 0;
        F.ret = t180;
        goto gm_fret;
        goto me3;
mn174:;
ma13:;
        uint8_t t183 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn182:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 219, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_names_error_oet8t6b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l3 = t5;
        {
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_tf5srh4mp4b3i t10 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t11;
            if (((((uint8_t *)&t10))[8] != 0)) {
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&t10) + 0));
                t11 = t12;
            } else {
                uint8_t t13 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l4 = t11;
        }
        {
            void * t14 = gm_l0;
            uint64_t t15 = gm_l4;
            gm_t6qooom8qa2do t16;
            gm_f_resolve_decl_pus042v((void *)&t16, (void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = ((GM_LD(uint8_t, ((uint8_t *)&t16) + 0) == ((uint8_t)11ULL)));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t20 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_names_type_rjdgib9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = gm_l2;
        gm_tkn42u7na3t81 t5 = gm_f_type_param_3av4r8i(t3, t4);
        uint8_t t6;
        if (((((uint8_t *)&t5))[4] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 138, 8);
me7:;
        if (!t6) goto ie2;
        {
            uint8_t t11 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = gm_l1;
        gm_b16_8 t15 = gm_l2;
        gm_tf5srh4mp4b3i t16 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t16))[8] != 0)) goto ie12;
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&t16));
        gm_l3 = t17;
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l3;
            uint8_t t21 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t19, t20, ((uint32_t)0ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t23 = gm_l2;
        uint8_t t24 = gm_f_is_builtin_type_name_v5ttoka(t23);
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
inline __attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_operand_3n5jchh F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* inner */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* inner */
    GM_STACK_CHECK(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma4; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t11 != ((uint8_t)28ULL)) goto mn10;
    gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t1) + 8) + 8));
    gm_l1 = t12;
    void * t13 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
    void * t14 = gm_f_prefix_operand_3n5jchh(t13);
    t2 = t14;
    goto me3;
mn10:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)24ULL)) goto mn15;
    void * t17 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l2 = t17;
    void * t18 = gm_l2;
    void * t19 = gm_f_prefix_operand_3n5jchh(t18);
    t2 = t19;
    goto me3;
mn15:;
ma6:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)25ULL)) goto pa22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t24;
    goto po21;
pa22:;
    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t26 != ((uint8_t)26ULL)) goto pa25;
    void * t27 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t27;
    goto po21;
pa25:;
    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t29 != ((uint8_t)27ULL)) goto pa28;
    void * t30 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t30;
    goto po21;
pa28:;
    goto mn20;
po21:;
    void * t31 = gm_l3;
    void * t32 = gm_f_prefix_operand_3n5jchh(t31);
    t2 = t32;
    goto me3;
mn20:;
ma7:;
    void * t34 = gm_l0;
    t2 = t34;
    goto me3;
mn33:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 295, 64);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_push_down_hge098p F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* inner */
    void * gm_l2; /* b */
    void * gm_l3; /* b */
    void * gm_l4; /* b */
    void * gm_l5; /* b */
    void * gm_l6; /* b */
    void * gm_l7; /* b */
    gm_tao63pk3r18v5 gm_l8; /* x */
    void * gm_l9; /* callee */
    void * gm_l10; /* base */
    void * gm_l11; /* next */
    void * gm_l12; /* callee */
    void * gm_l13; /* down */
    void * gm_l14; /* i */
    gm_t08olq97rscn3 gm_l15; /* name */
    uint32_t gm_l16; /* i */
    gm_tl874t2atnlh3 gm_l17; /* at */
    gm_tao63pk3r18v5 gm_l18; /* x */
    gm_b16_8 gm_l19; /* args */
    uint8_t gm_l20; /* trailing */
    GM_STACK_CHECK(&F, 1763u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        void * t3;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; default: goto ma9; }
ma5:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)20ULL)) goto pa14;
        void * t16 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t16;
        goto po13;
pa14:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)22ULL)) goto pa17;
        void * t19 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t19;
        goto po13;
pa17:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)15ULL)) goto pa20;
        void * t22 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t22;
        goto po13;
pa20:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)16ULL)) goto pa23;
        void * t25 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t25;
        goto po13;
pa23:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)18ULL)) goto pa26;
        void * t28 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t28;
        goto po13;
pa26:;
        goto mn12;
po13:;
        void * t29 = gm_l2;
        t3 = t29;
        goto me4;
mn12:;
ma6:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)23ULL)) goto mn30;
        void * t32 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t32;
        void * t33 = gm_l7;
        t3 = t33;
        goto me4;
mn30:;
ma7:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)21ULL)) goto mn34;
        gm_tao63pk3r18v5 t36 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t36;
        void * t37 = GM_LD(void *, (((uint8_t *)&gm_l8) + 0));
        t3 = t37;
        goto me4;
mn34:;
ma8:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)19ULL)) goto mn38;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t40;
        void * t41;
        void * t43 = gm_l9;
        uint8_t t44 = gm_f_calls_on_value_q0pa6h5(t43);
        if (!t44) goto ie42;
        void * t46 = gm_l9;
        void * t47 = t46;
        t41 = t47;
        goto ix42;
ie42:;
        uint8_t t49 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        t41 = ((void *)0);
ix42:;
        t3 = t41;
        goto me4;
mn38:;
ma9:;
        uint8_t t51 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me4;
mn50:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 894, 13);
me4:;
        gm_l10 = t3;
    }
    {
        void * t52 = gm_l10;
        gm_tq7s98ihonkju t53 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t52)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8b8p636a5m)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcsffver18si54, 901, 23);
        void * t54 = GM_LD(void *, (((uint8_t *)&t53) + 8));
        gm_l11 = t54;
    }
    {
        void * t56 = gm_l10;
        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t56) + 8) + 0));
        if (t57 != ((uint8_t)19ULL)) goto ie55;
        void * t58 = GM_LD(void *, (((((uint8_t *)t56) + 8) + 8) + 0));
        gm_l12 = t58;
        void * t59 = gm_l12;
        uint8_t t60 = gm_f_calls_on_value_q0pa6h5(t59);
        uint8_t t61 = (uint8_t)!t60;
        if (!t61) goto ie55;
        {
            void * t63 = gm_l0;
            void * t64 = gm_l10;
            void * t65 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t65;
        }
        {
            void * t66 = gm_l11;
            uint8_t *t67 = (((uint8_t *)t66) + 72);
            void * t68 = gm_l0;
            gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 72));
            void * t70 = gm_l10;
            gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
            gm_tl874t2atnlh3 t72 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t69), GM_CONV(gm_cseahvg8v7p0vs, t71)));
            GM_ST(gm_tl874t2atnlh3, t67, t72);
        }
        goto ix55;
ie55:;
        {
            void * t74 = gm_l0;
            void * t75 = gm_l10;
            void * t76 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t77;
            if ((GM_LD(void *, ((uint8_t *)&t76)) != 0)) {
                void * t78 = GM_LD(void *, (((uint8_t *)&t76) + 0));
                t77 = t78;
            } else {
                uint8_t t79 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l13 = t77;
        }
        {
            void * t80 = gm_l13;
            gm_tq7s98ihonkju t81 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t80)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klh5uk0bvu4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srcsffver18si54, 907, 16);
            void * t82 = GM_LD(void *, (((uint8_t *)&t81) + 8));
            gm_l11 = t82;
        }
ix55:;
    }
    {
        void * t83 = gm_l1;
        gm_t2gtbh39ptekd t84;
        uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        switch (t96) { case ((uint8_t)0ULL): goto ma94; case ((uint8_t)1ULL): goto ma94; case ((uint8_t)2ULL): goto ma94; case ((uint8_t)3ULL): goto ma94; case ((uint8_t)4ULL): goto ma94; case ((uint8_t)5ULL): goto ma94; case ((uint8_t)6ULL): goto ma94; case ((uint8_t)7ULL): goto ma94; case ((uint8_t)8ULL): goto ma94; case ((uint8_t)9ULL): goto ma94; case ((uint8_t)10ULL): goto ma94; case ((uint8_t)11ULL): goto ma94; case ((uint8_t)12ULL): goto ma94; case ((uint8_t)13ULL): goto ma94; case ((uint8_t)14ULL): goto ma94; case ((uint8_t)15ULL): goto ma88; case ((uint8_t)16ULL): goto ma89; case ((uint8_t)17ULL): goto ma94; case ((uint8_t)18ULL): goto ma90; case ((uint8_t)19ULL): goto ma93; case ((uint8_t)20ULL): goto ma86; case ((uint8_t)21ULL): goto ma92; case ((uint8_t)22ULL): goto ma87; case ((uint8_t)23ULL): goto ma91; case ((uint8_t)24ULL): goto ma94; case ((uint8_t)25ULL): goto ma94; case ((uint8_t)26ULL): goto ma94; case ((uint8_t)27ULL): goto ma94; case ((uint8_t)28ULL): goto ma94; case ((uint8_t)29ULL): goto ma94; case ((uint8_t)30ULL): goto ma94; case ((uint8_t)31ULL): goto ma94; case ((uint8_t)32ULL): goto ma94; case ((uint8_t)33ULL): goto ma94; case ((uint8_t)34ULL): goto ma94; case ((uint8_t)35ULL): goto ma94; case ((uint8_t)36ULL): goto ma94; case ((uint8_t)37ULL): goto ma94; case ((uint8_t)38ULL): goto ma94; case ((uint8_t)39ULL): goto ma94; case ((uint8_t)40ULL): goto ma94; case ((uint8_t)41ULL): goto ma94; case ((uint8_t)42ULL): goto ma94; case ((uint8_t)43ULL): goto ma94; case ((uint8_t)44ULL): goto ma94; case ((uint8_t)45ULL): goto ma94; case ((uint8_t)46ULL): goto ma94; case ((uint8_t)47ULL): goto ma94; case ((uint8_t)48ULL): goto ma94; case ((uint8_t)49ULL): goto ma94; case ((uint8_t)50ULL): goto ma94; default: goto ma94; }
ma86:;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t98 != ((uint8_t)20ULL)) goto mn97;
        void * t99 = GM_LD(void *, (((((uint8_t *)t83) + 8) + 8) + 8));
        gm_l14 = t99;
        void * t100 = gm_l11;
        void * t101 = gm_l14;
        gm_t3mf58gv9kf3n t102;
        GM_ST(void *, ((uint8_t *)&t102 + 0), t100);
        GM_ST(void *, ((uint8_t *)&t102 + 8), t101);
        gm_t2gtbh39ptekd t103;
        memset(&t103, 0, sizeof t103);
        GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)20ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t103 + 8), t102);
        t84 = t103;
        goto me85;
mn97:;
ma87:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t105 != ((uint8_t)22ULL)) goto mn104;
        gm_t08olq97rscn3 t106 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t83) + 8) + 8) + 8));
        gm_l15 = t106;
        gm_tioie2ju3q4ul t107;
        void * t108 = gm_l11;
        GM_ST(void *, (uint8_t *)&t107 + 0, t108);
        memcpy((uint8_t *)&t107 + 8, ((uint8_t *)&gm_l15), 32);
        gm_t2gtbh39ptekd t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t109 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t109 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)22ULL));
        GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t109 + 8), t107);
        t84 = t109;
        goto me85;
mn104:;
ma88:;
        uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t111 != ((uint8_t)15ULL)) goto mn110;
        void * t112 = gm_l11;
        gm_t2gtbh39ptekd t113;
        memset(&t113, 0, sizeof t113);
        GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)15ULL));
        GM_ST(void *, ((uint8_t *)&t113 + 8), t112);
        t84 = t113;
        goto me85;
mn110:;
ma89:;
        uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t115 != ((uint8_t)16ULL)) goto mn114;
        void * t116 = gm_l11;
        gm_t2gtbh39ptekd t117;
        memset(&t117, 0, sizeof t117);
        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)16ULL));
        GM_ST(void *, ((uint8_t *)&t117 + 8), t116);
        t84 = t117;
        goto me85;
mn114:;
ma90:;
        uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t119 != ((uint8_t)18ULL)) goto mn118;
        void * t120 = gm_l11;
        gm_t2gtbh39ptekd t121;
        memset(&t121, 0, sizeof t121);
        GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)18ULL));
        GM_ST(void *, ((uint8_t *)&t121 + 8), t120);
        t84 = t121;
        goto me85;
mn118:;
ma91:;
        uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t123 != ((uint8_t)23ULL)) goto mn122;
        uint32_t t124 = GM_LD(uint32_t, (((((uint8_t *)t83) + 8) + 8) + 8));
        gm_l16 = t124;
        gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t83) + 8) + 8) + 12));
        gm_l17 = t125;
        gm_tupk62cr6lbdu t126;
        void * t127 = gm_l11;
        GM_ST(void *, (uint8_t *)&t126 + 0, t127);
        uint32_t t128 = gm_l16;
        GM_ST(uint32_t, (uint8_t *)&t126 + 8, t128);
        gm_tl874t2atnlh3 t129 = gm_l17;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t126 + 12, t129);
        gm_t2gtbh39ptekd t130;
        memset(&t130, 0, sizeof t130);
        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)23ULL));
        GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t130 + 8), t126);
        t84 = t130;
        goto me85;
mn122:;
ma92:;
        uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t132 != ((uint8_t)21ULL)) goto mn131;
        gm_tao63pk3r18v5 t133 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t83) + 8) + 8));
        gm_l18 = t133;
        gm_tao63pk3r18v5 t134;
        memset((uint8_t *)&t134 + 25, 0, 7);
        void * t135 = gm_l11;
        GM_ST(void *, (uint8_t *)&t134 + 0, t135);
        void * t136 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
        GM_ST(void *, (uint8_t *)&t134 + 8, t136);
        void * t137 = GM_LD(void *, (((uint8_t *)&gm_l18) + 16));
        GM_ST(void *, (uint8_t *)&t134 + 16, t137);
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 24));
        GM_ST(uint8_t, (uint8_t *)&t134 + 24, t138);
        gm_t2gtbh39ptekd t139;
        GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t139 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t139 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t139 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)21ULL));
        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t139 + 8), t134);
        t84 = t139;
        goto me85;
mn131:;
ma93:;
        uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
        if (t141 != ((uint8_t)19ULL)) goto mn140;
        gm_b16_8 t142 = GM_LD(gm_b16_8, (((((uint8_t *)t83) + 8) + 8) + 8));
        gm_l19 = t142;
        uint8_t t143 = GM_LD(uint8_t, (((((uint8_t *)t83) + 8) + 8) + 24));
        gm_l20 = t143;
        gm_tlljg61a2ctgq t144;
        memset((uint8_t *)&t144 + 25, 0, 7);
        void * t145 = gm_l11;
        GM_ST(void *, (uint8_t *)&t144 + 0, t145);
        gm_b16_8 t146 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 8, t146);
        uint8_t t147 = gm_l20;
        GM_ST(uint8_t, (uint8_t *)&t144 + 24, t147);
        gm_t2gtbh39ptekd t148;
        GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t148 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t148 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t148 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t148 + 8), t144);
        t84 = t148;
        goto me85;
mn140:;
ma94:;
        uint8_t t150 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me85;
mn149:;
ma95:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 909, 26);
me85:;
        F.l21 = t84;
    }
    {
        gm_tc3oht4ksth9d t151;
        memset((uint8_t *)&t151 + 4, 0, 4);
        memset((uint8_t *)&t151 + 84, 0, 4);
        void * t152 = gm_l1;
        uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)t152) + 0));
        GM_ST(uint32_t, (uint8_t *)&t151 + 0, t153);
        memcpy((uint8_t *)&t151 + 8, ((uint8_t *)&F.l21), 64);
        void * t154 = gm_l1;
        gm_tl874t2atnlh3 t155 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t154) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t151 + 72, t155);
        gm_tq7s98ihonkju t156 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t151, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kijoa5b5euq)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 8, gm_srcsffver18si54, 920, 12);
        void * t157 = GM_LD(void *, (((uint8_t *)&t156) + 8));
        uint8_t t158 = 0;
        F.ret = t157;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_pushes_down_0djaflp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pushes_down_0djaflp F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    void * gm_l5; /* base */
    void * gm_l6; /* base */
    gm_tao63pk3r18v5 gm_l7; /* x */
    void * gm_l8; /* callee */
    void * gm_l9; /* next */
    void * gm_l10; /* callee */
    GM_STACK_CHECK(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; default: goto ma9; }
ma5:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)20ULL)) goto pa14;
        void * t16 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t16;
        goto po13;
pa14:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)22ULL)) goto pa17;
        void * t19 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t19;
        goto po13;
pa17:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)15ULL)) goto pa20;
        void * t22 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t22;
        goto po13;
pa20:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)16ULL)) goto pa23;
        void * t25 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t25;
        goto po13;
pa23:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)18ULL)) goto pa26;
        void * t28 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t28;
        goto po13;
pa26:;
        goto mn12;
po13:;
        void * t29 = gm_l1;
        t3 = t29;
        goto me4;
mn12:;
ma6:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)23ULL)) goto mn30;
        void * t32 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t32;
        void * t33 = gm_l6;
        t3 = t33;
        goto me4;
mn30:;
ma7:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)21ULL)) goto mn34;
        gm_tao63pk3r18v5 t36 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t36;
        void * t37 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
        t3 = t37;
        goto me4;
mn34:;
ma8:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)19ULL)) goto mn38;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t40;
        void * t41;
        void * t43 = gm_l8;
        uint8_t t44 = gm_f_calls_on_value_q0pa6h5(t43);
        if (!t44) goto ie42;
        void * t46 = gm_l8;
        void * t47 = t46;
        t41 = t47;
        goto ix42;
ie42:;
        uint8_t t49 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        t41 = ((void *)0);
ix42:;
        t3 = t41;
        goto me4;
mn38:;
ma9:;
        uint8_t t51 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me4;
mn50:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 303, 13);
me4:;
        gm_l9 = t3;
    }
    {
        void * t53 = gm_l9;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
        if (t54 != ((uint8_t)19ULL)) goto ie52;
        void * t55 = GM_LD(void *, (((((uint8_t *)t53) + 8) + 8) + 0));
        gm_l10 = t55;
        void * t56 = gm_l10;
        uint8_t t57 = gm_f_calls_on_value_q0pa6h5(t56);
        uint8_t t58 = (uint8_t)!t57;
        if (!t58) goto ie52;
        {
            uint8_t t60 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        void * t61 = gm_l9;
        uint8_t t62 = gm_f_pushes_down_0djaflp(t61);
        uint8_t t63 = 0;
        F.ret = t62;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_jjodbei F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    uint8_t gm_l4; /* sized */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_f_resolve_type_unchecked_psme7dn((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_t53at80pfdus3 t9;
        gm_f_kind_jcnt4hm((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        uint8_t t10;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t15 != ((uint8_t)11ULL)) goto pa14;
        goto po13;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t17 != ((uint8_t)12ULL)) goto pa16;
        goto po13;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t19 != ((uint8_t)13ULL)) goto pa18;
        goto po13;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t21 != ((uint8_t)14ULL)) goto pa20;
        goto po13;
pa20:;
        goto mn12;
po13:;
        t10 = ((uint8_t)1);
        goto me11;
mn12:;
        t10 = ((uint8_t)0);
        goto me11;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 320, 14);
me11:;
        gm_l4 = t10;
    }
    {
        uint8_t t24 = gm_l4;
        if (!t24) goto ie23;
        void * t25 = gm_l0;
        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 1728) + 0) + 8);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        if (!t27) goto ie23;
        void * t28 = gm_l0;
        uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)t28) + 1816));
        uint8_t t30 = ((t29 == ((uint32_t)0ULL)));
        if (!t30) goto ie23;
        void * t31 = gm_l0;
        gm_t88rcucg1b7uk t32 = gm_l3;
        void * t33 = gm_l2;
        gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
        uint8_t t35 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t31, t32, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t36 = (uint8_t)!t35;
        if (!t36) goto ie23;
        {
            void * t38 = gm_l0;
            void * t39 = (void *)(((uint8_t *)t38) + 8);
            gm_t88rcucg1b7uk t40 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        gm_t88rcucg1b7uk t42 = gm_l3;
        uint8_t t43 = 0;
        F.ret = t42;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_unchecked_psme7dn(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_type_unchecked_psme7dn F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* tys */
    gm_t08olq97rscn3 gm_l4; /* id */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint32_t gm_l6; /* p */
    uint64_t gm_l7; /* d */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* msg */
    gm_b16_8 gm_l12; /* hint */
    gm_b16_8 gm_l14; /* last */
    void * gm_l15; /* inner */
    gm_b16_8 gm_l16; /* elems */
    uint8_t gm_l17; /* named */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t08olq97rscn3 gm_l26; /* n */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    gm_b16_8 gm_l30; /* n */
    void * gm_l31; /* inner */
    uint8_t gm_l32; /* is_const */
    void * gm_l33; /* inner */
    void * gm_l34; /* sig */
    uint8_t gm_l36; /* is_const */
    void * gm_l37; /* inner */
    uint8_t gm_l38; /* is_const */
    void * gm_l39; /* inner */
    gm_t3mf58gv9kf3n gm_l40; /* a */
    gm_t88rcucg1b7uk gm_l41; /* elem */
    void * gm_l42; /* len */
    gm_t88rcucg1b7uk gm_l43; /* index */
    uint64_t gm_l44; /* n */
    gm_t88rcucg1b7uk gm_l45; /* l */
    void * gm_l46; /* err */
    void * gm_l47; /* ok */
    void * gm_l48; /* err */
    void * gm_l49; /* ok */
    gm_t88rcucg1b7uk gm_l50; /* et */
    gm_t88rcucg1b7uk gm_l51; /* ot */
    void * gm_l52; /* sig */
    void * gm_l54; /* inner */
    void * gm_l55; /* callee */
    gm_b16_8 gm_l56; /* args */
    void * gm_l57; /* base */
    gm_t9805qtonbsag gm_l58; /* template */
    gm_tkb8hpcqf70t1 gm_l59; /* g */
    uint32_t gm_l60; /* t */
    uint32_t gm_l61; /* t */
    gm_b16_8 gm_l62; /* targs */
    gm_b16_8 gm_l63; /* params */
    gm_b16_8 gm_l64; /* <tmp> */
    uint64_t gm_l65; /* <tmp> */
    uint64_t gm_l66; /* i */
    uint32_t gm_l67; /* pid */
    gm_t88rcucg1b7uk gm_l68; /* a */
    gm_b16_8 gm_l69; /* <tmp> */
    uint64_t gm_l70; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l71; /* c */
    gm_b16_8 gm_l72; /* <tmp> */
    uint64_t gm_l73; /* <tmp> */
    gm_t88rcucg1b7uk gm_l74; /* a */
    gm_t88rcucg1b7uk gm_l75; /* part */
    gm_t88rcucg1b7uk gm_l76; /* inst */
    gm_b16_8 gm_l77; /* attrs */
    void * gm_l78; /* inner */
    void * gm_l79; /* f */
    gm_b16_8 gm_l80; /* <tmp> */
    uint64_t gm_l81; /* <tmp> */
    gm_b16_8 gm_l83; /* name */
    gm_b16_8 gm_l85; /* args */
    void * gm_l87; /* hook */
    void * gm_l88; /* base */
    gm_t08olq97rscn3 gm_l89; /* name */
    uint64_t gm_l90; /* m */
    uint64_t gm_l91; /* d */
    gm_t88rcucg1b7uk gm_l92; /* t */
    GM_STACK_CHECK(&F, 8755u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l2;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        switch (t25) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma23; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma23; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma23; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma23; case ((uint8_t)15ULL): goto ma23; case ((uint8_t)16ULL): goto ma23; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma23; case ((uint8_t)19ULL): goto ma17; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma23; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma23; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma15; case ((uint8_t)31ULL): goto ma16; case ((uint8_t)32ULL): goto ma19; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma23; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma23; case ((uint8_t)37ULL): goto ma23; case ((uint8_t)38ULL): goto ma23; case ((uint8_t)39ULL): goto ma23; case ((uint8_t)40ULL): goto ma23; case ((uint8_t)41ULL): goto ma23; case ((uint8_t)42ULL): goto ma23; case ((uint8_t)43ULL): goto ma23; case ((uint8_t)44ULL): goto ma23; case ((uint8_t)45ULL): goto ma23; case ((uint8_t)46ULL): goto ma23; case ((uint8_t)47ULL): goto ma23; case ((uint8_t)48ULL): goto ma23; case ((uint8_t)49ULL): goto ma21; case ((uint8_t)50ULL): goto ma23; default: goto ma23; }
ma6:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t27 != ((uint8_t)6ULL)) goto mn26;
        gm_t08olq97rscn3 t28 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t4) + 8) + 8));
        gm_l4 = t28;
        {
            void * t31 = gm_l0;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tkn42u7na3t81 t33 = gm_f_type_param_3av4r8i(t31, t32);
            if (!((((uint8_t *)&t33))[4] != 0)) goto ie30;
            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t33));
            gm_l5 = t34;
            {
                void * t37 = gm_l3;
                gm_t88rcucg1b7uk t38 = gm_l5;
                gm_t53at80pfdus3 t39;
                gm_f_kind_jcnt4hm((void *)&t39, t37, GM_CONV(gm_csguankko4lmhd, t38));
                uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
                if (t40 != ((uint8_t)19ULL)) goto ie36;
                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&t39) + 8));
                gm_l6 = t41;
                uint32_t t42 = gm_l6;
                uint64_t t43 = (uint64_t)t42;
                uint64_t t44 = (uintptr_t)t43;
                void * t45 = gm_l0;
                uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 1128) + 0) + 8);
                if (t44 >= (uintptr_t)(t46)) gm_trap_index(t44, t46, gm_srcsffver18si54, 335, 49);
                uint8_t t47 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t45) + 1128) + 0)) + t44 * 48) + 0) + 0) == ((uint8_t)1ULL)));
                if (!t47) goto ie36;
                {
                    void * t49 = gm_l0;
                    void * t50 = gm_l2;
                    gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 72));
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                    gm_tnpifv20irnph t53;
                    GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                    gm_tabb710d9mq2e t54;
                    gm_f_format_o5o5km2((void *)&t54, (void *)NULL, cx, NULL, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcsffver18si54, 336, 37);
                    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                    gm_b0_1 t57 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t49, t51, t56);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t58 = gm_l3;
                    gm_t88rcucg1b7uk t59 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t60 = 0;
                    F.ret = t59;
                    goto gm_fret;
                }
                goto ix36;
ie36:;
ix36:;
            }
            {
                gm_t88rcucg1b7uk t61 = gm_l5;
                uint8_t t62 = 0;
                F.ret = t61;
                goto gm_fret;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t64 = gm_l0;
            uint64_t t65 = gm_l1;
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t67 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t64, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t67))[8] != 0)) goto ie63;
            uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&t67));
            gm_l7 = t68;
            {
                void * t70 = gm_l0;
                uint64_t t71 = gm_l7;
                gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b0_1 t73 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t70, t71, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t74 = gm_l0;
                uint64_t t75 = gm_l7;
                gm_t6qooom8qa2do t76;
                gm_f_resolve_decl_pus042v((void *)&t76, (void *)NULL, cx, NULL, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                switch (t82) { case ((uint8_t)0ULL): goto ma78; case ((uint8_t)1ULL): goto ma80; case ((uint8_t)2ULL): goto ma80; case ((uint8_t)3ULL): goto ma80; case ((uint8_t)4ULL): goto ma80; case ((uint8_t)5ULL): goto ma80; case ((uint8_t)6ULL): goto ma80; case ((uint8_t)7ULL): goto ma80; case ((uint8_t)8ULL): goto ma80; case ((uint8_t)9ULL): goto ma80; case ((uint8_t)10ULL): goto ma80; case ((uint8_t)11ULL): goto ma79; default: goto ma80; }
ma78:;
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                if (t84 != ((uint8_t)0ULL)) goto mn83;
                gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t76) + 8));
                gm_l8 = t85;
                gm_t88rcucg1b7uk t86 = gm_l8;
                uint8_t t87 = 0;
                F.ret = t86;
                goto gm_fret;
                goto me77;
mn83:;
ma79:;
                uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
                if (t89 != ((uint8_t)11ULL)) goto mn88;
                void * t90 = gm_l3;
                gm_t88rcucg1b7uk t91 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t92 = 0;
                F.ret = t91;
                goto gm_fret;
                goto me77;
mn88:;
ma80:;
                {
                    void * t95 = gm_l0;
                    void * t96 = gm_l2;
                    gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t96) + 72));
                    gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                    gm_tnpifv20irnph t99;
                    GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t98);
                    gm_tabb710d9mq2e t100;
                    gm_f_format_sv91363((void *)&t100, (void *)NULL, cx, NULL, t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_srcsffver18si54, 347, 41);
                    gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
                    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                    gm_b0_1 t103 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t95, t97, t102);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t104 = gm_l3;
                    gm_t88rcucg1b7uk t105 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t104);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t106 = 0;
                    F.ret = t105;
                    goto gm_fret;
                }
                goto me77;
mn93:;
ma81:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 343, 17);
me77:;
            }
            goto ix63;
ie63:;
ix63:;
        }
        {
            void * t108 = gm_l0;
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tkn42u7na3t81 t110 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t110))[4] != 0)) goto ie107;
            gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t110));
            gm_l9 = t111;
            {
                gm_t88rcucg1b7uk t113 = gm_l9;
                uint8_t t114 = 0;
                F.ret = t113;
                goto gm_fret;
            }
            goto ix107;
ie107:;
ix107:;
        }
        {
            gm_b16_8 t115;
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint8_t t118 = gm_f_is_oversized_int_name_nr25haf(t117);
            if (!t118) goto ie116;
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tnpifv20irnph t121;
            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t120);
            gm_tabb710d9mq2e t122;
            gm_f_format_oq3rvor((void *)&t122, (void *)NULL, cx, NULL, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 8, gm_srcsffver18si54, 354, 17);
            gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&t122) + 8));
            gm_b16_8 t124 = t123;
            t115 = t124;
            goto ix116;
ie116:;
            gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tnpifv20irnph t127;
            GM_ST(gm_b16_8, ((uint8_t *)&t127 + 0), t126);
            gm_tabb710d9mq2e t128;
            gm_f_format_pplcoh0((void *)&t128, (void *)NULL, cx, NULL, t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_srcsffver18si54, 356, 17);
            gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
            gm_b16_8 t130 = t129;
            t115 = t130;
ix116:;
            gm_l10 = t115;
        }
        {
            void * t131 = gm_l2;
            gm_tl874t2atnlh3 t132 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t131) + 72));
            gm_b16_8 t133 = gm_l10;
            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
            gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t132, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t136 = gm_l0;
            gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_th2lqbo77pvsc t138;
            gm_f_universe_hint_ams3f10((void *)&t138, (void *)NULL, cx, NULL, t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t138))[16] != 0)) goto ie135;
            gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
            gm_l12 = t139;
            {
                void * t141 = (void *)((uint8_t *)&F.l11);
                gm_b16_8 t142 = gm_l12;
                gm_f_add_note_46v502s(t141, t142);
            }
            {
                void * t144 = gm_l0;
                uint64_t t145 = gm_l1;
                gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                void * t147 = gm_l2;
                gm_tl874t2atnlh3 t148 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t147) + 72));
                gm_tqetrgo0q24b5 t149;
                gm_f_import_fix_7822666((void *)&t149, (void *)NULL, cx, NULL, t144, t145, t146, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t149))[40] != 0)) goto ie143;
                F.l13 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t149) + 0));
                {
                    uint64_t t151 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                    uint64_t r152; bool o153 = __builtin_sub_overflow(t151, ((uint64_t)1ULL), &r152);
                    if (o153) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 362, 41);
                    uint64_t t154 = r152;
                    uint64_t t155 = (uintptr_t)t154;
                    uint64_t t156 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                    if (t155 >= (uintptr_t)(t156)) gm_trap_index(t155, t156, gm_srcsffver18si54, 362, 29);
                    gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t155 * 32) + 16));
                    gm_l14 = t157;
                }
                {
                    void * t158 = (void *)((uint8_t *)&F.l11);
                    gm_b16_8 t159 = gm_l14;
                    gm_tnpifv20irnph t160;
                    GM_ST(gm_b16_8, ((uint8_t *)&t160 + 0), t159);
                    gm_tabb710d9mq2e t161;
                    gm_f_format_ooibcq2((void *)&t161, (void *)NULL, cx, NULL, t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 8, gm_srcsffver18si54, 363, 37);
                    gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t161) + 8));
                    gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                    gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                    gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                    gm_b0_1 t166 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t158, t163, t165);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix143;
ie143:;
ix143:;
            }
            goto ix135;
ie135:;
ix135:;
        }
        {
            void * t167 = gm_l0;
            gm_b0_1 t168 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t167, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t169 = gm_l3;
            gm_t88rcucg1b7uk t170 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t171 = 0;
            F.ret = t170;
            goto gm_fret;
        }
        goto me5;
mn26:;
ma7:;
        uint8_t t173 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t173 != ((uint8_t)9ULL)) goto mn172;
        void * t174 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l15 = t174;
        void * t175 = gm_l0;
        uint64_t t176 = gm_l1;
        void * t177 = gm_l15;
        gm_t88rcucg1b7uk t178 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t175, t176, t177);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t179 = 0;
        F.ret = t178;
        goto gm_fret;
        goto me5;
mn172:;
ma8:;
        uint8_t t181 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t181 != ((uint8_t)10ULL)) goto mn180;
        gm_b16_8 t182 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l16 = t182;
        {
            gm_b16_8 t185 = gm_l16;
            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
            uint8_t t187 = gm_f_has_spread_bndtpd6(t186);
            if (!t187) goto ie184;
            {
                void * t189 = gm_l0;
                void * t190 = gm_l2;
                gm_tl874t2atnlh3 t191 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t190) + 72));
                gm_b16_8 t192;
                GM_ST(void *, (uint8_t *)&t192, (void *)(gm_shbaullahutpe));
                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(36));
                gm_b0_1 t193 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t189, t191, t192);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t194 = gm_l3;
                gm_t88rcucg1b7uk t195 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t194);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t196 = 0;
                F.ret = t195;
                goto gm_fret;
            }
            goto ix184;
ie184:;
ix184:;
        }
        {
            uint8_t t197 = 0;
            uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
            uint8_t t199 = (t198 > ((uint64_t)0ULL));
            if (t199) {
                uint64_t t200 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                if (t200 >= (uintptr_t)(t201)) gm_trap_index(t200, t201, gm_srcsffver18si54, 375, 40);
                uint8_t t202;
                if ((((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t200 * 136) + 0))[32] != 0)) goto mn204;
                t202 = ((uint8_t)0);
                goto me203;
mn204:;
                t202 = ((uint8_t)1);
                goto me203;
mn205:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 375, 40);
me203:;
                t197 = t202;
            } else t197 = 0;
            gm_l17 = t197;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t207 = gm_l16;
                gm_l19 = t207;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t209 = gm_l20;
                    uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t211 = (t209 < t210);
                    if (!t211) goto lb208;
                    {
                        uint64_t t214 = gm_l20;
                        uint64_t t215 = (uintptr_t)t214;
                        uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_tv1o1j44ii4db t219 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t215 * 136));
                        F.l21 = t219;
                        {
                            void * t221 = (void *)((uint8_t *)&F.l18);
                            void * t222 = gm_l0;
                            uint64_t t223 = gm_l1;
                            void * t224 = (void *)(((uint8_t *)&F.l21) + 40);
                            gm_t88rcucg1b7uk t225 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t222, t223, t224);
                            if (gm_unw.kind) {
                                goto gm_ll212;
                            }
                            gm_tftb1odc739t5 t226 = gm_f_push_k3l9nic(t221, GM_CONV(gm_csguankko4lmhd, t225));
                            if (((((uint8_t *)&t226))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t226) + 1, gm_srcsffver18si54, 377, 31);
                            gm_b0_1 t227 = GM_LD(gm_b0_1, (((uint8_t *)&t226) + 1));
                        }
                        goto me217;
mn218:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 377, 13);
me217:;
                    }
lc208:;
                    {
                        uint64_t t229 = gm_l20;
                        uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                        uint64_t t232 = r230;
                        gm_l20 = t232;
                    }
                }
gm_ll212:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb208; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc208; }
                }
                goto gm_lad;
lb208:;
            }
        }
        {
            gm_f_list_epvducg((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t234 = gm_l17;
            if (!t234) goto ie233;
            {
                {
                    gm_b16_8 t237 = gm_l16;
                    gm_l23 = t237;
                }
                {
                    gm_l24 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t239 = gm_l24;
                        uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint8_t t241 = (t239 < t240);
                        if (!t241) goto lb238;
                        {
                            uint64_t t244 = gm_l24;
                            uint64_t t245 = (uintptr_t)t244;
                            uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            gm_tv1o1j44ii4db t249 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t245 * 136));
                            F.l25 = t249;
                            {
                                gm_t1s75i08qtptd t251 = GM_LD(gm_t1s75i08qtptd, (((uint8_t *)&F.l25) + 0));
                                gm_t08olq97rscn3 t252;
                                if (((((uint8_t *)&t251))[32] != 0)) {
                                    t252 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t251) + 0));
                                } else {
                                    {
                                        void * t254 = gm_l0;
                                        void * t255 = gm_l2;
                                        gm_tl874t2atnlh3 t256 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t255) + 72));
                                        gm_b16_8 t257;
                                        GM_ST(void *, (uint8_t *)&t257, (void *)(gm_sjdn5gs7lu39i));
                                        GM_ST(uintptr_t, (uint8_t *)&t257 + 8, (uintptr_t)(56));
                                        gm_b0_1 t258 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t254, t256, t257);
                                        if (gm_unw.kind) {
                                            goto gm_ll242;
                                        }
                                    }
                                    void * t259 = gm_l3;
                                    gm_t88rcucg1b7uk t260 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t259);
                                    if (gm_unw.kind) {
                                        goto gm_ll242;
                                    }
                                    uint8_t t261 = 0;
                                    F.ret = t260;
                                    goto gm_fret;
                                }
                                gm_l26 = t252;
                            }
                            {
                                void * t262 = (void *)((uint8_t *)&F.l22);
                                gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 0));
                                gm_tftb1odc739t5 t264 = gm_f_push_3ef746b(t262, t263);
                                if (((((uint8_t *)&t264))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t264) + 1, gm_srcsffver18si54, 385, 21);
                                gm_b0_1 t265 = GM_LD(gm_b0_1, (((uint8_t *)&t264) + 1));
                            }
                            goto me247;
mn248:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 380, 17);
me247:;
                        }
lc238:;
                        {
                            uint64_t t267 = gm_l24;
                            uint64_t r268; bool o269 = __builtin_add_overflow(t267, ((uint64_t)1ULL), &r268);
                            uint64_t t270 = r268;
                            gm_l24 = t270;
                        }
                    }
gm_ll242:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb238; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc238; }
                    }
                    goto gm_lad;
lb238:;
                }
            }
            {
                {
                    gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                    gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
                    gm_l27 = t273;
                }
                {
                    gm_l28 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t275 = gm_l28;
                        uint64_t t276 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        uint8_t t277 = (t275 < t276);
                        if (!t277) goto lb274;
                        {
                            gm_tf1bjg81u0glv t280;
                            uint64_t t281 = gm_l28;
                            GM_ST(uint64_t, (uint8_t *)&t280 + 0, t281);
                            uint64_t t282 = gm_l28;
                            uint64_t t283 = (uintptr_t)t282;
                            uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                            gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t283 * 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t280 + 8, t285);
                            uint64_t t288 = GM_LD(uint64_t, (((uint8_t *)&t280) + 0));
                            gm_l29 = t288;
                            gm_b16_8 t289 = GM_LD(gm_b16_8, (((uint8_t *)&t280) + 8));
                            gm_l30 = t289;
                            {
                                gm_b16_8 t292 = gm_l30;
                                uint64_t t293 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                                void * t294 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l22) + 0)));
                                uint64_t t295 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t296 = gm_l29;
                                uint64_t t297 = (uintptr_t)t296;
                                if (t295 > t297 || t297 > (uintptr_t)t293) gm_trap_slice(t295, t297, t293, gm_srcsffver18si54, 388, 29);
                                gm_b16_8 t298;
                                GM_ST(void *, (uint8_t *)&t298, (void *)(((uint8_t *)t294 + t295 * 16)));
                                GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(t297 - t295));
                                uint8_t t299 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t298) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t298))) + i * 16, ((uint8_t *)&t292))) { t299 = 1; break; }
                                uint8_t t300 = (uint8_t)(t299);
                                if (!t300) goto ie291;
                                {
                                    void * t302 = gm_l0;
                                    void * t303 = gm_l2;
                                    gm_tl874t2atnlh3 t304 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t303) + 72));
                                    gm_b16_8 t305 = gm_l30;
                                    gm_tnpifv20irnph t306;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t306 + 0), t305);
                                    gm_tabb710d9mq2e t307;
                                    gm_f_format_6nmb1bf((void *)&t307, (void *)NULL, cx, NULL, t306);
                                    if (gm_unw.kind) {
                                        goto gm_ll278;
                                    }
                                    if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 8, gm_srcsffver18si54, 389, 41);
                                    gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
                                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                                    gm_b0_1 t310 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t302, t304, t309);
                                    if (gm_unw.kind) {
                                        goto gm_ll278;
                                    }
                                }
                                {
                                    void * t311 = gm_l3;
                                    gm_t88rcucg1b7uk t312 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t311);
                                    if (gm_unw.kind) {
                                        goto gm_ll278;
                                    }
                                    uint8_t t313 = 0;
                                    F.ret = t312;
                                    goto gm_fret;
                                }
                                goto ix291;
ie291:;
ix291:;
                            }
                            goto me286;
mn287:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 387, 17);
me286:;
                        }
lc274:;
                        {
                            uint64_t t315 = gm_l28;
                            uint64_t r316; bool o317 = __builtin_add_overflow(t315, ((uint64_t)1ULL), &r316);
                            uint64_t t318 = r316;
                            gm_l28 = t318;
                        }
                    }
gm_ll278:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb274; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc274; }
                    }
                    goto gm_lad;
lb274:;
                }
            }
            goto ix233;
ie233:;
ix233:;
        }
        {
            void * t319 = gm_l3;
            gm_t6j6smj7qoctu t320;
            gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
            GM_ST(gm_b16_8, (uint8_t *)&t320 + 0, t322);
            gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
            GM_ST(gm_b16_8, (uint8_t *)&t320 + 16, t324);
            gm_t53at80pfdus3 t325;
            GM_ST(uint8_t, (uint8_t *)&t325 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t325 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t325 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t325 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t325 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t325 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t325 + 8), t320);
            gm_t88rcucg1b7uk t326 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t319, &t325);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t327 = 0;
            F.ret = t326;
            goto gm_fret;
        }
        goto me5;
mn180:;
ma9:;
        uint8_t t329 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t329 != ((uint8_t)24ULL)) goto mn328;
        void * t330 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l31 = t330;
        void * t331 = gm_l3;
        void * t332 = gm_l0;
        uint64_t t333 = gm_l1;
        void * t334 = gm_l31;
        gm_t88rcucg1b7uk t335 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t332, t333, t334);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t336 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t331, t335);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t337 = 0;
        F.ret = t336;
        goto gm_fret;
        goto me5;
mn328:;
ma10:;
        uint8_t t339 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t339 != ((uint8_t)25ULL)) goto mn338;
        uint8_t t340 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l32 = t340;
        void * t341 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l33 = t341;
        {
            void * t344 = gm_l33;
            uint8_t t345 = GM_LD(uint8_t, ((((uint8_t *)t344) + 8) + 0));
            if (t345 != ((uint8_t)30ULL)) goto ie343;
            void * t346 = GM_LD(void *, ((((uint8_t *)t344) + 8) + 8));
            gm_l34 = t346;
            {
                uint8_t t349 = gm_l32;
                if (!t349) goto ie348;
                {
                    void * t351 = gm_l0;
                    void * t352 = gm_l2;
                    gm_tl874t2atnlh3 t353 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t352) + 72));
                    gm_b16_8 t354;
                    GM_ST(void *, (uint8_t *)&t354, (void *)(gm_sgva3apqq9jo2));
                    GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(67));
                    gm_b0_1 t355 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t351, t353, t354);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix348;
ie348:;
ix348:;
            }
            {
                void * t356 = gm_l0;
                uint64_t t357 = gm_l1;
                void * t358 = gm_l34;
                gm_b16_8 t359 = gm_f_empty_slice_3jt8vgk();
                gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
                gm_tg93b272d11vo t361;
                gm_f_resolve_fn_sig_pamuhve((void *)&t361, (void *)NULL, cx, NULL, t356, t357, t358, ((uint8_t)0), t360);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqqhoivahirr0 t362;
                if (((((uint8_t *)&t361))[136] != 0)) {
                    t362 = (*(gm_tqqhoivahirr0 *)(((uint8_t *)&t361) + 0));
                } else {
                    void * t363 = gm_l3;
                    gm_t88rcucg1b7uk t364 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t363);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t365 = 0;
                    F.ret = t364;
                    goto gm_fret;
                }
                F.l35 = t362;
            }
            {
                void * t366 = gm_l3;
                void * t367 = gm_l0;
                void * t368 = (void *)((uint8_t *)&F.l35);
                gm_tddtjfggdat0d t369;
                gm_f_fn_ty_qaih7ts((void *)&t369, (void *)NULL, cx, NULL, t367, t368);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t53at80pfdus3 t370;
                GM_ST(uint8_t, (uint8_t *)&t370 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t370 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t370 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)16ULL));
                GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t370 + 8), t369);
                gm_t88rcucg1b7uk t371 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t366, &t370);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t372 = 0;
                F.ret = t371;
                goto gm_fret;
            }
            goto ix343;
ie343:;
ix343:;
        }
        {
            void * t373 = gm_l3;
            uint8_t t374 = gm_l32;
            void * t375 = gm_l0;
            uint64_t t376 = gm_l1;
            void * t377 = gm_l33;
            gm_t88rcucg1b7uk t378 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t375, t376, t377);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t379 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t373, t374, t378);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t380 = 0;
            F.ret = t379;
            goto gm_fret;
        }
        goto me5;
mn338:;
ma11:;
        uint8_t t382 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t382 != ((uint8_t)26ULL)) goto mn381;
        uint8_t t383 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l36 = t383;
        void * t384 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l37 = t384;
        void * t385 = gm_l3;
        uint8_t t386 = gm_l36;
        void * t387 = gm_l0;
        uint64_t t388 = gm_l1;
        void * t389 = gm_l37;
        gm_t88rcucg1b7uk t390 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t387, t388, t389);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t391 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t385, t386, t390);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t392 = 0;
        F.ret = t391;
        goto gm_fret;
        goto me5;
mn381:;
ma12:;
        uint8_t t394 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t394 != ((uint8_t)27ULL)) goto mn393;
        uint8_t t395 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l38 = t395;
        void * t396 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l39 = t396;
        void * t397 = gm_l3;
        uint8_t t398 = gm_l38;
        void * t399 = gm_l0;
        uint64_t t400 = gm_l1;
        void * t401 = gm_l39;
        gm_t88rcucg1b7uk t402 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t399, t400, t401);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t403 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t397, t398, t402);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t404 = 0;
        F.ret = t403;
        goto gm_fret;
        goto me5;
mn393:;
ma13:;
        uint8_t t406 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t406 != ((uint8_t)28ULL)) goto mn405;
        gm_t3mf58gv9kf3n t407 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t4) + 8) + 8));
        gm_l40 = t407;
        {
            void * t409 = gm_l0;
            uint64_t t410 = gm_l1;
            void * t411 = GM_LD(void *, (((uint8_t *)&gm_l40) + 8));
            gm_t88rcucg1b7uk t412 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t409, t410, t411);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l41 = t412;
        }
        {
            void * t413 = GM_LD(void *, (((uint8_t *)&gm_l40) + 0));
            void * t414;
            if ((GM_LD(void *, ((uint8_t *)&t413)) != 0)) {
                void * t415 = GM_LD(void *, (((uint8_t *)&t413) + 0));
                t414 = t415;
            } else {
                {
                    void * t417 = gm_l0;
                    void * t418 = gm_l2;
                    gm_tl874t2atnlh3 t419 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t418) + 72));
                    gm_b16_8 t420;
                    GM_ST(void *, (uint8_t *)&t420, (void *)(gm_sm9funvq1222p));
                    GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)(43));
                    gm_b0_1 t421 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t417, t419, t420);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t422 = gm_l3;
                gm_t88rcucg1b7uk t423 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t422);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t424 = 0;
                F.ret = t423;
                goto gm_fret;
            }
            gm_l42 = t414;
        }
        {
            void * t426 = gm_l0;
            uint64_t t427 = gm_l1;
            void * t428 = gm_l42;
            uint8_t t429 = gm_f_length_is_type_soi53ah((void *)NULL, cx, NULL, t426, t427, t428);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t429) goto ie425;
            {
                void * t431 = gm_l0;
                uint64_t t432 = gm_l1;
                void * t433 = gm_l42;
                gm_t88rcucg1b7uk t434 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t431, t432, t433);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l43 = t434;
            }
            {
                void * t435 = gm_l0;
                gm_t88rcucg1b7uk t436 = gm_l43;
                gm_t88rcucg1b7uk t437 = gm_l41;
                void * t438 = gm_l42;
                gm_tl874t2atnlh3 t439 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t438) + 72));
                gm_t88rcucg1b7uk t440 = gm_f_enum_array_type_0sic6t0((void *)NULL, cx, NULL, t435, t436, t437, t439);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t441 = 0;
                F.ret = t440;
                goto gm_fret;
            }
            goto ix425;
ie425:;
ix425:;
        }
        {
            void * t442 = gm_l0;
            uint64_t t443 = gm_l1;
            void * t444 = gm_l42;
            gm_t5e6kkuq21nig t445 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t442, t443, t444);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t451 = GM_LD(uint8_t, (((uint8_t *)&t445) + 0));
            switch (t451) { case ((uint8_t)0ULL): goto ma447; case ((uint8_t)1ULL): goto ma448; case ((uint8_t)2ULL): goto ma449; default: goto ma450; }
ma447:;
            uint8_t t453 = GM_LD(uint8_t, (((uint8_t *)&t445) + 0));
            if (t453 != ((uint8_t)0ULL)) goto mn452;
            uint64_t t454 = GM_LD(uint64_t, (((uint8_t *)&t445) + 8));
            gm_l44 = t454;
            void * t455 = gm_l3;
            uint64_t t456 = gm_l44;
            gm_t88rcucg1b7uk t457 = gm_l41;
            gm_t88rcucg1b7uk t458 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t455, t456, t457);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t459 = 0;
            F.ret = t458;
            goto gm_fret;
            goto me446;
mn452:;
ma448:;
            uint8_t t461 = GM_LD(uint8_t, (((uint8_t *)&t445) + 0));
            if (t461 != ((uint8_t)1ULL)) goto mn460;
            gm_t88rcucg1b7uk t462 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t445) + 8));
            gm_l45 = t462;
            void * t463 = gm_l3;
            gm_t88rcucg1b7uk t464 = gm_l45;
            gm_t88rcucg1b7uk t465 = gm_l41;
            gm_tgglga8ro3isc t466;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t466 + 0), t464);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t466 + 4), t465);
            gm_t53at80pfdus3 t467;
            memset(&t467, 0, sizeof t467);
            GM_ST(uint8_t, ((uint8_t *)&t467 + 0), ((uint8_t)20ULL));
            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t467 + 8), t466);
            gm_t88rcucg1b7uk t468 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t463, &t467);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t469 = 0;
            F.ret = t468;
            goto gm_fret;
            goto me446;
mn460:;
ma449:;
            uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)&t445) + 0));
            if (t471 != ((uint8_t)2ULL)) goto mn470;
            void * t472 = gm_l3;
            gm_t88rcucg1b7uk t473 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t472);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t474 = 0;
            F.ret = t473;
            goto gm_fret;
            goto me446;
mn470:;
ma450:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 417, 13);
me446:;
        }
        goto me5;
mn405:;
ma14:;
        uint8_t t478 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t478 != ((uint8_t)29ULL)) goto pa477;
        void * t479 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l46 = t479;
        void * t480 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l47 = t480;
        goto po476;
pa477:;
        uint8_t t482 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t482 != ((uint8_t)17ULL)) goto pa481;
        void * t483 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l46 = t483;
        void * t484 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l47 = t484;
        goto po476;
pa481:;
        goto mn475;
po476:;
        {
            void * t486 = gm_l0;
            uint64_t t487 = gm_l1;
            void * t488 = gm_l46;
            gm_t88rcucg1b7uk t489 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t486, t487, t488);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l50 = t489;
        }
        {
            void * t490 = gm_l0;
            uint64_t t491 = gm_l1;
            void * t492 = gm_l47;
            gm_t88rcucg1b7uk t493 = gm_f_component_type_q9ge5lo((void *)NULL, cx, NULL, t490, t491, t492);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l51 = t493;
        }
        {
            void * t494 = gm_l3;
            gm_t88rcucg1b7uk t495 = gm_l50;
            gm_t88rcucg1b7uk t496 = gm_l51;
            gm_t88rcucg1b7uk t497 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t494, t495, t496);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t498 = 0;
            F.ret = t497;
            goto gm_fret;
        }
        goto me5;
mn475:;
ma15:;
        uint8_t t500 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t500 != ((uint8_t)30ULL)) goto mn499;
        void * t501 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l52 = t501;
        {
            void * t503 = gm_l0;
            uint64_t t504 = gm_l1;
            void * t505 = gm_l52;
            gm_b16_8 t506 = gm_f_empty_slice_3jt8vgk();
            gm_b16_8 t507 = GM_LD(gm_b16_8, ((uint8_t *)&t506));
            gm_tg93b272d11vo t508;
            gm_f_resolve_fn_sig_pamuhve((void *)&t508, (void *)NULL, cx, NULL, t503, t504, t505, ((uint8_t)0), t507);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqqhoivahirr0 t509;
            if (((((uint8_t *)&t508))[136] != 0)) {
                t509 = (*(gm_tqqhoivahirr0 *)(((uint8_t *)&t508) + 0));
            } else {
                void * t510 = gm_l3;
                gm_t88rcucg1b7uk t511 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t510);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t512 = 0;
                F.ret = t511;
                goto gm_fret;
            }
            F.l53 = t509;
        }
        {
            void * t513 = gm_l3;
            void * t514 = gm_l0;
            void * t515 = (void *)((uint8_t *)&F.l53);
            gm_tddtjfggdat0d t516;
            gm_f_fn_ty_qaih7ts((void *)&t516, (void *)NULL, cx, NULL, t514, t515);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t517;
            GM_ST(uint8_t, (uint8_t *)&t517 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t517 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t517 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t517 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t517 + 8), t516);
            gm_t88rcucg1b7uk t518 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t513, &t517);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t519 = 0;
            F.ret = t518;
            goto gm_fret;
        }
        goto me5;
mn499:;
ma16:;
        uint8_t t521 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t521 != ((uint8_t)31ULL)) goto mn520;
        void * t522 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l54 = t522;
        void * t523 = gm_l3;
        void * t524 = gm_l0;
        uint64_t t525 = gm_l1;
        void * t526 = gm_l54;
        gm_t88rcucg1b7uk t527 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t524, t525, t526);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t528;
        memset(&t528, 0, sizeof t528);
        GM_ST(uint8_t, ((uint8_t *)&t528 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t528 + 8), t527);
        gm_t88rcucg1b7uk t529 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t523, &t528);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t530 = 0;
        F.ret = t529;
        goto gm_fret;
        goto me5;
mn520:;
ma17:;
        uint8_t t532 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t532 != ((uint8_t)19ULL)) goto mn531;
        void * t533 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l55 = t533;
        gm_b16_8 t534 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l56 = t534;
        {
            void * t537 = gm_l55;
            uint8_t t538 = GM_LD(uint8_t, ((((uint8_t *)t537) + 8) + 0));
            if (t538 != ((uint8_t)22ULL)) goto ie536;
            void * t539 = GM_LD(void *, (((((uint8_t *)t537) + 8) + 8) + 0));
            gm_l57 = t539;
            void * t540 = gm_l0;
            uint64_t t541 = gm_l1;
            void * t542 = gm_l57;
            uint8_t t543 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t540, t541, t542);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t543) goto ie536;
            {
                void * t545 = gm_l3;
                gm_t88rcucg1b7uk t546 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t545);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t547 = 0;
                F.ret = t546;
                goto gm_fret;
            }
            goto ix536;
ie536:;
ix536:;
        }
        {
            gm_t9805qtonbsag t548;
            memset(&t548, 0, sizeof t548);
            gm_l58 = t548;
        }
        {
            void * t550 = gm_l0;
            uint64_t t551 = gm_l1;
            void * t552 = gm_l55;
            gm_toblvhepnjqkh t553;
            gm_f_generic_decl_oir9hm6((void *)&t553, (void *)NULL, cx, NULL, t550, t551, t552, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t553))[24] != 0)) goto ie549;
            gm_l59 = (*(gm_tkb8hpcqf70t1 *)(((uint8_t *)&t553) + 0));
            {
                void * t555 = gm_l0;
                uint64_t t556 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 0));
                gm_t6qooom8qa2do t557;
                gm_f_resolve_decl_pus042v((void *)&t557, (void *)NULL, cx, NULL, t555, t556);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t563 = GM_LD(uint8_t, (((uint8_t *)&t557) + 0));
                switch (t563) { case ((uint8_t)0ULL): goto ma561; case ((uint8_t)1ULL): goto ma559; case ((uint8_t)2ULL): goto ma561; case ((uint8_t)3ULL): goto ma561; case ((uint8_t)4ULL): goto ma561; case ((uint8_t)5ULL): goto ma561; case ((uint8_t)6ULL): goto ma561; case ((uint8_t)7ULL): goto ma561; case ((uint8_t)8ULL): goto ma561; case ((uint8_t)9ULL): goto ma561; case ((uint8_t)10ULL): goto ma561; case ((uint8_t)11ULL): goto ma560; default: goto ma561; }
ma559:;
                uint8_t t565 = GM_LD(uint8_t, (((uint8_t *)&t557) + 0));
                if (t565 != ((uint8_t)1ULL)) goto mn564;
                uint32_t t566 = GM_LD(uint32_t, (((uint8_t *)&t557) + 8));
                gm_l60 = t566;
                {
                    void * t568 = gm_l0;
                    uint64_t t569 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 0));
                    gm_tl874t2atnlh3 t570 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l59) + 8));
                    gm_b0_1 t571 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t568, t569, t570);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint32_t t572 = gm_l60;
                    gm_t9805qtonbsag t573;
                    GM_ST(uint8_t, (uint8_t *)&t573 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t573 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t573), t572);
                    ((uint8_t *)&t573)[4] = 1;
                    gm_l58 = t573;
                }
                goto me558;
mn564:;
ma560:;
                uint8_t t575 = GM_LD(uint8_t, (((uint8_t *)&t557) + 0));
                if (t575 != ((uint8_t)11ULL)) goto mn574;
                void * t576 = gm_l3;
                gm_t88rcucg1b7uk t577 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t576);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t578 = 0;
                F.ret = t577;
                goto gm_fret;
                goto me558;
mn574:;
ma561:;
                goto me558;
mn579:;
ma562:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 438, 17);
me558:;
            }
            goto ix549;
ie549:;
            void * t582 = gm_l55;
            uint8_t t583;
            uint8_t t586 = GM_LD(uint8_t, ((((uint8_t *)t582) + 8) + 0));
            if (t586 != ((uint8_t)22ULL)) goto mn585;
            t583 = ((uint8_t)1);
            goto me584;
mn585:;
            t583 = ((uint8_t)0);
            goto me584;
mn587:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 446, 23);
me584:;
            if (!t583) goto ie581;
            {
                void * t589 = gm_l3;
                gm_t88rcucg1b7uk t590 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t589);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t591 = 0;
                F.ret = t590;
                goto gm_fret;
            }
            goto ix581;
ie581:;
ix581:;
ix549:;
        }
        {
            gm_t9805qtonbsag t592 = gm_l58;
            uint32_t t593;
            if (((((uint8_t *)&t592))[4] != 0)) {
                uint32_t t594 = GM_LD(uint32_t, (((uint8_t *)&t592) + 0));
                t593 = t594;
            } else {
                {
                    void * t596 = gm_l0;
                    void * t597 = gm_l2;
                    gm_tl874t2atnlh3 t598 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t597) + 72));
                    gm_b16_8 t599;
                    GM_ST(void *, (uint8_t *)&t599, (void *)(gm_s2328gjukbu28));
                    GM_ST(uintptr_t, (uint8_t *)&t599 + 8, (uintptr_t)(15));
                    gm_b0_1 t600 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t596, t598, t599);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t601 = gm_l3;
                gm_t88rcucg1b7uk t602 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t601);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t603 = 0;
                F.ret = t602;
                goto gm_fret;
            }
            gm_l61 = t593;
        }
        {
            void * t604 = gm_l0;
            uint64_t t605 = gm_l1;
            uint32_t t606 = gm_l61;
            gm_b16_8 t607 = gm_l56;
            gm_b16_8 t608 = GM_LD(gm_b16_8, ((uint8_t *)&t607));
            void * t609 = gm_l2;
            gm_tl874t2atnlh3 t610 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t609) + 72));
            gm_th2lqbo77pvsc t611;
            gm_f_generic_type_args_5b8bvov((void *)&t611, (void *)NULL, cx, NULL, t604, t605, t606, t608, t610);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t612;
            if (((((uint8_t *)&t611))[16] != 0)) {
                gm_b16_8 t613 = GM_LD(gm_b16_8, (((uint8_t *)&t611) + 0));
                t612 = t613;
            } else {
                void * t614 = gm_l3;
                gm_t88rcucg1b7uk t615 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t614);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t616 = 0;
                F.ret = t615;
                goto gm_fret;
            }
            gm_l62 = t612;
        }
        {
            uint32_t t617 = gm_l61;
            uint64_t t618 = (uint64_t)t617;
            uint64_t t619 = (uintptr_t)t618;
            void * t620 = gm_l0;
            uint64_t t621 = GM_LD(uintptr_t, ((((uint8_t *)t620) + 1088) + 0) + 8);
            if (t619 >= (uintptr_t)(t621)) gm_trap_index(t619, t621, gm_srcsffver18si54, 454, 23);
            gm_b16_8 t622 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t620) + 1088) + 0)) + t619 * 184) + 120));
            gm_l63 = t622;
        }
        {
            {
                gm_b16_8 t624 = gm_l63;
                gm_b16_8 t625 = GM_LD(gm_b16_8, ((uint8_t *)&t624));
                gm_l64 = t625;
            }
            {
                gm_l65 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t627 = gm_l65;
                    uint64_t t628 = GM_LD(uintptr_t, ((uint8_t *)&gm_l64) + 8);
                    uint8_t t629 = (t627 < t628);
                    if (!t629) goto lb626;
                    {
                        uint64_t t632 = gm_l65;
                        uint64_t t633 = gm_l65;
                        uint64_t t634 = (uintptr_t)t633;
                        uint64_t t635 = GM_LD(uintptr_t, ((uint8_t *)&gm_l64) + 8);
                        uint32_t t636 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l64)) + t634 * 4));
                        gm_tn68el8d14vku t637;
                        GM_ST(uint32_t, (uint8_t *)&t637 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t637 + 0), t632);
                        GM_ST(uint32_t, ((uint8_t *)&t637 + 8), t636);
                        uint64_t t640 = GM_LD(uint64_t, (((uint8_t *)&t637) + 0));
                        gm_l66 = t640;
                        uint32_t t641 = GM_LD(uint32_t, (((uint8_t *)&t637) + 8));
                        gm_l67 = t641;
                        {
                            uint64_t t644 = gm_l66;
                            uint64_t t645 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                            uint8_t t646 = (t644 >= t645);
                            if (!t646) goto ie643;
                            {
                                goto lb626;
                            }
                            goto ix643;
ie643:;
ix643:;
                        }
                        {
                            uint64_t t648 = gm_l66;
                            uint64_t t649 = (uintptr_t)t648;
                            uint64_t t650 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                            if (t649 >= (uintptr_t)(t650)) gm_trap_index(t649, t650, gm_srcsffver18si54, 457, 22);
                            gm_t88rcucg1b7uk t651 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l62)) + t649 * 4));
                            gm_l68 = t651;
                        }
                        {
                            {
                                uint32_t t653 = gm_l67;
                                uint64_t t654 = (uint64_t)t653;
                                uint64_t t655 = (uintptr_t)t654;
                                void * t656 = gm_l0;
                                uint64_t t657 = GM_LD(uintptr_t, ((((uint8_t *)t656) + 1128) + 0) + 8);
                                if (t655 >= (uintptr_t)(t657)) gm_trap_index(t655, t657, gm_srcsffver18si54, 458, 26);
                                gm_b16_8 t658 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t656) + 1128) + 0)) + t655 * 48) + 8) + 0));
                                gm_l69 = t658;
                            }
                            {
                                gm_l70 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t660 = gm_l70;
                                    uint64_t t661 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                    uint8_t t662 = (t660 < t661);
                                    if (!t662) goto lb659;
                                    {
                                        uint64_t t665 = gm_l70;
                                        uint64_t t666 = (uintptr_t)t665;
                                        uint64_t t667 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                        gm_t9qkh0rsooc8d t670 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t666 * 1));
                                        gm_l71 = t670;
                                        {
                                            void * t673 = gm_l0;
                                            gm_t88rcucg1b7uk t674 = gm_l68;
                                            gm_t9qkh0rsooc8d t675 = gm_l71;
                                            uint8_t t676 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t673, t674, t675);
                                            if (gm_unw.kind) {
                                                goto gm_ll663;
                                            }
                                            uint8_t t677 = (uint8_t)!t676;
                                            if (!t677) goto ie672;
                                            {
                                                void * t680 = gm_l0;
                                                uint32_t t681 = GM_LD(uint32_t, (((uint8_t *)t680) + 1768));
                                                uint8_t t682 = (t681 > ((uint32_t)0ULL));
                                                if (!t682) goto ie679;
                                                void * t683 = gm_l3;
                                                gm_t88rcucg1b7uk t684 = gm_l68;
                                                uint8_t t685 = gm_f_has_param_u9t0efs(t683, GM_CONV(gm_csguankko4lmhd, t684));
                                                if (!t685) goto ie679;
                                                {
                                                    void * t687 = gm_l0;
                                                    void * t688 = (void *)(((uint8_t *)t687) + 1776);
                                                    gm_t8er5olt969ip t689;
                                                    memset((uint8_t *)&t689 + 5, 0, 3);
                                                    gm_t88rcucg1b7uk t690 = gm_l68;
                                                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t689 + 0, t690);
                                                    gm_t9qkh0rsooc8d t691 = gm_l71;
                                                    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t689 + 4, t691);
                                                    uint32_t t692 = gm_l67;
                                                    GM_ST(uint32_t, (uint8_t *)&t689 + 8, t692);
                                                    uint32_t t693 = gm_l61;
                                                    GM_ST(uint32_t, (uint8_t *)&t689 + 12, t693);
                                                    void * t694 = gm_l2;
                                                    gm_tl874t2atnlh3 t695 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t694) + 72));
                                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t689 + 16, t695);
                                                    gm_tftb1odc739t5 t696 = gm_f_push_afk8dfq(t688, &t689);
                                                    if (((((uint8_t *)&t696))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t696) + 1, gm_srcsffver18si54, 463, 29);
                                                    gm_b0_1 t697 = GM_LD(gm_b0_1, (((uint8_t *)&t696) + 1));
                                                }
                                                goto ix679;
ie679:;
                                                {
                                                    void * t699 = gm_l0;
                                                    void * t700 = gm_l2;
                                                    gm_tl874t2atnlh3 t701 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t700) + 72));
                                                    void * t702 = gm_l0;
                                                    gm_t88rcucg1b7uk t703 = gm_l68;
                                                    gm_t9qkh0rsooc8d t704 = gm_l71;
                                                    uint32_t t705 = gm_l67;
                                                    uint32_t t706 = gm_l61;
                                                    gm_b16_8 t707 = gm_f_unsatisfied_jnmtj8v((void *)NULL, cx, NULL, t702, t703, t704, t705, t706);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll663;
                                                    }
                                                    gm_b0_1 t708 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t699, t701, t707);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll663;
                                                    }
                                                }
ix679:;
                                            }
                                            goto ix672;
ie672:;
ix672:;
                                        }
                                        goto me668;
mn669:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 458, 17);
me668:;
                                    }
lc659:;
                                    {
                                        uint64_t t710 = gm_l70;
                                        uint64_t r711; bool o712 = __builtin_add_overflow(t710, ((uint64_t)1ULL), &r711);
                                        uint64_t t713 = r711;
                                        gm_l70 = t713;
                                    }
                                }
gm_ll663:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb659; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc659; }
                                }
                                goto gm_ll630;
lb659:;
                            }
                        }
                        goto me638;
mn639:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 455, 13);
me638:;
                    }
lc626:;
                    {
                        uint64_t t715 = gm_l65;
                        uint64_t r716; bool o717 = __builtin_add_overflow(t715, ((uint64_t)1ULL), &r716);
                        uint64_t t718 = r716;
                        gm_l65 = t718;
                    }
                }
gm_ll630:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb626; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc626; }
                }
                goto gm_lad;
lb626:;
            }
        }
        {
            {
                gm_b16_8 t720 = gm_l62;
                gm_l72 = t720;
            }
            {
                gm_l73 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t722 = gm_l73;
                    uint64_t t723 = GM_LD(uintptr_t, ((uint8_t *)&gm_l72) + 8);
                    uint8_t t724 = (t722 < t723);
                    if (!t724) goto lb721;
                    {
                        uint64_t t727 = gm_l73;
                        uint64_t t728 = (uintptr_t)t727;
                        uint64_t t729 = GM_LD(uintptr_t, ((uint8_t *)&gm_l72) + 8);
                        gm_t88rcucg1b7uk t732 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l72)) + t728 * 4));
                        gm_l74 = t732;
                        {
                            void * t735 = gm_l0;
                            gm_t88rcucg1b7uk t736 = gm_l74;
                            gm_tkn42u7na3t81 t737 = gm_f_second_class_part_k6i54mk(t735, GM_CONV(gm_csguankko4lmhd, t736));
                            if (!((((uint8_t *)&t737))[4] != 0)) goto ie734;
                            gm_t88rcucg1b7uk t738 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t737));
                            gm_l75 = t738;
                            {
                                void * t740 = gm_l0;
                                void * t741 = gm_l2;
                                gm_tl874t2atnlh3 t742 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t741) + 72));
                                void * t743 = gm_l0;
                                gm_t88rcucg1b7uk t744 = gm_l74;
                                gm_t88rcucg1b7uk t745 = gm_l75;
                                gm_b16_8 t746 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t743, t744, t745);
                                if (gm_unw.kind) {
                                    goto gm_ll725;
                                }
                                gm_b0_1 t747 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t740, t742, t746);
                                if (gm_unw.kind) {
                                    goto gm_ll725;
                                }
                            }
                            goto ix734;
ie734:;
ix734:;
                        }
                        goto me730;
mn731:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 470, 13);
me730:;
                    }
lc721:;
                    {
                        uint64_t t749 = gm_l73;
                        uint64_t r750; bool o751 = __builtin_add_overflow(t749, ((uint64_t)1ULL), &r750);
                        uint64_t t752 = r750;
                        gm_l73 = t752;
                    }
                }
gm_ll725:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb721; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc721; }
                }
                goto gm_lad;
lb721:;
            }
        }
        {
            void * t753 = gm_l0;
            uint32_t t754 = gm_l61;
            gm_b16_8 t755 = gm_l62;
            gm_b16_8 t756 = GM_LD(gm_b16_8, ((uint8_t *)&t755));
            gm_t88rcucg1b7uk t757 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t753, t754, t756);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l76 = t757;
        }
        {
            void * t758 = gm_l0;
            void * t759 = gm_l2;
            gm_tl874t2atnlh3 t760 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t759) + 72));
            gm_b0_1 t761 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t758, t760);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t88rcucg1b7uk t762 = gm_l76;
            uint8_t t763 = 0;
            F.ret = t762;
            goto gm_fret;
        }
        goto me5;
mn531:;
ma18:;
        uint8_t t765 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t765 != ((uint8_t)35ULL)) goto mn764;
        gm_b16_8 t766 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l77 = t766;
        void * t767 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l78 = t767;
        {
            void * t770 = gm_l78;
            uint8_t t771 = GM_LD(uint8_t, ((((uint8_t *)t770) + 8) + 0));
            if (t771 != ((uint8_t)25ULL)) goto ie769;
            void * t772 = GM_LD(void *, (((((uint8_t *)t770) + 8) + 8) + 8));
            gm_l79 = t772;
            void * t773 = gm_l79;
            uint8_t t774;
            uint8_t t777 = GM_LD(uint8_t, ((((uint8_t *)t773) + 8) + 0));
            if (t777 != ((uint8_t)30ULL)) goto mn776;
            t774 = ((uint8_t)1);
            goto me775;
mn776:;
            t774 = ((uint8_t)0);
            goto me775;
mn778:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 478, 49);
me775:;
            if (!t774) goto ie769;
            {
                void * t780 = gm_l0;
                uint64_t t781 = gm_l1;
                gm_b16_8 t782 = gm_l77;
                gm_b16_8 t783 = GM_LD(gm_b16_8, ((uint8_t *)&t782));
                void * t784 = gm_l78;
                void * t785 = gm_l2;
                gm_tl874t2atnlh3 t786 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t785) + 72));
                gm_t88rcucg1b7uk t787 = gm_f_callconv_fn_ptr_l9kavc3((void *)NULL, cx, NULL, t780, t781, t783, t784, t786);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t788 = 0;
                F.ret = t787;
                goto gm_fret;
            }
            goto ix769;
ie769:;
ix769:;
        }
        {
            {
                gm_b16_8 t790 = gm_l77;
                gm_l80 = t790;
            }
            {
                gm_l81 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t792 = gm_l81;
                    uint64_t t793 = GM_LD(uintptr_t, ((uint8_t *)&gm_l80) + 8);
                    uint8_t t794 = (t792 < t793);
                    if (!t794) goto lb791;
                    {
                        uint64_t t797 = gm_l81;
                        uint64_t t798 = (uintptr_t)t797;
                        uint64_t t799 = GM_LD(uintptr_t, ((uint8_t *)&gm_l80) + 8);
                        gm_tkacitepg5fk0 t802 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l80)) + t798 * 72));
                        F.l82 = t802;
                        {
                            uint8_t t805 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l82) + 0) + 0) + 8) == 3) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l82) + 0) + 0)), gm_sqes10usq4q5e, 3) == 0));
                            if (!t805) goto ie804;
                            void * t806 = gm_l78;
                            uint8_t t807;
                            uint8_t t813 = GM_LD(uint8_t, ((((uint8_t *)t806) + 8) + 0));
                            switch (t813) { case ((uint8_t)0ULL): goto ma811; case ((uint8_t)1ULL): goto ma811; case ((uint8_t)2ULL): goto ma811; case ((uint8_t)3ULL): goto ma811; case ((uint8_t)4ULL): goto ma811; case ((uint8_t)5ULL): goto ma811; case ((uint8_t)6ULL): goto ma811; case ((uint8_t)7ULL): goto ma811; case ((uint8_t)8ULL): goto ma811; case ((uint8_t)9ULL): goto ma811; case ((uint8_t)10ULL): goto ma811; case ((uint8_t)11ULL): goto ma811; case ((uint8_t)12ULL): goto ma811; case ((uint8_t)13ULL): goto ma811; case ((uint8_t)14ULL): goto ma811; case ((uint8_t)15ULL): goto ma811; case ((uint8_t)16ULL): goto ma811; case ((uint8_t)17ULL): goto ma811; case ((uint8_t)18ULL): goto ma811; case ((uint8_t)19ULL): goto ma811; case ((uint8_t)20ULL): goto ma811; case ((uint8_t)21ULL): goto ma811; case ((uint8_t)22ULL): goto ma811; case ((uint8_t)23ULL): goto ma811; case ((uint8_t)24ULL): goto ma811; case ((uint8_t)25ULL): goto ma811; case ((uint8_t)26ULL): goto ma810; case ((uint8_t)27ULL): goto ma811; case ((uint8_t)28ULL): goto ma809; case ((uint8_t)29ULL): goto ma811; case ((uint8_t)30ULL): goto ma811; case ((uint8_t)31ULL): goto ma811; case ((uint8_t)32ULL): goto ma811; case ((uint8_t)33ULL): goto ma811; case ((uint8_t)34ULL): goto ma811; case ((uint8_t)35ULL): goto ma811; case ((uint8_t)36ULL): goto ma811; case ((uint8_t)37ULL): goto ma811; case ((uint8_t)38ULL): goto ma811; case ((uint8_t)39ULL): goto ma811; case ((uint8_t)40ULL): goto ma811; case ((uint8_t)41ULL): goto ma811; case ((uint8_t)42ULL): goto ma811; case ((uint8_t)43ULL): goto ma811; case ((uint8_t)44ULL): goto ma811; case ((uint8_t)45ULL): goto ma811; case ((uint8_t)46ULL): goto ma811; case ((uint8_t)47ULL): goto ma811; case ((uint8_t)48ULL): goto ma811; case ((uint8_t)49ULL): goto ma811; case ((uint8_t)50ULL): goto ma811; default: goto ma811; }
ma809:;
                            uint8_t t815 = GM_LD(uint8_t, ((((uint8_t *)t806) + 8) + 0));
                            if (t815 != ((uint8_t)28ULL)) goto mn814;
                            t807 = ((uint8_t)1);
                            goto me808;
mn814:;
ma810:;
                            uint8_t t817 = GM_LD(uint8_t, ((((uint8_t *)t806) + 8) + 0));
                            if (t817 != ((uint8_t)26ULL)) goto mn816;
                            t807 = ((uint8_t)1);
                            goto me808;
mn816:;
ma811:;
                            t807 = ((uint8_t)0);
                            goto me808;
mn818:;
ma812:;
                            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 482, 45);
me808:;
                            if (!t807) goto ie804;
                            {
                                uint8_t t821 = 0;
                                uint64_t t822 = GM_LD(uintptr_t, ((uint8_t *)&gm_l77) + 8);
                                uint8_t t823 = (t822 > ((uint64_t)1ULL));
                                if (!t823) {
                                    uint64_t t824 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t825 = GM_LD(uintptr_t, ((uint8_t *)&gm_l77) + 8);
                                    if (t824 >= (uintptr_t)(t825)) gm_trap_index(t824, t825, gm_srcsffver18si54, 483, 41);
                                    uint8_t t826;
                                    if ((((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l77)) + t824 * 72) + 32))[16] != 0)) goto mn828;
                                    t826 = ((uint8_t)0);
                                    goto me827;
mn828:;
                                    t826 = ((uint8_t)1);
                                    goto me827;
mn829:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 483, 41);
me827:;
                                    t821 = t826;
                                } else t821 = 1;
                                if (!t821) goto ie820;
                                {
                                    void * t831 = gm_l0;
                                    void * t832 = gm_l2;
                                    gm_tl874t2atnlh3 t833 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t832) + 72));
                                    gm_b16_8 t834;
                                    GM_ST(void *, (uint8_t *)&t834, (void *)(gm_sgrpmaenv85i1));
                                    GM_ST(uintptr_t, (uint8_t *)&t834 + 8, (uintptr_t)(72));
                                    gm_b0_1 t835 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t831, t833, t834);
                                    if (gm_unw.kind) {
                                        goto gm_ll795;
                                    }
                                }
                                {
                                    void * t836 = gm_l3;
                                    gm_t88rcucg1b7uk t837 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t836);
                                    if (gm_unw.kind) {
                                        goto gm_ll795;
                                    }
                                    uint8_t t838 = 0;
                                    F.ret = t837;
                                    goto gm_fret;
                                }
                                goto ix820;
ie820:;
ix820:;
                            }
                            {
                                void * t839 = gm_l0;
                                uint64_t t840 = gm_l1;
                                void * t841 = gm_l78;
                                void * t842 = gm_l2;
                                gm_tl874t2atnlh3 t843 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t842) + 72));
                                gm_t88rcucg1b7uk t844 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t839, t840, t841, t843);
                                if (gm_unw.kind) {
                                    goto gm_ll795;
                                }
                                uint8_t t845 = 0;
                                F.ret = t844;
                                goto gm_fret;
                            }
                            goto ix804;
ie804:;
ix804:;
                        }
                        goto me800;
mn801:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 481, 13);
me800:;
                    }
lc791:;
                    {
                        uint64_t t847 = gm_l81;
                        uint64_t r848; bool o849 = __builtin_add_overflow(t847, ((uint64_t)1ULL), &r848);
                        uint64_t t850 = r848;
                        gm_l81 = t850;
                    }
                }
gm_ll795:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb791; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc791; }
                }
                goto gm_lad;
lb791:;
            }
        }
        {
            void * t852 = gm_l78;
            uint8_t t853;
            uint8_t t856 = GM_LD(uint8_t, ((((uint8_t *)t852) + 8) + 0));
            if (t856 != ((uint8_t)28ULL)) goto mn855;
            t853 = ((uint8_t)1);
            goto me854;
mn855:;
            t853 = ((uint8_t)0);
            goto me854;
mn857:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 490, 16);
me854:;
            if (!t853) goto ie851;
            {
                void * t859 = gm_l0;
                uint64_t t860 = gm_l1;
                gm_b16_8 t861 = gm_l77;
                gm_b16_8 t862 = GM_LD(gm_b16_8, ((uint8_t *)&t861));
                void * t863 = gm_l78;
                void * t864 = gm_l2;
                gm_tl874t2atnlh3 t865 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t864) + 72));
                gm_t88rcucg1b7uk t866 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t859, t860, t862, t863, t865);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t867 = 0;
                F.ret = t866;
                goto gm_fret;
            }
            goto ix851;
ie851:;
ix851:;
        }
        {
            void * t869 = gm_l78;
            uint8_t t870;
            uint8_t t876 = GM_LD(uint8_t, ((((uint8_t *)t869) + 8) + 0));
            switch (t876) { case ((uint8_t)0ULL): goto ma874; case ((uint8_t)1ULL): goto ma874; case ((uint8_t)2ULL): goto ma874; case ((uint8_t)3ULL): goto ma874; case ((uint8_t)4ULL): goto ma874; case ((uint8_t)5ULL): goto ma874; case ((uint8_t)6ULL): goto ma874; case ((uint8_t)7ULL): goto ma874; case ((uint8_t)8ULL): goto ma874; case ((uint8_t)9ULL): goto ma874; case ((uint8_t)10ULL): goto ma874; case ((uint8_t)11ULL): goto ma874; case ((uint8_t)12ULL): goto ma874; case ((uint8_t)13ULL): goto ma874; case ((uint8_t)14ULL): goto ma874; case ((uint8_t)15ULL): goto ma874; case ((uint8_t)16ULL): goto ma874; case ((uint8_t)17ULL): goto ma874; case ((uint8_t)18ULL): goto ma874; case ((uint8_t)19ULL): goto ma874; case ((uint8_t)20ULL): goto ma874; case ((uint8_t)21ULL): goto ma874; case ((uint8_t)22ULL): goto ma874; case ((uint8_t)23ULL): goto ma874; case ((uint8_t)24ULL): goto ma874; case ((uint8_t)25ULL): goto ma874; case ((uint8_t)26ULL): goto ma874; case ((uint8_t)27ULL): goto ma874; case ((uint8_t)28ULL): goto ma874; case ((uint8_t)29ULL): goto ma874; case ((uint8_t)30ULL): goto ma874; case ((uint8_t)31ULL): goto ma874; case ((uint8_t)32ULL): goto ma872; case ((uint8_t)33ULL): goto ma873; case ((uint8_t)34ULL): goto ma874; case ((uint8_t)35ULL): goto ma874; case ((uint8_t)36ULL): goto ma874; case ((uint8_t)37ULL): goto ma874; case ((uint8_t)38ULL): goto ma874; case ((uint8_t)39ULL): goto ma874; case ((uint8_t)40ULL): goto ma874; case ((uint8_t)41ULL): goto ma874; case ((uint8_t)42ULL): goto ma874; case ((uint8_t)43ULL): goto ma874; case ((uint8_t)44ULL): goto ma874; case ((uint8_t)45ULL): goto ma874; case ((uint8_t)46ULL): goto ma874; case ((uint8_t)47ULL): goto ma874; case ((uint8_t)48ULL): goto ma874; case ((uint8_t)49ULL): goto ma874; case ((uint8_t)50ULL): goto ma874; default: goto ma874; }
ma872:;
            uint8_t t878 = GM_LD(uint8_t, ((((uint8_t *)t869) + 8) + 0));
            if (t878 != ((uint8_t)32ULL)) goto mn877;
            t870 = ((uint8_t)0);
            goto me871;
mn877:;
ma873:;
            uint8_t t880 = GM_LD(uint8_t, ((((uint8_t *)t869) + 8) + 0));
            if (t880 != ((uint8_t)33ULL)) goto mn879;
            t870 = ((uint8_t)0);
            goto me871;
mn879:;
ma874:;
            t870 = ((uint8_t)1);
            goto me871;
mn881:;
ma875:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 491, 16);
me871:;
            if (!t870) goto ie868;
            {
                void * t883 = gm_l0;
                void * t884 = gm_l2;
                gm_tl874t2atnlh3 t885 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t884) + 72));
                gm_b16_8 t886;
                GM_ST(void *, (uint8_t *)&t886, (void *)(gm_ssj1a8k9nspq4));
                GM_ST(uintptr_t, (uint8_t *)&t886 + 8, (uintptr_t)(81));
                gm_b0_1 t887 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t883, t885, t886);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t888 = gm_l3;
                gm_t88rcucg1b7uk t889 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t888);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t890 = 0;
                F.ret = t889;
                goto gm_fret;
            }
            goto ix868;
ie868:;
ix868:;
        }
        {
            gm_b16_8 t891;
            void * t893 = gm_l78;
            uint8_t t894;
            uint8_t t897 = GM_LD(uint8_t, ((((uint8_t *)t893) + 8) + 0));
            if (t897 != ((uint8_t)33ULL)) goto mn896;
            t894 = ((uint8_t)1);
            goto me895;
mn896:;
            t894 = ((uint8_t)0);
            goto me895;
mn898:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 495, 24);
me895:;
            if (!t894) goto ie892;
            gm_b16_8 t900;
            GM_ST(void *, (uint8_t *)&t900, (void *)(gm_s8p1h96ji9kh0));
            GM_ST(uintptr_t, (uint8_t *)&t900 + 8, (uintptr_t)(5));
            gm_b16_8 t901 = t900;
            t891 = t901;
            goto ix892;
ie892:;
            gm_b16_8 t903;
            GM_ST(void *, (uint8_t *)&t903, (void *)(gm_sc49g68j2cekh));
            GM_ST(uintptr_t, (uint8_t *)&t903 + 8, (uintptr_t)(6));
            gm_b16_8 t904 = t903;
            t891 = t904;
ix892:;
            gm_l83 = t891;
        }
        {
            void * t905 = gm_l0;
            uint64_t t906 = gm_l1;
            gm_tf5srh4mp4b3i t907;
            memset(&t907, 0, sizeof t907);
            gm_b16_8 t908 = gm_l83;
            void * t909 = gm_l2;
            gm_t88rcucg1b7uk t910 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t905, t906, t907, t908, t909);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t911 = 0;
            F.ret = t910;
            goto gm_fret;
        }
        goto me5;
mn764:;
ma19:;
        uint8_t t913 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t913 != ((uint8_t)32ULL)) goto mn912;
        void * t914 = gm_l0;
        uint64_t t915 = gm_l1;
        gm_tf5srh4mp4b3i t916;
        memset(&t916, 0, sizeof t916);
        gm_b16_8 t917;
        GM_ST(void *, (uint8_t *)&t917, (void *)(gm_sc49g68j2cekh));
        GM_ST(uintptr_t, (uint8_t *)&t917 + 8, (uintptr_t)(6));
        void * t918 = gm_l2;
        gm_t88rcucg1b7uk t919 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t914, t915, t916, t917, t918);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t920 = 0;
        F.ret = t919;
        goto gm_fret;
        goto me5;
mn912:;
ma20:;
        uint8_t t922 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t922 != ((uint8_t)33ULL)) goto mn921;
        void * t923 = gm_l0;
        uint64_t t924 = gm_l1;
        gm_tf5srh4mp4b3i t925;
        memset(&t925, 0, sizeof t925);
        gm_b16_8 t926;
        GM_ST(void *, (uint8_t *)&t926, (void *)(gm_s8p1h96ji9kh0));
        GM_ST(uintptr_t, (uint8_t *)&t926 + 8, (uintptr_t)(5));
        void * t927 = gm_l2;
        gm_t88rcucg1b7uk t928 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t923, t924, t925, t926, t927);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t929 = 0;
        F.ret = t928;
        goto gm_fret;
        goto me5;
mn921:;
ma21:;
        uint8_t t931 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t931 != ((uint8_t)49ULL)) goto mn930;
        gm_tet9guhev5r4u t932 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t4) + 8) + 8));
        F.l84 = t932;
        {
            uint8_t t935 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l84) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l84) + 0) + 0)), gm_slbgk65sat4bm, 7) == 0));
            if (!t935) goto ie934;
            {
                if (!(((((uint8_t *)&F.l84) + 32))[16] != 0)) goto ie937;
                gm_b16_8 t938 = GM_LD(gm_b16_8, (((uint8_t *)&F.l84) + 32));
                gm_l85 = t938;
                uint64_t t939 = GM_LD(uintptr_t, ((uint8_t *)&gm_l85) + 8);
                uint8_t t940 = ((t939 == ((uint64_t)1ULL)));
                if (!t940) goto ie937;
                uint64_t t941 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t942 = GM_LD(uintptr_t, ((uint8_t *)&gm_l85) + 8);
                if (t941 >= (uintptr_t)(t942)) gm_trap_index(t941, t942, gm_srcsffver18si54, 502, 75);
                uint8_t t943 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l85)) + t941 * 144) + 0) + 0));
                if (t943 != ((uint8_t)0ULL)) goto ie937;
                gm_tc3oht4ksth9d t944 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l85)) + t941 * 144) + 0) + 8));
                F.l86 = t944;
                {
                    void * t946 = gm_l0;
                    void * t947 = GM_LD(void *, (((uint8_t *)t946) + 2184));
                    void * t948;
                    if ((GM_LD(void *, ((uint8_t *)&t947)) != 0)) {
                        void * t949 = GM_LD(void *, (((uint8_t *)&t947) + 0));
                        t948 = t949;
                    } else {
                        {
                            void * t951 = gm_l0;
                            void * t952 = gm_l2;
                            gm_tl874t2atnlh3 t953 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t952) + 72));
                            gm_b16_8 t954;
                            GM_ST(void *, (uint8_t *)&t954, (void *)(gm_smi7k9v6u7cmo));
                            GM_ST(uintptr_t, (uint8_t *)&t954 + 8, (uintptr_t)(10));
                            gm_b0_1 t955 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t951, t953, t954);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t956 = gm_l3;
                        gm_t88rcucg1b7uk t957 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t956);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t958 = 0;
                        F.ret = t957;
                        goto gm_fret;
                    }
                    gm_l87 = t948;
                }
                {
                    void * t959 = gm_l87;
                    void * t960 = gm_l0;
                    uint64_t t961 = gm_l1;
                    void * t962 = (void *)((uint8_t *)&F.l86);
                    void * t963 = t959;
                    gm_t88rcucg1b7uk t964;
                    if (GM_PTR_TAGGED(t963)) {
                        t964 = GM_CONV(gm_t88rcucg1b7uk, ((gm_csguankko4lmhd (*)(void *, uint64_t, void *))GM_PTR_UNTAG(t963))(t960, t961, t962));
                    } else {
                        t964 = ((gm_t88rcucg1b7uk (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *))t963)(NULL, cx, NULL, t960, t961, t962);
                    }
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t965 = 0;
                    F.ret = t964;
                    goto gm_fret;
                }
                goto ix937;
ie937:;
ix937:;
            }
            {
                void * t966 = gm_l0;
                void * t967 = gm_l2;
                gm_tl874t2atnlh3 t968 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t967) + 72));
                gm_b16_8 t969;
                GM_ST(void *, (uint8_t *)&t969, (void *)(gm_sfpopfqbubj89));
                GM_ST(uintptr_t, (uint8_t *)&t969 + 8, (uintptr_t)(34));
                gm_b0_1 t970 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t966, t968, t969);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t971 = gm_l3;
                gm_t88rcucg1b7uk t972 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t971);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t973 = 0;
                F.ret = t972;
                goto gm_fret;
            }
            goto ix934;
ie934:;
ix934:;
        }
        goto me5;
mn930:;
ma22:;
        uint8_t t975 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t975 != ((uint8_t)22ULL)) goto mn974;
        void * t976 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l88 = t976;
        gm_t08olq97rscn3 t977 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l89 = t977;
        {
            void * t980 = gm_l0;
            uint64_t t981 = gm_l1;
            void * t982 = gm_l88;
            gm_tf5srh4mp4b3i t983 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t980, t981, t982);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t983))[8] != 0)) goto ie979;
            uint64_t t984 = GM_LD(uint64_t, ((uint8_t *)&t983));
            gm_l90 = t984;
            {
                void * t986 = gm_l0;
                uint64_t t987 = gm_l90;
                uint64_t t988 = gm_l1;
                gm_tf5srh4mp4b3i t989 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t986, t987, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l89)), t988);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t990;
                if (((((uint8_t *)&t989))[8] != 0)) {
                    uint64_t t991 = GM_LD(uint64_t, (((uint8_t *)&t989) + 0));
                    t990 = t991;
                } else {
                    void * t992 = gm_l3;
                    gm_t88rcucg1b7uk t993 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t992);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t994 = 0;
                    F.ret = t993;
                    goto gm_fret;
                }
                gm_l91 = t990;
            }
            {
                void * t995 = gm_l0;
                uint64_t t996 = gm_l91;
                gm_t6qooom8qa2do t997;
                gm_f_resolve_decl_pus042v((void *)&t997, (void *)NULL, cx, NULL, t995, t996);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t1003 = GM_LD(uint8_t, (((uint8_t *)&t997) + 0));
                switch (t1003) { case ((uint8_t)0ULL): goto ma999; case ((uint8_t)1ULL): goto ma1001; case ((uint8_t)2ULL): goto ma1001; case ((uint8_t)3ULL): goto ma1001; case ((uint8_t)4ULL): goto ma1001; case ((uint8_t)5ULL): goto ma1001; case ((uint8_t)6ULL): goto ma1001; case ((uint8_t)7ULL): goto ma1001; case ((uint8_t)8ULL): goto ma1001; case ((uint8_t)9ULL): goto ma1001; case ((uint8_t)10ULL): goto ma1001; case ((uint8_t)11ULL): goto ma1000; default: goto ma1001; }
ma999:;
                uint8_t t1005 = GM_LD(uint8_t, (((uint8_t *)&t997) + 0));
                if (t1005 != ((uint8_t)0ULL)) goto mn1004;
                gm_t88rcucg1b7uk t1006 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t997) + 8));
                gm_l92 = t1006;
                gm_t88rcucg1b7uk t1007 = gm_l92;
                uint8_t t1008 = 0;
                F.ret = t1007;
                goto gm_fret;
                goto me998;
mn1004:;
ma1000:;
                uint8_t t1010 = GM_LD(uint8_t, (((uint8_t *)&t997) + 0));
                if (t1010 != ((uint8_t)11ULL)) goto mn1009;
                void * t1011 = gm_l3;
                gm_t88rcucg1b7uk t1012 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1011);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t1013 = 0;
                F.ret = t1012;
                goto gm_fret;
                goto me998;
mn1009:;
ma1001:;
                {
                    void * t1016 = gm_l0;
                    void * t1017 = gm_l2;
                    gm_tl874t2atnlh3 t1018 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1017) + 72));
                    gm_b16_8 t1019 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l89) + 0));
                    gm_tnpifv20irnph t1020;
                    GM_ST(gm_b16_8, ((uint8_t *)&t1020 + 0), t1019);
                    gm_tabb710d9mq2e t1021;
                    gm_f_format_sv91363((void *)&t1021, (void *)NULL, cx, NULL, t1020);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t1021))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1021) + 8, gm_srcsffver18si54, 521, 41);
                    gm_b16_8 t1022 = GM_LD(gm_b16_8, (((uint8_t *)&t1021) + 8));
                    gm_b16_8 t1023 = GM_LD(gm_b16_8, ((uint8_t *)&t1022));
                    gm_b0_1 t1024 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t1016, t1018, t1023);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t1025 = gm_l3;
                    gm_t88rcucg1b7uk t1026 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1025);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1027 = 0;
                    F.ret = t1026;
                    goto gm_fret;
                }
                goto me998;
mn1014:;
ma1002:;
                gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 517, 17);
me998:;
            }
            goto ix979;
ie979:;
ix979:;
        }
        {
            void * t1029 = gm_l0;
            uint64_t t1030 = gm_l1;
            void * t1031 = gm_l88;
            uint8_t t1032 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t1029, t1030, t1031);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t1032) goto ie1028;
            {
                void * t1034 = gm_l3;
                gm_t88rcucg1b7uk t1035 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1034);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t1036 = 0;
                F.ret = t1035;
                goto gm_fret;
            }
            goto ix1028;
ie1028:;
ix1028:;
        }
        goto me5;
mn974:;
ma23:;
        goto me5;
mn1037:;
ma24:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 332, 5);
me5:;
    }
    {
        void * t1039 = gm_l0;
        void * t1040 = gm_l2;
        gm_tl874t2atnlh3 t1041 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1040) + 72));
        gm_b16_8 t1042;
        GM_ST(void *, (uint8_t *)&t1042, (void *)(gm_s2328gjukbu28));
        GM_ST(uintptr_t, (uint8_t *)&t1042 + 8, (uintptr_t)(15));
        gm_b0_1 t1043 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t1039, t1041, t1042);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t1044 = gm_l3;
        gm_t88rcucg1b7uk t1045 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t1044);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t1046 = 0;
        F.ret = t1045;
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
extern __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8_c(void * a0, gm_t66j1im9ip128 a1) { gm_f_restore_scope_edqq1v8(a0, &a1); }
extern __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t66j1im9ip128 gm_f_scope_mark_i3b2mvo_c(void * a0) { gm_t66j1im9ip128 gm_r; gm_f_scope_mark_i3b2mvo(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_syntax_is_type_pe6ugv2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_is_type_pe6ugv2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    gm_t08olq97rscn3 gm_l4; /* id */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* inner */
    void * gm_l7; /* inner */
    void * gm_l8; /* callee */
    gm_t08olq97rscn3 gm_l9; /* id */
    uint64_t gm_l10; /* d */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    uint64_t gm_l13; /* m */
    uint64_t gm_l14; /* d */
    GM_STACK_CHECK(&F, 835u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)6ULL)) goto mn14;
        gm_t08olq97rscn3 t16 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t16;
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l1;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            gm_tf5srh4mp4b3i t22 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t22))[8] != 0)) goto ie18;
            uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t22));
            gm_l5 = t23;
            {
                void * t25 = gm_l0;
                uint64_t t26 = gm_l5;
                uint32_t t27 = gm_l3;
                uint32_t r28; bool o29 = __builtin_add_overflow(t27, ((uint32_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 155, 86);
                uint32_t t30 = r28;
                uint8_t t31 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t25, t26, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint8_t t34 = gm_f_is_builtin_type_name_v5ttoka(t33);
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto me3;
mn14:;
ma5:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)9ULL)) goto mn36;
        void * t38 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t38;
        void * t39 = gm_l0;
        uint64_t t40 = gm_l1;
        void * t41 = gm_l6;
        uint32_t t42 = gm_l3;
        uint8_t t43 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t39, t40, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me3;
mn36:;
ma6:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)35ULL)) goto mn45;
        void * t47 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l7 = t47;
        void * t48 = gm_l0;
        uint64_t t49 = gm_l1;
        void * t50 = gm_l7;
        uint32_t t51 = gm_l3;
        uint8_t t52 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t48, t49, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t53 = 0;
        F.ret = t52;
        goto gm_fret;
        goto me3;
mn45:;
ma7:;
        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t57 != ((uint8_t)32ULL)) goto pa56;
        goto po55;
pa56:;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t59 != ((uint8_t)33ULL)) goto pa58;
        goto po55;
pa58:;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t61 != ((uint8_t)25ULL)) goto pa60;
        goto po55;
pa60:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)26ULL)) goto pa62;
        goto po55;
pa62:;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t65 != ((uint8_t)27ULL)) goto pa64;
        goto po55;
pa64:;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t67 != ((uint8_t)29ULL)) goto pa66;
        goto po55;
pa66:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)30ULL)) goto pa68;
        goto po55;
pa68:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t71 != ((uint8_t)31ULL)) goto pa70;
        goto po55;
pa70:;
        goto mn54;
po55:;
        uint8_t t72 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn54:;
ma8:;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t76 != ((uint8_t)24ULL)) goto pa75;
        goto po74;
pa75:;
        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t78 != ((uint8_t)28ULL)) goto pa77;
        goto po74;
pa77:;
        goto mn73;
po74:;
        void * t79 = gm_l2;
        uint8_t t80 = gm_f_hoists_13k2a2s(t79);
        uint8_t t81 = (uint8_t)!t80;
        uint8_t t82 = 0;
        F.ret = t81;
        goto gm_fret;
        goto me3;
mn73:;
ma9:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t84 != ((uint8_t)19ULL)) goto mn83;
        void * t85 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t85;
        void * t86 = gm_l8;
        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        switch (t92) { case ((uint8_t)0ULL): goto ma90; case ((uint8_t)1ULL): goto ma90; case ((uint8_t)2ULL): goto ma90; case ((uint8_t)3ULL): goto ma90; case ((uint8_t)4ULL): goto ma90; case ((uint8_t)5ULL): goto ma90; case ((uint8_t)6ULL): goto ma88; case ((uint8_t)7ULL): goto ma90; case ((uint8_t)8ULL): goto ma90; case ((uint8_t)9ULL): goto ma90; case ((uint8_t)10ULL): goto ma90; case ((uint8_t)11ULL): goto ma90; case ((uint8_t)12ULL): goto ma90; case ((uint8_t)13ULL): goto ma90; case ((uint8_t)14ULL): goto ma90; case ((uint8_t)15ULL): goto ma90; case ((uint8_t)16ULL): goto ma90; case ((uint8_t)17ULL): goto ma90; case ((uint8_t)18ULL): goto ma90; case ((uint8_t)19ULL): goto ma90; case ((uint8_t)20ULL): goto ma90; case ((uint8_t)21ULL): goto ma90; case ((uint8_t)22ULL): goto ma89; case ((uint8_t)23ULL): goto ma90; case ((uint8_t)24ULL): goto ma90; case ((uint8_t)25ULL): goto ma90; case ((uint8_t)26ULL): goto ma90; case ((uint8_t)27ULL): goto ma90; case ((uint8_t)28ULL): goto ma90; case ((uint8_t)29ULL): goto ma90; case ((uint8_t)30ULL): goto ma90; case ((uint8_t)31ULL): goto ma90; case ((uint8_t)32ULL): goto ma90; case ((uint8_t)33ULL): goto ma90; case ((uint8_t)34ULL): goto ma90; case ((uint8_t)35ULL): goto ma90; case ((uint8_t)36ULL): goto ma90; case ((uint8_t)37ULL): goto ma90; case ((uint8_t)38ULL): goto ma90; case ((uint8_t)39ULL): goto ma90; case ((uint8_t)40ULL): goto ma90; case ((uint8_t)41ULL): goto ma90; case ((uint8_t)42ULL): goto ma90; case ((uint8_t)43ULL): goto ma90; case ((uint8_t)44ULL): goto ma90; case ((uint8_t)45ULL): goto ma90; case ((uint8_t)46ULL): goto ma90; case ((uint8_t)47ULL): goto ma90; case ((uint8_t)48ULL): goto ma90; case ((uint8_t)49ULL): goto ma90; case ((uint8_t)50ULL): goto ma90; default: goto ma90; }
ma88:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t94 != ((uint8_t)6ULL)) goto mn93;
        gm_t08olq97rscn3 t95 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t86) + 8) + 8));
        gm_l9 = t95;
        {
            void * t97 = gm_l0;
            uint64_t t98 = gm_l1;
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
            gm_tf5srh4mp4b3i t100 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t97, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t101;
            if (((((uint8_t *)&t100))[8] != 0)) {
                uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&t100) + 0));
                t101 = t102;
            } else {
                uint8_t t103 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l10 = t101;
        }
        {
            void * t104 = gm_l0;
            uint64_t t105 = gm_l10;
            uint32_t t106 = gm_l3;
            uint32_t r107; bool o108 = __builtin_add_overflow(t106, ((uint32_t)1ULL), &r107);
            if (o108) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 165, 42);
            uint32_t t109 = r107;
            uint8_t t110 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t104, t105, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t111 = 0;
            F.ret = t110;
            goto gm_fret;
        }
        goto me87;
mn93:;
ma89:;
        uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
        if (t113 != ((uint8_t)22ULL)) goto mn112;
        void * t114 = gm_l0;
        uint64_t t115 = gm_l1;
        void * t116 = gm_l8;
        uint32_t t117 = gm_l3;
        uint8_t t118 = gm_f_syntax_is_type_pe6ugv2((void *)NULL, cx, NULL, t114, t115, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t119 = 0;
        F.ret = t118;
        goto gm_fret;
        goto me87;
mn112:;
ma90:;
        uint8_t t121 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me87;
mn120:;
ma91:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 162, 32);
me87:;
        goto me3;
mn83:;
ma10:;
        uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t123 != ((uint8_t)22ULL)) goto mn122;
        void * t124 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l11 = t124;
        gm_t08olq97rscn3 t125 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l12 = t125;
        {
            void * t127 = gm_l0;
            uint64_t t128 = gm_l1;
            void * t129 = gm_l11;
            uint32_t t130 = gm_l3;
            gm_tf5srh4mp4b3i t131 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t127, t128, t129, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t132;
            if (((((uint8_t *)&t131))[8] != 0)) {
                uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&t131) + 0));
                t132 = t133;
            } else {
                uint8_t t134 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l13 = t132;
        }
        {
            void * t135 = gm_l0;
            void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
            uint64_t t137 = gm_l13;
            gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
            gm_tf5srh4mp4b3i t139 = gm_f_lookup_59juqoo(t136, t137, t138);
            uint64_t t140;
            if (((((uint8_t *)&t139))[8] != 0)) {
                uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&t139) + 0));
                t140 = t141;
            } else {
                uint8_t t142 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l14 = t140;
        }
        {
            void * t144 = gm_l0;
            uint64_t t145 = gm_l14;
            uint64_t t146 = gm_l1;
            uint8_t t147 = gm_f_decl_visible_bd168ti(t144, t145, t146);
            uint8_t t148 = (uint8_t)!t147;
            if (!t148) goto ie143;
            {
                uint8_t t150 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix143;
ie143:;
ix143:;
        }
        {
            void * t151 = gm_l0;
            uint64_t t152 = gm_l14;
            uint32_t t153 = gm_l3;
            uint32_t r154; bool o155 = __builtin_add_overflow(t153, ((uint32_t)1ULL), &r154);
            if (o155) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 175, 38);
            uint32_t t156 = r154;
            uint8_t t157 = gm_f_decl_is_type_oh0cpn9((void *)NULL, cx, NULL, t151, t152, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t158 = 0;
            F.ret = t157;
            goto gm_fret;
        }
        goto me3;
mn122:;
ma11:;
        uint8_t t160 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn159:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 153, 5);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_syntax_module_vho1001(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_syntax_module_vho1001 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    uint64_t gm_l4; /* d */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    uint64_t gm_l10; /* m */
    void * gm_l11; /* decl */
    uint64_t gm_l12; /* dm */
    uint64_t gm_l14; /* m */
    GM_STACK_CHECK(&F, 1423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t3 = gm_l3;
        uint8_t t4 = (t3 > ((uint32_t)16ULL));
        if (!t4) goto ie2;
        {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t8 = gm_l2;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; default: goto ma12; }
ma10:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t16 != ((uint8_t)6ULL)) goto mn15;
        gm_t08olq97rscn3 t17 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t8) + 8) + 8));
        gm_l5 = t17;
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l1;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tf5srh4mp4b3i t22 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t23;
            if (((((uint8_t *)&t22))[8] != 0)) {
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&t22) + 0));
                t23 = t24;
            } else {
                gm_tf5srh4mp4b3i t25;
                memset(&t25, 0, sizeof t25);
                uint8_t t26 = 0;
                F.ret = t25;
                goto gm_fret;
            }
            gm_l4 = t23;
        }
        goto me9;
mn15:;
ma11:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t28 != ((uint8_t)22ULL)) goto mn27;
        void * t29 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l6 = t29;
        gm_t08olq97rscn3 t30 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l7 = t30;
        {
            void * t32 = gm_l0;
            uint64_t t33 = gm_l1;
            void * t34 = gm_l6;
            uint32_t t35 = gm_l3;
            uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 189, 48);
            uint32_t t38 = r36;
            gm_tf5srh4mp4b3i t39 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t32, t33, t34, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t40;
            if (((((uint8_t *)&t39))[8] != 0)) {
                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t39) + 0));
                t40 = t41;
            } else {
                gm_tf5srh4mp4b3i t42;
                memset(&t42, 0, sizeof t42);
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            gm_l8 = t40;
        }
        {
            void * t44 = gm_l0;
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
            uint64_t t46 = gm_l8;
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t48 = gm_f_lookup_59juqoo(t45, t46, t47);
            uint64_t t49;
            if (((((uint8_t *)&t48))[8] != 0)) {
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&t48) + 0));
                t49 = t50;
            } else {
                gm_tf5srh4mp4b3i t51;
                memset(&t51, 0, sizeof t51);
                uint8_t t52 = 0;
                F.ret = t51;
                goto gm_fret;
            }
            gm_l4 = t49;
        }
        {
            void * t54 = gm_l0;
            uint64_t t55 = gm_l4;
            uint64_t t56 = gm_l1;
            uint8_t t57 = gm_f_decl_visible_bd168ti(t54, t55, t56);
            uint8_t t58 = (uint8_t)!t57;
            if (!t58) goto ie53;
            {
                gm_tf5srh4mp4b3i t60;
                memset(&t60, 0, sizeof t60);
                uint8_t t61 = 0;
                F.ret = t60;
                goto gm_fret;
            }
            goto ix53;
ie53:;
ix53:;
        }
        goto me9;
mn27:;
ma12:;
        gm_tf5srh4mp4b3i t63;
        memset(&t63, 0, sizeof t63);
        uint8_t t64 = 0;
        F.ret = t63;
        goto gm_fret;
        goto me9;
mn62:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 186, 5);
me9:;
    }
    {
        void * t65 = gm_l0;
        uint64_t t66 = gm_l4;
        gm_tagrkbqh054h7 t67;
        gm_f_state_a5bnsjb((void *)&t67, (void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
        if (t70 != ((uint8_t)2ULL)) goto mn69;
        F.l9 = (*(gm_t6qooom8qa2do *)(((uint8_t *)&t67) + 8));
        {
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
            if (t73 != ((uint8_t)9ULL)) goto ie72;
            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
            gm_l10 = t74;
            {
                uint64_t t76 = gm_l10;
                gm_tf5srh4mp4b3i t77;
                GM_ST(uint8_t, (uint8_t *)&t77 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t77 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t77 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t77), t76);
                ((uint8_t *)&t77)[8] = 1;
                uint8_t t78 = 0;
                F.ret = t77;
                goto gm_fret;
            }
            goto ix72;
ie72:;
            {
                gm_tf5srh4mp4b3i t80;
                memset(&t80, 0, sizeof t80);
                uint8_t t81 = 0;
                F.ret = t80;
                goto gm_fret;
            }
ix72:;
        }
        goto me68;
mn69:;
        goto me68;
mn82:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 195, 5);
me68:;
    }
    {
        uint64_t t84 = gm_l4;
        uint64_t t85 = (uintptr_t)t84;
        void * t86 = gm_l0;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        uint64_t t88 = GM_LD(uintptr_t, ((((uint8_t *)t87) + 240) + 0) + 8);
        if (t85 >= (uintptr_t)(t88)) gm_trap_index(t85, t88, gm_srcsffver18si54, 202, 14);
        void * t89 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t87) + 240) + 0)) + t85 * 216);
        gm_l11 = t89;
    }
    {
        void * t90 = gm_l11;
        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)t90) + 32));
        gm_l12 = t91;
    }
    {
        void * t93 = gm_l11;
        uint8_t t94 = GM_LD(uint8_t, (((((uint8_t *)t93) + 40) + 64) + 0));
        if (t94 != ((uint8_t)0ULL)) goto ie92;
        gm_tv2uf4leil5hq t95 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t93) + 40) + 64) + 8));
        F.l13 = t95;
        {
            uint8_t t98 = 0;
            void * t99 = GM_LD(void *, (((uint8_t *)&F.l13) + 24));
            uint8_t t100;
            if ((GM_LD(void *, ((uint8_t *)&t99)) != 0)) goto mn102;
            t100 = ((uint8_t)0);
            goto me101;
mn102:;
            t100 = ((uint8_t)1);
            goto me101;
mn103:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 205, 8);
me101:;
            if (!t100) {
                uint64_t t104 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 8) + 8);
                uint8_t t105 = (!(t104 == ((uint64_t)1ULL)));
                t98 = t105;
            } else t98 = 1;
            if (!t98) goto ie97;
            {
                gm_tf5srh4mp4b3i t107;
                memset(&t107, 0, sizeof t107);
                uint8_t t108 = 0;
                F.ret = t107;
                goto gm_fret;
            }
            goto ix97;
ie97:;
ix97:;
        }
        {
            void * t109 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
            uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t109) + 8) + 0));
            switch (t115) { case ((uint8_t)0ULL): goto ma113; case ((uint8_t)1ULL): goto ma113; case ((uint8_t)2ULL): goto ma113; case ((uint8_t)3ULL): goto ma113; case ((uint8_t)4ULL): goto ma113; case ((uint8_t)5ULL): goto ma113; case ((uint8_t)6ULL): goto ma112; case ((uint8_t)7ULL): goto ma113; case ((uint8_t)8ULL): goto ma113; case ((uint8_t)9ULL): goto ma113; case ((uint8_t)10ULL): goto ma113; case ((uint8_t)11ULL): goto ma113; case ((uint8_t)12ULL): goto ma113; case ((uint8_t)13ULL): goto ma113; case ((uint8_t)14ULL): goto ma113; case ((uint8_t)15ULL): goto ma113; case ((uint8_t)16ULL): goto ma113; case ((uint8_t)17ULL): goto ma113; case ((uint8_t)18ULL): goto ma113; case ((uint8_t)19ULL): goto ma113; case ((uint8_t)20ULL): goto ma113; case ((uint8_t)21ULL): goto ma113; case ((uint8_t)22ULL): goto ma112; case ((uint8_t)23ULL): goto ma113; case ((uint8_t)24ULL): goto ma113; case ((uint8_t)25ULL): goto ma113; case ((uint8_t)26ULL): goto ma113; case ((uint8_t)27ULL): goto ma113; case ((uint8_t)28ULL): goto ma113; case ((uint8_t)29ULL): goto ma113; case ((uint8_t)30ULL): goto ma113; case ((uint8_t)31ULL): goto ma113; case ((uint8_t)32ULL): goto ma113; case ((uint8_t)33ULL): goto ma113; case ((uint8_t)34ULL): goto ma113; case ((uint8_t)35ULL): goto ma113; case ((uint8_t)36ULL): goto ma113; case ((uint8_t)37ULL): goto ma113; case ((uint8_t)38ULL): goto ma111; case ((uint8_t)39ULL): goto ma113; case ((uint8_t)40ULL): goto ma113; case ((uint8_t)41ULL): goto ma113; case ((uint8_t)42ULL): goto ma113; case ((uint8_t)43ULL): goto ma113; case ((uint8_t)44ULL): goto ma113; case ((uint8_t)45ULL): goto ma113; case ((uint8_t)46ULL): goto ma113; case ((uint8_t)47ULL): goto ma113; case ((uint8_t)48ULL): goto ma113; case ((uint8_t)49ULL): goto ma113; case ((uint8_t)50ULL): goto ma113; default: goto ma113; }
ma111:;
            uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t109) + 8) + 0));
            if (t117 != ((uint8_t)38ULL)) goto mn116;
            {
                void * t120 = gm_l0;
                uint64_t t121 = gm_l4;
                gm_t6qooom8qa2do t122;
                gm_f_resolve_decl_pus042v((void *)&t122, (void *)NULL, cx, NULL, t120, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&t122) + 0));
                if (t123 != ((uint8_t)9ULL)) goto ie119;
                uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&t122) + 8));
                gm_l14 = t124;
                {
                    uint64_t t126 = gm_l14;
                    gm_tf5srh4mp4b3i t127;
                    GM_ST(uint8_t, (uint8_t *)&t127 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t127 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t127 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t127), t126);
                    ((uint8_t *)&t127)[8] = 1;
                    uint8_t t128 = 0;
                    F.ret = t127;
                    goto gm_fret;
                }
                goto ix119;
ie119:;
                {
                    gm_tf5srh4mp4b3i t130;
                    memset(&t130, 0, sizeof t130);
                    uint8_t t131 = 0;
                    F.ret = t130;
                    goto gm_fret;
                }
ix119:;
            }
            goto me110;
mn116:;
ma112:;
            uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t109) + 8) + 0));
            if (t135 != ((uint8_t)6ULL)) goto pa134;
            goto po133;
pa134:;
            uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t109) + 8) + 0));
            if (t137 != ((uint8_t)22ULL)) goto pa136;
            goto po133;
pa136:;
            goto mn132;
po133:;
            void * t138 = gm_l0;
            uint64_t t139 = gm_l12;
            void * t140 = GM_LD(void *, (((uint8_t *)&F.l13) + 40));
            uint32_t t141 = gm_l3;
            uint32_t r142; bool o143 = __builtin_add_overflow(t141, ((uint32_t)1ULL), &r142);
            if (o143) gm_trap("integer overflow in `+`", gm_srcsffver18si54, 212, 68);
            uint32_t t144 = r142;
            gm_tf5srh4mp4b3i t145 = gm_f_syntax_module_vho1001((void *)NULL, cx, NULL, t138, t139, t140, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t146 = 0;
            F.ret = t145;
            goto gm_fret;
            goto me110;
mn132:;
ma113:;
            gm_tf5srh4mp4b3i t148;
            memset(&t148, 0, sizeof t148);
            uint8_t t149 = 0;
            F.ret = t148;
            goto gm_fret;
            goto me110;
mn147:;
ma114:;
            gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 206, 5);
me110:;
        }
        goto ix92;
ie92:;
        {
            gm_tf5srh4mp4b3i t151;
            memset(&t151, 0, sizeof t151);
            uint8_t t152 = 0;
            F.ret = t151;
            goto gm_fret;
        }
ix92:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_param_3av4r8i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1488) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 55, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1488) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 56, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1488) + 0)) + t15 * 24) + 0));
                gm_b16_8 t19 = gm_l1;
                uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (!t20) goto ie13;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t t23 = (uintptr_t)t22;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 1488) + 0) + 8);
                    if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcsffver18si54, 56, 57);
                    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 1488) + 0)) + t23 * 24) + 16));
                    gm_tkn42u7na3t81 t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t27), t26);
                    ((uint8_t *)&t27)[4] = 1;
                    uint8_t t28 = 0;
                    F.ret = t27;
                    goto gm_fret;
                }
                goto ix13;
ie13:;
ix13:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_tkn42u7na3t81 t29;
        memset(&t29, 0, sizeof t29);
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_value_param_tof1h6t(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_param_tof1h6t F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 1528) + 0) + 8);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcsffver18si54, 128, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t t15 = (uintptr_t)t14;
                void * t16 = gm_l0;
                uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1528) + 0) + 8);
                if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srcsffver18si54, 129, 12);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1528) + 0)) + t15 * 24) + 0));
                gm_b16_8 t19 = gm_l1;
                uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                if (!t20) goto ie13;
                {
                    uint64_t t22 = gm_l2;
                    uint64_t t23 = (uintptr_t)t22;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 1528) + 0) + 8);
                    if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcsffver18si54, 129, 57);
                    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 1528) + 0)) + t23 * 24) + 16));
                    gm_t9805qtonbsag t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t27), t26);
                    ((uint8_t *)&t27)[4] = 1;
                    uint8_t t28 = 0;
                    F.ret = t27;
                    goto gm_fret;
                }
                goto ix13;
ie13:;
ix13:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_t9805qtonbsag t29;
        memset(&t29, 0, sizeof t29);
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_with_operand_ajbkgld(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_with_operand_ajbkgld F;
    void * gm_l0; /* prefix */
    void * gm_l1; /* operand */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* inner */
    uint8_t gm_l4; /* c */
    void * gm_l5; /* inner */
    uint8_t gm_l6; /* c */
    void * gm_l7; /* inner */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* inner */
    GM_STACK_CHECK(&F, 905u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t2gtbh39ptekd t3;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; default: goto ma10; }
ma5:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)28ULL)) goto mn13;
        gm_t3mf58gv9kf3n t15 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        void * t16 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t17 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t18 = gm_l1;
        void * t19 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t20;
        GM_ST(void *, ((uint8_t *)&t20 + 0), t16);
        GM_ST(void *, ((uint8_t *)&t20 + 8), t19);
        gm_t2gtbh39ptekd t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)28ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t21 + 8), t20);
        t3 = t21;
        goto me4;
mn13:;
ma6:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)24ULL)) goto mn22;
        void * t24 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t24;
        void * t25 = gm_l3;
        void * t26 = gm_l1;
        void * t27 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2gtbh39ptekd t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)24ULL));
        GM_ST(void *, ((uint8_t *)&t28 + 8), t27);
        t3 = t28;
        goto me4;
mn22:;
ma7:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t30 != ((uint8_t)25ULL)) goto mn29;
        uint8_t t31 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t31;
        void * t32 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t32;
        uint8_t t33 = gm_l4;
        void * t34 = gm_l5;
        void * t35 = gm_l1;
        void * t36 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), t33);
        GM_ST(void *, ((uint8_t *)&t37 + 8), t36);
        gm_t2gtbh39ptekd t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)25ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t38 + 8), t37);
        t3 = t38;
        goto me4;
mn29:;
ma8:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)26ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t41;
        void * t42 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t42;
        uint8_t t43 = gm_l6;
        void * t44 = gm_l7;
        void * t45 = gm_l1;
        void * t46 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), t43);
        GM_ST(void *, ((uint8_t *)&t47 + 8), t46);
        gm_t2gtbh39ptekd t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)26ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t48 + 8), t47);
        t3 = t48;
        goto me4;
mn39:;
ma9:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t50 != ((uint8_t)27ULL)) goto mn49;
        uint8_t t51 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t51;
        void * t52 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l9 = t52;
        uint8_t t53 = gm_l8;
        void * t54 = gm_l9;
        void * t55 = gm_l1;
        void * t56 = gm_f_with_operand_ajbkgld((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), t53);
        GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
        gm_t2gtbh39ptekd t58;
        memset(&t58, 0, sizeof t58);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)27ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t58 + 8), t57);
        t3 = t58;
        goto me4;
mn49:;
ma10:;
        void * t60 = gm_l1;
        gm_tq7s98ihonkju t61 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t60)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3n1bpabq7f)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srcsffver18si54, 931, 21);
        void * t62 = GM_LD(void *, (((uint8_t *)&t61) + 8));
        uint8_t t63 = 0;
        F.ret = t62;
        goto gm_fret;
        goto me4;
mn59:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcsffver18si54, 925, 26);
me4:;
        F.l10 = t3;
    }
    {
        gm_tc3oht4ksth9d t64;
        memset((uint8_t *)&t64 + 4, 0, 4);
        memset((uint8_t *)&t64 + 84, 0, 4);
        void * t65 = gm_l0;
        uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)t65) + 0));
        GM_ST(uint32_t, (uint8_t *)&t64 + 0, t66);
        memcpy((uint8_t *)&t64 + 8, ((uint8_t *)&F.l10), 64);
        void * t67 = gm_l0;
        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t64 + 72, t68);
        gm_tq7s98ihonkju t69 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &t64, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6r9cr7441u)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcsffver18si54, 933, 12);
        void * t70 = GM_LD(void *, (((uint8_t *)&t69) + 8));
        uint8_t t71 = 0;
        F.ret = t70;
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

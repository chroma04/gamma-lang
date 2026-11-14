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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; gm_toceffb697p0u f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t pe[1]; } gm_tb9j2bleq9j8f;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tcsu09l1pp5hf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t p3[7]; gm_t4c7ipjj9s3fp f3; } gm_td5sh6gra0q3b;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tg0vmr665gcag;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_t08olq97rscn3 f2; } gm_tqtt2php6e8lk;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tsu0mic7gtnv6;
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tnvnsr1bfvtbd f0; } gm_t5jnq73fben73;
typedef struct { uint64_t f0; gm_tqtt2php6e8lk f1; } gm_t7qqq0mh1r8ke;
_Static_assert(sizeof(gm_tb9j2bleq9j8f) == 24 && _Alignof(gm_tb9j2bleq9j8f) == 4 && offsetof(gm_tb9j2bleq9j8f, f0) == 0 && offsetof(gm_tb9j2bleq9j8f, f1) == 1 && offsetof(gm_tb9j2bleq9j8f, f2) == 4 && offsetof(gm_tb9j2bleq9j8f, f3) == 20 && offsetof(gm_tb9j2bleq9j8f, f4) == 21 && offsetof(gm_tb9j2bleq9j8f, f5) == 22, "layout of `(mutable: bool, typed: bool, op: ?Span, keep_mutable: bool, fn_lit: bool, uninit: bool)`");
_Static_assert(sizeof(gm_tcsu09l1pp5hf) == 80 && _Alignof(gm_tcsu09l1pp5hf) == 8 && offsetof(gm_tcsu09l1pp5hf, f0) == 0 && offsetof(gm_tcsu09l1pp5hf, f1) == 16 && offsetof(gm_tcsu09l1pp5hf, f2) == 32 && offsetof(gm_tcsu09l1pp5hf, f3) == 72, "layout of `SourceFile`");
_Static_assert(sizeof(gm_td5sh6gra0q3b) == 96 && _Alignof(gm_td5sh6gra0q3b) == 8 && offsetof(gm_td5sh6gra0q3b, f0) == 0 && offsetof(gm_td5sh6gra0q3b, f1) == 16 && offsetof(gm_td5sh6gra0q3b, f2) == 32 && offsetof(gm_td5sh6gra0q3b, f3) == 40, "layout of `Scope`");
_Static_assert(sizeof(gm_tg0vmr665gcag) == 216 && _Alignof(gm_tg0vmr665gcag) == 8 && offsetof(gm_tg0vmr665gcag, f0) == 0 && offsetof(gm_tg0vmr665gcag, f1) == 40 && offsetof(gm_tg0vmr665gcag, f2) == 96 && offsetof(gm_tg0vmr665gcag, f3) == 136 && offsetof(gm_tg0vmr665gcag, f4) == 176, "layout of `FileFacts`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
typedef struct { gm_t4gut2c9fj1kt f0; uint8_t f1; uint8_t pe[7]; } gm_tkc9ngh8g1s0j;
typedef struct { uint64_t f0; gm_tcsu09l1pp5hf f1; } gm_tntfcv47jbeh7;
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
_Static_assert(sizeof(gm_tqtt2php6e8lk) == 56 && _Alignof(gm_tqtt2php6e8lk) == 8 && offsetof(gm_tqtt2php6e8lk, f0) == 0 && offsetof(gm_tqtt2php6e8lk, f1) == 8 && offsetof(gm_tqtt2php6e8lk, f2) == 24, "layout of `Parsed`");
typedef struct { void * f0; gm_tg0vmr665gcag f1; gm_trqfvmjtt4sij f2; } gm_ts271qav2bkv9;
_Static_assert(sizeof(gm_tsu0mic7gtnv6) == 56 && _Alignof(gm_tsu0mic7gtnv6) == 8 && offsetof(gm_tsu0mic7gtnv6, f0) == 0 && offsetof(gm_tsu0mic7gtnv6, f1) == 16, "layout of `Allow`");
_Static_assert(sizeof(gm_t5jnq73fben73) == 40 && _Alignof(gm_t5jnq73fben73) == 8 && offsetof(gm_t5jnq73fben73, f0) == 0, "layout of `(fix: ?(at: Span, to: string))`");
_Static_assert(sizeof(gm_t7qqq0mh1r8ke) == 64 && _Alignof(gm_t7qqq0mh1r8ke) == 8 && offsetof(gm_t7qqq0mh1r8ke, f0) == 0 && offsetof(gm_t7qqq0mh1r8ke, f1) == 8, "layout of `(i: usize, x: Parsed)`");
_Static_assert(sizeof(gm_tkc9ngh8g1s0j) == 192 && _Alignof(gm_tkc9ngh8g1s0j) == 8 && offsetof(gm_tkc9ngh8g1s0j, f0) == 0 && offsetof(gm_tkc9ngh8g1s0j, f1) == 184, "layout of `?Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tb9j2bleq9j8f v0; gm_tbgd14esf0jvi v1; gm_t5jnq73fben73 v2; } f1; } gm_tl5vm0tcqvdvb;
_Static_assert(sizeof(gm_tntfcv47jbeh7) == 88 && _Alignof(gm_tntfcv47jbeh7) == 8 && offsetof(gm_tntfcv47jbeh7, f0) == 0 && offsetof(gm_tntfcv47jbeh7, f1) == 8, "layout of `(i: usize, x: SourceFile)`");
_Static_assert(sizeof(gm_ts271qav2bkv9) == 264 && _Alignof(gm_ts271qav2bkv9) == 8 && offsetof(gm_ts271qav2bkv9, f0) == 0 && offsetof(gm_ts271qav2bkv9, f1) == 8 && offsetof(gm_ts271qav2bkv9, f2) == 224, "layout of `Walk`");
typedef struct { gm_tl5vm0tcqvdvb f0; uint8_t f1; uint8_t pe[7]; } gm_te3ir342llopq;
_Static_assert(sizeof(gm_tl5vm0tcqvdvb) == 48 && _Alignof(gm_tl5vm0tcqvdvb) == 8 && offsetof(gm_tl5vm0tcqvdvb, f0) == 0 && offsetof(gm_tl5vm0tcqvdvb, f1) == 8, "layout of `Site`");
_Static_assert(sizeof(gm_te3ir342llopq) == 56 && _Alignof(gm_te3ir342llopq) == 8 && offsetof(gm_te3ir342llopq, f0) == 0 && offsetof(gm_te3ir342llopq, f1) == 48, "layout of `?Site`");
struct gm_fr_absolute_path_gj3a0ni {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_canonical_2k2pqlg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_default_config_s3r9hnq {
    gm_frame h;
    gm_tin97tt4ud0s1 l0; /* c */
    gm_tk9n3vlila9qg l3; /* r */
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_default_level_urberdg {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_deny_warnings_vk3pcl7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1ivdmti {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_4u7p3q6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5n7pq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_root_vg2av48 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_finding_diag_2vp4thv {
    gm_frame h;
    gm_t4gut2c9fj1kt l8; /* d */
    gm_t4gut2c9fj1kt l10; /* d */
    gm_tl5vm0tcqvdvb l11; /* site */
    gm_tnvnsr1bfvtbd l17; /* fix */
    gm_tl5vm0tcqvdvb l22; /* site */
    gm_t4gut2c9fj1kt l30; /* d */
    gm_t4gut2c9fj1kt l36; /* d */
    gm_t4gut2c9fj1kt l38; /* d */
    gm_t4gut2c9fj1kt l45; /* d */
    gm_t4gut2c9fj1kt l50; /* d */
    gm_t4gut2c9fj1kt l57; /* d */
    gm_t4gut2c9fj1kt l63; /* d */
    gm_t4gut2c9fj1kt l69; /* d */
    gm_t4gut2c9fj1kt l76; /* d */
    gm_tkc9ngh8g1s0j ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_s7cd6p5 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_in_scope_tm13267 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_level_m5neg79 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_level_name_qqj3mmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_lint_diag_4vra4qu {
    gm_frame h;
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_lint_eo1tub5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rbj3fr6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uaa66su {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_parse_level_4iat2k8 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_rule_scmud9h {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_run_n6la0ov {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* scoped */
    gm_tcsu09l1pp5hf l10; /* f */
    gm_trqfvmjtt4sij l11; /* findings */
    gm_trqfvmjtt4sij l12; /* found */
    gm_ts271qav2bkv9 l13; /* w */
    gm_t4c7ipjj9s3fp l14; /* when_names */
    gm_t4c7ipjj9s3fp l15; /* at */
    gm_tqtt2php6e8lk l19; /* p */
    gm_tcsu09l1pp5hf l23; /* f */
    gm_tj5f8vajf35o7 l29; /* f */
    gm_t4gut2c9fj1kt l30; /* d */
    gm_t4gut2c9fj1kt l34; /* d */
    gm_trqfvmjtt4sij l35; /* all */
    gm_t4gut2c9fj1kt l39; /* d */
    gm_tsu0mic7gtnv6 l44; /* a */
    gm_tc70a9dbbmgkg ret;
};
struct gm_fr_scope_al6a28r {
    gm_frame h;
    gm_td5sh6gra0q3b ret;
};
struct gm_fr_set_5cf9rlg {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_text_of_lbn39it {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_walk_tech5q5 {
    gm_frame h;
    gm_ts271qav2bkv9 ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_whole_lines_s23hlj6 {
    gm_frame h;
    gm_toceffb697p0u ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_dangling_notes_s4j764p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dangling_message_s0d8vok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_default_config_s3r9hnq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0);
__attribute__((aligned(4))) void gm_f_deny_warnings_vk3pcl7(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4u7p3q6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_s4sbegm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_finding_diag_2vp4thv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_format_331tgtc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_40d716h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_7e5ha9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7h4sakv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b46cbct(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cpa0si0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_duvvd80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ec2c39v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_iqr642o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mqu5cbu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ojhiv11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r4p6poi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rbr6trv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rrko99s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tovpseb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_u8cjln1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uv4nplt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vft2h8j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_1ji4too(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gk4t09c(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_in_scope_tm13267(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_level_name_qqj3mmj(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_names_in_1jbgo4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_level_4iat2k8(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_tf0cjh1(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_sokl06f(void * a0, uint32_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_n6la0ov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, void * a3, void * a4);
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_set_5cf9rlg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_walk_file_do4uq8b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_whole_lines_s23hlj6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_ciuj8hs0q3l[8] __attribute__((aligned(8)));
extern uint8_t gm_cqss81ml8qn[920] __attribute__((aligned(8)));
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1fsfd0lcgf8e[17];
extern const uint8_t gm_s1gd0gkbe5tu9[55];
extern const uint8_t gm_s25qdppmkh7s6[7];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s4296alm81si3[14];
extern const uint8_t gm_s51jnn7biksj8[68];
extern const uint8_t gm_s5qukklsuqpi4[17];
extern const uint8_t gm_s6o3rebeo47ka[16];
extern const uint8_t gm_s6vfshjjhk2g7[19];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s920j0georape[5];
extern const uint8_t gm_sa0ep7oknkrm1[18];
extern const uint8_t gm_sa7chm2nt7fsk[27];
extern const uint8_t gm_sbf1f7fu8bvpt[5];
extern const uint8_t gm_sbmlclkb2d9o9[74];
extern const uint8_t gm_sc46nvblu52f3[18];
extern const uint8_t gm_sc5ekkhtqttfb[4];
extern const uint8_t gm_sch3p0ffmutvn[16];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_senuk3dngdds0[17];
extern const uint8_t gm_sfj8otp2pq7qq[55];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgrd6s4kl4dhp[29];
extern const uint8_t gm_sgu334n5g1kog[16];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_sijvlope91krv[31];
extern const uint8_t gm_siooblm685vmo[16];
extern const uint8_t gm_siun1n8hqooca[9];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sl9dbf0dicmme[39];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soofuqhnovv5o[5];
extern const uint8_t gm_southrlg4k72m[6];
extern const uint8_t gm_spn8bpm132g2m[19];
extern const uint8_t gm_spp2t9pj35hcq[7];
extern const uint8_t gm_sq1d6esradpbv[21];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuktvltncsesc[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srvn31r258t3t[16];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_stooh1vej0ram[16];
extern const uint8_t gm_su3sef888krmb[22];
extern const uint8_t gm_sv242olhg8pke[13];
extern const uint8_t gm_sv44r8pinka4e[13];
extern const uint8_t gm_svo4dku3ftf1l[177];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_absolute_path_gj3a0ni F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_f_working_dir_dj7eq93((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonical_2k2pqlg F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        uint8_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
        t5 = ((uint8_t)0);
        goto me6;
mn7:;
        t5 = ((uint8_t)1);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me6:;
        uint8_t t9 = 0;
        F.ret = t5;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_level_urberdg F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t1;
    void * t3 = gm_l0;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 32));
    if (!t4) goto ie2;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t10 = t9;
    t1 = t10;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1ivdmti F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4u7p3q6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4u7p3q6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5n7pq1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_vg2av48 F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_s7cd6p5 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_1ivdmti();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_level_m5neg79 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tf5srh4mp4b3i t3 = gm_f_rule_scmud9h(t2);
        uint64_t t4;
        if (((((uint8_t *)&t3))[8] != 0)) {
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_t9qkh0rsooc8d t6;
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t8 = gm_l2;
        uint64_t t9 = (uintptr_t)t8;
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uintptr_t, ((((uint8_t *)t10) + 0) + 0) + 8);
        if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcuktvltncsesc, 80, 12);
        gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t10) + 0) + 0)) + t9 * 1));
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_diag_4vra4qu F;
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
    gm_tl874t2atnlh3 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_f_lint_eo1tub5((void *)&(*(gm_t4gut2c9fj1kt *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rbj3fr6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_4u7p3q6();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uaa66su F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n5n7pq1();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rule_scmud9h F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tf5srh4mp4b3i t2 = gm_f_lint_rule_o3ccae1(t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_scope_al6a28r F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* std_root */
    uint8_t gm_l2; /* linting_std */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 33, 0, 7);
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        uint8_t t4 = gm_l2;
        gm_t4c7ipjj9s3fp t5;
        gm_f_hash_map_p3t5tj8((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 0, t2);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 16, t3);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 32, t4);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_td5sh6gra0q3b *)gm_out) + 40, t5);
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_64igq5o F;
    uint32_t gm_l0; /* file */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t1 = gm_l0;
    uint64_t t2 = gm_l1;
    uint32_t t3 = (uint32_t)t2;
    uint64_t t4 = gm_l2;
    uint32_t t5 = (uint32_t)t4;
    gm_tl874t2atnlh3 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t1);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_th2lqbo77pvsc t3;
    gm_f_strip_prefix_vl3t0h4((void *)&t3, (void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4;
    if (((((uint8_t *)&t3))[16] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
me5:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_of_lbn39it F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    void * t3 = gm_f_file_o15fqku(t1, t2);
    uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 16) + 8);
    void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 16)));
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t7 = (uint64_t)t6;
    uint64_t t8 = (uintptr_t)t7;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t10 = (uint64_t)t9;
    uint64_t t11 = (uintptr_t)t10;
    if (t8 > t11 || t11 > (uintptr_t)t4) gm_trap_slice(t8, t11, t4, gm_srcuktvltncsesc, 145, 76);
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t5 + t8 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(t11 - t8));
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_walk_tech5q5 F;
    void * gm_l0; /* sm */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tg0vmr665gcag t2;
    gm_f_file_facts_tdo30n8((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_uaa66su((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(void *, (uint8_t *)&(*(gm_ts271qav2bkv9 *)gm_out) + 0, t1);
    GM_ST(gm_tg0vmr665gcag, (uint8_t *)&(*(gm_ts271qav2bkv9 *)gm_out) + 8, t2);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_ts271qav2bkv9 *)gm_out) + 224, t3);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_default_config_s3r9hnq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_default_config_s3r9hnq F;
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_rbj3fr6((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l0 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = ((uint8_t *)gm_cqss81ml8qn);
            if ((uintptr_t)0 > (uintptr_t)23 || (uintptr_t)23 > (uintptr_t)23) gm_trap_slice((uintptr_t)0, (uintptr_t)23, 23, gm_srcuktvltncsesc, 55, 14);
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(((uint8_t *)t3 + (uintptr_t)0 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)((uintptr_t)23 - (uintptr_t)0));
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
                    gm_tk9n3vlila9qg t16 = GM_LD(gm_tk9n3vlila9qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 40));
                    F.l3 = t16;
                    {
                        void * t18 = (void *)(((uint8_t *)&F.l0) + 0);
                        void * t19 = (void *)((uint8_t *)&F.l3);
                        gm_t9qkh0rsooc8d t20 = gm_f_default_level_urberdg(t19);
                        gm_tftb1odc739t5 t21 = gm_f_push_tf0cjh1(t18, t20);
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcuktvltncsesc, 55, 26);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 55, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t24 = gm_l2;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l2 = t27;
                }
            }
lb5:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l0), 40);
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0);
inline __attribute__((aligned(4))) void gm_f_deny_warnings_vk3pcl7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deny_warnings_vk3pcl7 F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uint64_t, gm_ciuj8hs0q3l);
            gm_l1 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = gm_l1;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l3 = t13;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 0) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcuktvltncsesc, 74, 12);
                        uint8_t t20 = ((GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 0) + 0)) + t17 * 1) + 0) == ((uint8_t)1ULL)));
                        if (!t20) goto ie15;
                        {
                            uint64_t t22 = gm_l3;
                            uint64_t t23 = (uintptr_t)t22;
                            void * t24 = gm_l0;
                            uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 0) + 0) + 8);
                            if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srcuktvltncsesc, 74, 41);
                            gm_t9qkh0rsooc8d t26;
                            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 0) + 0)) + t23 * 1), t26);
                        }
                        goto ix15;
ie15:;
ix15:;
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 73, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l2;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l2 = t31;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_finding_diag_2vp4thv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_finding_diag_2vp4thv F;
    void * gm_l0; /* sm */
    void * gm_l1; /* f */
    void * gm_l2; /* sites */
    void * gm_l3; /* when_names */
    gm_b16_8 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint8_t gm_l6; /* param */
    uint8_t gm_l7; /* assigned */
    gm_b16_8 gm_l9; /* msg */
    uint8_t gm_l12; /* typed */
    gm_toceffb697p0u gm_l13; /* op */
    gm_tl874t2atnlh3 gm_l14; /* o */
    gm_tl874t2atnlh3 gm_l15; /* at */
    gm_b16_8 gm_l16; /* to */
    gm_tbgd14esf0jvi gm_l18; /* p */
    gm_b16_8 gm_l19; /* what */
    gm_b16_8 gm_l20; /* name */
    gm_tl874t2atnlh3 gm_l21; /* span */
    uint8_t gm_l23; /* mutable */
    uint8_t gm_l24; /* typed */
    gm_toceffb697p0u gm_l25; /* op */
    uint8_t gm_l26; /* keep_mutable */
    uint8_t gm_l27; /* fn_lit */
    uint8_t gm_l28; /* uninit */
    gm_b16_8 gm_l29; /* marker */
    gm_tl874t2atnlh3 gm_l31; /* o */
    gm_b16_8 gm_l32; /* name */
    gm_tl874t2atnlh3 gm_l33; /* span */
    gm_tl874t2atnlh3 gm_l34; /* item */
    gm_b16_8 gm_l35; /* what */
    gm_tl874t2atnlh3 gm_l37; /* lines */
    gm_b16_8 gm_l39; /* name */
    gm_tl874t2atnlh3 gm_l40; /* span */
    gm_t9qkh0rsooc8d gm_l41; /* vis */
    gm_b16_8 gm_l42; /* what */
    gm_b16_8 gm_l43; /* marker */
    gm_b16_8 gm_l44; /* why */
    gm_tl874t2atnlh3 gm_l46; /* span */
    gm_b16_8 gm_l47; /* name */
    gm_tl874t2atnlh3 gm_l48; /* local */
    gm_b16_8 gm_l49; /* why */
    gm_tl874t2atnlh3 gm_l51; /* span */
    gm_b16_8 gm_l52; /* name */
    gm_tl874t2atnlh3 gm_l53; /* catch_all */
    uint8_t gm_l54; /* is_result */
    gm_b16_8 gm_l55; /* value */
    gm_b16_8 gm_l56; /* absent */
    gm_tl874t2atnlh3 gm_l58; /* span */
    gm_b16_8 gm_l59; /* name */
    gm_tl874t2atnlh3 gm_l60; /* local */
    gm_tl874t2atnlh3 gm_l61; /* origin */
    gm_b16_8 gm_l62; /* src */
    gm_tl874t2atnlh3 gm_l64; /* span */
    gm_b16_8 gm_l65; /* name */
    gm_tl874t2atnlh3 gm_l66; /* list */
    gm_tl874t2atnlh3 gm_l67; /* growth */
    gm_b16_8 gm_l68; /* src */
    gm_tl874t2atnlh3 gm_l70; /* span */
    gm_tl874t2atnlh3 gm_l71; /* operand */
    uint8_t gm_l72; /* same */
    gm_b16_8 gm_l73; /* whole */
    gm_b16_8 gm_l74; /* o */
    gm_b16_8 gm_l75; /* replacement */
    gm_tl874t2atnlh3 gm_l77; /* span */
    uint8_t gm_l78; /* always */
    gm_tl874t2atnlh3 gm_l79; /* span */
    GM_STACK_CHECK(&F, 7692u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma11; default: goto ma15; }
ma4:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t18 != ((uint8_t)0ULL)) goto mn17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l4 = t19;
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 16));
        gm_l5 = t20;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 28));
        gm_l6 = t21;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 29));
        gm_l7 = t22;
        {
            void * t25 = gm_l3;
            gm_b16_8 t26 = gm_l4;
            uint8_t t27 = gm_f_contains_57n7jqd(t25, t26);
            if (!t27) goto ie24;
            {
                gm_tkc9ngh8g1s0j t29;
                memset(&t29, 0, sizeof t29);
                uint8_t t30 = 0;
                F.ret = t29;
                goto gm_fret;
            }
            goto ix24;
ie24:;
ix24:;
        }
        {
            uint8_t t32 = gm_l6;
            if (!t32) goto ie31;
            {
                gm_tl874t2atnlh3 t34 = gm_l5;
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s5qukklsuqpi4));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(16));
                gm_b16_8 t36 = gm_l4;
                gm_tnpifv20irnph t37;
                GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
                gm_tabb710d9mq2e t38;
                gm_f_format_ojhiv11((void *)&t38, (void *)NULL, cx, NULL, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_srcuktvltncsesc, 215, 58);
                gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                gm_f_lint_diag_4vra4qu((void *)&F.l8, (void *)NULL, cx, NULL, t34, t35, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t41 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t42 = gm_l4;
                gm_tnpifv20irnph t43;
                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
                gm_tabb710d9mq2e t44;
                gm_f_format_b46cbct((void *)&t44, (void *)NULL, cx, NULL, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srcuktvltncsesc, 216, 28);
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                gm_f_add_note_46v502s(t41, t46);
            }
            {
                const uint8_t *t47 = ((uint8_t *)&F.l8);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t47, 184);
                goto gm_fdone;
            }
            goto ix31;
ie31:;
ix31:;
        }
        {
            gm_b16_8 t48;
            uint8_t t50 = gm_l7;
            if (!t50) goto ie49;
            gm_b16_8 t52 = gm_l4;
            gm_tnpifv20irnph t53;
            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
            gm_tabb710d9mq2e t54;
            gm_f_format_u8cjln1((void *)&t54, (void *)NULL, cx, NULL, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcuktvltncsesc, 220, 17);
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
            gm_b16_8 t56 = t55;
            t48 = t56;
            goto ix49;
ie49:;
            gm_b16_8 t58 = gm_l4;
            gm_tnpifv20irnph t59;
            GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
            gm_tabb710d9mq2e t60;
            gm_f_format_ec2c39v((void *)&t60, (void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_srcuktvltncsesc, 222, 17);
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
            gm_b16_8 t62 = t61;
            t48 = t62;
ix49:;
            gm_l9 = t48;
        }
        {
            gm_tl874t2atnlh3 t63 = gm_l5;
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_stooh1vej0ram));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(15));
            gm_b16_8 t65 = gm_l9;
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
            gm_f_lint_diag_4vra4qu((void *)&F.l10, (void *)NULL, cx, NULL, t63, t64, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t68 = gm_l2;
            gm_tl874t2atnlh3 t69 = gm_l5;
            gm_te3ir342llopq t70;
            gm_f_get_gk4t09c((void *)&t70, t68, GM_CONV(gm_cseahvg8v7p0vs, t69));
            if (!((((uint8_t *)&t70))[48] != 0)) goto ie67;
            F.l11 = (*(gm_tl5vm0tcqvdvb *)(((uint8_t *)&t70) + 0));
            {
                uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                switch (t77) { case ((uint8_t)0ULL): goto ma73; case ((uint8_t)1ULL): goto ma74; case ((uint8_t)2ULL): goto ma75; default: goto ma76; }
ma73:;
                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                if (t79 != ((uint8_t)0ULL)) goto mn78;
                uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 8) + 1));
                gm_l12 = t80;
                gm_toceffb697p0u t81 = GM_LD(gm_toceffb697p0u, ((((uint8_t *)&F.l11) + 8) + 4));
                gm_l13 = t81;
                uint8_t t83 = gm_l12;
                uint8_t t84 = (uint8_t)!t83;
                if (!t84) goto ie82;
                uint8_t t85 = gm_l7;
                uint8_t t86 = (uint8_t)!t85;
                if (!t86) goto ie82;
                {
                    if (!((((uint8_t *)&gm_l13))[12] != 0)) goto ie88;
                    gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l13));
                    gm_l14 = t89;
                    {
                        void * t91 = (void *)((uint8_t *)&F.l10);
                        gm_b16_8 t92;
                        GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sgrd6s4kl4dhp));
                        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(28));
                        uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                        uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                        uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 8));
                        gm_tl874t2atnlh3 t96;
                        GM_ST(uint32_t, ((uint8_t *)&t96 + 0), t93);
                        GM_ST(uint32_t, ((uint8_t *)&t96 + 4), t94);
                        GM_ST(uint32_t, ((uint8_t *)&t96 + 8), t95);
                        gm_b16_8 t97;
                        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sc5ekkhtqttfb));
                        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(3));
                        gm_b0_1 t98 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t91, t92, t96, t97);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        const uint8_t *t99 = ((uint8_t *)&F.l10);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                        memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t99, 184);
                        goto gm_fdone;
                    }
                    goto ix88;
ie88:;
ix88:;
                }
                goto ix82;
ie82:;
ix82:;
                goto me72;
mn78:;
ma74:;
                uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                if (t101 != ((uint8_t)1ULL)) goto mn100;
                gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l11) + 8) + 0));
                gm_l15 = t102;
                gm_b16_8 t103 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 8) + 16));
                gm_l16 = t103;
                uint8_t t105 = gm_l7;
                uint8_t t106 = (uint8_t)!t105;
                if (!t106) goto ie104;
                {
                    void * t108 = (void *)((uint8_t *)&F.l10);
                    gm_b16_8 t109 = gm_l16;
                    gm_tnpifv20irnph t110;
                    GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t109);
                    gm_tabb710d9mq2e t111;
                    gm_f_format_ooibcq2((void *)&t111, (void *)NULL, cx, NULL, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcuktvltncsesc, 234, 35);
                    gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                    gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                    gm_tl874t2atnlh3 t114 = gm_l15;
                    gm_b16_8 t115 = gm_l16;
                    gm_b0_1 t116 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t108, t113, t114, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    const uint8_t *t117 = ((uint8_t *)&F.l10);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                    ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                    memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t117, 184);
                    goto gm_fdone;
                }
                goto ix104;
ie104:;
ix104:;
                goto me72;
mn100:;
ma75:;
                uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                if (t119 != ((uint8_t)2ULL)) goto mn118;
                gm_tnvnsr1bfvtbd t120 = GM_LD(gm_tnvnsr1bfvtbd, ((((uint8_t *)&F.l11) + 8) + 0));
                F.l17 = t120;
                if (!((((uint8_t *)&F.l17))[32] != 0)) goto ie121;
                gm_tbgd14esf0jvi t122 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)&F.l17));
                gm_l18 = t122;
                {
                    gm_b16_8 t124;
                    uint64_t t126 = GM_LD(uintptr_t, (((uint8_t *)&gm_l18) + 16) + 8);
                    uint8_t t127 = ((t126 == ((uint64_t)0ULL)));
                    if (!t127) goto ie125;
                    gm_b16_8 t129;
                    GM_ST(void *, (uint8_t *)&t129, (void *)(gm_s6vfshjjhk2g7));
                    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(18));
                    gm_b16_8 t130 = t129;
                    t124 = t130;
                    goto ix125;
ie125:;
                    gm_b16_8 t132;
                    GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sa0ep7oknkrm1));
                    GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(17));
                    gm_b16_8 t133 = t132;
                    t124 = t133;
ix125:;
                    gm_l19 = t124;
                }
                {
                    void * t134 = (void *)((uint8_t *)&F.l10);
                    gm_b16_8 t135 = gm_l19;
                    gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l18) + 0));
                    gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 16));
                    gm_b0_1 t138 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t134, t135, t136, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    const uint8_t *t139 = ((uint8_t *)&F.l10);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                    ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                    memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t139, 184);
                    goto gm_fdone;
                }
                goto ix121;
ie121:;
ix121:;
                goto me72;
mn118:;
ma76:;
                gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 226, 17);
me72:;
            }
            goto ix67;
ie67:;
ix67:;
        }
        {
            void * t140 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t141 = gm_l4;
            gm_tnpifv20irnph t142;
            GM_ST(gm_b16_8, ((uint8_t *)&t142 + 0), t141);
            gm_tabb710d9mq2e t143;
            gm_f_format_rbr6trv((void *)&t143, (void *)NULL, cx, NULL, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_srcuktvltncsesc, 244, 24);
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
            gm_f_add_note_46v502s(t140, t145);
        }
        {
            const uint8_t *t146 = ((uint8_t *)&F.l10);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t146, 184);
            goto gm_fdone;
        }
        goto me3;
mn17:;
ma5:;
        uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t148 != ((uint8_t)1ULL)) goto mn147;
        gm_b16_8 t149 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l20 = t149;
        gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 16));
        gm_l21 = t150;
        {
            void * t153 = gm_l3;
            gm_b16_8 t154 = gm_l20;
            uint8_t t155 = gm_f_contains_57n7jqd(t153, t154);
            if (!t155) goto ie152;
            {
                gm_tkc9ngh8g1s0j t157;
                memset(&t157, 0, sizeof t157);
                uint8_t t158 = 0;
                F.ret = t157;
                goto gm_fret;
            }
            goto ix152;
ie152:;
ix152:;
        }
        {
            void * t159 = gm_l2;
            gm_tl874t2atnlh3 t160 = gm_l21;
            gm_te3ir342llopq t161;
            gm_f_get_gk4t09c((void *)&t161, t159, GM_CONV(gm_cseahvg8v7p0vs, t160));
            gm_tl5vm0tcqvdvb t162;
            if (((((uint8_t *)&t161))[48] != 0)) {
                t162 = (*(gm_tl5vm0tcqvdvb *)(((uint8_t *)&t161) + 0));
            } else {
                gm_tkc9ngh8g1s0j t163;
                memset(&t163, 0, sizeof t163);
                uint8_t t164 = 0;
                F.ret = t163;
                goto gm_fret;
            }
            F.l22 = t162;
        }
        {
            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
            if (t166 != ((uint8_t)0ULL)) goto ie165;
            uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 0));
            gm_l23 = t167;
            uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 1));
            gm_l24 = t168;
            gm_toceffb697p0u t169 = GM_LD(gm_toceffb697p0u, ((((uint8_t *)&F.l22) + 8) + 4));
            gm_l25 = t169;
            uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 20));
            gm_l26 = t170;
            uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 21));
            gm_l27 = t171;
            uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 22));
            gm_l28 = t172;
            {
                uint8_t t175 = 0;
                uint8_t t176 = 0;
                uint8_t t177 = gm_l23;
                uint8_t t178 = (uint8_t)!t177;
                if (!t178) {
                    uint8_t t179 = gm_l27;
                    t176 = t179;
                } else t176 = 1;
                if (!t176) {
                    uint8_t t180 = gm_l28;
                    t175 = t180;
                } else t175 = 1;
                if (!t175) goto ie174;
                {
                    gm_tkc9ngh8g1s0j t182;
                    memset(&t182, 0, sizeof t182);
                    uint8_t t183 = 0;
                    F.ret = t182;
                    goto gm_fret;
                }
                goto ix174;
ie174:;
ix174:;
            }
            {
                gm_b16_8 t184;
                uint8_t t186 = gm_l24;
                if (!t186) goto ie185;
                gm_b16_8 t188;
                GM_ST(void *, (uint8_t *)&t188, (void *)(gm_s39mljh669qgl));
                GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(1));
                gm_b16_8 t189 = t188;
                t184 = t189;
                goto ix185;
ie185:;
                gm_b16_8 t191;
                GM_ST(void *, (uint8_t *)&t191, (void *)(gm_ss67f0gmfen1m));
                GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(2));
                gm_b16_8 t192 = t191;
                t184 = t192;
ix185:;
                gm_l29 = t184;
            }
            {
                gm_tl874t2atnlh3 t193 = gm_l21;
                gm_b16_8 t194;
                GM_ST(void *, (uint8_t *)&t194, (void *)(gm_senuk3dngdds0));
                GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(16));
                gm_t6j6smj7qoctu t195;
                gm_b16_8 t196 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t195 + 0, t196);
                gm_b16_8 t197 = gm_l29;
                GM_ST(gm_b16_8, (uint8_t *)&t195 + 16, t197);
                gm_tabb710d9mq2e t198;
                gm_f_format_7e5ha9c((void *)&t198, (void *)NULL, cx, NULL, &t195);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_srcuktvltncsesc, 253, 54);
                gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
                gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                gm_f_lint_diag_4vra4qu((void *)&F.l30, (void *)NULL, cx, NULL, t193, t194, t200);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                if (!((((uint8_t *)&gm_l25))[12] != 0)) goto ie201;
                gm_tl874t2atnlh3 t202 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l25));
                gm_l31 = t202;
                {
                    uint8_t t205 = gm_l26;
                    uint8_t t206 = (uint8_t)!t205;
                    if (!t206) goto ie204;
                    {
                        void * t208 = (void *)((uint8_t *)&F.l30);
                        gm_b16_8 t209;
                        GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sa7chm2nt7fsk));
                        GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(26));
                        gm_tl874t2atnlh3 t210 = gm_l31;
                        gm_b16_8 t211 = gm_l29;
                        gm_b0_1 t212 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t208, t209, t210, t211);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix204;
ie204:;
ix204:;
                }
                goto ix201;
ie201:;
ix201:;
            }
            {
                const uint8_t *t213 = ((uint8_t *)&F.l30);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t213, 184);
                goto gm_fdone;
            }
            goto ix165;
ie165:;
            {
                gm_tkc9ngh8g1s0j t215;
                memset(&t215, 0, sizeof t215);
                uint8_t t216 = 0;
                F.ret = t215;
                goto gm_fret;
            }
ix165:;
        }
        goto me3;
mn147:;
ma6:;
        uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t218 != ((uint8_t)2ULL)) goto mn217;
        gm_b16_8 t219 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l32 = t219;
        gm_tl874t2atnlh3 t220 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 16));
        gm_l33 = t220;
        gm_tl874t2atnlh3 t221 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 28));
        gm_l34 = t221;
        gm_b16_8 t222 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 40));
        gm_l35 = t222;
        {
            void * t225 = gm_l3;
            gm_b16_8 t226 = gm_l32;
            uint8_t t227 = gm_f_contains_57n7jqd(t225, t226);
            if (!t227) goto ie224;
            {
                gm_tkc9ngh8g1s0j t229;
                memset(&t229, 0, sizeof t229);
                uint8_t t230 = 0;
                F.ret = t229;
                goto gm_fret;
            }
            goto ix224;
ie224:;
ix224:;
        }
        {
            uint8_t t232 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l35)), gm_sg3kpa3h16no3, 6) == 0));
            if (!t232) goto ie231;
            {
                gm_tl874t2atnlh3 t234 = gm_l33;
                gm_b16_8 t235;
                GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s4296alm81si3));
                GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(13));
                gm_b16_8 t236 = gm_l32;
                gm_tnpifv20irnph t237;
                GM_ST(gm_b16_8, ((uint8_t *)&t237 + 0), t236);
                gm_tabb710d9mq2e t238;
                gm_f_format_7h4sakv((void *)&t238, (void *)NULL, cx, NULL, t237);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 8, gm_srcuktvltncsesc, 262, 55);
                gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&t238) + 8));
                gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
                gm_f_lint_diag_4vra4qu((void *)&F.l36, (void *)NULL, cx, NULL, t234, t235, t240);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t242 = gm_l0;
                gm_tl874t2atnlh3 t243 = gm_l34;
                gm_toceffb697p0u t244 = gm_f_whole_lines_s23hlj6(t242, GM_CONV(gm_cseahvg8v7p0vs, t243));
                if (!((((uint8_t *)&t244))[12] != 0)) goto ie241;
                gm_tl874t2atnlh3 t245 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t244));
                gm_l37 = t245;
                {
                    void * t247 = (void *)((uint8_t *)&F.l36);
                    gm_b16_8 t248;
                    GM_ST(void *, (uint8_t *)&t248, (void *)(gm_sc46nvblu52f3));
                    GM_ST(uintptr_t, (uint8_t *)&t248 + 8, (uintptr_t)(17));
                    gm_tl874t2atnlh3 t249 = gm_l37;
                    gm_b16_8 t250;
                    GM_ST(void *, (uint8_t *)&t250, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(0));
                    gm_b0_1 t251 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t247, t248, t249, t250);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix241;
ie241:;
ix241:;
            }
            {
                const uint8_t *t252 = ((uint8_t *)&F.l36);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
                ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
                memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t252, 184);
                goto gm_fdone;
            }
            goto ix231;
ie231:;
ix231:;
        }
        {
            gm_tl874t2atnlh3 t253 = gm_l33;
            gm_b16_8 t254;
            GM_ST(void *, (uint8_t *)&t254, (void *)(gm_spn8bpm132g2m));
            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(18));
            gm_t6j6smj7qoctu t255;
            gm_b16_8 t256 = gm_l35;
            GM_ST(gm_b16_8, (uint8_t *)&t255 + 0, t256);
            gm_b16_8 t257 = gm_l32;
            GM_ST(gm_b16_8, (uint8_t *)&t255 + 16, t257);
            gm_tabb710d9mq2e t258;
            gm_f_format_r4p6poi((void *)&t258, (void *)NULL, cx, NULL, &t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 8, gm_srcuktvltncsesc, 266, 56);
            gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&t258) + 8));
            gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
            gm_f_lint_diag_4vra4qu((void *)&F.l38, (void *)NULL, cx, NULL, t253, t254, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t261 = (void *)((uint8_t *)&F.l38);
            gm_b16_8 t262;
            GM_ST(void *, (uint8_t *)&t262, (void *)(gm_sbmlclkb2d9o9));
            GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(73));
            gm_f_add_note_46v502s(t261, t262);
        }
        {
            const uint8_t *t263 = ((uint8_t *)&F.l38);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t263, 184);
            goto gm_fdone;
        }
        goto me3;
mn217:;
ma7:;
        uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t265 != ((uint8_t)6ULL)) goto mn264;
        gm_b16_8 t266 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l39 = t266;
        gm_tl874t2atnlh3 t267 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 16));
        gm_l40 = t267;
        gm_t9qkh0rsooc8d t268 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t2) + 8) + 28));
        gm_l41 = t268;
        gm_b16_8 t269 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 32));
        gm_l42 = t269;
        {
            void * t272 = gm_l3;
            gm_b16_8 t273 = gm_l39;
            uint8_t t274 = gm_f_contains_57n7jqd(t272, t273);
            if (!t274) goto ie271;
            {
                gm_tkc9ngh8g1s0j t276;
                memset(&t276, 0, sizeof t276);
                uint8_t t277 = 0;
                F.ret = t276;
                goto gm_fret;
            }
            goto ix271;
ie271:;
ix271:;
        }
        {
            gm_b16_8 t278;
            GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sjqs7q3ostqbm));
            GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(3));
            gm_l43 = t278;
        }
        {
            gm_b16_8 t279;
            GM_ST(void *, (uint8_t *)&t279, (void *)(gm_s51jnn7biksj8));
            GM_ST(uintptr_t, (uint8_t *)&t279 + 8, (uintptr_t)(67));
            gm_l44 = t279;
        }
        {
            uint8_t t281 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l41) + 0) == ((uint8_t)1ULL)));
            if (!t281) goto ie280;
            {
                gm_b16_8 t283;
                GM_ST(void *, (uint8_t *)&t283, (void *)(gm_shofa8pdml9fb));
                GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(12));
                gm_l43 = t283;
            }
            {
                gm_b16_8 t284;
                GM_ST(void *, (uint8_t *)&t284, (void *)(gm_sl9dbf0dicmme));
                GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(38));
                gm_l44 = t284;
            }
            goto ix280;
ie280:;
ix280:;
        }
        {
            gm_tl874t2atnlh3 t285 = gm_l40;
            gm_b16_8 t286;
            GM_ST(void *, (uint8_t *)&t286, (void *)(gm_sv44r8pinka4e));
            GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(12));
            gm_t0uuhaljsf410 t287;
            gm_b16_8 t288 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t287 + 0, t288);
            gm_b16_8 t289 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t287 + 16, t289);
            gm_b16_8 t290 = gm_l43;
            GM_ST(gm_b16_8, (uint8_t *)&t287 + 32, t290);
            gm_b16_8 t291 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t287 + 48, t291);
            gm_tabb710d9mq2e t292;
            gm_f_format_uv4nplt((void *)&t292, (void *)NULL, cx, NULL, &t287);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 8, gm_srcuktvltncsesc, 278, 50);
            gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)&t292) + 8));
            gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
            gm_f_lint_diag_4vra4qu((void *)&F.l45, (void *)NULL, cx, NULL, t285, t286, t294);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t295 = (void *)((uint8_t *)&F.l45);
            gm_b16_8 t296;
            GM_ST(void *, (uint8_t *)&t296, (void *)(gm_sfj8otp2pq7qq));
            GM_ST(uintptr_t, (uint8_t *)&t296 + 8, (uintptr_t)(54));
            gm_f_add_note_46v502s(t295, t296);
        }
        {
            const uint8_t *t297 = ((uint8_t *)&F.l45);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t297, 184);
            goto gm_fdone;
        }
        goto me3;
mn264:;
ma8:;
        uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t299 != ((uint8_t)7ULL)) goto mn298;
        gm_tl874t2atnlh3 t300 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 0));
        gm_l46 = t300;
        gm_b16_8 t301 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 16));
        gm_l47 = t301;
        gm_tl874t2atnlh3 t302 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 32));
        gm_l48 = t302;
        gm_b16_8 t303 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 48));
        gm_l49 = t303;
        {
            gm_tl874t2atnlh3 t305 = gm_l46;
            gm_b16_8 t306;
            GM_ST(void *, (uint8_t *)&t306, (void *)(gm_s1fsfd0lcgf8e));
            GM_ST(uintptr_t, (uint8_t *)&t306 + 8, (uintptr_t)(16));
            gm_b16_8 t307 = gm_l47;
            gm_b16_8 t308 = gm_l49;
            gm_b16_8 t309 = gm_f_dangling_message_s0d8vok((void *)NULL, cx, NULL, t307, t308);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_lint_diag_4vra4qu((void *)&F.l50, (void *)NULL, cx, NULL, t305, t306, t309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t310 = (void *)((uint8_t *)&F.l50);
            gm_b16_8 t311 = gm_l47;
            gm_tl874t2atnlh3 t312 = gm_l48;
            gm_b0_1 t313 = gm_f_add_dangling_notes_s4j764p((void *)NULL, cx, NULL, t310, t311, t312);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            const uint8_t *t314 = ((uint8_t *)&F.l50);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t314, 184);
            goto gm_fdone;
        }
        goto me3;
mn298:;
ma9:;
        uint8_t t316 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t316 != ((uint8_t)8ULL)) goto mn315;
        gm_tl874t2atnlh3 t317 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 0));
        gm_l51 = t317;
        gm_b16_8 t318 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 16));
        gm_l52 = t318;
        gm_tl874t2atnlh3 t319 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 32));
        gm_l53 = t319;
        uint8_t t320 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 44));
        gm_l54 = t320;
        {
            gm_b16_8 t322;
            uint8_t t324 = gm_l54;
            if (!t324) goto ie323;
            gm_b16_8 t326;
            GM_ST(void *, (uint8_t *)&t326, (void *)(gm_sch3p0ffmutvn));
            GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(15));
            gm_b16_8 t327 = t326;
            t322 = t327;
            goto ix323;
ie323:;
            gm_b16_8 t329;
            GM_ST(void *, (uint8_t *)&t329, (void *)(gm_s6o3rebeo47ka));
            GM_ST(uintptr_t, (uint8_t *)&t329 + 8, (uintptr_t)(15));
            gm_b16_8 t330 = t329;
            t322 = t330;
ix323:;
            gm_l55 = t322;
        }
        {
            gm_b16_8 t331;
            uint8_t t333 = gm_l54;
            if (!t333) goto ie332;
            gm_b16_8 t335;
            GM_ST(void *, (uint8_t *)&t335, (void *)(gm_siun1n8hqooca));
            GM_ST(uintptr_t, (uint8_t *)&t335 + 8, (uintptr_t)(8));
            gm_b16_8 t336 = t335;
            t331 = t336;
            goto ix332;
ie332:;
            gm_b16_8 t338;
            GM_ST(void *, (uint8_t *)&t338, (void *)(gm_s25qdppmkh7s6));
            GM_ST(uintptr_t, (uint8_t *)&t338 + 8, (uintptr_t)(6));
            gm_b16_8 t339 = t338;
            t331 = t339;
ix332:;
            gm_l56 = t331;
        }
        {
            gm_tl874t2atnlh3 t340 = gm_l51;
            gm_b16_8 t341;
            GM_ST(void *, (uint8_t *)&t341, (void *)(gm_sq1d6esradpbv));
            GM_ST(uintptr_t, (uint8_t *)&t341 + 8, (uintptr_t)(20));
            gm_tfeab279c16rr t342;
            gm_b16_8 t343 = gm_l52;
            GM_ST(gm_b16_8, (uint8_t *)&t342 + 0, t343);
            gm_b16_8 t344 = gm_l55;
            GM_ST(gm_b16_8, (uint8_t *)&t342 + 16, t344);
            gm_b16_8 t345 = gm_l56;
            GM_ST(gm_b16_8, (uint8_t *)&t342 + 32, t345);
            gm_tabb710d9mq2e t346;
            gm_f_format_331tgtc((void *)&t346, (void *)NULL, cx, NULL, &t342);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t346))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t346) + 8, gm_srcuktvltncsesc, 290, 58);
            gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)&t346) + 8));
            gm_b16_8 t348 = GM_LD(gm_b16_8, ((uint8_t *)&t347));
            gm_f_lint_diag_4vra4qu((void *)&F.l57, (void *)NULL, cx, NULL, t340, t341, t348);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t349 = (void *)((uint8_t *)&F.l57);
            gm_tl874t2atnlh3 t350 = gm_l53;
            gm_b16_8 t351 = gm_l56;
            gm_tnpifv20irnph t352;
            GM_ST(gm_b16_8, ((uint8_t *)&t352 + 0), t351);
            gm_tabb710d9mq2e t353;
            gm_f_format_cpa0si0((void *)&t353, (void *)NULL, cx, NULL, t352);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t353))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t353) + 8, gm_srcuktvltncsesc, 291, 36);
            gm_b16_8 t354 = GM_LD(gm_b16_8, (((uint8_t *)&t353) + 8));
            gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&t354));
            gm_f_add_label_50fj7vv(t349, GM_CONV(gm_cseahvg8v7p0vs, t350), t355);
        }
        {
            void * t356 = (void *)((uint8_t *)&F.l57);
            gm_b16_8 t357 = gm_l52;
            gm_tnpifv20irnph t358;
            GM_ST(gm_b16_8, ((uint8_t *)&t358 + 0), t357);
            gm_tabb710d9mq2e t359;
            gm_f_format_rrko99s((void *)&t359, (void *)NULL, cx, NULL, t358);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t359))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t359) + 8, gm_srcuktvltncsesc, 292, 24);
            gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)&t359) + 8));
            gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
            gm_f_add_note_46v502s(t356, t361);
        }
        {
            const uint8_t *t362 = ((uint8_t *)&F.l57);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t362, 184);
            goto gm_fdone;
        }
        goto me3;
mn315:;
ma10:;
        uint8_t t364 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t364 != ((uint8_t)9ULL)) goto mn363;
        gm_tl874t2atnlh3 t365 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 0));
        gm_l58 = t365;
        gm_b16_8 t366 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 16));
        gm_l59 = t366;
        gm_tl874t2atnlh3 t367 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 32));
        gm_l60 = t367;
        gm_tl874t2atnlh3 t368 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 44));
        gm_l61 = t368;
        {
            void * t370 = gm_l0;
            gm_tl874t2atnlh3 t371 = gm_l61;
            gm_b16_8 t372 = gm_f_text_of_lbn39it(t370, GM_CONV(gm_cseahvg8v7p0vs, t371));
            gm_l62 = t372;
        }
        {
            gm_tl874t2atnlh3 t373 = gm_l58;
            gm_b16_8 t374;
            GM_ST(void *, (uint8_t *)&t374, (void *)(gm_sv242olhg8pke));
            GM_ST(uintptr_t, (uint8_t *)&t374 + 8, (uintptr_t)(12));
            gm_t6j6smj7qoctu t375;
            gm_b16_8 t376 = gm_l59;
            GM_ST(gm_b16_8, (uint8_t *)&t375 + 0, t376);
            gm_b16_8 t377 = gm_l62;
            GM_ST(gm_b16_8, (uint8_t *)&t375 + 16, t377);
            gm_tabb710d9mq2e t378;
            gm_f_format_vft2h8j((void *)&t378, (void *)NULL, cx, NULL, &t375);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t378))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t378) + 8, gm_srcuktvltncsesc, 297, 50);
            gm_b16_8 t379 = GM_LD(gm_b16_8, (((uint8_t *)&t378) + 8));
            gm_b16_8 t380 = GM_LD(gm_b16_8, ((uint8_t *)&t379));
            gm_f_lint_diag_4vra4qu((void *)&F.l63, (void *)NULL, cx, NULL, t373, t374, t380);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t381 = (void *)((uint8_t *)&F.l63);
            gm_tl874t2atnlh3 t382 = gm_l60;
            gm_b16_8 t383 = gm_l59;
            gm_tnpifv20irnph t384;
            GM_ST(gm_b16_8, ((uint8_t *)&t384 + 0), t383);
            gm_tabb710d9mq2e t385;
            gm_f_format_40d716h((void *)&t385, (void *)NULL, cx, NULL, t384);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t385))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t385) + 8, gm_srcuktvltncsesc, 298, 32);
            gm_b16_8 t386 = GM_LD(gm_b16_8, (((uint8_t *)&t385) + 8));
            gm_b16_8 t387 = GM_LD(gm_b16_8, ((uint8_t *)&t386));
            gm_f_add_label_50fj7vv(t381, GM_CONV(gm_cseahvg8v7p0vs, t382), t387);
        }
        {
            void * t388 = (void *)((uint8_t *)&F.l63);
            gm_t6j6smj7qoctu t389;
            gm_b16_8 t390 = gm_l59;
            GM_ST(gm_b16_8, (uint8_t *)&t389 + 0, t390);
            gm_b16_8 t391 = gm_l62;
            GM_ST(gm_b16_8, (uint8_t *)&t389 + 16, t391);
            gm_tabb710d9mq2e t392;
            gm_f_format_mqu5cbu((void *)&t392, (void *)NULL, cx, NULL, &t389);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t392))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t392) + 8, gm_srcuktvltncsesc, 299, 24);
            gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&t392) + 8));
            gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
            gm_f_add_note_46v502s(t388, t394);
        }
        {
            const uint8_t *t395 = ((uint8_t *)&F.l63);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t395, 184);
            goto gm_fdone;
        }
        goto me3;
mn363:;
ma11:;
        uint8_t t397 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t397 != ((uint8_t)10ULL)) goto mn396;
        gm_tl874t2atnlh3 t398 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 0));
        gm_l64 = t398;
        gm_b16_8 t399 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 16));
        gm_l65 = t399;
        gm_tl874t2atnlh3 t400 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 32));
        gm_l66 = t400;
        gm_tl874t2atnlh3 t401 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 44));
        gm_l67 = t401;
        {
            void * t403 = gm_l0;
            gm_tl874t2atnlh3 t404 = gm_l66;
            gm_b16_8 t405 = gm_f_text_of_lbn39it(t403, GM_CONV(gm_cseahvg8v7p0vs, t404));
            gm_l68 = t405;
        }
        {
            gm_tl874t2atnlh3 t406 = gm_l64;
            gm_b16_8 t407;
            GM_ST(void *, (uint8_t *)&t407, (void *)(gm_su3sef888krmb));
            GM_ST(uintptr_t, (uint8_t *)&t407 + 8, (uintptr_t)(21));
            gm_t6j6smj7qoctu t408;
            gm_b16_8 t409 = gm_l65;
            GM_ST(gm_b16_8, (uint8_t *)&t408 + 0, t409);
            gm_b16_8 t410 = gm_l68;
            GM_ST(gm_b16_8, (uint8_t *)&t408 + 16, t410);
            gm_tabb710d9mq2e t411;
            gm_f_format_iqr642o((void *)&t411, (void *)NULL, cx, NULL, &t408);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t411))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t411) + 8, gm_srcuktvltncsesc, 304, 59);
            gm_b16_8 t412 = GM_LD(gm_b16_8, (((uint8_t *)&t411) + 8));
            gm_b16_8 t413 = GM_LD(gm_b16_8, ((uint8_t *)&t412));
            gm_f_lint_diag_4vra4qu((void *)&F.l69, (void *)NULL, cx, NULL, t406, t407, t413);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t414 = (void *)((uint8_t *)&F.l69);
            gm_tl874t2atnlh3 t415 = gm_l67;
            gm_b16_8 t416 = gm_l65;
            gm_tnpifv20irnph t417;
            GM_ST(gm_b16_8, ((uint8_t *)&t417 + 0), t416);
            gm_tabb710d9mq2e t418;
            gm_f_format_duvvd80((void *)&t418, (void *)NULL, cx, NULL, t417);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 8, gm_srcuktvltncsesc, 305, 33);
            gm_b16_8 t419 = GM_LD(gm_b16_8, (((uint8_t *)&t418) + 8));
            gm_b16_8 t420 = GM_LD(gm_b16_8, ((uint8_t *)&t419));
            gm_f_add_label_50fj7vv(t414, GM_CONV(gm_cseahvg8v7p0vs, t415), t420);
        }
        {
            void * t421 = (void *)((uint8_t *)&F.l69);
            gm_b16_8 t422;
            GM_ST(void *, (uint8_t *)&t422, (void *)(gm_svo4dku3ftf1l));
            GM_ST(uintptr_t, (uint8_t *)&t422 + 8, (uintptr_t)(176));
            gm_f_add_note_46v502s(t421, t422);
        }
        {
            const uint8_t *t423 = ((uint8_t *)&F.l69);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t423, 184);
            goto gm_fdone;
        }
        goto me3;
mn396:;
ma12:;
        uint8_t t425 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t425 != ((uint8_t)3ULL)) goto mn424;
        gm_tl874t2atnlh3 t426 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 0));
        gm_l70 = t426;
        gm_tl874t2atnlh3 t427 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 12));
        gm_l71 = t427;
        uint8_t t428 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 24));
        gm_l72 = t428;
        {
            void * t430 = gm_l0;
            gm_tl874t2atnlh3 t431 = gm_l70;
            gm_b16_8 t432 = gm_f_text_of_lbn39it(t430, GM_CONV(gm_cseahvg8v7p0vs, t431));
            gm_l73 = t432;
        }
        {
            uint8_t t434 = 0;
            gm_b16_8 t435 = gm_l73;
            gm_b16_8 t436;
            GM_ST(void *, (uint8_t *)&t436, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t436 + 8, (uintptr_t)(4));
            uint8_t t437 = gm_f_contains_gkki4kq(t435, t436);
            if (!t437) {
                gm_b16_8 t438 = gm_l73;
                gm_b16_8 t439;
                GM_ST(void *, (uint8_t *)&t439, (void *)(gm_sogehh8t1m7tn));
                GM_ST(uintptr_t, (uint8_t *)&t439 + 8, (uintptr_t)(5));
                uint8_t t440 = gm_f_contains_gkki4kq(t438, t439);
                t434 = t440;
            } else t434 = 1;
            uint8_t t441 = (uint8_t)!t434;
            if (!t441) goto ie433;
            {
                gm_tkc9ngh8g1s0j t443;
                memset(&t443, 0, sizeof t443);
                uint8_t t444 = 0;
                F.ret = t443;
                goto gm_fret;
            }
            goto ix433;
ie433:;
ix433:;
        }
        {
            void * t445 = gm_l0;
            gm_tl874t2atnlh3 t446 = gm_l71;
            gm_b16_8 t447 = gm_f_text_of_lbn39it(t445, GM_CONV(gm_cseahvg8v7p0vs, t446));
            gm_l74 = t447;
        }
        {
            gm_b16_8 t448 = gm_l74;
            gm_l75 = t448;
        }
        {
            uint8_t t450 = gm_l72;
            uint8_t t451 = (uint8_t)!t450;
            if (!t451) goto ie449;
            {
                uint8_t t454 = 0;
                uint8_t t455 = 0;
                gm_b16_8 t456 = gm_l74;
                gm_b16_8 t457;
                GM_ST(void *, (uint8_t *)&t457, (void *)(gm_southrlg4k72m));
                GM_ST(uintptr_t, (uint8_t *)&t457 + 8, (uintptr_t)(5));
                uint8_t t458 = gm_f_contains_gkki4kq(t456, t457);
                if (!t458) {
                    gm_b16_8 t459 = gm_l74;
                    gm_b16_8 t460;
                    GM_ST(void *, (uint8_t *)&t460, (void *)(gm_sbf1f7fu8bvpt));
                    GM_ST(uintptr_t, (uint8_t *)&t460 + 8, (uintptr_t)(4));
                    uint8_t t461 = gm_f_contains_gkki4kq(t459, t460);
                    t455 = t461;
                } else t455 = 1;
                if (!t455) {
                    gm_b16_8 t462 = gm_l74;
                    gm_b16_8 t463;
                    GM_ST(void *, (uint8_t *)&t463, (void *)(gm_spp2t9pj35hcq));
                    GM_ST(uintptr_t, (uint8_t *)&t463 + 8, (uintptr_t)(6));
                    uint8_t t464 = gm_f_contains_gkki4kq(t462, t463);
                    t454 = t464;
                } else t454 = 1;
                if (!t454) goto ie453;
                {
                    gm_b16_8 t466 = gm_l74;
                    gm_tnpifv20irnph t467;
                    GM_ST(gm_b16_8, ((uint8_t *)&t467 + 0), t466);
                    gm_tabb710d9mq2e t468;
                    gm_f_format_a2pi3bt((void *)&t468, (void *)NULL, cx, NULL, t467);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t468))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t468) + 8, gm_srcuktvltncsesc, 316, 35);
                    gm_b16_8 t469 = GM_LD(gm_b16_8, (((uint8_t *)&t468) + 8));
                    gm_b16_8 t470 = GM_LD(gm_b16_8, ((uint8_t *)&t469));
                    gm_l75 = t470;
                }
                goto ix453;
ie453:;
                {
                    gm_b16_8 t472 = gm_l74;
                    gm_tnpifv20irnph t473;
                    GM_ST(gm_b16_8, ((uint8_t *)&t473 + 0), t472);
                    gm_tabb710d9mq2e t474;
                    gm_f_format_udg5t2e((void *)&t474, (void *)NULL, cx, NULL, t473);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t474))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t474) + 8, gm_srcuktvltncsesc, 318, 35);
                    gm_b16_8 t475 = GM_LD(gm_b16_8, (((uint8_t *)&t474) + 8));
                    gm_b16_8 t476 = GM_LD(gm_b16_8, ((uint8_t *)&t475));
                    gm_l75 = t476;
                }
ix453:;
            }
            goto ix449;
ie449:;
ix449:;
        }
        {
            gm_tl874t2atnlh3 t477 = gm_l70;
            gm_b16_8 t478;
            GM_ST(void *, (uint8_t *)&t478, (void *)(gm_srvn31r258t3t));
            GM_ST(uintptr_t, (uint8_t *)&t478 + 8, (uintptr_t)(15));
            gm_b16_8 t479;
            GM_ST(void *, (uint8_t *)&t479, (void *)(gm_s1gd0gkbe5tu9));
            GM_ST(uintptr_t, (uint8_t *)&t479 + 8, (uintptr_t)(54));
            gm_f_lint_diag_4vra4qu((void *)&F.l76, (void *)NULL, cx, NULL, t477, t478, t479);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t480 = (void *)((uint8_t *)&F.l76);
            gm_b16_8 t481 = gm_l75;
            gm_tnpifv20irnph t482;
            GM_ST(gm_b16_8, ((uint8_t *)&t482 + 0), t481);
            gm_tabb710d9mq2e t483;
            gm_f_format_ooibcq2((void *)&t483, (void *)NULL, cx, NULL, t482);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t483))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t483) + 8, gm_srcuktvltncsesc, 322, 23);
            gm_b16_8 t484 = GM_LD(gm_b16_8, (((uint8_t *)&t483) + 8));
            gm_b16_8 t485 = GM_LD(gm_b16_8, ((uint8_t *)&t484));
            gm_tl874t2atnlh3 t486 = gm_l70;
            gm_b16_8 t487 = gm_l75;
            gm_b0_1 t488 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t480, t485, t486, t487);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            const uint8_t *t489 = ((uint8_t *)&F.l76);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
            ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
            memmove((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t489, 184);
            goto gm_fdone;
        }
        goto me3;
mn424:;
ma13:;
        uint8_t t491 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t491 != ((uint8_t)4ULL)) goto mn490;
        gm_tl874t2atnlh3 t492 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t2) + 8) + 0));
        gm_l77 = t492;
        uint8_t t493 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 12));
        gm_l78 = t493;
        gm_tl874t2atnlh3 t494 = gm_l77;
        gm_b16_8 t495;
        GM_ST(void *, (uint8_t *)&t495, (void *)(gm_siooblm685vmo));
        GM_ST(uintptr_t, (uint8_t *)&t495 + 8, (uintptr_t)(15));
        uint8_t t496 = gm_l78;
        gm_t9qkh0rsooc8d t497;
        GM_ST(uint8_t, ((uint8_t *)&t497 + 0), t496);
        gm_tabb710d9mq2e t498;
        gm_f_format_5mja5ge((void *)&t498, (void *)NULL, cx, NULL, t497);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t498))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t498) + 8, gm_srcuktvltncsesc, 325, 84);
        gm_b16_8 t499 = GM_LD(gm_b16_8, (((uint8_t *)&t498) + 8));
        gm_b16_8 t500 = GM_LD(gm_b16_8, ((uint8_t *)&t499));
        gm_t4gut2c9fj1kt t501;
        gm_f_lint_diag_4vra4qu((void *)&t501, (void *)NULL, cx, NULL, t494, t495, t500);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
        GM_ST(gm_t4gut2c9fj1kt, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t501);
        goto gm_fdone;
        goto me3;
mn490:;
ma14:;
        uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t503 != ((uint8_t)5ULL)) goto mn502;
        gm_tl874t2atnlh3 t504 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 8));
        gm_l79 = t504;
        gm_tl874t2atnlh3 t505 = gm_l79;
        gm_b16_8 t506;
        GM_ST(void *, (uint8_t *)&t506, (void *)(gm_sgu334n5g1kog));
        GM_ST(uintptr_t, (uint8_t *)&t506 + 8, (uintptr_t)(15));
        gm_b16_8 t507;
        GM_ST(void *, (uint8_t *)&t507, (void *)(gm_sijvlope91krv));
        GM_ST(uintptr_t, (uint8_t *)&t507 + 8, (uintptr_t)(30));
        gm_t4gut2c9fj1kt t508;
        gm_f_lint_diag_4vra4qu((void *)&t508, (void *)NULL, cx, NULL, t505, t506, t507);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 185, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 186, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 188, 0);
        ((uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out))[184] = 1;
        GM_ST(gm_t4gut2c9fj1kt, (uint8_t *)&(*(gm_tkc9ngh8g1s0j *)gm_out) + 0, t508);
        goto gm_fdone;
        goto me3;
mn502:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 211, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_in_scope_tm13267(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_in_scope_tm13267 F;
    void * gm_l0; /* sc */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* dir */
    gm_b16_8 gm_l4; /* owner */
    gm_b16_8 gm_l5; /* known */
    GM_STACK_CHECK(&F, 616u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        uint8_t t5 = 0;
        gm_b16_8 t6 = gm_l2;
        void * t7 = gm_l0;
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 0));
        uint8_t t9 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) {
            uint8_t t11 = 0;
            gm_b16_8 t12 = gm_l2;
            void * t13 = gm_l0;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 16));
            uint8_t t15 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t12, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t15) {
                void * t16 = gm_l0;
                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t16) + 32));
                uint8_t t18 = (uint8_t)!t17;
                t11 = t18;
            } else t11 = 0;
            t5 = t11;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            uint8_t t20 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        gm_b16_8 t21 = gm_l2;
        gm_th2lqbo77pvsc t22;
        gm_f_parent_lk0iero((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t23;
        if (((((uint8_t *)&t22))[16] != 0)) {
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 0));
            t23 = t24;
        } else {
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(0));
            t23 = t25;
        }
        gm_l3 = t23;
    }
    {
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(0));
        gm_l4 = t26;
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 40);
        gm_b16_8 t30 = gm_l3;
        gm_th2lqbo77pvsc t31;
        gm_f_get_e3joqcc((void *)&t31, t29, t30);
        if (!((((uint8_t *)&t31))[16] != 0)) goto ie27;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_l5 = t32;
        {
            gm_b16_8 t34 = gm_l5;
            gm_l4 = t34;
        }
        goto ix27;
ie27:;
        {
            gm_b16_8 t36 = gm_l3;
            gm_th2lqbo77pvsc t37;
            gm_f_find_root_vg2av48((void *)&t37, (void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t38;
            if (((((uint8_t *)&t37))[16] != 0)) {
                gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 0));
                t38 = t39;
            } else {
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(0));
                t38 = t40;
            }
            gm_l4 = t38;
        }
        {
            void * t41 = gm_l0;
            void * t42 = (void *)(((uint8_t *)t41) + 40);
            gm_b16_8 t43 = gm_l3;
            gm_b16_8 t44 = gm_l4;
            gm_tftb1odc739t5 t45 = gm_f_put_7shfu9u(t42, t43, t44);
            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcuktvltncsesc, 133, 9);
            gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
        }
ix27:;
    }
    {
        uint8_t t47 = 0;
        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t49 = ((t48 == ((uint64_t)0ULL)));
        if (!t49) {
            gm_b16_8 t50 = gm_l4;
            gm_b16_8 t51 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t52 = gm_l0;
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t52) + 0));
            uint8_t t54 = (gm_eq0iqiubfm7e(((uint8_t *)&t51), ((uint8_t *)&t53)));
            t47 = t54;
        } else t47 = 1;
        uint8_t t55 = 0;
        F.ret = t47;
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_level_name_qqj3mmj(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_level_name_qqj3mmj F;
    gm_t9qkh0rsooc8d gm_l0; /* l */
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
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_slb6cj7ndgvdm));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(5));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_soofuqhnovv5o));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t15 != ((uint8_t)2ULL)) goto mn14;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s920j0georape));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
    t1 = t16;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 29, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_level_4iat2k8(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_level_4iat2k8 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_slb6cj7ndgvdm, 5) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t5;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[1] = 1;
    t1 = t5;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_soofuqhnovv5o, 4) == 0)) goto mn6;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t8;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[1] = 1;
    t1 = t8;
    goto me2;
mn6:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s920j0georape, 4) == 0)) goto mn9;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
    t1 = t11;
    goto me2;
mn9:;
    gm_taari1s0m51a7 t13;
    memset(&t13, 0, sizeof t13);
    t1 = t13;
    goto me2;
mn12:;
    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 22, 47);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_n6la0ov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, void * a3, void * a4) {
    struct gm_fr_run_n6la0ov F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* parsed */
    uint8_t gm_l2; /* program */
    void * gm_l3; /* sc */
    void * gm_l4; /* cfg */
    void * gm_l5; /* sm */
    void * gm_l6; /* diags */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    void * gm_l24; /* p */
    uint64_t gm_l25; /* before */
    void * gm_l26; /* facts */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_t2lqu8jsj511d gm_l31; /* out */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* code */
    uint8_t gm_l41; /* allowed */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_b16_8 gm_l47; /* r */
    GM_STACK_CHECK(&F, 2825u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 8));
        gm_l6 = t7;
    }
    {
        void * t9 = gm_l6;
        uint8_t t10 = gm_f_has_errors_if7ai0m(t9);
        if (!t10) goto ie8;
        {
            gm_tc70a9dbbmgkg t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t15 = gm_l5;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + 0) + 0));
            gm_l8 = t16;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l9;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l9;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tcsu09l1pp5hf t28 = GM_LD(gm_tcsu09l1pp5hf, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t24 * 80));
                    F.l10 = t28;
                    {
                        void * t30 = (void *)((uint8_t *)&F.l7);
                        void * t31 = gm_l3;
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                        uint8_t t33 = gm_f_in_scope_tm13267((void *)NULL, cx, NULL, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_tftb1odc739t5 t34 = gm_f_push_vd1290p(t30, t33);
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcuktvltncsesc, 156, 31);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 156, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t37 = gm_l9;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l9 = t40;
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
    {
        void * t41 = gm_l0;
        uint8_t t42 = gm_l2;
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        gm_f_analyze_np1m50h((void *)&F.l11, (void *)NULL, cx, NULL, t41, t42, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_prbqo9j((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t45 = gm_l5;
        gm_f_walk_tech5q5((void *)&F.l13, (void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_hash_map_cuh4fs9((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_hash_map_s7cd6p5((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t47 = gm_l1;
            gm_l16 = t47;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l17;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    gm_t7qqq0mh1r8ke t54;
                    uint64_t t55 = gm_l17;
                    GM_ST(uint64_t, (uint8_t *)&t54 + 0, t55);
                    uint64_t t56 = gm_l17;
                    uint64_t t57 = (uintptr_t)t56;
                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    memcpy((uint8_t *)&t54 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t57 * 56), 56);
                    uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&t54) + 0));
                    gm_l18 = t61;
                    F.l19 = (*(gm_tqtt2php6e8lk *)(((uint8_t *)&t54) + 8));
                    {
                        void * t63 = (void *)((uint8_t *)&F.l15);
                        uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)&F.l19) + 0));
                        uint64_t t65 = gm_l18;
                        gm_tftb1odc739t5 t66 = gm_f_put_sokl06f(t63, t64, t65);
                        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcuktvltncsesc, 162, 37);
                        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                    }
                    goto me59;
mn60:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 162, 5);
me59:;
                }
lc48:;
                {
                    uint64_t t69 = gm_l17;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l17 = t72;
                }
            }
lb48:;
        }
    }
    {
        {
            void * t74 = gm_l5;
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((((uint8_t *)t74) + 0) + 0));
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            gm_l20 = t76;
        }
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t78 = gm_l21;
                uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                uint8_t t80 = (t78 < t79);
                if (!t80) goto lb77;
                {
                    gm_tntfcv47jbeh7 t83;
                    uint64_t t84 = gm_l21;
                    GM_ST(uint64_t, (uint8_t *)&t83 + 0, t84);
                    uint64_t t85 = gm_l21;
                    uint64_t t86 = (uintptr_t)t85;
                    uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    memcpy((uint8_t *)&t83 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t86 * 80), 80);
                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&t83) + 0));
                    gm_l22 = t90;
                    F.l23 = (*(gm_tcsu09l1pp5hf *)(((uint8_t *)&t83) + 8));
                    {
                        uint8_t t93 = 0;
                        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                        gm_b16_8 t95;
                        GM_ST(void *, (uint8_t *)&t95, (void *)(gm_s1f440st4mnkp));
                        GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(1));
                        uint8_t t96 = gm_f_starts_with_6vvt01e(t94, t95);
                        if (!t96) {
                            uint64_t t97 = gm_l22;
                            uint64_t t98 = (uintptr_t)t97;
                            uint64_t t99 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                            if (t98 >= (uintptr_t)(t99)) gm_trap_index(t98, t99, gm_srcuktvltncsesc, 164, 43);
                            uint8_t t100 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t98 * 1));
                            uint8_t t101 = (uint8_t)!t100;
                            t93 = t101;
                        } else t93 = 1;
                        if (!t93) goto ie92;
                        {
                            goto lc77;
                        }
                        goto ix92;
ie92:;
ix92:;
                    }
                    {
                        gm_b16_8 t103 = gm_l1;
                        gm_b16_8 t104 = t103;
                        void * t105 = (void *)((uint8_t *)&F.l15);
                        uint64_t t106 = gm_l22;
                        uint32_t t107 = (uint32_t)t106;
                        gm_tf5srh4mp4b3i t108 = gm_f_get_1ji4too(t105, t107);
                        uint64_t t109;
                        if (((((uint8_t *)&t108))[8] != 0)) {
                            uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&t108) + 0));
                            t109 = t110;
                        } else {
                            goto lc77;
                        }
                        uint64_t t111 = (uintptr_t)t109;
                        uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&t104) + 8);
                        if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_srcuktvltncsesc, 167, 15);
                        void * t113 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t104)) + t111 * 56);
                        gm_l24 = t113;
                    }
                    {
                        uint64_t t114 = GM_LD(uintptr_t, (((((uint8_t *)&F.l13) + 8) + 96) + 0) + 8);
                        gm_l25 = t114;
                    }
                    {
                        void * t115 = (void *)((uint8_t *)&F.l13);
                        void * t116 = gm_l24;
                        void * t117 = (void *)(((uint8_t *)t116) + 24);
                        gm_b0_1 t118 = gm_f_walk_file_do4uq8b((void *)NULL, cx, NULL, t115, t117);
                        if (gm_unw.kind) {
                            goto gm_ll81;
                        }
                    }
                    {
                        void * t119 = gm_l24;
                        gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)t119) + 8));
                        uint64_t t121 = GM_LD(uintptr_t, (((((uint8_t *)&F.l13) + 8) + 96) + 0) + 8);
                        void * t122 = ((uint8_t *)GM_LD(void *, (((((uint8_t *)&F.l13) + 8) + 96) + 0)));
                        uint64_t t123 = gm_l25;
                        uint64_t t124 = (uintptr_t)t123;
                        if (t124 > (uintptr_t)t121 || (uintptr_t)t121 > (uintptr_t)t121) gm_trap_slice(t124, (uintptr_t)t121, t121, gm_srcuktvltncsesc, 170, 28);
                        gm_b16_8 t125;
                        GM_ST(void *, (uint8_t *)&t125, (void *)(((uint8_t *)t122 + t124 * 12)));
                        GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)((uintptr_t)t121 - t124));
                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                        void * t127 = (void *)((uint8_t *)&F.l14);
                        gm_b0_1 t128 = gm_f_names_in_1jbgo4k((void *)NULL, cx, NULL, t120, t126, t127);
                        if (gm_unw.kind) {
                            goto gm_ll81;
                        }
                    }
                    goto me88;
mn89:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 163, 5);
me88:;
                }
lc77:;
                {
                    uint64_t t130 = gm_l21;
                    uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                    uint64_t t133 = r131;
                    gm_l21 = t133;
                }
            }
gm_ll81:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb77; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc77; }
            }
            goto gm_lad;
lb77:;
        }
    }
    {
        void * t134 = (void *)(((uint8_t *)&F.l13) + 8);
        gm_l26 = t134;
    }
    {
        {
            gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            gm_l27 = t136;
        }
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t138 = gm_l28;
                uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                uint8_t t140 = (t138 < t139);
                if (!t140) goto lb137;
                {
                    uint64_t t143 = gm_l28;
                    uint64_t t144 = (uintptr_t)t143;
                    uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    gm_tj5f8vajf35o7 t148 = GM_LD(gm_tj5f8vajf35o7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t144 * 72));
                    F.l29 = t148;
                    {
                        void * t151 = gm_l5;
                        void * t152 = (void *)((uint8_t *)&F.l29);
                        void * t153 = gm_l26;
                        void * t154 = (void *)(((uint8_t *)t153) + 40);
                        void * t155 = (void *)((uint8_t *)&F.l14);
                        gm_tkc9ngh8g1s0j t156;
                        gm_f_finding_diag_2vp4thv((void *)&t156, (void *)NULL, cx, NULL, t151, t152, t154, t155);
                        if (gm_unw.kind) {
                            goto gm_ll141;
                        }
                        if (!((((uint8_t *)&t156))[184] != 0)) goto ie150;
                        F.l30 = (*(gm_t4gut2c9fj1kt *)(((uint8_t *)&t156) + 0));
                        {
                            void * t158 = (void *)((uint8_t *)&F.l12);
                            gm_tftb1odc739t5 t159 = gm_f_push_l0rgbjf(t158, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l30)));
                            if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 1, gm_srcuktvltncsesc, 174, 67);
                            gm_b0_1 t160 = GM_LD(gm_b0_1, (((uint8_t *)&t159) + 1));
                        }
                        goto ix150;
ie150:;
ix150:;
                    }
                    goto me146;
mn147:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 173, 5);
me146:;
                }
lc137:;
                {
                    uint64_t t162 = gm_l28;
                    uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                    uint64_t t165 = r163;
                    gm_l28 = t165;
                }
            }
gm_ll141:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb137; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc137; }
            }
            goto gm_lad;
lb137:;
        }
    }
    {
        void * t166 = gm_l26;
        uint64_t t167 = GM_LD(uintptr_t, ((((uint8_t *)t166) + 176) + 0) + 8);
        gm_t2lqu8jsj511d t168;
        GM_ST(uint64_t, ((uint8_t *)&t168 + 0), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t168 + 8), t167);
        gm_l31 = t168;
    }
    {
        {
            void * t170 = gm_l26;
            gm_b16_8 t171 = GM_LD(gm_b16_8, ((((uint8_t *)t170) + 176) + 0));
            gm_l32 = t171;
        }
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t173 = gm_l33;
                uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                uint8_t t175 = (t173 < t174);
                if (!t175) goto lb172;
                {
                    uint64_t t178 = gm_l33;
                    uint64_t t179 = (uintptr_t)t178;
                    uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    gm_t4gut2c9fj1kt t183 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t179 * 184));
                    F.l34 = t183;
                    {
                        void * t185 = gm_l6;
                        gm_f_push_o4ckdr1(t185, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l34)));
                    }
                    goto me181;
mn182:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 177, 5);
me181:;
                }
lc172:;
                {
                    uint64_t t187 = gm_l33;
                    uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                    uint64_t t190 = r188;
                    gm_l33 = t190;
                }
            }
lb172:;
        }
    }
    {
        gm_f_list_prbqo9j((void *)&F.l35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t191 = (void *)((uint8_t *)&F.l35);
        void * t192 = gm_l26;
        gm_b16_8 t193 = GM_LD(gm_b16_8, ((((uint8_t *)t192) + 0) + 0));
        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&t193));
        gm_tftb1odc739t5 t195 = gm_f_extend_s4sbegm(t191, t194);
        if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_srcuktvltncsesc, 180, 5);
        gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
    }
    {
        void * t197 = (void *)((uint8_t *)&F.l35);
        gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
        gm_tftb1odc739t5 t200 = gm_f_extend_s4sbegm(t197, t199);
        if (((((uint8_t *)&t200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t200) + 1, gm_srcuktvltncsesc, 181, 5);
        gm_b0_1 t201 = GM_LD(gm_b0_1, (((uint8_t *)&t200) + 1));
    }
    {
        {
            gm_l37 = ((uint64_t)0ULL);
        }
        {
            uint64_t t203 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 0) + 8);
            gm_l36 = t203;
        }
        {
            for (;;) {
                uint64_t t205 = gm_l37;
                uint64_t t206 = gm_l36;
                uint8_t t207 = (t205 < t206);
                if (!t207) goto lb204;
                {
                    uint64_t t210 = gm_l37;
                    uint64_t t213 = GM_LD(uint64_t, ((uint8_t *)&t210));
                    gm_l38 = t213;
                    {
                        uint64_t t215 = gm_l38;
                        uint64_t t216 = (uintptr_t)t215;
                        uint64_t t217 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 0) + 8);
                        if (t216 >= (uintptr_t)(t217)) gm_trap_index(t216, t217, gm_srcuktvltncsesc, 183, 14);
                        gm_t4gut2c9fj1kt t218 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l35) + 0)) + t216 * 184));
                        F.l39 = t218;
                    }
                    {
                        gm_th2lqbo77pvsc t219 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l39) + 160));
                        gm_b16_8 t220;
                        if (((((uint8_t *)&t219))[16] != 0)) {
                            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 0));
                            t220 = t221;
                        } else {
                            gm_b16_8 t222;
                            GM_ST(void *, (uint8_t *)&t222, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(0));
                            t220 = t222;
                        }
                        gm_l40 = t220;
                    }
                    {
                        gm_l41 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t224 = gm_l26;
                            gm_b16_8 t225 = GM_LD(gm_b16_8, ((((uint8_t *)t224) + 136) + 0));
                            gm_l42 = t225;
                        }
                        {
                            gm_l43 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t227 = gm_l43;
                                uint64_t t228 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                uint8_t t229 = (t227 < t228);
                                if (!t229) goto lb226;
                                {
                                    uint64_t t232 = gm_l43;
                                    uint64_t t233 = (uintptr_t)t232;
                                    uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                    gm_tsu0mic7gtnv6 t237 = GM_LD(gm_tsu0mic7gtnv6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t233 * 56));
                                    F.l44 = t237;
                                    {
                                        uint32_t t240 = GM_LD(uint32_t, ((((uint8_t *)&F.l44) + 0) + 0));
                                        uint32_t t241 = GM_LD(uint32_t, ((((uint8_t *)&F.l39) + 24) + 0));
                                        uint8_t t242 = ((t240 == t241));
                                        if (!t242) goto ie239;
                                        uint32_t t243 = GM_LD(uint32_t, ((((uint8_t *)&F.l44) + 0) + 4));
                                        uint32_t t244 = GM_LD(uint32_t, ((((uint8_t *)&F.l39) + 24) + 4));
                                        uint8_t t245 = (t243 <= t244);
                                        if (!t245) goto ie239;
                                        uint32_t t246 = GM_LD(uint32_t, ((((uint8_t *)&F.l39) + 24) + 8));
                                        uint32_t t247 = GM_LD(uint32_t, ((((uint8_t *)&F.l44) + 0) + 8));
                                        uint8_t t248 = (t246 <= t247);
                                        if (!t248) goto ie239;
                                        {
                                            {
                                                gm_b16_8 t251 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l44) + 16) + 0));
                                                gm_l45 = t251;
                                            }
                                            {
                                                gm_l46 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t253 = gm_l46;
                                                    uint64_t t254 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                                                    uint8_t t255 = (t253 < t254);
                                                    if (!t255) goto lb252;
                                                    {
                                                        uint64_t t258 = gm_l46;
                                                        uint64_t t259 = (uintptr_t)t258;
                                                        uint64_t t260 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                                                        gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t259 * 16));
                                                        gm_l47 = t263;
                                                        {
                                                            uint8_t t266 = 0;
                                                            gm_b16_8 t267 = gm_l47;
                                                            gm_b16_8 t268 = gm_l40;
                                                            uint8_t t269 = (gm_eq0iqiubfm7e(((uint8_t *)&t267), ((uint8_t *)&t268)));
                                                            if (!t269) {
                                                                uint8_t t270 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l47)), gm_s0nf1rg04f3m5, 3) == 0));
                                                                t266 = t270;
                                                            } else t266 = 1;
                                                            if (!t266) goto ie265;
                                                            {
                                                                gm_l41 = ((uint8_t)1);
                                                            }
                                                            goto ix265;
ie265:;
ix265:;
                                                        }
                                                        goto me261;
mn262:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 188, 17);
me261:;
                                                    }
lc252:;
                                                    {
                                                        uint64_t t273 = gm_l46;
                                                        uint64_t r274; bool o275 = __builtin_add_overflow(t273, ((uint64_t)1ULL), &r274);
                                                        uint64_t t276 = r274;
                                                        gm_l46 = t276;
                                                    }
                                                }
lb252:;
                                            }
                                        }
                                        goto ix239;
ie239:;
ix239:;
                                    }
                                    goto me235;
mn236:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 186, 9);
me235:;
                                }
lc226:;
                                {
                                    uint64_t t278 = gm_l43;
                                    uint64_t r279; bool o280 = __builtin_add_overflow(t278, ((uint64_t)1ULL), &r279);
                                    uint64_t t281 = r279;
                                    gm_l43 = t281;
                                }
                            }
lb226:;
                        }
                    }
                    {
                        uint8_t t283 = gm_l41;
                        if (!t283) goto ie282;
                        {
                            goto lc204;
                        }
                        goto ix282;
ie282:;
ix282:;
                    }
                    {
                        void * t285 = gm_l4;
                        gm_b16_8 t286 = gm_l40;
                        gm_t9qkh0rsooc8d t287 = gm_f_level_m5neg79(t285, t286);
                        uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&t287) + 0));
                        switch (t293) { case ((uint8_t)0ULL): goto ma289; case ((uint8_t)1ULL): goto ma290; case ((uint8_t)2ULL): goto ma291; default: goto ma292; }
ma289:;
                        uint8_t t295 = GM_LD(uint8_t, (((uint8_t *)&t287) + 0));
                        if (t295 != ((uint8_t)0ULL)) goto mn294;
                        goto lc204;
                        goto me288;
mn294:;
ma290:;
                        uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&t287) + 0));
                        if (t297 != ((uint8_t)1ULL)) goto mn296;
                        {
                            uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 0));
                            uint64_t r300; bool o301 = __builtin_add_overflow(t299, ((uint64_t)1ULL), &r300);
                            if (o301) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 196, 24);
                            uint64_t t302 = r300;
                            GM_ST(uint64_t, (((uint8_t *)&gm_l31) + 0), t302);
                        }
                        goto me288;
mn296:;
ma291:;
                        uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&t287) + 0));
                        if (t304 != ((uint8_t)2ULL)) goto mn303;
                        {
                            gm_t9qkh0rsooc8d t306;
                            GM_ST(uint8_t, ((uint8_t *)&t306 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l39) + 0), t306);
                        }
                        {
                            uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                            uint64_t r308; bool o309 = __builtin_add_overflow(t307, ((uint64_t)1ULL), &r308);
                            if (o309) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 199, 17);
                            uint64_t t310 = r308;
                            GM_ST(uint64_t, (((uint8_t *)&gm_l31) + 8), t310);
                        }
                        goto me288;
mn303:;
ma292:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 194, 9);
me288:;
                    }
                    {
                        void * t311 = gm_l6;
                        gm_f_push_o4ckdr1(t311, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l39)));
                    }
                    goto me211;
mn212:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 182, 5);
me211:;
                }
lc204:;
                {
                    uint64_t t313 = gm_l37;
                    uint64_t r314; bool o315 = __builtin_add_overflow(t313, ((uint64_t)1ULL), &r314);
                    uint64_t t316 = r314;
                    gm_l37 = t316;
                }
            }
lb204:;
        }
    }
    {
        gm_t2lqu8jsj511d t317 = gm_l31;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out))[16] = 1;
        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 0, t317);
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
extern __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_set_5cf9rlg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_set_5cf9rlg F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* name */
    gm_t9qkh0rsooc8d gm_l2; /* level */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s0nf1rg04f3m5, 3) == 0));
        if (!t3) goto ie2;
        {
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                uint64_t t6 = GM_LD(uint64_t, gm_ciuj8hs0q3l);
                gm_l3 = t6;
            }
            {
                for (;;) {
                    uint64_t t8 = gm_l4;
                    uint64_t t9 = gm_l3;
                    uint8_t t10 = (t8 < t9);
                    if (!t10) goto lb7;
                    {
                        uint64_t t13 = gm_l4;
                        uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                        gm_l5 = t16;
                        {
                            uint64_t t18 = gm_l5;
                            uint64_t t19 = (uintptr_t)t18;
                            void * t20 = gm_l0;
                            uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 0) + 0) + 8);
                            if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcuktvltncsesc, 63, 34);
                            gm_t9qkh0rsooc8d t22 = gm_l2;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 0) + 0)) + t19 * 1), t22);
                        }
                        goto me14;
mn15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 63, 9);
me14:;
                    }
lc7:;
                    {
                        uint64_t t24 = gm_l4;
                        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                        uint64_t t27 = r25;
                        gm_l4 = t27;
                    }
                }
lb7:;
            }
        }
        {
            gm_th2lqbo77pvsc t28;
            memset(&t28, 0, sizeof t28);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t30 = gm_l1;
        gm_tf5srh4mp4b3i t31 = gm_f_rule_scmud9h(t30);
        uint64_t t32;
        if (((((uint8_t *)&t31))[8] != 0)) {
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&t31) + 0));
            t32 = t33;
        } else {
            gm_b16_8 t34 = gm_l1;
            gm_tnpifv20irnph t35;
            GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
            gm_tabb710d9mq2e t36;
            gm_f_format_tovpseb((void *)&t36, (void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcuktvltncsesc, 66, 33);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t38);
            goto gm_fdone;
        }
        gm_l6 = t32;
    }
    {
        uint64_t t39 = gm_l6;
        uint64_t t40 = (uintptr_t)t39;
        void * t41 = gm_l0;
        uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 0) + 0) + 8);
        if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_srcuktvltncsesc, 67, 5);
        gm_t9qkh0rsooc8d t43 = gm_l2;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t41) + 0) + 0)) + t40 * 1), t43);
    }
    {
        gm_th2lqbo77pvsc t44;
        memset(&t44, 0, sizeof t44);
        uint8_t t45 = 0;
        F.ret = t44;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_whole_lines_s23hlj6(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_whole_lines_s23hlj6 F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* src */
    uint64_t gm_l3; /* lo */
    uint64_t gm_l4; /* hi */
    uint64_t gm_l5; /* start */
    uint64_t gm_l6; /* end */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    gm_b16_8 gm_l9; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 901u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t4 = gm_f_file_o15fqku(t2, t3);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 16));
        gm_l2 = t5;
    }
    {
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        uint64_t t7 = (uint64_t)t6;
        gm_l3 = t7;
    }
    {
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t9 = (uint64_t)t8;
        gm_l4 = t9;
    }
    {
        uint64_t t10 = gm_l3;
        gm_l5 = t10;
    }
    {
        for (;;) {
            uint64_t t12 = gm_l5;
            uint8_t t13 = (t12 > ((uint64_t)0ULL));
            if (!t13) goto lb11;
            uint64_t t14 = gm_l5;
            uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
            if (o16) gm_trap("integer overflow in `-`", gm_srcuktvltncsesc, 337, 29);
            uint64_t t17 = r15;
            uint64_t t18 = (uintptr_t)t17;
            uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srcuktvltncsesc, 337, 25);
            uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t18 * 1));
            uint8_t t21 = (!(t20 == ((uint8_t)10ULL)));
            if (!t21) goto lb11;
            {
                uint64_t t24 = gm_l5;
                uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                if (o26) gm_trap("integer overflow in `-`", gm_srcuktvltncsesc, 337, 54);
                uint64_t t27 = r25;
                gm_l5 = t27;
            }
lc11:;
        }
lb11:;
    }
    {
        uint64_t t28 = gm_l4;
        gm_l6 = t28;
    }
    {
        for (;;) {
            uint64_t t30 = gm_l6;
            uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t32 = (t30 < t31);
            if (!t32) goto lb29;
            uint64_t t33 = gm_l6;
            uint64_t t34 = (uintptr_t)t33;
            uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_srcuktvltncsesc, 339, 29);
            uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t34 * 1));
            uint8_t t37 = (!(t36 == ((uint8_t)10ULL)));
            if (!t37) goto lb29;
            {
                uint64_t t40 = gm_l6;
                uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                if (o42) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 339, 52);
                uint64_t t43 = r41;
                gm_l6 = t43;
            }
lc29:;
        }
lb29:;
    }
    {
        uint64_t t45 = gm_l6;
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t47 = (t45 < t46);
        if (!t47) goto ie44;
        {
            uint64_t t49 = gm_l6;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            if (o51) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 340, 24);
            uint64_t t52 = r50;
            gm_l6 = t52;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t54 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t55 = gm_l5;
        uint64_t t56 = (uintptr_t)t55;
        uint64_t t57 = gm_l3;
        uint64_t t58 = (uintptr_t)t57;
        if (t56 > t58 || t58 > (uintptr_t)t53) gm_trap_slice(t56, t58, t53, gm_srcuktvltncsesc, 341, 15);
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t54 + t56 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58 - t56));
        gm_l7 = t59;
    }
    {
        uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t61 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t62 = gm_l4;
        uint64_t t63 = (uintptr_t)t62;
        uint64_t t64 = gm_l6;
        uint64_t t65 = (uintptr_t)t64;
        if (t63 > t65 || t65 > (uintptr_t)t60) gm_trap_slice(t63, t65, t60, gm_srcuktvltncsesc, 342, 14);
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t61 + t63 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(t65 - t63));
        gm_l8 = t66;
    }
    {
        gm_b16_8 t68 = gm_l7;
        gm_b16_8 t69 = gm_f_trim_fdrfe6i(t68);
        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&t69) + 8);
        uint8_t t71 = (t70 > ((uint64_t)0ULL));
        if (!t71) goto ie67;
        {
            gm_toceffb697p0u t73;
            memset(&t73, 0, sizeof t73);
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto ix67;
ie67:;
ix67:;
    }
    {
        gm_b16_8 t75 = gm_l8;
        gm_b16_8 t76 = gm_f_trim_start_179crak(t75);
        gm_l9 = t76;
    }
    {
        uint8_t t78 = 0;
        uint8_t t79 = 0;
        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        uint8_t t81 = ((t80 == ((uint64_t)0ULL)));
        if (!t81) {
            gm_b16_8 t82 = gm_l9;
            gm_b16_8 t83;
            GM_ST(void *, (uint8_t *)&t83, (void *)(gm_si4j8sh2qika1));
            GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(2));
            uint8_t t84 = gm_f_starts_with_6vvt01e(t82, t83);
            t79 = t84;
        } else t79 = 1;
        if (!t79) {
            gm_b16_8 t85 = gm_l8;
            gm_b16_8 t86 = gm_f_trim_fdrfe6i(t85);
            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&t86) + 8);
            uint8_t t88 = ((t87 == ((uint64_t)0ULL)));
            t78 = t88;
        } else t78 = 1;
        uint8_t t89 = (uint8_t)!t78;
        if (!t89) goto ie77;
        {
            gm_toceffb697p0u t91;
            memset(&t91, 0, sizeof t91);
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix77;
ie77:;
ix77:;
    }
    {
        uint8_t t94 = 0;
        uint64_t t95 = gm_l5;
        uint8_t t96 = ((t95 == ((uint64_t)0ULL)));
        if (!t96) {
            uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t98 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t99 = gm_l5;
            uint64_t t100 = (uintptr_t)t99;
            if ((uintptr_t)0 > t100 || t100 > (uintptr_t)t97) gm_trap_slice((uintptr_t)0, t100, t97, gm_srcuktvltncsesc, 348, 23);
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(((uint8_t *)t98 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(t100 - (uintptr_t)0));
            gm_b16_8 t102;
            GM_ST(void *, (uint8_t *)&t102, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(2));
            uint8_t t103 = gm_f_ends_with_biriu5l(t101, t102);
            t94 = t103;
        } else t94 = 1;
        if (!t94) goto ie93;
        uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t105 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t106 = gm_l6;
        uint64_t t107 = (uintptr_t)t106;
        if (t107 > (uintptr_t)t104 || (uintptr_t)t104 > (uintptr_t)t104) gm_trap_slice(t107, (uintptr_t)t104, t104, gm_srcuktvltncsesc, 348, 59);
        gm_b16_8 t108;
        GM_ST(void *, (uint8_t *)&t108, (void *)(((uint8_t *)t105 + t107 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)((uintptr_t)t104 - t107));
        gm_b16_8 t109;
        GM_ST(void *, (uint8_t *)&t109, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(1));
        uint8_t t110 = gm_f_starts_with_6vvt01e(t108, t109);
        if (!t110) goto ie93;
        {
            uint64_t t112 = gm_l6;
            uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
            if (o114) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 348, 90);
            uint64_t t115 = r113;
            gm_l6 = t115;
        }
        goto ix93;
ie93:;
ix93:;
    }
    {
        uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint64_t t117 = gm_l5;
        uint64_t t118 = gm_l6;
        gm_tl874t2atnlh3 t119 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t116, t117, t118));
        gm_toceffb697p0u t120;
        GM_ST(uint8_t, (uint8_t *)&t120 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t120 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t120), t119);
        ((uint8_t *)&t120)[12] = 1;
        uint8_t t121 = 0;
        F.ret = t120;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

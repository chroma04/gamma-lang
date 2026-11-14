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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; } gm_t724v60om7sh5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_t8er5olt969ip;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; } gm_t9rbkbpjtolar;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t724v60om7sh5) == 5 && _Alignof(gm_t724v60om7sh5) == 1 && offsetof(gm_t724v60om7sh5, f0) == 0 && offsetof(gm_t724v60om7sh5, f1) == 1 && offsetof(gm_t724v60om7sh5, f2) == 2 && offsetof(gm_t724v60om7sh5, f3) == 3 && offsetof(gm_t724v60om7sh5, f4) == 4, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_t8er5olt969ip) == 28 && _Alignof(gm_t8er5olt969ip) == 4 && offsetof(gm_t8er5olt969ip, f0) == 0 && offsetof(gm_t8er5olt969ip, f1) == 4 && offsetof(gm_t8er5olt969ip, f2) == 8 && offsetof(gm_t8er5olt969ip, f3) == 12 && offsetof(gm_t8er5olt969ip, f4) == 16, "layout of `Deferred`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_t9rbkbpjtolar) == 6 && _Alignof(gm_t9rbkbpjtolar) == 1 && offsetof(gm_t9rbkbpjtolar, f0) == 0 && offsetof(gm_t9rbkbpjtolar, f1) == 1 && offsetof(gm_t9rbkbpjtolar, f2) == 2 && offsetof(gm_t9rbkbpjtolar, f3) == 3 && offsetof(gm_t9rbkbpjtolar, f4) == 4 && offsetof(gm_t9rbkbpjtolar, f5) == 5, "layout of `(Constraint, Constraint, Constraint, Constraint, Constraint, Constraint)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tnnjeerolos0a f0; gm_tnnjeerolos0a f1; } gm_tcf2f71ra2nps;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcf2f71ra2nps) == 16 && _Alignof(gm_tcf2f71ra2nps) == 4 && offsetof(gm_tcf2f71ra2nps, f0) == 0 && offsetof(gm_tcf2f71ra2nps, f1) == 8, "layout of `(ArrayKind, ArrayKind)`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
typedef struct { gm_t428ue2u3hf5g f0; uint8_t f1; uint8_t pe[7]; } gm_trgmlgt2gmmjs;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_trgmlgt2gmmjs) == 40 && _Alignof(gm_trgmlgt2gmmjs) == 8 && offsetof(gm_trgmlgt2gmmjs, f0) == 0 && offsetof(gm_trgmlgt2gmmjs, f1) == 32, "layout of `?ConstKey`");
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_all_ord_8bea963 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_all_plain_6k5r6vb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bit_and_3tgam8m {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bits_struct_ty_l1kjtap {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_bodiless_def_u9kqo09 {
    gm_frame h;
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_bound_to_5c11t0h {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_canonical_bytes_trb762l {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l21; /* x */
    gm_tpautr4br7h3l l29; /* def */
    gm_t0mglksrkfa5e l34; /* f */
    uint8_t ret;
};
struct gm_fr_canonical_field_otka8vm {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    uint8_t ret;
};
struct gm_fr_constraint_name_ombut3o {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_declare_constraint_s1juggf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
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
struct gm_fr_empty_slice_d3uqusp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
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
struct gm_fr_end_constraint_defer_52jt3m9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* deferred */
    gm_b0_1 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_eq_iba53hs {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* seen */
    uint8_t ret;
};
struct gm_fr_has_eq_in_5a59avv {
    gm_frame h;
    gm_tpautr4br7h3l l16; /* def */
    gm_t0mglksrkfa5e l19; /* f */
    gm_tckdobunaulee l23; /* v */
    uint8_t ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_ord_1io9knm {
    gm_frame h;
    gm_tckdobunaulee l11; /* v */
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_implies_ibnta05 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_inner_second_class_part_ufhro3f {
    gm_frame h;
    gm_toa2vhe3l4hfr l19; /* a */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_instantiate_35r5pb2 {
    gm_frame h;
    gm_tqqhoivahirr0 l5; /* sig */
    gm_trqfvmjtt4sij l6; /* map */
    gm_trqfvmjtt4sij l11; /* params */
    gm_t5v0c8ia6a5mo l14; /* p */
    gm_t5v0c8ia6a5mo l15; /* q */
    gm_trqfvmjtt4sij l16; /* kept */
    gm_tqqhoivahirr0 l17; /* inst */
    uint32_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_key_dmcraup {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* i */
    gm_trqfvmjtt4sij l4; /* bytes */
    gm_tbci5ospk5nnn l5; /* x */
    gm_t428ue2u3hf5g ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_n60iihm {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    uint8_t ret;
};
struct gm_fr_key_int_pqscuvn {
    gm_frame h;
    gm_tbci5ospk5nnn l4; /* v */
    gm_thrdpodrs8pgt ret;
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
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ib5k4gb {
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
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_match_effects_0l1vbuc {
    gm_frame h;
    gm_toa2vhe3l4hfr l16; /* x */
    gm_trqfvmjtt4sij l27; /* rest */
    gm_b0_1 ret;
};
struct gm_fr_match_fn_params_7to7qbv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_match_params_6vndkud {
    gm_frame h;
    gm_t53at80pfdus3 l5; /* a */
    gm_trqfvmjtt4sij l14; /* bytes */
    gm_toa2vhe3l4hfr l41; /* p */
    gm_tddtjfggdat0d l50; /* f */
    gm_tddtjfggdat0d l51; /* g */
    gm_tddtjfggdat0d l52; /* g */
    gm_tddtjfggdat0d l53; /* f */
    gm_tddtjfggdat0d l54; /* g */
    gm_b0_1 ret;
};
struct gm_fr_not_first_class_dve75oi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_param_constraints_2k5fnnl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_has_2r09oc7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_param_kind_a0aikmb {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_parse_constraint_6f1aotv {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_plain_fields_i69c54j {
    gm_frame h;
    gm_tpautr4br7h3l l1; /* def */
    gm_t0mglksrkfa5e l4; /* f */
    uint8_t ret;
};
struct gm_fr_pop_knqt0be {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_satisfies_p0n2er8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_scope_type_params_2di4dm5 {
    gm_frame h;
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_t66j1im9ip128 ret;
};
struct gm_fr_second_class_part_k6i54mk {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
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
struct gm_fr_soa_const_view_od97rln {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsatisfied_jnmtj8v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unscope_type_params_bofhhep {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_value_at_sthggua {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_tjoi8o798ne9k ret;
};
struct gm_fr_value_key_vki574i {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_tbci5ospk5nnn l8; /* i */
    gm_trqfvmjtt4sij l10; /* bytes */
    gm_trqfvmjtt4sij l13; /* bytes */
    gm_trqfvmjtt4sij l18; /* bytes */
    gm_trgmlgt2gmmjs ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b);
inline bool gm_eqmvsckgkj25(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_all_ord_8bea963(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_all_plain_6k5r6vb(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_bitwise_f0f970e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_canonical_bytes_trb762l(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_canonical_field_otka8vm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_constraint_name_ombut3o(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_954a9tt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_980ugfo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9h7i0ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hfu2i92(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ii7qb9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n0hp6t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_from_u128_gl5fk0t(void *gm_out, void *env, gm_node *cx, gm_node *inst, unsigned __int128 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_6qnso5a(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_has_eq_in_5a59avv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_ord_1io9knm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_implies_ibnta05(gm_t9qkh0rsooc8d a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_5jc3e2i(gm_b16_8 a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_inner_second_class_part_ufhro3f(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_effects_0l1vbuc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_fn_params_7to7qbv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_constraint_6f1aotv(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j_c(void * a0, gm_tpautr4br7h3l a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1es0np6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_aqjeqnr_2(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi(void * a0, const void * a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_shr_4jp99a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_soa_const_view_od97rln(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4);
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep_c(void * a0, gm_t66j1im9ip128 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_value_at_sthggua(void *gm_out, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tjoi8o798ne9k gm_f_value_at_sthggua_c(gm_tkaapcii86udk a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cem22767mo4[4] __attribute__((aligned(4)));
extern const uint8_t gm_s0rru461jb8nu[7];
extern const uint8_t gm_s670np414hdkp[8];
extern const uint8_t gm_s7kt2bei71pgo[4];
extern const uint8_t gm_s7s25m3nlrif2[6];
extern const uint8_t gm_sd2s2op6i577j[36];
extern const uint8_t gm_se19nf1421e37[4];
extern const uint8_t gm_seggohi50ng09[4];
extern const uint8_t gm_sfv26hkjngt1i[6];
extern const uint8_t gm_sicfped8s368m[58];
extern const uint8_t gm_snoj0t5lf2lht[4];
extern const uint8_t gm_sqri8tum3tpfm[3];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srckg5f2c0p31ur[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srio0gkdttkj2[61];
extern const uint8_t gm_srj9vrlard0qi[14];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)1) return gm_eq9m6mvtmgde(a + 4, b + 4);
    return true;
}
inline bool gm_eqmvsckgkj25(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t5, t8);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_name_h7mjik7 F;
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
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 136) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 268, 59);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 136) + 0)) + t3 * 16));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_and_3tgam8m F;
    GM_STACK_CHECK(&F, 1u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bodiless_def_u9kqo09 F;
    gm_t9qkh0rsooc8d gm_l0; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 1, 0, 7);
    memset((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 57, 0, 7);
    gm_t9qkh0rsooc8d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t3 = gm_f_empty_slice_6trg8r7();
    gm_t9805qtonbsag t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 0, t1);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 8, t2);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 24, t3);
    GM_ST(void *, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 40, ((void *)0));
    GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 48, t4);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 56, ((uint8_t)0));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 28)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_has_eq_iba53hs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 36u);
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
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = (void *)((uint8_t *)&F.l2);
        uint8_t t5 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = 0;
        F.ret = t5;
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)2ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 142u);
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
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
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
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ib5k4gb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d3uqusp();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_trqfvmjtt4sij t2 = F.l1;
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_trim_8rdppmk(t3);
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t4 = 0;
        uint8_t t5 = gm_l0;
        if (t5) {
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            t4 = t7;
        } else t4 = 0;
        gm_trqfvmjtt4sij t8 = F.l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t8);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_constraints_2k5fnnl F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 1128) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srckg5f2c0p31ur, 252, 78);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 1128) + 0)) + t3 * 48) + 8) + 0));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_kind_a0aikmb F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
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
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 1128) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srckg5f2c0p31ur, 249, 62);
    gm_tnnjeerolos0a t6 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 1128) + 0)) + t3 * 48) + 0));
    F.ret = t6;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_knqt0be F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* x */
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
            gm_tkn42u7na3t81 t8;
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
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        gm_t88rcucg1b7uk t29 = gm_l2;
        gm_tkn42u7na3t81 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30), t29);
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_soa_const_view_od97rln(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_const_view_od97rln F;
    void * gm_l0; /* tys */
    uint32_t gm_l1; /* pat */
    uint32_t gm_l2; /* actual */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t2 = 0;
        void * t3 = gm_l0;
        void * t4 = gm_l0;
        uint32_t t5 = gm_l1;
        uint32_t t6 = gm_f_template_9o92cvv(t4, t5);
        gm_b16_8 t7 = gm_f_adt_name_h7mjik7(t3, t6);
        gm_b16_8 t8 = t7;
        uint8_t t9 = (((GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) == 13) && memcmp(GM_LD(void *, ((uint8_t *)&t8)), gm_srj9vrlard0qi, 13) == 0));
        if (t9) {
            void * t10 = gm_l0;
            void * t11 = gm_l0;
            uint32_t t12 = gm_l2;
            uint32_t t13 = gm_f_template_9o92cvv(t11, t12);
            gm_b16_8 t14 = gm_f_adt_name_h7mjik7(t10, t13);
            gm_b16_8 t15 = t14;
            uint8_t t16 = (((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&t15)), gm_s670np414hdkp, 7) == 0));
            t2 = t16;
        } else t2 = 0;
        uint8_t t17 = 0;
        F.ret = t2;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t6, t9);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
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
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_all_ord_8bea963(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_ord_8bea963 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* elems */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t88rcucg1b7uk gm_l4; /* e */
    GM_STACK_CHECK(&F, 147u);
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
                    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 4));
                    gm_l4 = t15;
                    {
                        void * t18 = gm_l0;
                        gm_t88rcucg1b7uk t19 = gm_l4;
                        uint8_t t20 = gm_f_has_ord_1io9knm(t18, GM_CONV(gm_csguankko4lmhd, t19));
                        uint8_t t21 = (uint8_t)!t20;
                        if (!t21) goto ie17;
                        {
                            uint8_t t23 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 99, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l3 = t28;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t29 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_all_plain_6k5r6vb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_plain_6k5r6vb F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* elems */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_t88rcucg1b7uk gm_l5; /* e */
    GM_STACK_CHECK(&F, 292u);
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
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 4));
                    gm_tlcm4lvu3g1a8 t15;
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 8), t14);
                    gm_tlcm4lvu3g1a8 t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l4 = t19;
                    gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t16) + 8));
                    gm_l5 = t20;
                    {
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        void * t24 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                        uint64_t t25 = gm_l4;
                        uint64_t t26 = (uintptr_t)t25;
                        if ((uintptr_t)0 > t26 || t26 > (uintptr_t)t23) gm_trap_slice((uintptr_t)0, t26, t23, gm_srckg5f2c0p31ur, 167, 12);
                        gm_b16_8 t27;
                        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t24 + (uintptr_t)0 * 4)));
                        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - (uintptr_t)0));
                        gm_t88rcucg1b7uk t28 = gm_l5;
                        gm_tf5srh4mp4b3i t29 = gm_f_index_of_5jc3e2i(t27, GM_CONV(gm_csguankko4lmhd, t28));
                        gm_tf5srh4mp4b3i t30 = t29;
                        uint8_t t31;
                        if (((((uint8_t *)&t30))[8] != 0)) goto mn33;
                        t31 = ((uint8_t)0);
                        goto me32;
mn33:;
                        t31 = ((uint8_t)1);
                        goto me32;
mn34:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 167, 12);
me32:;
                        if (!t31) goto ie22;
                        {
                            goto lc4;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    {
                        void * t37 = gm_l0;
                        gm_t88rcucg1b7uk t38 = gm_l5;
                        uint8_t t39 = gm_f_is_plain_n60iihm(t37, GM_CONV(gm_csguankko4lmhd, t38));
                        uint8_t t40 = (uint8_t)!t39;
                        if (!t40) goto ie36;
                        {
                            uint8_t t42 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 164, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t44 = gm_l3;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l3 = t47;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t48 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bits_struct_ty_l1kjtap F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l2 = t9;
        {
            uint32_t t12 = gm_l2;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1088) + 0) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srckg5f2c0p31ur, 189, 21);
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 0));
            if (t17 != ((uint8_t)0ULL)) goto ie11;
            gm_tpautr4br7h3l t18 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 24) + 8));
            F.l3 = t18;
            {
                gm_tkn42u7na3t81 t20 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l3) + 40));
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                gm_tkn42u7na3t81 t23;
                memset(&t23, 0, sizeof t23);
                uint8_t t24 = 0;
                F.ret = t23;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            gm_tkn42u7na3t81 t26;
            memset(&t26, 0, sizeof t26);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bound_to_5c11t0h F;
    gm_b16_8 gm_l0; /* binds */
    uint32_t gm_l1; /* p */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l4; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 180u);
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
                    gm_tplpbqf3duvn3 t15 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 8));
                    gm_l4 = t15;
                    {
                        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t19 = gm_l1;
                        uint8_t t20 = ((t18 == t19));
                        if (!t20) goto ie17;
                        {
                            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 4));
                            gm_tkn42u7na3t81 t23;
                            GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23), t22);
                            ((uint8_t *)&t23)[4] = 1;
                            uint8_t t24 = 0;
                            F.ret = t23;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 322, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb4:;
        }
    }
    {
        gm_tkn42u7na3t81 t30;
        memset(&t30, 0, sizeof t30);
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_canonical_bytes_trb762l(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_canonical_bytes_trb762l F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l3; /* out */
    void * gm_l4; /* tys */
    unsigned __int128 gm_l5; /* bits */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint8_t gm_l9; /* b */
    double gm_l10; /* x */
    uint64_t gm_l11; /* raw */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    void * gm_l15; /* a */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* size */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_b16_8 gm_l22; /* elems */
    gm_b16_8 gm_l23; /* offs */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t88rcucg1b7uk gm_l27; /* e */
    uint32_t gm_l28; /* id */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* i */
    GM_STACK_CHECK(&F, 2599u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; default: goto ma9; }
ma5:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        unsigned __int128 t14 = GM_LD(unsigned __int128, (((uint8_t *)a1) + 16));
        gm_l5 = t14;
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l6 = t17;
            }
            {
                for (;;) {
                    uint64_t t19 = gm_l7;
                    uint64_t t20 = gm_l6;
                    uint8_t t21 = (t19 < t20);
                    if (!t21) goto lb18;
                    {
                        uint64_t t24 = gm_l7;
                        uint64_t t25 = t24;
                        uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&t25));
                        gm_l8 = t28;
                        {
                            uint64_t t30 = gm_l8;
                            uint64_t t31 = (uintptr_t)t30;
                            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srckg5f2c0p31ur, 520, 35);
                            unsigned __int128 t33 = gm_l5;
                            uint64_t t34 = gm_l8;
                            uint64_t r35; bool o36 = __builtin_mul_overflow(t34, ((uint64_t)8ULL), &r35);
                            if (o36) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 520, 58);
                            uint64_t t37 = r35;
                            unsigned __int128 t38 = (unsigned __int128)t37;
                            unsigned __int128 n39 = (unsigned __int128)t38;
                            if (n39 >= 128) gm_trap_shift((uint64_t)n39, "u128", gm_srckg5f2c0p31ur, 520, 45);
                            unsigned __int128 t40 = (unsigned __int128)(t33 >> (int)n39);
                            uint8_t t41 = (uint8_t)t40;
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t31 * 1), t41);
                        }
                        goto me26;
mn27:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 520, 13);
me26:;
                    }
lc18:;
                    {
                        uint64_t t43 = gm_l7;
                        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                        uint64_t t46 = r44;
                        gm_l7 = t46;
                    }
                }
lb18:;
            }
        }
        {
            void * t47 = gm_l4;
            gm_t88rcucg1b7uk t48 = gm_l2;
            uint8_t t49 = gm_f_is_integer_8d4f9ek(t47, GM_CONV(gm_csguankko4lmhd, t48));
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto me4;
mn12:;
ma6:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t52 != ((uint8_t)2ULL)) goto mn51;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)a1) + 16));
        gm_l9 = t53;
        {
            uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t57 = (t56 > ((uint64_t)0ULL));
            if (!t57) goto ie55;
            {
                uint64_t t59 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srckg5f2c0p31ur, 524, 30);
                uint8_t *t61 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t59 * 1);
                uint8_t t62;
                uint8_t t64 = gm_l9;
                if (!t64) goto ie63;
                uint8_t t66 = ((uint8_t)1ULL);
                t62 = t66;
                goto ix63;
ie63:;
                uint8_t t68 = ((uint8_t)0ULL);
                t62 = t68;
ix63:;
                GM_ST(uint8_t, t61, t62);
            }
            goto ix55;
ie55:;
ix55:;
        }
        {
            uint8_t t69 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me4;
mn51:;
ma7:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t71 != ((uint8_t)1ULL)) goto mn70;
        double t72 = GM_LD(double, (((uint8_t *)a1) + 16));
        gm_l10 = t72;
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            void * t74 = gm_l4;
            gm_t88rcucg1b7uk t75 = gm_l2;
            gm_t53at80pfdus3 t76;
            gm_f_kind_jcnt4hm((void *)&t76, t74, GM_CONV(gm_csguankko4lmhd, t75));
            gm_t53at80pfdus3 t77 = t76;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
            switch (t83) { case ((uint8_t)0ULL): goto ma81; case ((uint8_t)1ULL): goto ma81; case ((uint8_t)2ULL): goto ma81; case ((uint8_t)3ULL): goto ma79; case ((uint8_t)4ULL): goto ma81; case ((uint8_t)5ULL): goto ma81; case ((uint8_t)6ULL): goto ma81; case ((uint8_t)7ULL): goto ma81; case ((uint8_t)8ULL): goto ma81; case ((uint8_t)9ULL): goto ma81; case ((uint8_t)10ULL): goto ma81; case ((uint8_t)11ULL): goto ma81; case ((uint8_t)12ULL): goto ma81; case ((uint8_t)13ULL): goto ma81; case ((uint8_t)14ULL): goto ma81; case ((uint8_t)15ULL): goto ma81; case ((uint8_t)16ULL): goto ma81; case ((uint8_t)17ULL): goto ma81; case ((uint8_t)18ULL): goto ma81; case ((uint8_t)19ULL): goto ma81; case ((uint8_t)20ULL): goto ma81; case ((uint8_t)21ULL): goto ma81; case ((uint8_t)22ULL): goto ma81; case ((uint8_t)23ULL): goto ma81; case ((uint8_t)24ULL): goto ma81; case ((uint8_t)25ULL): goto ma81; case ((uint8_t)26ULL): goto ma81; default: goto ma81; }
ma79:;
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
            if (t85 != ((uint8_t)3ULL)) goto mn84;
            uint16_t t86 = GM_LD(uint16_t, (((uint8_t *)&t77) + 8));
            if (!(t86 == ((uint16_t)64ULL))) goto mn84;
            {
                double t88 = gm_l10;
                double t89 = t88;
                uint64_t t90;
                memset(&t90, 0, sizeof t90); memcpy(&t90, ((uint8_t *)&t89), sizeof t90 < 8 ? sizeof t90 : 8);
                gm_l11 = t90;
            }
            {
                uint64_t t92 = gm_l11;
                uint64_t t93 = (uint64_t)(t92 & ((uint64_t)4503599627370495ULL));
                uint8_t t94 = (!(t93 == ((uint64_t)0ULL)));
                if (!t94) goto ie91;
                uint64_t t95 = gm_l11;
                unsigned __int128 n96 = (unsigned __int128)((uint64_t)52ULL);
                if (n96 >= 64) gm_trap_shift((uint64_t)n96, "u64", gm_srckg5f2c0p31ur, 532, 60);
                uint64_t t97 = (uint64_t)(t95 >> (int)n96);
                uint64_t t98 = (uint64_t)(t97 & ((uint64_t)2047ULL));
                uint8_t t99 = ((t98 == ((uint64_t)2047ULL)));
                if (!t99) goto ie91;
                {
                    gm_l11 = ((uint64_t)9221120237041090560ULL);
                }
                goto ix91;
ie91:;
ix91:;
            }
            {
                uint64_t t102 = gm_l11;
                uint64_t t103 = (uint64_t)(t102 & ((uint64_t)9223372036854775807ULL));
                uint8_t t104 = ((t103 == ((uint64_t)0ULL)));
                if (!t104) goto ie101;
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                goto ix101;
ie101:;
ix101:;
            }
            goto me78;
mn84:;
ma80:;
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
            if (t107 != ((uint8_t)3ULL)) goto mn106;
            uint16_t t108 = GM_LD(uint16_t, (((uint8_t *)&t77) + 8));
            if (!(t108 == ((uint16_t)32ULL))) goto mn106;
            {
                double t110 = gm_l10;
                float t111 = (float)((double)t110);
                float t112 = t111;
                uint32_t t113;
                memset(&t113, 0, sizeof t113); memcpy(&t113, ((uint8_t *)&t112), sizeof t113 < 4 ? sizeof t113 : 4);
                uint64_t t114 = (uint64_t)t113;
                gm_l11 = t114;
            }
            {
                uint64_t t116 = gm_l11;
                uint64_t t117 = (uint64_t)(t116 & ((uint64_t)8388607ULL));
                uint8_t t118 = (!(t117 == ((uint64_t)0ULL)));
                if (!t118) goto ie115;
                uint64_t t119 = gm_l11;
                unsigned __int128 n120 = (unsigned __int128)((uint64_t)23ULL);
                if (n120 >= 64) gm_trap_shift((uint64_t)n120, "u64", gm_srckg5f2c0p31ur, 537, 53);
                uint64_t t121 = (uint64_t)(t119 >> (int)n120);
                uint64_t t122 = (uint64_t)(t121 & ((uint64_t)255ULL));
                uint8_t t123 = ((t122 == ((uint64_t)255ULL)));
                if (!t123) goto ie115;
                {
                    gm_l11 = ((uint64_t)2143289344ULL);
                }
                goto ix115;
ie115:;
ix115:;
            }
            {
                uint64_t t126 = gm_l11;
                uint64_t t127 = (uint64_t)(t126 & ((uint64_t)2147483647ULL));
                uint8_t t128 = ((t127 == ((uint64_t)0ULL)));
                if (!t128) goto ie125;
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                goto ix125;
ie125:;
ix125:;
            }
            goto me78;
mn106:;
ma81:;
            uint8_t t131 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
            goto me78;
mn130:;
ma82:;
            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 529, 13);
me78:;
        }
        {
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l12 = t133;
            }
            {
                for (;;) {
                    uint64_t t135 = gm_l13;
                    uint64_t t136 = gm_l12;
                    uint8_t t137 = (t135 < t136);
                    if (!t137) goto lb134;
                    {
                        uint64_t t140 = gm_l13;
                        uint64_t t141 = t140;
                        uint64_t t144 = GM_LD(uint64_t, ((uint8_t *)&t141));
                        gm_l14 = t144;
                        {
                            uint64_t t146 = gm_l14;
                            uint64_t t147 = (uintptr_t)t146;
                            uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t147 >= (uintptr_t)(t148)) gm_trap_index(t147, t148, gm_srckg5f2c0p31ur, 542, 35);
                            uint64_t t149 = gm_l11;
                            uint64_t t150 = gm_l14;
                            uint64_t r151; bool o152 = __builtin_mul_overflow(t150, ((uint64_t)8ULL), &r151);
                            if (o152) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 542, 56);
                            uint64_t t153 = r151;
                            uint64_t t154 = (uint64_t)t153;
                            unsigned __int128 n155 = (unsigned __int128)t154;
                            if (n155 >= 64) gm_trap_shift((uint64_t)n155, "u64", gm_srckg5f2c0p31ur, 542, 45);
                            uint64_t t156 = (uint64_t)(t149 >> (int)n155);
                            uint8_t t157 = (uint8_t)t156;
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t147 * 1), t157);
                        }
                        goto me142;
mn143:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 542, 13);
me142:;
                    }
lc134:;
                    {
                        uint64_t t159 = gm_l13;
                        uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                        uint64_t t162 = r160;
                        gm_l13 = t162;
                    }
                }
lb134:;
            }
        }
        {
            uint8_t t163 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me4;
mn70:;
ma8:;
        uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t165 != ((uint8_t)3ULL)) goto mn164;
        void * t166 = GM_LD(void *, (((uint8_t *)a1) + 16));
        gm_l15 = t166;
        void * t167 = gm_l4;
        gm_t88rcucg1b7uk t168 = gm_l2;
        gm_t53at80pfdus3 t169;
        gm_f_kind_jcnt4hm((void *)&t169, t167, GM_CONV(gm_csguankko4lmhd, t168));
        gm_t53at80pfdus3 t170 = t169;
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&t170) + 0));
        switch (t177) { case ((uint8_t)0ULL): goto ma175; case ((uint8_t)1ULL): goto ma175; case ((uint8_t)2ULL): goto ma175; case ((uint8_t)3ULL): goto ma175; case ((uint8_t)4ULL): goto ma175; case ((uint8_t)5ULL): goto ma175; case ((uint8_t)6ULL): goto ma175; case ((uint8_t)7ULL): goto ma175; case ((uint8_t)8ULL): goto ma175; case ((uint8_t)9ULL): goto ma175; case ((uint8_t)10ULL): goto ma175; case ((uint8_t)11ULL): goto ma172; case ((uint8_t)12ULL): goto ma173; case ((uint8_t)13ULL): goto ma175; case ((uint8_t)14ULL): goto ma175; case ((uint8_t)15ULL): goto ma175; case ((uint8_t)16ULL): goto ma175; case ((uint8_t)17ULL): goto ma174; case ((uint8_t)18ULL): goto ma175; case ((uint8_t)19ULL): goto ma175; case ((uint8_t)20ULL): goto ma175; case ((uint8_t)21ULL): goto ma175; case ((uint8_t)22ULL): goto ma175; case ((uint8_t)23ULL): goto ma175; case ((uint8_t)24ULL): goto ma175; case ((uint8_t)25ULL): goto ma175; case ((uint8_t)26ULL): goto ma175; default: goto ma175; }
ma172:;
        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&t170) + 0));
        if (t179 != ((uint8_t)11ULL)) goto mn178;
        gm_t88rcucg1b7uk t180 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t170) + 8) + 8));
        gm_l16 = t180;
        {
            void * t182 = gm_l0;
            gm_t88rcucg1b7uk t183 = gm_l16;
            gm_tbccvikg6b96e t184;
            gm_f_layout_a5ovnrm((void *)&t184, (void *)NULL, cx, NULL, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbccvikg6b96e t185 = t184;
            uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)&t185) + 0));
            uint64_t t187 = (uint64_t)t186;
            gm_l17 = t187;
        }
        {
            {
                void * t189 = gm_l15;
                gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)t189) + 0));
                gm_b16_8 t191 = t190;
                gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                gm_l18 = t192;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t194 = gm_l19;
                    uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t196 = (t194 < t195);
                    if (!t196) goto lb193;
                    {
                        gm_thf8as07kj3uj t199;
                        memset((uint8_t *)&t199 + 8, 0, 8);
                        uint64_t t200 = gm_l19;
                        GM_ST(uint64_t, (uint8_t *)&t199 + 0, t200);
                        uint64_t t201 = gm_l19;
                        uint64_t t202 = (uintptr_t)t201;
                        uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        memcpy((uint8_t *)&t199 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t202 * 48), 48);
                        gm_thf8as07kj3uj t204 = t199;
                        uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&t204) + 0));
                        gm_l20 = t207;
                        gm_t6evimt84lndq t208 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t204) + 16));
                        F.l21 = t208;
                        {
                            uint8_t t211 = 0;
                            uint64_t t212 = gm_l20;
                            uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
                            if (o214) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 549, 25);
                            uint64_t t215 = r213;
                            uint64_t t216 = gm_l17;
                            uint64_t r217; bool o218 = __builtin_mul_overflow(t215, t216, &r217);
                            if (o218) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 549, 24);
                            uint64_t t219 = r217;
                            uint64_t t220 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            uint8_t t221 = (t219 > t220);
                            if (!t221) {
                                void * t222 = gm_l0;
                                gm_t88rcucg1b7uk t223 = gm_l16;
                                uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t225 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t226 = gm_l20;
                                uint64_t t227 = gm_l17;
                                uint64_t r228; bool o229 = __builtin_mul_overflow(t226, t227, &r228);
                                if (o229) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 549, 87);
                                uint64_t t230 = r228;
                                uint64_t t231 = (uintptr_t)t230;
                                uint64_t t232 = gm_l20;
                                uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                                if (o234) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 549, 98);
                                uint64_t t235 = r233;
                                uint64_t t236 = gm_l17;
                                uint64_t r237; bool o238 = __builtin_mul_overflow(t235, t236, &r237);
                                if (o238) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 549, 97);
                                uint64_t t239 = r237;
                                uint64_t t240 = (uintptr_t)t239;
                                if (t231 > t240 || t240 > (uintptr_t)t224) gm_trap_slice(t231, t240, t224, gm_srckg5f2c0p31ur, 549, 83);
                                gm_b16_8 t241;
                                GM_ST(void *, (uint8_t *)&t241, (void *)(((uint8_t *)t225 + t231 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(t240 - t231));
                                uint8_t t242 = gm_f_canonical_bytes_trb762l((void *)NULL, cx, NULL, t222, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l21)), t223, t241);
                                if (gm_unw.kind) {
                                    goto gm_ll197;
                                }
                                uint8_t t243 = (uint8_t)!t242;
                                t211 = t243;
                            } else t211 = 1;
                            if (!t211) goto ie210;
                            {
                                uint8_t t245 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix210;
ie210:;
ix210:;
                        }
                        goto me205;
mn206:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 548, 17);
me205:;
                    }
lc193:;
                    {
                        uint64_t t247 = gm_l19;
                        uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                        uint64_t t250 = r248;
                        gm_l19 = t250;
                    }
                }
gm_ll197:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb193; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc193; }
                }
                goto gm_lad;
lb193:;
            }
        }
        {
            uint8_t t251 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me171;
mn178:;
ma173:;
        uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&t170) + 0));
        if (t253 != ((uint8_t)12ULL)) goto mn252;
        gm_b16_8 t254 = GM_LD(gm_b16_8, ((((uint8_t *)&t170) + 8) + 0));
        gm_l22 = t254;
        {
            void * t256 = gm_l0;
            gm_t88rcucg1b7uk t257 = gm_l2;
            gm_tbccvikg6b96e t258;
            gm_f_layout_a5ovnrm((void *)&t258, (void *)NULL, cx, NULL, t256, t257);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbccvikg6b96e t259 = t258;
            gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&t259) + 16));
            gm_l23 = t260;
        }
        {
            {
                gm_b16_8 t262 = gm_l22;
                gm_l24 = t262;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t264 = gm_l25;
                    uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t266 = (t264 < t265);
                    if (!t266) goto lb263;
                    {
                        uint64_t t269 = gm_l25;
                        uint64_t t270 = gm_l25;
                        uint64_t t271 = (uintptr_t)t270;
                        uint64_t t272 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t271 * 4));
                        gm_tlcm4lvu3g1a8 t274;
                        GM_ST(uint32_t, (uint8_t *)&t274 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t274 + 0), t269);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t274 + 8), t273);
                        gm_tlcm4lvu3g1a8 t275 = t274;
                        uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&t275) + 0));
                        gm_l26 = t278;
                        gm_t88rcucg1b7uk t279 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t275) + 8));
                        gm_l27 = t279;
                        {
                            uint8_t t282 = 0;
                            uint64_t t283 = gm_l26;
                            void * t284 = gm_l15;
                            uint64_t t285 = GM_LD(uintptr_t, (((uint8_t *)t284) + 0) + 8);
                            uint8_t t286 = (t283 >= t285);
                            if (!t286) {
                                void * t287 = gm_l0;
                                uint64_t t288 = gm_l26;
                                uint64_t t289 = (uintptr_t)t288;
                                void * t290 = gm_l15;
                                uint64_t t291 = GM_LD(uintptr_t, (((uint8_t *)t290) + 0) + 8);
                                if (t289 >= (uintptr_t)(t291)) gm_trap_index(t289, t291, gm_srckg5f2c0p31ur, 556, 66);
                                gm_t88rcucg1b7uk t292 = gm_l27;
                                uint64_t t293 = gm_l26;
                                uint64_t t294 = (uintptr_t)t293;
                                uint64_t t295 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                if (t294 >= (uintptr_t)(t295)) gm_trap_index(t294, t295, gm_srckg5f2c0p31ur, 556, 81);
                                uint64_t t296 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t294 * 8));
                                gm_b16_8 t297 = gm_l3;
                                uint8_t t298 = gm_f_canonical_field_otka8vm((void *)NULL, cx, NULL, t287, &(*(gm_t6evimt84lndq *)((uint8_t *)GM_LD(void *, (((uint8_t *)t290) + 0)) + t289 * 48)), t292, t296, t297);
                                if (gm_unw.kind) {
                                    goto gm_ll267;
                                }
                                uint8_t t299 = (uint8_t)!t298;
                                t282 = t299;
                            } else t282 = 1;
                            if (!t282) goto ie281;
                            {
                                uint8_t t301 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix281;
ie281:;
ix281:;
                        }
                        goto me276;
mn277:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 555, 17);
me276:;
                    }
lc263:;
                    {
                        uint64_t t303 = gm_l25;
                        uint64_t r304; bool o305 = __builtin_add_overflow(t303, ((uint64_t)1ULL), &r304);
                        uint64_t t306 = r304;
                        gm_l25 = t306;
                    }
                }
gm_ll267:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb263; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc263; }
                }
                goto gm_lad;
lb263:;
            }
        }
        {
            uint8_t t307 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me171;
mn252:;
ma174:;
        uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&t170) + 0));
        if (t309 != ((uint8_t)17ULL)) goto mn308;
        uint32_t t310 = GM_LD(uint32_t, ((((uint8_t *)&t170) + 8) + 0));
        gm_l28 = t310;
        {
            uint32_t t313 = gm_l28;
            uint64_t t314 = (uint64_t)t313;
            uint64_t t315 = (uintptr_t)t314;
            void * t316 = gm_l0;
            uint64_t t317 = GM_LD(uintptr_t, ((((uint8_t *)t316) + 1088) + 0) + 8);
            if (t315 >= (uintptr_t)(t317)) gm_trap_index(t315, t317, gm_srckg5f2c0p31ur, 561, 33);
            uint8_t t318 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t316) + 1088) + 0)) + t315 * 184) + 24) + 0));
            if (t318 != ((uint8_t)0ULL)) goto ie312;
            gm_tpautr4br7h3l t319 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t316) + 1088) + 0)) + t315 * 184) + 24) + 8));
            F.l29 = t319;
            {
                uint8_t t322;
                if ((((((uint8_t *)&F.l29) + 40))[4] != 0)) goto mn324;
                t322 = ((uint8_t)0);
                goto me323;
mn324:;
                t322 = ((uint8_t)1);
                goto me323;
mn325:;
                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 562, 20);
me323:;
                if (!t322) goto ie321;
                {
                    uint8_t t327 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix321;
ie321:;
ix321:;
            }
            {
                void * t328 = gm_l0;
                gm_t88rcucg1b7uk t329 = gm_l2;
                gm_tbccvikg6b96e t330;
                gm_f_layout_a5ovnrm((void *)&t330, (void *)NULL, cx, NULL, t328, t329);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbccvikg6b96e t331 = t330;
                gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&t331) + 16));
                gm_l30 = t332;
            }
            {
                {
                    gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                    gm_b16_8 t335 = t334;
                    gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                    gm_l31 = t336;
                }
                {
                    gm_l32 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t338 = gm_l32;
                        uint64_t t339 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        uint8_t t340 = (t338 < t339);
                        if (!t340) goto lb337;
                        {
                            gm_t8ta482bl6be0 t343;
                            uint64_t t344 = gm_l32;
                            GM_ST(uint64_t, (uint8_t *)&t343 + 0, t344);
                            uint64_t t345 = gm_l32;
                            uint64_t t346 = (uintptr_t)t345;
                            uint64_t t347 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                            memcpy((uint8_t *)&t343 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t346 * 40), 40);
                            gm_t8ta482bl6be0 t348 = t343;
                            uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)&t348) + 0));
                            gm_l33 = t351;
                            gm_t0mglksrkfa5e t352 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t348) + 8));
                            F.l34 = t352;
                            {
                                uint8_t t355 = 0;
                                uint64_t t356 = gm_l33;
                                void * t357 = gm_l15;
                                uint64_t t358 = GM_LD(uintptr_t, (((uint8_t *)t357) + 0) + 8);
                                uint8_t t359 = (t356 >= t358);
                                if (!t359) {
                                    void * t360 = gm_l0;
                                    uint64_t t361 = gm_l33;
                                    uint64_t t362 = (uintptr_t)t361;
                                    void * t363 = gm_l15;
                                    uint64_t t364 = GM_LD(uintptr_t, (((uint8_t *)t363) + 0) + 8);
                                    if (t362 >= (uintptr_t)(t364)) gm_trap_index(t362, t364, gm_srckg5f2c0p31ur, 565, 66);
                                    gm_t88rcucg1b7uk t365 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 16));
                                    uint64_t t366 = gm_l33;
                                    uint64_t t367 = (uintptr_t)t366;
                                    uint64_t t368 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                    if (t367 >= (uintptr_t)(t368)) gm_trap_index(t367, t368, gm_srckg5f2c0p31ur, 565, 84);
                                    uint64_t t369 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t367 * 8));
                                    gm_b16_8 t370 = gm_l3;
                                    uint8_t t371 = gm_f_canonical_field_otka8vm((void *)NULL, cx, NULL, t360, &(*(gm_t6evimt84lndq *)((uint8_t *)GM_LD(void *, (((uint8_t *)t363) + 0)) + t362 * 48)), t365, t369, t370);
                                    if (gm_unw.kind) {
                                        goto gm_ll341;
                                    }
                                    uint8_t t372 = (uint8_t)!t371;
                                    t355 = t372;
                                } else t355 = 1;
                                if (!t355) goto ie354;
                                {
                                    uint8_t t374 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto ix354;
ie354:;
ix354:;
                            }
                            goto me349;
mn350:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 564, 17);
me349:;
                        }
lc337:;
                        {
                            uint64_t t376 = gm_l32;
                            uint64_t r377; bool o378 = __builtin_add_overflow(t376, ((uint64_t)1ULL), &r377);
                            uint64_t t379 = r377;
                            gm_l32 = t379;
                        }
                    }
gm_ll341:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb337; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc337; }
                    }
                    goto gm_lad;
lb337:;
                }
            }
            {
                uint8_t t380 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix312;
ie312:;
            {
                uint8_t t382 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix312:;
        }
        goto me171;
mn308:;
ma175:;
        uint8_t t384 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me171;
mn383:;
ma176:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 545, 20);
me171:;
        goto me4;
mn164:;
ma9:;
        uint8_t t386 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me4;
mn385:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 518, 5);
me4:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_canonical_field_otka8vm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4) {
    struct gm_fr_canonical_field_otka8vm F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint64_t gm_l3; /* offset */
    gm_b16_8 gm_l4; /* out */
    uint64_t gm_l5; /* start */
    uint64_t gm_l6; /* end */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t2 = gm_l3;
        uint64_t t3 = (uint64_t)t2;
        gm_l5 = t3;
    }
    {
        uint64_t t4 = gm_l5;
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_tbccvikg6b96e t7;
        gm_f_layout_a5ovnrm((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbccvikg6b96e t8 = t7;
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&t8) + 0));
        uint64_t t10 = (uint64_t)t9;
        uint64_t r11; bool o12 = __builtin_add_overflow(t4, t10, &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 578, 12);
        uint64_t t13 = r11;
        gm_l6 = t13;
    }
    {
        uint64_t t15 = gm_l6;
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t17 = (t15 > t16);
        if (!t17) goto ie14;
        {
            uint8_t t19 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l2;
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        void * t23 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
        uint64_t t24 = gm_l5;
        uint64_t t25 = (uintptr_t)t24;
        uint64_t t26 = gm_l6;
        uint64_t t27 = (uintptr_t)t26;
        if (t25 > t27 || t27 > (uintptr_t)t22) gm_trap_slice(t25, t27, t22, gm_srckg5f2c0p31ur, 580, 37);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t23 + t25 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t25));
        uint8_t t29 = gm_f_canonical_bytes_trb762l((void *)NULL, cx, NULL, t20, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l1)), t21, t28);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_constraint_name_ombut3o(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_constraint_name_ombut3o F;
    gm_t9qkh0rsooc8d gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; default: goto ma10; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sqri8tum3tpfm));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(2));
    t1 = t14;
    goto me2;
mn12:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s7kt2bei71pgo));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
    t1 = t17;
    goto me2;
mn15:;
ma5:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)2ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_snoj0t5lf2lht));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
    t1 = t20;
    goto me2;
mn18:;
ma6:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)3ULL)) goto mn21;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_seggohi50ng09));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
    t1 = t23;
    goto me2;
mn21:;
ma7:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t25 != ((uint8_t)4ULL)) goto mn24;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_se19nf1421e37));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
    t1 = t26;
    goto me2;
mn24:;
ma8:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)5ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s7s25m3nlrif2));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(5));
    t1 = t29;
    goto me2;
mn27:;
ma9:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)6ULL)) goto mn30;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sfv26hkjngt1i));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(5));
    t1 = t32;
    goto me2;
mn30:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 14, 55);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_declare_constraint_s1juggf F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* c */
    gm_b16_8 gm_l3; /* msg */
    gm_t9qkh0rsooc8d gm_l4; /* k */
    gm_t88rcucg1b7uk gm_l5; /* t */
    uint32_t gm_l6; /* p */
    GM_STACK_CHECK(&F, 641u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 0) + 0));
        gm_taari1s0m51a7 t4 = gm_f_parse_constraint_6f1aotv(t3);
        gm_taari1s0m51a7 t5 = t4;
        gm_t9qkh0rsooc8d t6;
        if (((((uint8_t *)&t5))[1] != 0)) {
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            {
                void * t9 = gm_l2;
                gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 0) + 0));
                gm_tnpifv20irnph t11;
                GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
                gm_tabb710d9mq2e t12;
                gm_f_format_n0hp6t5((void *)&t12, (void *)NULL, cx, NULL, t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t13 = t12;
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srckg5f2c0p31ur, 29, 16);
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
                gm_l3 = t14;
            }
            {
                void * t16 = gm_l2;
                uint8_t t17 = (((GM_LD(uintptr_t, ((((uint8_t *)t16) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)t16) + 0) + 0)), gm_s0rru461jb8nu, 6) == 0));
                if (!t17) goto ie15;
                {
                    gm_b16_8 t19 = gm_l3;
                    gm_tnpifv20irnph t20;
                    GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
                    gm_tabb710d9mq2e t21;
                    gm_f_format_954a9tt((void *)&t21, (void *)NULL, cx, NULL, t20);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t22 = t21;
                    if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srckg5f2c0p31ur, 30, 44);
                    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
                    gm_l3 = t23;
                }
                goto ix15;
ie15:;
ix15:;
            }
            {
                void * t24 = gm_l0;
                void * t25 = gm_l2;
                gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t25) + 0) + 16));
                gm_b16_8 t27 = gm_l3;
                gm_b16_8 t28 = t27;
                gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                gm_b0_1 t30 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t24, t26, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t31;
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        void * t33 = gm_l0;
        uint64_t t34 = gm_l1;
        void * t35 = gm_l2;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 32));
        gm_t88rcucg1b7uk t37 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t33, t34, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t37;
    }
    {
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 8);
        gm_t88rcucg1b7uk t40 = gm_l5;
        gm_t53at80pfdus3 t41;
        gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
        gm_t53at80pfdus3 t42 = t41;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
        switch (t48) { case ((uint8_t)0ULL): goto ma46; case ((uint8_t)1ULL): goto ma46; case ((uint8_t)2ULL): goto ma46; case ((uint8_t)3ULL): goto ma46; case ((uint8_t)4ULL): goto ma46; case ((uint8_t)5ULL): goto ma46; case ((uint8_t)6ULL): goto ma46; case ((uint8_t)7ULL): goto ma46; case ((uint8_t)8ULL): goto ma46; case ((uint8_t)9ULL): goto ma46; case ((uint8_t)10ULL): goto ma46; case ((uint8_t)11ULL): goto ma46; case ((uint8_t)12ULL): goto ma46; case ((uint8_t)13ULL): goto ma46; case ((uint8_t)14ULL): goto ma46; case ((uint8_t)15ULL): goto ma46; case ((uint8_t)16ULL): goto ma46; case ((uint8_t)17ULL): goto ma46; case ((uint8_t)18ULL): goto ma46; case ((uint8_t)19ULL): goto ma44; case ((uint8_t)20ULL): goto ma46; case ((uint8_t)21ULL): goto ma46; case ((uint8_t)22ULL): goto ma46; case ((uint8_t)23ULL): goto ma46; case ((uint8_t)24ULL): goto ma46; case ((uint8_t)25ULL): goto ma46; case ((uint8_t)26ULL): goto ma45; default: goto ma46; }
ma44:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
        if (t50 != ((uint8_t)19ULL)) goto mn49;
        uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&t42) + 8));
        gm_l6 = t51;
        uint32_t t52 = gm_l6;
        uint64_t t53 = (uint64_t)t52;
        uint64_t t54 = (uintptr_t)t53;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uintptr_t, ((((uint8_t *)t55) + 1128) + 0) + 8);
        if (t54 >= (uintptr_t)(t56)) gm_trap_index(t54, t56, gm_srckg5f2c0p31ur, 36, 22);
        void * t57 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 1128) + 0)) + t54 * 48) + 8);
        gm_t9qkh0rsooc8d t58 = gm_l4;
        gm_tftb1odc739t5 t59 = gm_f_push_aqjeqnr_2(t57, t58);
        gm_tftb1odc739t5 t60 = t59;
        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srckg5f2c0p31ur, 36, 22);
        gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
        goto me43;
mn49:;
ma45:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
        if (t63 != ((uint8_t)26ULL)) goto mn62;
        goto me43;
mn62:;
ma46:;
        void * t66 = gm_l0;
        void * t67 = gm_l2;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 32));
        gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 72));
        gm_b16_8 t70;
        GM_ST(void *, (uint8_t *)&t70, (void *)(gm_srio0gkdttkj2));
        GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(60));
        gm_b0_1 t71 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t66, t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me43;
mn65:;
ma47:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 35, 5);
me43:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_end_constraint_defer_52jt3m9 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t8er5olt969ip gm_l4; /* d */
    GM_STACK_CHECK(&F, 335u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 1768));
        uint32_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 53, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 1768), t7);
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 1768));
        uint8_t t11 = (t10 > ((uint32_t)0ULL));
        if (!t11) goto ie8;
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t15 = gm_l0;
        gm_trqfvmjtt4sij t16 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t15) + 1776));
        F.l1 = t16;
    }
    {
        void * t17 = gm_l0;
        uint8_t *t18 = (((uint8_t *)t17) + 1776);
        gm_f_list_ib5k4gb((void *)&(*(gm_trqfvmjtt4sij *)t18), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l2 = t20;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t22 = gm_l3;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t8er5olt969ip t32 = GM_LD(gm_t8er5olt969ip, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t28 * 28));
                    gm_l4 = t32;
                    {
                        void * t35 = gm_l0;
                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 0));
                        gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l4) + 4));
                        uint8_t t38 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t35, t36, t37);
                        if (gm_unw.kind) {
                            goto gm_ll25;
                        }
                        uint8_t t39 = (uint8_t)!t38;
                        if (!t39) goto ie34;
                        {
                            void * t41 = gm_l0;
                            gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                            void * t43 = gm_l0;
                            gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 0));
                            gm_t9qkh0rsooc8d t45 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l4) + 4));
                            uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
                            uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 12));
                            gm_b16_8 t48 = gm_f_unsatisfied_jnmtj8v((void *)NULL, cx, NULL, t43, t44, t45, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                            gm_b0_1 t49 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t41, t42, t48);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                        }
                        goto ix34;
ie34:;
ix34:;
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 57, 5);
me30:;
                }
lc21:;
                {
                    uint64_t t51 = gm_l3;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l3 = t54;
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
extern __attribute__((aligned(4))) uint8_t gm_f_has_eq_iba53hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_has_eq_in_5a59avv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_has_eq_in_5a59avv F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* seen */
    uint32_t gm_l3; /* p */
    gm_b16_8 gm_l4; /* elems */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* e */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* err */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    uint32_t gm_l14; /* id */
    uint8_t gm_l15; /* r */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l20; /* variants */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_t88rcucg1b7uk gm_l24; /* p */
    GM_STACK_CHECK(&F, 1039u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma8:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t20 != ((uint8_t)15ULL)) goto pa19;
        goto po18;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t22 != ((uint8_t)25ULL)) goto pa21;
        goto po18;
pa21:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t24 != ((uint8_t)18ULL)) goto pa23;
        goto po18;
pa23:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t26 != ((uint8_t)6ULL)) goto pa25;
        goto po18;
pa25:;
        goto mn17;
po18:;
        uint8_t t27 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn17:;
ma9:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t29 != ((uint8_t)19ULL)) goto mn28;
        uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l3 = t30;
        void * t31 = gm_l0;
        uint32_t t32 = gm_l3;
        gm_t9qkh0rsooc8d t33;
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0ULL));
        uint8_t t34 = gm_f_param_has_2r09oc7(t31, t32, t33);
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me7;
mn28:;
ma10:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t37 != ((uint8_t)12ULL)) goto mn36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l4 = t38;
        {
            {
                gm_b16_8 t41 = gm_l4;
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
                        uint64_t t48 = gm_l6;
                        uint64_t t49 = (uintptr_t)t48;
                        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t49 * 4));
                        gm_l7 = t53;
                        {
                            void * t56 = gm_l0;
                            gm_t88rcucg1b7uk t57 = gm_l7;
                            void * t58 = gm_l2;
                            uint8_t t59 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t56, t57, t58);
                            if (gm_unw.kind) {
                                goto gm_ll46;
                            }
                            uint8_t t60 = (uint8_t)!t59;
                            if (!t60) goto ie55;
                            {
                                uint8_t t62 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix55;
ie55:;
ix55:;
                        }
                        goto me51;
mn52:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 118, 13);
me51:;
                    }
lc42:;
                    {
                        uint64_t t64 = gm_l6;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        uint64_t t67 = r65;
                        gm_l6 = t67;
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
            uint8_t t68 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me7;
mn36:;
ma11:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t72 != ((uint8_t)11ULL)) goto pa71;
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l8 = t73;
        goto po70;
pa71:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t75 != ((uint8_t)20ULL)) goto pa74;
        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l8 = t76;
        goto po70;
pa74:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t78 != ((uint8_t)10ULL)) goto pa77;
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l8 = t79;
        goto po70;
pa77:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t81 != ((uint8_t)13ULL)) goto pa80;
        gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l8 = t82;
        goto po70;
pa80:;
        goto mn69;
po70:;
        void * t83 = gm_l0;
        gm_t88rcucg1b7uk t84 = gm_l8;
        void * t85 = gm_l2;
        uint8_t t86 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t83, t84, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t87 = 0;
        F.ret = t86;
        goto gm_fret;
        goto me7;
mn69:;
ma12:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t89 != ((uint8_t)14ULL)) goto mn88;
        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l12 = t90;
        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l13 = t91;
        uint8_t t92 = 0;
        void * t93 = gm_l0;
        gm_t88rcucg1b7uk t94 = gm_l12;
        void * t95 = gm_l2;
        uint8_t t96 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t93, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t96) {
            void * t97 = gm_l0;
            gm_t88rcucg1b7uk t98 = gm_l13;
            void * t99 = gm_l2;
            uint8_t t100 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t97, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t92 = t100;
        } else t92 = 0;
        uint8_t t101 = 0;
        F.ret = t92;
        goto gm_fret;
        goto me7;
mn88:;
ma13:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t103 != ((uint8_t)17ULL)) goto mn102;
        uint32_t t104 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l14 = t104;
        {
            gm_t88rcucg1b7uk t107 = gm_l1;
            void * t108 = gm_l2;
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)t108) + 0));
            gm_b16_8 t110 = t109;
            uint8_t t111 = 0;
            gm_t88rcucg1b7uk t112 = t107;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t110) + 8)); i++) if (gm_eq9m6mvtmgde(((uint8_t *)GM_LD(void *, ((uint8_t *)&t110))) + i * 4, ((uint8_t *)&t112))) { t111 = 1; break; }
            uint8_t t113 = (uint8_t)(t111);
            if (!t113) goto ie106;
            {
                uint8_t t115 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix106;
ie106:;
ix106:;
        }
        {
            void * t116 = gm_l2;
            gm_t88rcucg1b7uk t117 = gm_l1;
            gm_tftb1odc739t5 t118 = gm_f_push_k3l9nic(t116, GM_CONV(gm_csguankko4lmhd, t117));
            gm_tftb1odc739t5 t119 = t118;
            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srckg5f2c0p31ur, 127, 13);
            gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
        }
        {
            gm_l15 = ((uint8_t)1);
        }
        {
            uint32_t t121 = gm_l14;
            uint64_t t122 = (uint64_t)t121;
            uint64_t t123 = (uintptr_t)t122;
            void * t124 = gm_l0;
            uint64_t t125 = GM_LD(uintptr_t, ((((uint8_t *)t124) + 1088) + 0) + 8);
            if (t123 >= (uintptr_t)(t125)) gm_trap_index(t123, t125, gm_srckg5f2c0p31ur, 129, 19);
            uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t124) + 1088) + 0)) + t123 * 184) + 24) + 0));
            switch (t131) { case ((uint8_t)0ULL): goto ma127; case ((uint8_t)1ULL): goto ma128; case ((uint8_t)2ULL): goto ma129; default: goto ma130; }
ma127:;
            uint8_t t133 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t124) + 1088) + 0)) + t123 * 184) + 24) + 0));
            if (t133 != ((uint8_t)0ULL)) goto mn132;
            gm_tpautr4br7h3l t134 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t124) + 1088) + 0)) + t123 * 184) + 24) + 8));
            F.l16 = t134;
            {
                gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                gm_l17 = t136;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t138 = gm_l18;
                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t140 = (t138 < t139);
                    if (!t140) goto lb137;
                    {
                        uint64_t t143 = gm_l18;
                        uint64_t t144 = (uintptr_t)t143;
                        uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_t0mglksrkfa5e t148 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t144 * 40));
                        F.l19 = t148;
                        {
                            void * t151 = gm_l0;
                            gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                            void * t153 = gm_l2;
                            uint8_t t154 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t151, t152, t153);
                            if (gm_unw.kind) {
                                goto gm_ll141;
                            }
                            uint8_t t155 = (uint8_t)!t154;
                            if (!t155) goto ie150;
                            {
                                gm_l15 = ((uint8_t)0);
                            }
                            {
                                goto lb137;
                            }
                            goto ix150;
ie150:;
ix150:;
                        }
                        goto me146;
mn147:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 130, 33);
me146:;
                    }
lc137:;
                    {
                        uint64_t t158 = gm_l18;
                        uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                        uint64_t t161 = r159;
                        gm_l18 = t161;
                    }
                }
gm_ll141:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb137; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc137; }
                }
                goto gm_lad;
lb137:;
            }
            goto me126;
mn132:;
ma128:;
            uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t124) + 1088) + 0)) + t123 * 184) + 24) + 0));
            if (t163 != ((uint8_t)1ULL)) goto mn162;
            gm_b16_8 t164 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t124) + 1088) + 0)) + t123 * 184) + 24) + 8) + 8));
            gm_l20 = t164;
            {
                gm_b16_8 t166 = gm_l20;
                gm_l21 = t166;
            }
            {
                gm_l22 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t168 = gm_l22;
                    uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    uint8_t t170 = (t168 < t169);
                    if (!t170) goto lb167;
                    {
                        uint64_t t173 = gm_l22;
                        uint64_t t174 = (uintptr_t)t173;
                        uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        gm_tckdobunaulee t178 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t174 * 72));
                        F.l23 = t178;
                        {
                            if (!(((((uint8_t *)&F.l23) + 16))[4] != 0)) goto ie180;
                            gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l23) + 16));
                            gm_l24 = t181;
                            void * t182 = gm_l0;
                            gm_t88rcucg1b7uk t183 = gm_l24;
                            void * t184 = gm_l2;
                            uint8_t t185 = gm_f_has_eq_in_5a59avv((void *)NULL, cx, NULL, t182, t183, t184);
                            if (gm_unw.kind) {
                                goto gm_ll171;
                            }
                            uint8_t t186 = (uint8_t)!t185;
                            if (!t186) goto ie180;
                            {
                                gm_l15 = ((uint8_t)0);
                            }
                            {
                                goto lb167;
                            }
                            goto ix180;
ie180:;
ix180:;
                        }
                        goto me176;
mn177:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 136, 40);
me176:;
                    }
lc167:;
                    {
                        uint64_t t189 = gm_l22;
                        uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                        uint64_t t192 = r190;
                        gm_l22 = t192;
                    }
                }
gm_ll171:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb167; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc167; }
                }
                goto gm_lad;
lb167:;
            }
            goto me126;
mn162:;
ma129:;
            uint8_t t194 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t124) + 1088) + 0)) + t123 * 184) + 24) + 0));
            if (t194 != ((uint8_t)2ULL)) goto mn193;
            {
                gm_l15 = ((uint8_t)0);
            }
            goto me126;
mn193:;
ma130:;
            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 129, 13);
me126:;
        }
        {
            {
                void * t197 = gm_l2;
                gm_tkn42u7na3t81 t198 = gm_f_pop_knqt0be(t197);
            }
        }
        {
            uint8_t t199 = gm_l15;
            uint8_t t200 = 0;
            F.ret = t199;
            goto gm_fret;
        }
        goto me7;
mn102:;
ma14:;
        uint8_t t202 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me7;
mn201:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 114, 5);
me7:;
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
__attribute__((aligned(4))) uint8_t gm_f_has_ord_1io9knm(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_ord_1io9knm F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* elems */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_tnnjeerolos0a gm_l4; /* kind */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint32_t gm_l7; /* p */
    gm_tep2drsl9408b gm_l8; /* u */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    gm_t53at80pfdus3 t5 = t4;
    uint8_t t6;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    switch (t16) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma8:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t20 != ((uint8_t)4ULL)) goto pa19;
    goto po18;
pa19:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t22 != ((uint8_t)0ULL)) goto pa21;
    goto po18;
pa21:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t24 != ((uint8_t)1ULL)) goto pa23;
    goto po18;
pa23:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t26 != ((uint8_t)2ULL)) goto pa25;
    goto po18;
pa25:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t28 != ((uint8_t)3ULL)) goto pa27;
    goto po18;
pa27:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t30 != ((uint8_t)9ULL)) goto pa29;
    goto po18;
pa29:;
    goto mn17;
po18:;
    t6 = ((uint8_t)1);
    goto me7;
mn17:;
ma9:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t32 != ((uint8_t)12ULL)) goto mn31;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
    gm_l2 = t33;
    void * t34 = gm_l0;
    gm_b16_8 t35 = gm_l2;
    uint8_t t36 = gm_f_all_ord_8bea963(t34, t35);
    t6 = t36;
    goto me7;
mn31:;
ma10:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t38 != ((uint8_t)11ULL)) goto mn37;
    gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
    gm_l3 = t39;
    gm_tnnjeerolos0a t40 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t5) + 8) + 12));
    gm_l4 = t40;
    uint8_t t41 = 0;
    gm_tnnjeerolos0a t42 = gm_l4;
    gm_tnnjeerolos0a t43;
    GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)2ULL));
    gm_tnnjeerolos0a t44;
    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
    gm_tcf2f71ra2nps t45;
    GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t45 + 0), t43);
    GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t45 + 8), t44);
    gm_tcf2f71ra2nps t46 = t45;
    uint8_t t47 = 0;
    gm_tnnjeerolos0a t48 = t42;
    if (gm_eqb2ul45tidi(((uint8_t *)&t46) + 0, ((uint8_t *)&t48))) t47 = 1;
    if (gm_eqb2ul45tidi(((uint8_t *)&t46) + 8, ((uint8_t *)&t48))) t47 = 1;
    uint8_t t49 = (uint8_t)(!t47);
    if (t49) {
        void * t50 = gm_l0;
        gm_t88rcucg1b7uk t51 = gm_l3;
        uint8_t t52 = gm_f_has_ord_1io9knm(t50, GM_CONV(gm_csguankko4lmhd, t51));
        t41 = t52;
    } else t41 = 0;
    t6 = t41;
    goto me7;
mn37:;
ma11:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t56 != ((uint8_t)20ULL)) goto pa55;
    gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
    gm_l5 = t57;
    goto po54;
pa55:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t59 != ((uint8_t)10ULL)) goto pa58;
    gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
    gm_l5 = t60;
    goto po54;
pa58:;
    goto mn53;
po54:;
    void * t61 = gm_l0;
    gm_t88rcucg1b7uk t62 = gm_l5;
    uint8_t t63 = gm_f_has_ord_1io9knm(t61, GM_CONV(gm_csguankko4lmhd, t62));
    t6 = t63;
    goto me7;
mn53:;
ma12:;
    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t65 != ((uint8_t)19ULL)) goto mn64;
    uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
    gm_l7 = t66;
    void * t67 = gm_l0;
    uint32_t t68 = gm_l7;
    gm_t9qkh0rsooc8d t69;
    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
    uint8_t t70 = gm_f_param_has_2r09oc7(t67, t68, t69);
    t6 = t70;
    goto me7;
mn64:;
ma13:;
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t72 != ((uint8_t)17ULL)) goto mn71;
    {
        void * t74 = gm_l0;
        gm_t88rcucg1b7uk t75 = gm_l1;
        gm_tdlpv0q69lov1 t76;
        gm_f_union_variants_a7khaqp((void *)&t76, t74, GM_CONV(gm_csguankko4lmhd, t75));
        gm_tdlpv0q69lov1 t77 = t76;
        gm_tep2drsl9408b t78;
        if (((((uint8_t *)&t77))[24] != 0)) {
            gm_tep2drsl9408b t79 = GM_LD(gm_tep2drsl9408b, (((uint8_t *)&t77) + 0));
            t78 = t79;
        } else {
            uint8_t t80 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l8 = t78;
    }
    {
        {
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
            gm_l9 = t82;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t84 = gm_l10;
                uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t86 = (t84 < t85);
                if (!t86) goto lb83;
                {
                    uint64_t t89 = gm_l10;
                    uint64_t t90 = (uintptr_t)t89;
                    uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_tckdobunaulee t94 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t90 * 72));
                    F.l11 = t94;
                    {
                        uint8_t t97;
                        if ((((((uint8_t *)&F.l11) + 16))[4] != 0)) goto mn99;
                        t97 = ((uint8_t)0);
                        goto me98;
mn99:;
                        t97 = ((uint8_t)1);
                        goto me98;
mn100:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 91, 16);
me98:;
                        if (!t97) goto ie96;
                        {
                            uint8_t t102 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix96;
ie96:;
ix96:;
                    }
                    goto me92;
mn93:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 90, 9);
me92:;
                }
lc83:;
                {
                    uint64_t t104 = gm_l10;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l10 = t107;
                }
            }
lb83:;
        }
    }
    uint8_t t108 = ((uint8_t)1);
    t6 = t108;
    goto me7;
mn71:;
ma14:;
    t6 = ((uint8_t)0);
    goto me7;
mn109:;
ma15:;
    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 81, 62);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_implies_ibnta05(gm_t9qkh0rsooc8d a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_implies_ibnta05 F;
    gm_t9qkh0rsooc8d gm_l0; /* a */
    gm_t9qkh0rsooc8d gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; default: goto ma10; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    uint8_t t14 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
    t1 = t14;
    goto me2;
mn12:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_t9qkh0rsooc8d t17 = gm_l1;
    gm_t9qkh0rsooc8d t18;
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
    gm_t0am46iohlpol t20;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 0), t18);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t19);
    gm_t0am46iohlpol t21 = t20;
    uint8_t t22 = 0;
    gm_t9qkh0rsooc8d t23 = t17;
    if (gm_eqmvsckgkj25(((uint8_t *)&t21) + 0, ((uint8_t *)&t23))) t22 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t21) + 1, ((uint8_t *)&t23))) t22 = 1;
    uint8_t t24 = (uint8_t)(t22);
    t1 = t24;
    goto me2;
mn15:;
ma5:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    gm_t9qkh0rsooc8d t27 = gm_l1;
    gm_t9qkh0rsooc8d t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
    gm_tk22flr9mh3ql t31;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 0), t28);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 1), t29);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 2), t30);
    gm_tk22flr9mh3ql t32 = t31;
    uint8_t t33 = 0;
    gm_t9qkh0rsooc8d t34 = t27;
    if (gm_eqmvsckgkj25(((uint8_t *)&t32) + 0, ((uint8_t *)&t34))) t33 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t32) + 1, ((uint8_t *)&t34))) t33 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t32) + 2, ((uint8_t *)&t34))) t33 = 1;
    uint8_t t35 = (uint8_t)(t33);
    t1 = t35;
    goto me2;
mn25:;
ma6:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t37 != ((uint8_t)3ULL)) goto mn36;
    gm_t9qkh0rsooc8d t38 = gm_l1;
    gm_t9qkh0rsooc8d t39;
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)3ULL));
    gm_t9qkh0rsooc8d t40;
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)2ULL));
    gm_t9qkh0rsooc8d t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t42;
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t43;
    GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)6ULL));
    gm_t724v60om7sh5 t44;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 0), t39);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 1), t40);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 2), t41);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 3), t42);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t44 + 4), t43);
    gm_t724v60om7sh5 t45 = t44;
    uint8_t t46 = 0;
    gm_t9qkh0rsooc8d t47 = t38;
    if (gm_eqmvsckgkj25(((uint8_t *)&t45) + 0, ((uint8_t *)&t47))) t46 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t45) + 1, ((uint8_t *)&t47))) t46 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t45) + 2, ((uint8_t *)&t47))) t46 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t45) + 3, ((uint8_t *)&t47))) t46 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t45) + 4, ((uint8_t *)&t47))) t46 = 1;
    uint8_t t48 = (uint8_t)(t46);
    t1 = t48;
    goto me2;
mn36:;
ma7:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)4ULL)) goto mn49;
    gm_t9qkh0rsooc8d t51 = gm_l1;
    gm_t9qkh0rsooc8d t52;
    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)4ULL));
    gm_t9qkh0rsooc8d t53;
    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)2ULL));
    gm_t9qkh0rsooc8d t54;
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t55;
    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t56;
    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)6ULL));
    gm_t724v60om7sh5 t57;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t57 + 0), t52);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t57 + 1), t53);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t57 + 2), t54);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t57 + 3), t55);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t57 + 4), t56);
    gm_t724v60om7sh5 t58 = t57;
    uint8_t t59 = 0;
    gm_t9qkh0rsooc8d t60 = t51;
    if (gm_eqmvsckgkj25(((uint8_t *)&t58) + 0, ((uint8_t *)&t60))) t59 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t58) + 1, ((uint8_t *)&t60))) t59 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t58) + 2, ((uint8_t *)&t60))) t59 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t58) + 3, ((uint8_t *)&t60))) t59 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t58) + 4, ((uint8_t *)&t60))) t59 = 1;
    uint8_t t61 = (uint8_t)(t59);
    t1 = t61;
    goto me2;
mn49:;
ma8:;
    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t63 != ((uint8_t)5ULL)) goto mn62;
    gm_t9qkh0rsooc8d t64 = gm_l1;
    gm_t9qkh0rsooc8d t65;
    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)5ULL));
    gm_t9qkh0rsooc8d t66;
    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)4ULL));
    gm_t9qkh0rsooc8d t67;
    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
    gm_t9qkh0rsooc8d t68;
    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t69;
    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t70;
    GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)6ULL));
    gm_t9rbkbpjtolar t71;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 0), t65);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 1), t66);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 2), t67);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 3), t68);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 4), t69);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t71 + 5), t70);
    gm_t9rbkbpjtolar t72 = t71;
    uint8_t t73 = 0;
    gm_t9qkh0rsooc8d t74 = t64;
    if (gm_eqmvsckgkj25(((uint8_t *)&t72) + 0, ((uint8_t *)&t74))) t73 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t72) + 1, ((uint8_t *)&t74))) t73 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t72) + 2, ((uint8_t *)&t74))) t73 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t72) + 3, ((uint8_t *)&t74))) t73 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t72) + 4, ((uint8_t *)&t74))) t73 = 1;
    if (gm_eqmvsckgkj25(((uint8_t *)&t72) + 5, ((uint8_t *)&t74))) t73 = 1;
    uint8_t t75 = (uint8_t)(t73);
    t1 = t75;
    goto me2;
mn62:;
ma9:;
    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t77 != ((uint8_t)6ULL)) goto mn76;
    uint8_t t78 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)6ULL)));
    t1 = t78;
    goto me2;
mn76:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 17, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_inner_second_class_part_ufhro3f(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inner_second_class_part_ufhro3f F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* x */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* x */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_t88rcucg1b7uk gm_l8; /* x */
    gm_t88rcucg1b7uk gm_l9; /* err */
    gm_t88rcucg1b7uk gm_l10; /* ok */
    gm_b16_8 gm_l11; /* elems */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t88rcucg1b7uk gm_l14; /* x */
    gm_t88rcucg1b7uk gm_l15; /* p */
    gm_b16_8 gm_l16; /* args */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t88rcucg1b7uk gm_l20; /* x */
    gm_t88rcucg1b7uk gm_l21; /* p */
    GM_STACK_CHECK(&F, 797u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma8:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t20 != ((uint8_t)25ULL)) goto pa19;
        goto po18;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t22 != ((uint8_t)6ULL)) goto pa21;
        goto po18;
pa21:;
        goto mn17;
po18:;
        gm_t88rcucg1b7uk t23 = gm_l1;
        gm_tkn42u7na3t81 t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t24), t23);
        ((uint8_t *)&t24)[4] = 1;
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
        goto me7;
mn17:;
ma9:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t29 != ((uint8_t)8ULL)) goto pa28;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l2 = t30;
        goto po27;
pa28:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t32 != ((uint8_t)9ULL)) goto pa31;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l2 = t33;
        goto po27;
pa31:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t35 != ((uint8_t)10ULL)) goto pa34;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l2 = t36;
        goto po27;
pa34:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t38 != ((uint8_t)13ULL)) goto pa37;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l2 = t39;
        goto po27;
pa37:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t41 != ((uint8_t)18ULL)) goto pa40;
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l2 = t42;
        goto po27;
pa40:;
        goto mn26;
po27:;
        void * t43 = gm_l0;
        gm_t88rcucg1b7uk t44 = gm_l2;
        gm_tkn42u7na3t81 t45 = gm_f_inner_second_class_part_ufhro3f(t43, GM_CONV(gm_csguankko4lmhd, t44));
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
        goto me7;
mn26:;
ma10:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t50 != ((uint8_t)11ULL)) goto pa49;
        gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 8));
        gm_l7 = t51;
        goto po48;
pa49:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t53 != ((uint8_t)20ULL)) goto pa52;
        gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l7 = t54;
        goto po48;
pa52:;
        goto mn47;
po48:;
        void * t55 = gm_l0;
        gm_t88rcucg1b7uk t56 = gm_l7;
        gm_tkn42u7na3t81 t57 = gm_f_inner_second_class_part_ufhro3f(t55, GM_CONV(gm_csguankko4lmhd, t56));
        uint8_t t58 = 0;
        F.ret = t57;
        goto gm_fret;
        goto me7;
mn47:;
ma11:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t60 != ((uint8_t)14ULL)) goto mn59;
        gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l9 = t61;
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l10 = t62;
        void * t63 = gm_l0;
        gm_t88rcucg1b7uk t64 = gm_l9;
        gm_tkn42u7na3t81 t65 = gm_f_inner_second_class_part_ufhro3f(t63, GM_CONV(gm_csguankko4lmhd, t64));
        gm_tkn42u7na3t81 t66 = t65;
        gm_tkn42u7na3t81 t67;
        if (((((uint8_t *)&t66))[4] != 0)) {
            t67 = t65;
        } else {
            void * t68 = gm_l0;
            gm_t88rcucg1b7uk t69 = gm_l10;
            gm_tkn42u7na3t81 t70 = gm_f_inner_second_class_part_ufhro3f(t68, GM_CONV(gm_csguankko4lmhd, t69));
            t67 = t70;
        }
        uint8_t t71 = 0;
        F.ret = t67;
        goto gm_fret;
        goto me7;
mn59:;
ma12:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t73 != ((uint8_t)12ULL)) goto mn72;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l11 = t74;
        {
            {
                gm_b16_8 t77 = gm_l11;
                gm_l12 = t77;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t79 = gm_l13;
                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t81 = (t79 < t80);
                    if (!t81) goto lb78;
                    {
                        uint64_t t84 = gm_l13;
                        uint64_t t85 = (uintptr_t)t84;
                        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t85 * 4));
                        gm_l14 = t89;
                        {
                            void * t92 = gm_l0;
                            gm_t88rcucg1b7uk t93 = gm_l14;
                            gm_tkn42u7na3t81 t94 = gm_f_inner_second_class_part_ufhro3f(t92, GM_CONV(gm_csguankko4lmhd, t93));
                            gm_tkn42u7na3t81 t95 = t94;
                            if (!((((uint8_t *)&t95))[4] != 0)) goto ie91;
                            gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t95));
                            gm_l15 = t96;
                            {
                                gm_t88rcucg1b7uk t98 = gm_l15;
                                gm_tkn42u7na3t81 t99;
                                GM_ST(uint8_t, (uint8_t *)&t99 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t99 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t99), t98);
                                ((uint8_t *)&t99)[4] = 1;
                                uint8_t t100 = 0;
                                F.ret = t99;
                                goto gm_fret;
                            }
                            goto ix91;
ie91:;
ix91:;
                        }
                        goto me87;
mn88:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 212, 13);
me87:;
                    }
lc78:;
                    {
                        uint64_t t102 = gm_l13;
                        uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                        uint64_t t105 = r103;
                        gm_l13 = t105;
                    }
                }
lb78:;
            }
        }
        {
            gm_tkn42u7na3t81 t106;
            memset(&t106, 0, sizeof t106);
            uint8_t t107 = 0;
            F.ret = t106;
            goto gm_fret;
        }
        goto me7;
mn72:;
ma13:;
        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t109 != ((uint8_t)17ULL)) goto mn108;
        gm_b16_8 t110 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 8));
        gm_l16 = t110;
        {
            {
                gm_b16_8 t113 = gm_l16;
                gm_l17 = t113;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t115 = gm_l18;
                    uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t117 = (t115 < t116);
                    if (!t117) goto lb114;
                    {
                        uint64_t t120 = gm_l18;
                        uint64_t t121 = (uintptr_t)t120;
                        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_toa2vhe3l4hfr t125 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t121 * 40));
                        F.l19 = t125;
                        {
                            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 0));
                            if (t128 != ((uint8_t)0ULL)) goto ie127;
                            gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 8));
                            gm_l20 = t129;
                            {
                                void * t132 = gm_l0;
                                gm_t88rcucg1b7uk t133 = gm_l20;
                                gm_tkn42u7na3t81 t134 = gm_f_inner_second_class_part_ufhro3f(t132, GM_CONV(gm_csguankko4lmhd, t133));
                                gm_tkn42u7na3t81 t135 = t134;
                                if (!((((uint8_t *)&t135))[4] != 0)) goto ie131;
                                gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t135));
                                gm_l21 = t136;
                                {
                                    gm_t88rcucg1b7uk t138 = gm_l21;
                                    gm_tkn42u7na3t81 t139;
                                    GM_ST(uint8_t, (uint8_t *)&t139 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t139 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t139), t138);
                                    ((uint8_t *)&t139)[4] = 1;
                                    uint8_t t140 = 0;
                                    F.ret = t139;
                                    goto gm_fret;
                                }
                                goto ix131;
ie131:;
ix131:;
                            }
                            goto ix127;
ie127:;
ix127:;
                        }
                        goto me123;
mn124:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 218, 13);
me123:;
                    }
lc114:;
                    {
                        uint64_t t142 = gm_l18;
                        uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                        uint64_t t145 = r143;
                        gm_l18 = t145;
                    }
                }
lb114:;
            }
        }
        {
            gm_tkn42u7na3t81 t146;
            memset(&t146, 0, sizeof t146);
            uint8_t t147 = 0;
            F.ret = t146;
            goto gm_fret;
        }
        goto me7;
mn108:;
ma14:;
        gm_tkn42u7na3t81 t149;
        memset(&t149, 0, sizeof t149);
        uint8_t t150 = 0;
        F.ret = t149;
        goto gm_fret;
        goto me7;
mn148:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 206, 5);
me7:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_instantiate_35r5pb2 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* id */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l10; /* g */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l18; /* kind */
    uint32_t gm_l19; /* id */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t88rcucg1b7uk gm_l22; /* a */
    uint32_t gm_l23; /* depth */
    GM_STACK_CHECK(&F, 1998u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 1264);
        gm_tiilmfd16v3fc t5;
        memset((uint8_t *)&t5 + 4, 0, 4);
        uint32_t t6 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t5 + 0, t6);
        gm_b16_8 t7 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t5 + 8, t7);
        gm_t9805qtonbsag t8 = gm_f_get_6qnso5a(t4, &t5);
        gm_t9805qtonbsag t9 = t8;
        if (!((((uint8_t *)&t9))[4] != 0)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&t9));
        gm_l4 = t10;
        {
            uint32_t t12 = gm_l4;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t14 = gm_l1;
        uint64_t t15 = (uint64_t)t14;
        uint64_t t16 = (uintptr_t)t15;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 344) + 0) + 8);
        if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srckg5f2c0p31ur, 282, 12);
        gm_tqqhoivahirr0 t19 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 344) + 0)) + t16 * 136));
        F.l5 = t19;
    }
    {
        gm_f_list_oalq03d((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 80));
            gm_b16_8 t22 = t21;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_l7 = t23;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l8;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l8;
                    uint64_t t31 = gm_l8;
                    uint64_t t32 = (uintptr_t)t31;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t32 * 4));
                    gm_tn68el8d14vku t35;
                    GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t35 + 0), t30);
                    GM_ST(uint32_t, ((uint8_t *)&t35 + 8), t34);
                    gm_tn68el8d14vku t36 = t35;
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&t36) + 0));
                    gm_l9 = t39;
                    uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&t36) + 8));
                    gm_l10 = t40;
                    {
                        void * t42 = (void *)((uint8_t *)&F.l6);
                        uint32_t t43 = gm_l10;
                        uint64_t t44 = gm_l9;
                        uint64_t t45 = (uintptr_t)t44;
                        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srckg5f2c0p31ur, 284, 72);
                        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t45 * 4));
                        gm_tplpbqf3duvn3 t48;
                        GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t43);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t48 + 4), t47);
                        gm_tftb1odc739t5 t49 = gm_f_push_h69n5fi(t42, GM_CONV(gm_cs7e9ppsd384bj, t48));
                        gm_tftb1odc739t5 t50 = t49;
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srckg5f2c0p31ur, 284, 43);
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 284, 5);
me37:;
                }
lc24:;
                {
                    uint64_t t53 = gm_l8;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l8 = t56;
                }
            }
lb24:;
        }
    }
    {
        gm_f_list_af74s0q((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_l12 = t58;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t60 = gm_l13;
                uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t62 = (t60 < t61);
                if (!t62) goto lb59;
                {
                    uint64_t t65 = gm_l13;
                    uint64_t t66 = (uintptr_t)t65;
                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_t5v0c8ia6a5mo t70 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t66 * 56));
                    F.l14 = t70;
                    {
                        uint8_t t73;
                        if ((((((uint8_t *)&F.l14) + 40))[4] != 0)) goto mn75;
                        t73 = ((uint8_t)0);
                        goto me74;
mn75:;
                        t73 = ((uint8_t)1);
                        goto me74;
mn76:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 287, 12);
me74:;
                        if (!t73) goto ie72;
                        {
                            goto lc59;
                        }
                        goto ix72;
ie72:;
ix72:;
                    }
                    {
                        gm_t5v0c8ia6a5mo t78 = F.l14;
                        F.l15 = t78;
                    }
                    {
                        void * t79 = gm_l0;
                        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        gm_b16_8 t82 = t81;
                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                        gm_t88rcucg1b7uk t84 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t79, t80, t83);
                        if (gm_unw.kind) {
                            goto gm_ll63;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16), t84);
                    }
                    {
                        void * t85 = (void *)((uint8_t *)&F.l11);
                        gm_tftb1odc739t5 t86 = gm_f_push_o8u01sh(t85, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l15)));
                        gm_tftb1odc739t5 t87 = t86;
                        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srckg5f2c0p31ur, 290, 9);
                        gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                    }
                    goto me68;
mn69:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 286, 5);
me68:;
                }
lc59:;
                {
                    uint64_t t90 = gm_l13;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l13 = t93;
                }
            }
gm_ll63:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
            }
            goto gm_lad;
lb59:;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t94 = (void *)((uint8_t *)&F.l16);
        gm_b16_8 t95 = gm_l2;
        gm_tftb1odc739t5 t96 = gm_f_extend_lfqasgq(t94, t95);
        gm_tftb1odc739t5 t97 = t96;
        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srckg5f2c0p31ur, 293, 5);
        gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
    }
    {
        memset((uint8_t *)&F.l17 + 28, 0, 4);
        memset((uint8_t *)&F.l17 + 60, 0, 4);
        memset((uint8_t *)&F.l17 + 129, 0, 7);
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&F.l17 + 0, t99);
        gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l17 + 16, t100);
        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
        GM_ST(uint64_t, (uint8_t *)&F.l17 + 32, t101);
        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&F.l17 + 40, t102);
        void * t103 = gm_l0;
        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t106 = t105;
        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
        gm_t88rcucg1b7uk t108 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t103, t104, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l17 + 56, t108);
        void * t109 = gm_l0;
        void * t110 = (void *)(((uint8_t *)&F.l5) + 64);
        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t112 = t111;
        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
        gm_tnpifv20irnph t114 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t109, t110, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&F.l17 + 64, t114);
        gm_b16_8 t115 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&F.l17 + 80, t115);
        gm_tiilmfd16v3fc t116;
        memset((uint8_t *)&t116 + 4, 0, 4);
        uint32_t t117 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t116 + 0, t117);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        gm_b16_8 t119 = t118;
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        GM_ST(gm_b16_8, (uint8_t *)&t116 + 8, t120);
        gm_tqu3m2l3n8mnc t121;
        GM_ST(uint8_t, (uint8_t *)&t121 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t121 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t121 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t121), t116);
        ((uint8_t *)&t121)[24] = 1;
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&F.l17 + 96, t121);
        GM_ST(uint8_t, (uint8_t *)&F.l17 + 128, ((uint8_t)0));
    }
    {
        void * t122 = gm_l0;
        gm_tl874t2atnlh3 t123 = gm_l3;
        gm_b0_1 t124 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t122, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t9qkh0rsooc8d t125;
        uint32_t t127 = gm_l1;
        uint64_t t128 = (uint64_t)t127;
        uint64_t t129 = (uintptr_t)t128;
        void * t130 = gm_l0;
        uint64_t t131 = GM_LD(uintptr_t, ((((uint8_t *)t130) + 384) + 0) + 8);
        if (t129 >= (uintptr_t)(t131)) gm_trap_index(t129, t131, gm_srckg5f2c0p31ur, 305, 27);
        uint8_t t132 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t130) + 384) + 0)) + t129 * 64) + 0) + 0) == ((uint8_t)2ULL)));
        if (!t132) goto ie126;
        gm_t9qkh0rsooc8d t134;
        GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)2ULL));
        gm_t9qkh0rsooc8d t135 = t134;
        t125 = t135;
        goto ix126;
ie126:;
        gm_t9qkh0rsooc8d t137;
        GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t138 = t137;
        t125 = t138;
ix126:;
        gm_l18 = t125;
    }
    {
        void * t139 = gm_l0;
        gm_tqqhoivahirr0 t140 = F.l17;
        gm_t9qkh0rsooc8d t141 = gm_l18;
        gm_tdgfduqjheqdm t142;
        gm_f_bodiless_def_u9kqo09((void *)&t142, t141);
        uint32_t t143 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t139, &t140, &t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t143;
    }
    {
        void * t144 = gm_l0;
        void * t145 = (void *)(((uint8_t *)t144) + 1264);
        gm_tiilmfd16v3fc t146;
        memset((uint8_t *)&t146 + 4, 0, 4);
        uint32_t t147 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t146 + 0, t147);
        gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        gm_b16_8 t149 = t148;
        gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
        GM_ST(gm_b16_8, (uint8_t *)&t146 + 8, t150);
        uint32_t t151 = gm_l19;
        gm_tftb1odc739t5 t152 = gm_f_put_f1iksvi(t145, &t146, t151);
        gm_tftb1odc739t5 t153 = t152;
        if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 1, gm_srckg5f2c0p31ur, 307, 5);
        gm_b0_1 t154 = GM_LD(gm_b0_1, (((uint8_t *)&t153) + 1));
    }
    {
        {
            gm_b16_8 t156 = gm_l2;
            gm_l20 = t156;
        }
        {
            gm_l21 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t158 = gm_l21;
                uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                uint8_t t160 = (t158 < t159);
                if (!t160) goto lb157;
                {
                    uint64_t t163 = gm_l21;
                    uint64_t t164 = (uintptr_t)t163;
                    uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t164 * 4));
                    gm_l22 = t168;
                    {
                        void * t171 = gm_l0;
                        void * t172 = (void *)(((uint8_t *)t171) + 8);
                        gm_t88rcucg1b7uk t173 = gm_l22;
                        uint8_t t174 = gm_f_has_param_u9t0efs(t172, GM_CONV(gm_csguankko4lmhd, t173));
                        if (!t174) goto ie170;
                        {
                            uint32_t t176 = gm_l19;
                            uint8_t t177 = 0;
                            F.ret = t176;
                            goto gm_fret;
                        }
                        goto ix170;
ie170:;
ix170:;
                    }
                    goto me166;
mn167:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 308, 5);
me166:;
                }
lc157:;
                {
                    uint64_t t179 = gm_l21;
                    uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                    uint64_t t182 = r180;
                    gm_l21 = t182;
                }
            }
lb157:;
        }
    }
    {
        void * t183 = gm_l0;
        uint32_t t184 = GM_LD(uint32_t, (((uint8_t *)t183) + 1360));
        uint32_t r185; bool o186 = __builtin_add_overflow(t184, ((uint32_t)1ULL), &r185);
        if (o186) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 311, 14);
        uint32_t t187 = r185;
        gm_l23 = t187;
    }
    {
        uint32_t t189 = gm_l23;
        uint32_t t190 = GM_LD(uint32_t, gm_cem22767mo4);
        uint8_t t191 = (t189 > t190);
        if (!t191) goto ie188;
        {
            void * t193 = gm_l0;
            gm_tl874t2atnlh3 t194 = gm_l3;
            uint32_t t195 = GM_LD(uint32_t, gm_cem22767mo4);
            gm_t88rcucg1b7uk t196;
            GM_ST(uint32_t, ((uint8_t *)&t196 + 0), t195);
            gm_tabb710d9mq2e t197;
            gm_f_format_hfu2i92((void *)&t197, (void *)NULL, cx, NULL, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t198 = t197;
            if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_srckg5f2c0p31ur, 313, 23);
            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
            gm_b16_8 t200 = t199;
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            gm_b0_1 t202 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t193, t194, t201);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint32_t t203 = gm_l19;
            uint8_t t204 = 0;
            F.ret = t203;
            goto gm_fret;
        }
        goto ix188;
ie188:;
ix188:;
    }
    {
        void * t205 = gm_l0;
        void * t206 = (void *)(((uint8_t *)t205) + 1320);
        gm_t0ut88vjdgrt1 t207;
        uint32_t t208 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t207 + 0, t208);
        uint32_t t209 = gm_l19;
        GM_ST(uint32_t, (uint8_t *)&t207 + 4, t209);
        gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        gm_b16_8 t211 = t210;
        gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&t211));
        GM_ST(gm_b16_8, (uint8_t *)&t207 + 8, t212);
        gm_tl874t2atnlh3 t213 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t207 + 24, t213);
        uint32_t t214 = gm_l23;
        GM_ST(uint32_t, (uint8_t *)&t207 + 36, t214);
        gm_tftb1odc739t5 t215 = gm_f_push_1es0np6(t206, &t207);
        gm_tftb1odc739t5 t216 = t215;
        if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 1, gm_srckg5f2c0p31ur, 316, 5);
        gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 1));
    }
    {
        uint32_t t218 = gm_l19;
        uint8_t t219 = 0;
        F.ret = t218;
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
inline __attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_int_key_dmcraup F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint64_t gm_l3; /* size */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* low */
    GM_STACK_CHECK(&F, 555u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_tbccvikg6b96e t4;
        gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbccvikg6b96e t5 = t4;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
        gm_l3 = t6;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tbci5ospk5nnn t7 = F.l2;
        F.l5 = t7;
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = gm_l3;
            gm_l6 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l7;
                uint64_t t12 = gm_l6;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l7;
                    uint64_t t17 = t16;
                    {
                        gm_tbci5ospk5nnn t21 = F.l5;
                        gm_tbci5ospk5nnn t22;
                        gm_f_from_u64_ud13q5v((void *)&t22, (void *)NULL, cx, NULL, ((uint64_t)255ULL));
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        gm_tbci5ospk5nnn t23;
                        gm_f_bit_and_3tgam8m((void *)&t23, (void *)NULL, cx, NULL, &t21, &t22);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        gm_tf5srh4mp4b3i t24 = gm_f_to_u64_m01t1da(&t23);
                        gm_tf5srh4mp4b3i t25 = t24;
                        uint64_t t26;
                        if (((((uint8_t *)&t25))[8] != 0)) {
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&t25) + 0));
                            t26 = t27;
                        } else {
                            t26 = ((uint64_t)0ULL);
                        }
                        gm_l8 = t26;
                    }
                    {
                        void * t28 = (void *)((uint8_t *)&F.l4);
                        uint64_t t29 = gm_l8;
                        uint8_t t30 = (uint8_t)t29;
                        gm_tftb1odc739t5 t31 = gm_f_push_t83rg2m(t28, t30);
                        gm_tftb1odc739t5 t32 = t31;
                        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srckg5f2c0p31ur, 462, 9);
                        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                    }
                    {
                        gm_f_shr_4jp99a9((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), ((uint64_t)8ULL));
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 460, 5);
me18:;
                }
lc10:;
                {
                    uint64_t t35 = gm_l7;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l7 = t38;
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
        gm_tcmkompbl09il t39;
        memset((uint8_t *)&t39 + 4, 0, 4);
        gm_t88rcucg1b7uk t40 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t39 + 0, t40);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t42 = t41;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 8, t43);
        gm_t428ue2u3hf5g t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t44 + 8), t39);
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
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_plain_n60iihm F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_b16_8 gm_l4; /* elems */
    uint32_t gm_l5; /* p */
    uint32_t gm_l6; /* id */
    GM_STACK_CHECK(&F, 368u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    gm_t53at80pfdus3 t5 = t4;
    uint8_t t6;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    switch (t15) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma8:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t19 != ((uint8_t)0ULL)) goto pa18;
    goto po17;
pa18:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t21 != ((uint8_t)1ULL)) goto pa20;
    goto po17;
pa20:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t23 != ((uint8_t)2ULL)) goto pa22;
    goto po17;
pa22:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t25 != ((uint8_t)3ULL)) goto pa24;
    goto po17;
pa24:;
    goto mn16;
po17:;
    t6 = ((uint8_t)1);
    goto me7;
mn16:;
ma9:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t29 != ((uint8_t)11ULL)) goto pa28;
    gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
    gm_l2 = t30;
    goto po27;
pa28:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t32 != ((uint8_t)20ULL)) goto pa31;
    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
    gm_l2 = t33;
    goto po27;
pa31:;
    goto mn26;
po27:;
    void * t34 = gm_l0;
    gm_t88rcucg1b7uk t35 = gm_l2;
    uint8_t t36 = gm_f_is_plain_n60iihm(t34, GM_CONV(gm_csguankko4lmhd, t35));
    t6 = t36;
    goto me7;
mn26:;
ma10:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t38 != ((uint8_t)12ULL)) goto mn37;
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
    gm_l4 = t39;
    void * t40 = gm_l0;
    gm_b16_8 t41 = gm_l4;
    uint8_t t42 = gm_f_all_plain_6k5r6vb(t40, t41);
    t6 = t42;
    goto me7;
mn37:;
ma11:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t44 != ((uint8_t)19ULL)) goto mn43;
    uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
    gm_l5 = t45;
    void * t46 = gm_l0;
    uint32_t t47 = gm_l5;
    gm_t9qkh0rsooc8d t48;
    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)6ULL));
    uint8_t t49 = gm_f_param_has_2r09oc7(t46, t47, t48);
    t6 = t49;
    goto me7;
mn43:;
ma12:;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t51 != ((uint8_t)17ULL)) goto mn50;
    uint32_t t52 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
    gm_l6 = t52;
    uint32_t t53 = gm_l6;
    uint64_t t54 = (uint64_t)t53;
    uint64_t t55 = (uintptr_t)t54;
    void * t56 = gm_l0;
    uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 1088) + 0) + 8);
    if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_srckg5f2c0p31ur, 156, 26);
    uint8_t t58;
    uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 1088) + 0)) + t55 * 184) + 24) + 0));
    if (t61 != ((uint8_t)0ULL)) goto mn60;
    gm_tpautr4br7h3l t62 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 1088) + 0)) + t55 * 184) + 24) + 8));
    F.l7 = t62;
    void * t63 = gm_l0;
    uint8_t t64 = gm_f_plain_fields_i69c54j(t63, &(*(gm_tpautr4br7h3l *)((uint8_t *)&F.l7)));
    t58 = t64;
    goto me59;
mn60:;
    t58 = ((uint8_t)0);
    goto me59;
mn65:;
    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 156, 20);
me59:;
    t6 = t58;
    goto me7;
mn50:;
ma13:;
    t6 = ((uint8_t)0);
    goto me7;
mn66:;
ma14:;
    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 151, 63);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_key_int_pqscuvn F;
    void * gm_l0; /* s */
    gm_t428ue2u3hf5g gm_l1; /* k */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 724u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l1) + 8) + 0));
        gm_l2 = t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l1) + 8) + 8));
        gm_l3 = t5;
        {
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 8);
            gm_t88rcucg1b7uk t10 = gm_l2;
            uint8_t t11 = gm_f_is_integer_8d4f9ek(t9, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t12 = (uint8_t)!t11;
            if (!t12) goto ie7;
            {
                gm_thrdpodrs8pgt t14;
                memset(&t14, 0, sizeof t14);
                uint8_t t15 = 0;
                F.ret = t14;
                goto gm_fret;
            }
            goto ix7;
ie7:;
ix7:;
        }
        {
            gm_f_zero_s25l990((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l5 = t16;
        }
        {
            for (;;) {
                uint64_t t18 = gm_l5;
                uint8_t t19 = (t18 > ((uint64_t)0ULL));
                if (!t19) goto lb17;
                {
                    uint64_t t22 = gm_l5;
                    uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
                    if (o24) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 447, 9);
                    uint64_t t25 = r23;
                    gm_l5 = t25;
                }
                {
                    gm_tbci5ospk5nnn t26;
                    gm_f_shl_qqecl81((void *)&t26, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), ((uint64_t)8ULL));
                    if (gm_unw.kind) {
                        goto gm_ll20;
                    }
                    uint64_t t27 = gm_l5;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srckg5f2c0p31ur, 448, 46);
                    uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t28 * 1));
                    uint64_t t31 = (uint64_t)t30;
                    gm_tbci5ospk5nnn t32;
                    gm_f_from_u64_ud13q5v((void *)&t32, (void *)NULL, cx, NULL, t31);
                    if (gm_unw.kind) {
                        goto gm_ll20;
                    }
                    gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), (void *)NULL, cx, NULL, &t26, &t32);
                    if (gm_unw.kind) {
                        goto gm_ll20;
                    }
                }
lc17:;
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
        {
            void * t34 = gm_l0;
            void * t35 = (void *)(((uint8_t *)t34) + 8);
            gm_t88rcucg1b7uk t36 = gm_l2;
            uint8_t t37 = gm_f_is_signed_integer_s66djm7(t35, GM_CONV(gm_csguankko4lmhd, t36));
            if (!t37) goto ie33;
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t39 = (t38 > ((uint64_t)0ULL));
            if (!t39) goto ie33;
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
            if (o42) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 450, 55);
            uint64_t t43 = r41;
            uint64_t t44 = (uintptr_t)t43;
            uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srckg5f2c0p31ur, 450, 53);
            uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t44 * 1));
            uint8_t t47 = (t46 >= ((uint8_t)128ULL));
            if (!t47) goto ie33;
            {
                gm_tbci5ospk5nnn t49 = F.l4;
                gm_tbci5ospk5nnn t50;
                gm_f_one_08qg1op((void *)&t50, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint64_t r52; bool o53 = __builtin_mul_overflow(t51, ((uint64_t)8ULL), &r52);
                if (o53) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 450, 106);
                uint64_t t54 = r52;
                uint64_t t55 = (uint64_t)t54;
                gm_tbci5ospk5nnn t56;
                gm_f_shl_qqecl81((void *)&t56, (void *)NULL, cx, NULL, &t50, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), (void *)NULL, cx, NULL, &t49, &t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            gm_tbci5ospk5nnn t57 = F.l4;
            gm_thrdpodrs8pgt t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t58 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t58 + 52, 0);
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t58), t57);
            ((uint8_t *)&t58)[48] = 1;
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_thrdpodrs8pgt t61;
            memset(&t61, 0, sizeof t61);
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_match_effects_0l1vbuc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_match_effects_0l1vbuc F;
    void * gm_l0; /* s */
    void * gm_l1; /* pat */
    void * gm_l2; /* actual */
    void * gm_l3; /* binds */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* p */
    gm_tqu3m2l3n8mnc gm_l7; /* found */
    int32_t gm_l8; /* count */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_tiilmfd16v3fc gm_l11; /* a */
    gm_tiilmfd16v3fc gm_l12; /* a */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t88rcucg1b7uk gm_l17; /* xt */
    gm_t88rcucg1b7uk gm_l18; /* yt */
    gm_tqu3m2l3n8mnc gm_l19; /* splice */
    int32_t gm_l20; /* splices */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tiilmfd16v3fc gm_l23; /* p */
    gm_tiilmfd16v3fc gm_l24; /* sp */
    gm_t88rcucg1b7uk gm_l25; /* t */
    uint32_t gm_l26; /* e */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_tiilmfd16v3fc gm_l30; /* a */
    uint8_t gm_l31; /* named */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tiilmfd16v3fc gm_l34; /* p */
    GM_STACK_CHECK(&F, 2163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_tiilmfd16v3fc t16 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 24));
                    gm_l6 = t16;
                    {
                        uint8_t t19 = 0;
                        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t21 = ((t20 == ((uint32_t)0ULL)));
                        if (!t21) {
                            uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)&gm_l6) + 8) + 8);
                            uint8_t t23 = ((t22 == ((uint64_t)0ULL)));
                            t19 = t23;
                        } else t19 = 1;
                        if (!t19) goto ie18;
                        {
                            goto lc5;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        gm_tqu3m2l3n8mnc t25;
                        memset(&t25, 0, sizeof t25);
                        gm_l7 = t25;
                    }
                    {
                        gm_l8 = ((int32_t)0LL);
                    }
                    {
                        {
                            void * t27 = gm_l2;
                            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 0));
                            gm_l9 = t28;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t30 = gm_l10;
                                uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t32 = (t30 < t31);
                                if (!t32) goto lb29;
                                {
                                    uint64_t t35 = gm_l10;
                                    uint64_t t36 = (uintptr_t)t35;
                                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_tiilmfd16v3fc t40 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t36 * 24));
                                    gm_l11 = t40;
                                    {
                                        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 0));
                                        uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                                        uint8_t t45 = ((t43 == t44));
                                        if (!t45) goto ie42;
                                        {
                                            gm_tiilmfd16v3fc t47 = gm_l11;
                                            gm_tqu3m2l3n8mnc t48;
                                            GM_ST(uint8_t, (uint8_t *)&t48 + 25, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t48 + 26, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t48 + 28, 0);
                                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t48), t47);
                                            ((uint8_t *)&t48)[24] = 1;
                                            gm_l7 = t48;
                                        }
                                        {
                                            int32_t t49 = gm_l8;
                                            int32_t r50; bool o51 = __builtin_add_overflow(t49, ((int32_t)1LL), &r50);
                                            if (o51) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 406, 17);
                                            int32_t t52 = r50;
                                            gm_l8 = t52;
                                        }
                                        goto ix42;
ie42:;
ix42:;
                                    }
                                    goto me38;
mn39:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 403, 9);
me38:;
                                }
lc29:;
                                {
                                    uint64_t t54 = gm_l10;
                                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                                    uint64_t t57 = r55;
                                    gm_l10 = t57;
                                }
                            }
lb29:;
                        }
                    }
                    {
                        int32_t t59 = gm_l8;
                        uint8_t t60 = (!(t59 == ((int32_t)1LL)));
                        if (!t60) goto ie58;
                        {
                            goto lc5;
                        }
                        goto ix58;
ie58:;
ix58:;
                    }
                    {
                        gm_tqu3m2l3n8mnc t62 = gm_l7;
                        gm_tqu3m2l3n8mnc t63 = t62;
                        gm_tiilmfd16v3fc t64;
                        if (((((uint8_t *)&t63))[24] != 0)) {
                            gm_tiilmfd16v3fc t65 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t63) + 0));
                            t64 = t65;
                        } else {
                            goto lc5;
                        }
                        gm_l12 = t64;
                    }
                    {
                        {
                            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                            gm_l13 = t67;
                        }
                        {
                            gm_l14 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t69 = gm_l14;
                                uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint8_t t71 = (t69 < t70);
                                if (!t71) goto lb68;
                                {
                                    gm_tdr8bh2bm3fc8 t74;
                                    uint64_t t75 = gm_l14;
                                    GM_ST(uint64_t, (uint8_t *)&t74 + 0, t75);
                                    uint64_t t76 = gm_l14;
                                    uint64_t t77 = (uintptr_t)t76;
                                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    memcpy((uint8_t *)&t74 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t77 * 40), 40);
                                    gm_tdr8bh2bm3fc8 t79 = t74;
                                    uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&t79) + 0));
                                    gm_l15 = t82;
                                    gm_toa2vhe3l4hfr t83 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)&t79) + 8));
                                    F.l16 = t83;
                                    {
                                        uint64_t t86 = gm_l15;
                                        uint64_t t87 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 8) + 8);
                                        uint8_t t88 = (t86 < t87);
                                        if (!t88) goto ie85;
                                        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                        if (t89 != ((uint8_t)0ULL)) goto ie85;
                                        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 8));
                                        gm_l17 = t90;
                                        uint64_t t91 = gm_l15;
                                        uint64_t t92 = (uintptr_t)t91;
                                        uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 8) + 8);
                                        if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_srckg5f2c0p31ur, 412, 67);
                                        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l12) + 8)) + t92 * 40) + 0));
                                        if (t94 != ((uint8_t)0ULL)) goto ie85;
                                        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l12) + 8)) + t92 * 40) + 8));
                                        gm_l18 = t95;
                                        {
                                            void * t97 = gm_l0;
                                            gm_t88rcucg1b7uk t98 = gm_l17;
                                            gm_t88rcucg1b7uk t99 = gm_l18;
                                            void * t100 = gm_l3;
                                            gm_b0_1 t101 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t97, t98, t99, t100);
                                            if (gm_unw.kind) {
                                                goto gm_ll72;
                                            }
                                        }
                                        goto ix85;
ie85:;
ix85:;
                                    }
                                    goto me80;
mn81:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 411, 9);
me80:;
                                }
lc68:;
                                {
                                    uint64_t t103 = gm_l14;
                                    uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                                    uint64_t t106 = r104;
                                    gm_l14 = t106;
                                }
                            }
gm_ll72:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb68; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc68; }
                            }
                            goto gm_ll9;
lb68:;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 399, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t108 = gm_l5;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l5 = t111;
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
        gm_tqu3m2l3n8mnc t112;
        memset(&t112, 0, sizeof t112);
        gm_l19 = t112;
    }
    {
        gm_l20 = ((int32_t)0LL);
    }
    {
        {
            void * t114 = gm_l1;
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)t114) + 0));
            gm_l21 = t115;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t117 = gm_l22;
                uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t119 = (t117 < t118);
                if (!t119) goto lb116;
                {
                    uint64_t t122 = gm_l22;
                    uint64_t t123 = (uintptr_t)t122;
                    uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_tiilmfd16v3fc t127 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t123 * 24));
                    gm_l23 = t127;
                    {
                        uint32_t t130 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 0));
                        uint8_t t131 = ((t130 == ((uint32_t)0ULL)));
                        if (!t131) goto ie129;
                        {
                            gm_tiilmfd16v3fc t133 = gm_l23;
                            gm_tqu3m2l3n8mnc t134;
                            GM_ST(uint8_t, (uint8_t *)&t134 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t134 + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&t134 + 28, 0);
                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t134), t133);
                            ((uint8_t *)&t134)[24] = 1;
                            gm_l19 = t134;
                        }
                        {
                            int32_t t135 = gm_l20;
                            int32_t r136; bool o137 = __builtin_add_overflow(t135, ((int32_t)1LL), &r136);
                            if (o137) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 420, 13);
                            int32_t t138 = r136;
                            gm_l20 = t138;
                        }
                        goto ix129;
ie129:;
ix129:;
                    }
                    goto me125;
mn126:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 417, 5);
me125:;
                }
lc116:;
                {
                    uint64_t t140 = gm_l22;
                    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                    uint64_t t143 = r141;
                    gm_l22 = t143;
                }
            }
lb116:;
        }
    }
    {
        int32_t t145 = gm_l20;
        uint8_t t146 = (!(t145 == ((int32_t)1LL)));
        if (!t146) goto ie144;
        {
            gm_b0_1 t148;
            uint8_t t149 = 0;
            F.ret = t148;
            goto gm_fret;
        }
        goto ix144;
ie144:;
ix144:;
    }
    {
        gm_tqu3m2l3n8mnc t150 = gm_l19;
        gm_tqu3m2l3n8mnc t151 = t150;
        gm_tiilmfd16v3fc t152;
        if (((((uint8_t *)&t151))[24] != 0)) {
            gm_tiilmfd16v3fc t153 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t151) + 0));
            t152 = t153;
        } else {
            gm_b0_1 t154;
            uint8_t t155 = 0;
            F.ret = t154;
            goto gm_fret;
        }
        gm_l24 = t152;
    }
    {
        uint64_t t157 = GM_LD(uintptr_t, (((uint8_t *)&gm_l24) + 8) + 8);
        uint8_t t158 = ((t157 == ((uint64_t)0ULL)));
        if (!t158) goto ie156;
        {
            gm_b0_1 t160;
            uint8_t t161 = 0;
            F.ret = t160;
            goto gm_fret;
        }
        goto ix156;
ie156:;
ix156:;
    }
    {
        uint64_t t163 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t164 = GM_LD(uintptr_t, (((uint8_t *)&gm_l24) + 8) + 8);
        if (t163 >= (uintptr_t)(t164)) gm_trap_index(t163, t164, gm_srckg5f2c0p31ur, 426, 17);
        uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l24) + 8)) + t163 * 40) + 0));
        if (t165 != ((uint8_t)0ULL)) goto ie162;
        gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l24) + 8)) + t163 * 40) + 8));
        gm_l25 = t166;
        {
            void * t169 = gm_l0;
            void * t170 = (void *)(((uint8_t *)t169) + 8);
            gm_t88rcucg1b7uk t171 = gm_l25;
            gm_t53at80pfdus3 t172;
            gm_f_kind_jcnt4hm((void *)&t172, t170, GM_CONV(gm_csguankko4lmhd, t171));
            gm_t53at80pfdus3 t173 = t172;
            uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&t173) + 0));
            if (t174 != ((uint8_t)19ULL)) goto ie168;
            uint32_t t175 = GM_LD(uint32_t, (((uint8_t *)&t173) + 8));
            gm_l26 = t175;
            {
                void * t178 = gm_l3;
                gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)t178) + 0));
                gm_b16_8 t180 = t179;
                gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                uint32_t t182 = gm_l26;
                gm_tkn42u7na3t81 t183 = gm_f_bound_to_5c11t0h(t181, t182);
                gm_tkn42u7na3t81 t184 = t183;
                uint8_t t185;
                if (((((uint8_t *)&t184))[4] != 0)) goto mn187;
                t185 = ((uint8_t)0);
                goto me186;
mn187:;
                t185 = ((uint8_t)1);
                goto me186;
mn188:;
                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 428, 8);
me186:;
                if (!t185) goto ie177;
                {
                    gm_b0_1 t190;
                    uint8_t t191 = 0;
                    F.ret = t190;
                    goto gm_fret;
                }
                goto ix177;
ie177:;
ix177:;
            }
            {
                gm_f_list_084fcd6((void *)&F.l27, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    void * t193 = gm_l2;
                    gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)t193) + 0));
                    gm_l28 = t194;
                }
                {
                    gm_l29 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t196 = gm_l29;
                        uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                        uint8_t t198 = (t196 < t197);
                        if (!t198) goto lb195;
                        {
                            uint64_t t201 = gm_l29;
                            uint64_t t202 = (uintptr_t)t201;
                            uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                            gm_tiilmfd16v3fc t206 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t202 * 24));
                            gm_l30 = t206;
                            {
                                gm_l31 = ((uint8_t)0);
                            }
                            {
                                {
                                    void * t209 = gm_l1;
                                    gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)t209) + 0));
                                    gm_l32 = t210;
                                }
                                {
                                    gm_l33 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t212 = gm_l33;
                                        uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                        uint8_t t214 = (t212 < t213);
                                        if (!t214) goto lb211;
                                        {
                                            uint64_t t217 = gm_l33;
                                            uint64_t t218 = (uintptr_t)t217;
                                            uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                            gm_tiilmfd16v3fc t222 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t218 * 24));
                                            gm_l34 = t222;
                                            {
                                                uint32_t t225 = GM_LD(uint32_t, (((uint8_t *)&gm_l34) + 0));
                                                uint8_t t226 = (!(t225 == ((uint32_t)0ULL)));
                                                if (!t226) goto ie224;
                                                uint32_t t227 = GM_LD(uint32_t, (((uint8_t *)&gm_l34) + 0));
                                                uint32_t t228 = GM_LD(uint32_t, (((uint8_t *)&gm_l30) + 0));
                                                uint8_t t229 = ((t227 == t228));
                                                if (!t229) goto ie224;
                                                {
                                                    gm_l31 = ((uint8_t)1);
                                                }
                                                goto ix224;
ie224:;
ix224:;
                                            }
                                            goto me220;
mn221:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 432, 9);
me220:;
                                        }
lc211:;
                                        {
                                            uint64_t t232 = gm_l33;
                                            uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                                            uint64_t t235 = r233;
                                            gm_l33 = t235;
                                        }
                                    }
lb211:;
                                }
                            }
                            {
                                uint8_t t237 = gm_l31;
                                uint8_t t238 = (uint8_t)!t237;
                                if (!t238) goto ie236;
                                {
                                    void * t240 = (void *)((uint8_t *)&F.l27);
                                    gm_tftb1odc739t5 t241 = gm_f_push_d1r4h19(t240, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l30)));
                                    gm_tftb1odc739t5 t242 = t241;
                                    if (((((uint8_t *)&t242))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t242) + 1, gm_srckg5f2c0p31ur, 435, 24);
                                    gm_b0_1 t243 = GM_LD(gm_b0_1, (((uint8_t *)&t242) + 1));
                                }
                                goto ix236;
ie236:;
ix236:;
                            }
                            goto me204;
mn205:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 430, 5);
me204:;
                        }
lc195:;
                        {
                            uint64_t t245 = gm_l29;
                            uint64_t r246; bool o247 = __builtin_add_overflow(t245, ((uint64_t)1ULL), &r246);
                            uint64_t t248 = r246;
                            gm_l29 = t248;
                        }
                    }
lb195:;
                }
            }
            {
                void * t249 = gm_l3;
                uint32_t t250 = gm_l26;
                void * t251 = gm_l0;
                void * t252 = (void *)(((uint8_t *)t251) + 8);
                gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                gm_b16_8 t254 = t253;
                gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                gm_tnpifv20irnph t256 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t255);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t53at80pfdus3 t257;
                memset(&t257, 0, sizeof t257);
                GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)24ULL));
                GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t257 + 8), t256);
                gm_t88rcucg1b7uk t258 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t252, &t257);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tplpbqf3duvn3 t259;
                GM_ST(uint32_t, ((uint8_t *)&t259 + 0), t250);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t259 + 4), t258);
                gm_tftb1odc739t5 t260 = gm_f_push_h69n5fi(t249, GM_CONV(gm_cs7e9ppsd384bj, t259));
                gm_tftb1odc739t5 t261 = t260;
                if (((((uint8_t *)&t261))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t261) + 1, gm_srckg5f2c0p31ur, 437, 5);
                gm_b0_1 t262 = GM_LD(gm_b0_1, (((uint8_t *)&t261) + 1));
            }
            goto ix168;
ie168:;
            {
                gm_b0_1 t264;
                uint8_t t265 = 0;
                F.ret = t264;
                goto gm_fret;
            }
ix168:;
        }
        goto ix162;
ie162:;
        {
            gm_b0_1 t267;
            uint8_t t268 = 0;
            F.ret = t267;
            goto gm_fret;
        }
ix162:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_match_fn_params_7to7qbv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_match_fn_params_7to7qbv F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    void * gm_l2; /* g */
    void * gm_l3; /* binds */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* p */
    GM_STACK_CHECK(&F, 383u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = gm_l2;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
        uint8_t t7 = (!(t4 == t6));
        if (!t7) goto ie2;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t12 = gm_l1;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 0));
            gm_l4 = t13;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l5;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l5;
                    uint64_t t21 = gm_l5;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t22 * 4));
                    gm_tlcm4lvu3g1a8 t25;
                    GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25 + 8), t24);
                    gm_tlcm4lvu3g1a8 t26 = t25;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&t26) + 0));
                    gm_l6 = t29;
                    gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t26) + 8));
                    gm_l7 = t30;
                    {
                        void * t32 = gm_l0;
                        gm_t88rcucg1b7uk t33 = gm_l7;
                        uint64_t t34 = gm_l6;
                        uint64_t t35 = (uintptr_t)t34;
                        void * t36 = gm_l2;
                        uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
                        if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srckg5f2c0p31ur, 385, 57);
                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 0)) + t35 * 4));
                        void * t39 = gm_l3;
                        gm_b0_1 t40 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t32, t33, t38, t39);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 385, 5);
me27:;
                }
lc14:;
                {
                    uint64_t t42 = gm_l5;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l5 = t45;
                }
            }
gm_ll18:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
            }
            goto gm_lad;
lb14:;
        }
    }
    {
        void * t46 = gm_l0;
        void * t47 = gm_l1;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 16));
        void * t49 = gm_l2;
        gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t49) + 16));
        void * t51 = gm_l3;
        gm_b0_1 t52 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t46, t48, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t53 = gm_l0;
        void * t54 = gm_l1;
        void * t55 = (void *)(((uint8_t *)t54) + 24);
        void * t56 = gm_l2;
        void * t57 = (void *)(((uint8_t *)t56) + 24);
        void * t58 = gm_l3;
        gm_b0_1 t59 = gm_f_match_effects_0l1vbuc((void *)NULL, cx, NULL, t53, t55, t57, t58);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_match_params_6vndkud F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* pat */
    gm_t88rcucg1b7uk gm_l2; /* actual */
    void * gm_l3; /* binds */
    void * gm_l4; /* tys */
    uint32_t gm_l6; /* p */
    gm_t88rcucg1b7uk gm_l7; /* len */
    gm_t88rcucg1b7uk gm_l8; /* x */
    gm_t88rcucg1b7uk gm_l9; /* len2 */
    gm_t88rcucg1b7uk gm_l10; /* y */
    uint64_t gm_l11; /* n */
    gm_t88rcucg1b7uk gm_l12; /* y */
    uint32_t gm_l13; /* p */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_t88rcucg1b7uk gm_l18; /* v */
    gm_t88rcucg1b7uk gm_l19; /* x */
    gm_t88rcucg1b7uk gm_l20; /* y */
    gm_t88rcucg1b7uk gm_l21; /* x */
    gm_t88rcucg1b7uk gm_l22; /* y */
    gm_t88rcucg1b7uk gm_l23; /* x */
    gm_t88rcucg1b7uk gm_l24; /* y */
    gm_t88rcucg1b7uk gm_l25; /* x */
    gm_t88rcucg1b7uk gm_l26; /* y */
    gm_t88rcucg1b7uk gm_l27; /* x */
    gm_t88rcucg1b7uk gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* e1 */
    gm_t88rcucg1b7uk gm_l30; /* o1 */
    gm_t88rcucg1b7uk gm_l31; /* e2 */
    gm_t88rcucg1b7uk gm_l32; /* o2 */
    uint32_t gm_l33; /* x */
    gm_b16_8 gm_l34; /* xs */
    uint32_t gm_l35; /* y */
    gm_b16_8 gm_l36; /* ys */
    uint8_t gm_l37; /* same */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    gm_t88rcucg1b7uk gm_l42; /* pt */
    gm_t88rcucg1b7uk gm_l43; /* qt */
    gm_b16_8 gm_l44; /* xs */
    gm_b16_8 gm_l45; /* ys */
    gm_b16_8 gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* i */
    gm_t88rcucg1b7uk gm_l49; /* x */
    gm_tnpifv20irnph gm_l55; /* x */
    gm_tnpifv20irnph gm_l56; /* y */
    GM_STACK_CHECK(&F, 2482u);
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
        gm_t88rcucg1b7uk t7 = gm_l2;
        uint8_t t8 = gm_f_has_infer_3o82a0b(t6, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) {
            void * t9 = gm_l4;
            gm_t88rcucg1b7uk t10 = gm_l2;
            uint8_t t11 = gm_f_is_error_8k27gd1(t9, GM_CONV(gm_csguankko4lmhd, t10));
            t5 = t11;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t15 = gm_l4;
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l5, t15, GM_CONV(gm_csguankko4lmhd, t16));
    }
    {
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_t53at80pfdus3 t19;
        gm_f_kind_jcnt4hm((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
        gm_t53at80pfdus3 t20 = t19;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        switch (t37) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma35; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma35; case ((uint8_t)7ULL): goto ma35; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma25; case ((uint8_t)11ULL): goto ma27; case ((uint8_t)12ULL): goto ma31; case ((uint8_t)13ULL): goto ma28; case ((uint8_t)14ULL): goto ma29; case ((uint8_t)15ULL): goto ma32; case ((uint8_t)16ULL): goto ma33; case ((uint8_t)17ULL): goto ma30; case ((uint8_t)18ULL): goto ma35; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma35; case ((uint8_t)22ULL): goto ma35; case ((uint8_t)23ULL): goto ma35; case ((uint8_t)24ULL): goto ma34; case ((uint8_t)25ULL): goto ma35; case ((uint8_t)26ULL): goto ma35; default: goto ma35; }
ma22:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t39 != ((uint8_t)19ULL)) goto mn38;
        uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&t20) + 8));
        gm_l6 = t40;
        void * t42 = gm_l3;
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 0));
        gm_b16_8 t44 = t43;
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        uint32_t t46 = gm_l6;
        gm_tkn42u7na3t81 t47 = gm_f_bound_to_5c11t0h(t45, t46);
        gm_tkn42u7na3t81 t48 = t47;
        uint8_t t49;
        if (((((uint8_t *)&t48))[4] != 0)) goto mn51;
        t49 = ((uint8_t)1);
        goto me50;
mn51:;
        t49 = ((uint8_t)0);
        goto me50;
mn52:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 335, 25);
me50:;
        if (!t49) goto ie41;
        {
            void * t54 = gm_l3;
            uint32_t t55 = gm_l6;
            gm_t88rcucg1b7uk t56 = gm_l2;
            gm_tplpbqf3duvn3 t57;
            GM_ST(uint32_t, ((uint8_t *)&t57 + 0), t55);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t57 + 4), t56);
            gm_tftb1odc739t5 t58 = gm_f_push_h69n5fi(t54, GM_CONV(gm_cs7e9ppsd384bj, t57));
            gm_tftb1odc739t5 t59 = t58;
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srckg5f2c0p31ur, 335, 60);
            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
        }
        goto ix41;
ie41:;
ix41:;
        goto me21;
mn38:;
ma23:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t62 != ((uint8_t)20ULL)) goto mn61;
        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 0));
        gm_l7 = t63;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 4));
        gm_l8 = t64;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        switch (t70) { case ((uint8_t)0ULL): goto ma68; case ((uint8_t)1ULL): goto ma68; case ((uint8_t)2ULL): goto ma68; case ((uint8_t)3ULL): goto ma68; case ((uint8_t)4ULL): goto ma68; case ((uint8_t)5ULL): goto ma68; case ((uint8_t)6ULL): goto ma68; case ((uint8_t)7ULL): goto ma68; case ((uint8_t)8ULL): goto ma68; case ((uint8_t)9ULL): goto ma68; case ((uint8_t)10ULL): goto ma68; case ((uint8_t)11ULL): goto ma67; case ((uint8_t)12ULL): goto ma68; case ((uint8_t)13ULL): goto ma68; case ((uint8_t)14ULL): goto ma68; case ((uint8_t)15ULL): goto ma68; case ((uint8_t)16ULL): goto ma68; case ((uint8_t)17ULL): goto ma68; case ((uint8_t)18ULL): goto ma68; case ((uint8_t)19ULL): goto ma68; case ((uint8_t)20ULL): goto ma66; case ((uint8_t)21ULL): goto ma68; case ((uint8_t)22ULL): goto ma68; case ((uint8_t)23ULL): goto ma68; case ((uint8_t)24ULL): goto ma68; case ((uint8_t)25ULL): goto ma68; case ((uint8_t)26ULL): goto ma68; default: goto ma68; }
ma66:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t72 != ((uint8_t)20ULL)) goto mn71;
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 0));
        gm_l9 = t73;
        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
        gm_l10 = t74;
        {
            void * t76 = gm_l0;
            gm_t88rcucg1b7uk t77 = gm_l7;
            gm_t88rcucg1b7uk t78 = gm_l9;
            void * t79 = gm_l3;
            gm_b0_1 t80 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t76, t77, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l8;
            gm_t88rcucg1b7uk t83 = gm_l10;
            void * t84 = gm_l3;
            gm_b0_1 t85 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t81, t82, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me65;
mn71:;
ma67:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t87 != ((uint8_t)11ULL)) goto mn86;
        uint64_t t88 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 8) + 0));
        gm_l11 = t88;
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 8));
        gm_l12 = t89;
        uint8_t t90 = GM_LD(uint8_t, (((((uint8_t *)&F.l5) + 8) + 12) + 0));
        if (t90 != ((uint8_t)0ULL)) goto mn86;
        {
            void * t93 = gm_l4;
            gm_t88rcucg1b7uk t94 = gm_l7;
            gm_t53at80pfdus3 t95;
            gm_f_kind_jcnt4hm((void *)&t95, t93, GM_CONV(gm_csguankko4lmhd, t94));
            gm_t53at80pfdus3 t96 = t95;
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t96) + 0));
            if (t97 != ((uint8_t)19ULL)) goto ie92;
            uint32_t t98 = GM_LD(uint32_t, (((uint8_t *)&t96) + 8));
            gm_l13 = t98;
            void * t99 = gm_l3;
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)t99) + 0));
            gm_b16_8 t101 = t100;
            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
            uint32_t t103 = gm_l13;
            gm_tkn42u7na3t81 t104 = gm_f_bound_to_5c11t0h(t102, t103);
            gm_tkn42u7na3t81 t105 = t104;
            uint8_t t106;
            if (((((uint8_t *)&t105))[4] != 0)) goto mn108;
            t106 = ((uint8_t)1);
            goto me107;
mn108:;
            t106 = ((uint8_t)0);
            goto me107;
mn109:;
            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 343, 51);
me107:;
            if (!t106) goto ie92;
            {
                gm_f_list_uvfu1k1((void *)&F.l14, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    gm_l15 = ((uint64_t)8ULL);
                }
                {
                    for (;;) {
                        uint64_t t113 = gm_l16;
                        uint64_t t114 = gm_l15;
                        uint8_t t115 = (t113 < t114);
                        if (!t115) goto lb112;
                        {
                            uint64_t t118 = gm_l16;
                            uint64_t t119 = t118;
                            uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&t119));
                            gm_l17 = t122;
                            {
                                void * t124 = (void *)((uint8_t *)&F.l14);
                                uint64_t t125 = gm_l11;
                                uint64_t t126 = gm_l17;
                                uint64_t r127; bool o128 = __builtin_mul_overflow(t126, ((uint64_t)8ULL), &r127);
                                if (o128) gm_trap("integer overflow in `*`", gm_srckg5f2c0p31ur, 345, 58);
                                uint64_t t129 = r127;
                                unsigned __int128 n130 = (unsigned __int128)t129;
                                if (n130 >= 64) gm_trap_shift((uint64_t)n130, "u64", gm_srckg5f2c0p31ur, 345, 49);
                                uint64_t t131 = (uint64_t)(t125 >> (int)n130);
                                uint8_t t132 = (uint8_t)t131;
                                gm_tftb1odc739t5 t133 = gm_f_push_t83rg2m(t124, t132);
                                gm_tftb1odc739t5 t134 = t133;
                                if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_srckg5f2c0p31ur, 345, 37);
                                gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
                            }
                            goto me120;
mn121:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 345, 21);
me120:;
                        }
lc112:;
                        {
                            uint64_t t137 = gm_l16;
                            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                            uint64_t t140 = r138;
                            gm_l16 = t140;
                        }
                    }
lb112:;
                }
            }
            {
                void * t141 = gm_l4;
                gm_tcmkompbl09il t142;
                memset((uint8_t *)&t142 + 4, 0, 4);
                void * t143 = gm_l4;
                gm_t88rcucg1b7uk t144 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t143, ((uint8_t)0), ((uint16_t)64ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t142 + 0, t144);
                gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                gm_b16_8 t146 = t145;
                gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                GM_ST(gm_b16_8, (uint8_t *)&t142 + 8, t147);
                gm_t428ue2u3hf5g t148;
                GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t148 + 8), t142);
                gm_t53at80pfdus3 t149;
                GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t149 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t149 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)21ULL));
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t149 + 8), t148);
                gm_t88rcucg1b7uk t150 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t141, &t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t150;
            }
            {
                void * t151 = gm_l3;
                uint32_t t152 = gm_l13;
                gm_t88rcucg1b7uk t153 = gm_l18;
                gm_tplpbqf3duvn3 t154;
                GM_ST(uint32_t, ((uint8_t *)&t154 + 0), t152);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t154 + 4), t153);
                gm_tftb1odc739t5 t155 = gm_f_push_h69n5fi(t151, GM_CONV(gm_cs7e9ppsd384bj, t154));
                gm_tftb1odc739t5 t156 = t155;
                if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_srckg5f2c0p31ur, 347, 21);
                gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
            }
            goto ix92;
ie92:;
ix92:;
        }
        {
            void * t158 = gm_l0;
            gm_t88rcucg1b7uk t159 = gm_l8;
            gm_t88rcucg1b7uk t160 = gm_l12;
            void * t161 = gm_l3;
            gm_b0_1 t162 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t158, t159, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me65;
mn86:;
ma68:;
        goto me65;
mn163:;
ma69:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 336, 32);
me65:;
        goto me21;
mn61:;
ma24:;
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t166 != ((uint8_t)8ULL)) goto mn165;
        gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 4));
        gm_l19 = t167;
        uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t169 != ((uint8_t)8ULL)) goto ie168;
        gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
        gm_l20 = t170;
        {
            void * t172 = gm_l0;
            gm_t88rcucg1b7uk t173 = gm_l19;
            gm_t88rcucg1b7uk t174 = gm_l20;
            void * t175 = gm_l3;
            gm_b0_1 t176 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t172, t173, t174, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix168;
ie168:;
ix168:;
        goto me21;
mn165:;
ma25:;
        uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t178 != ((uint8_t)10ULL)) goto mn177;
        gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 4));
        gm_l21 = t179;
        uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t181 != ((uint8_t)10ULL)) goto ie180;
        gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
        gm_l22 = t182;
        {
            void * t184 = gm_l0;
            gm_t88rcucg1b7uk t185 = gm_l21;
            gm_t88rcucg1b7uk t186 = gm_l22;
            void * t187 = gm_l3;
            gm_b0_1 t188 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t184, t185, t186, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix180;
ie180:;
ix180:;
        goto me21;
mn177:;
ma26:;
        uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t190 != ((uint8_t)9ULL)) goto mn189;
        gm_t88rcucg1b7uk t191 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 4));
        gm_l23 = t191;
        uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t193 != ((uint8_t)9ULL)) goto ie192;
        gm_t88rcucg1b7uk t194 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
        gm_l24 = t194;
        {
            void * t196 = gm_l0;
            gm_t88rcucg1b7uk t197 = gm_l23;
            gm_t88rcucg1b7uk t198 = gm_l24;
            void * t199 = gm_l3;
            gm_b0_1 t200 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t196, t197, t198, t199);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix192;
ie192:;
ix192:;
        goto me21;
mn189:;
ma27:;
        uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t202 != ((uint8_t)11ULL)) goto mn201;
        gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 8));
        gm_l25 = t203;
        uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t205 != ((uint8_t)11ULL)) goto ie204;
        gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 8));
        gm_l26 = t206;
        {
            void * t208 = gm_l0;
            gm_t88rcucg1b7uk t209 = gm_l25;
            gm_t88rcucg1b7uk t210 = gm_l26;
            void * t211 = gm_l3;
            gm_b0_1 t212 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t208, t209, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix204;
ie204:;
ix204:;
        goto me21;
mn201:;
ma28:;
        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t214 != ((uint8_t)13ULL)) goto mn213;
        gm_t88rcucg1b7uk t215 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t20) + 8));
        gm_l27 = t215;
        uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t217 != ((uint8_t)13ULL)) goto ie216;
        gm_t88rcucg1b7uk t218 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 8));
        gm_l28 = t218;
        {
            void * t220 = gm_l0;
            gm_t88rcucg1b7uk t221 = gm_l27;
            gm_t88rcucg1b7uk t222 = gm_l28;
            void * t223 = gm_l3;
            gm_b0_1 t224 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t220, t221, t222, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix216;
ie216:;
ix216:;
        goto me21;
mn213:;
ma29:;
        uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t226 != ((uint8_t)14ULL)) goto mn225;
        gm_t88rcucg1b7uk t227 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 0));
        gm_l29 = t227;
        gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t20) + 8) + 4));
        gm_l30 = t228;
        uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t230 != ((uint8_t)14ULL)) goto ie229;
        gm_t88rcucg1b7uk t231 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 0));
        gm_l31 = t231;
        gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l5) + 8) + 4));
        gm_l32 = t232;
        {
            void * t234 = gm_l0;
            gm_t88rcucg1b7uk t235 = gm_l29;
            gm_t88rcucg1b7uk t236 = gm_l31;
            void * t237 = gm_l3;
            gm_b0_1 t238 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t234, t235, t236, t237);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t239 = gm_l0;
            gm_t88rcucg1b7uk t240 = gm_l30;
            gm_t88rcucg1b7uk t241 = gm_l32;
            void * t242 = gm_l3;
            gm_b0_1 t243 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t239, t240, t241, t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix229;
ie229:;
ix229:;
        goto me21;
mn225:;
ma30:;
        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t245 != ((uint8_t)17ULL)) goto mn244;
        uint32_t t246 = GM_LD(uint32_t, ((((uint8_t *)&t20) + 8) + 0));
        gm_l33 = t246;
        gm_b16_8 t247 = GM_LD(gm_b16_8, ((((uint8_t *)&t20) + 8) + 8));
        gm_l34 = t247;
        uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t249 != ((uint8_t)17ULL)) goto ie248;
        uint32_t t250 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 8) + 0));
        gm_l35 = t250;
        gm_b16_8 t251 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 8) + 8));
        gm_l36 = t251;
        {
            uint8_t t253 = 0;
            void * t254 = gm_l4;
            uint32_t t255 = gm_l33;
            uint32_t t256 = gm_f_template_9o92cvv(t254, t255);
            void * t257 = gm_l4;
            uint32_t t258 = gm_l35;
            uint32_t t259 = gm_f_template_9o92cvv(t257, t258);
            uint8_t t260 = ((t256 == t259));
            if (!t260) {
                void * t261 = gm_l4;
                uint32_t t262 = gm_l33;
                uint32_t t263 = gm_l35;
                uint8_t t264 = gm_f_soa_const_view_od97rln(t261, t262, t263);
                t253 = t264;
            } else t253 = 1;
            gm_l37 = t253;
        }
        {
            uint8_t t266 = gm_l37;
            if (!t266) goto ie265;
            uint64_t t267 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
            uint64_t t268 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
            uint8_t t269 = ((t267 == t268));
            if (!t269) goto ie265;
            {
                {
                    gm_b16_8 t272 = gm_l34;
                    gm_l38 = t272;
                }
                {
                    gm_l39 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t274 = gm_l39;
                        uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                        uint8_t t276 = (t274 < t275);
                        if (!t276) goto lb273;
                        {
                            gm_tdr8bh2bm3fc8 t279;
                            uint64_t t280 = gm_l39;
                            GM_ST(uint64_t, (uint8_t *)&t279 + 0, t280);
                            uint64_t t281 = gm_l39;
                            uint64_t t282 = (uintptr_t)t281;
                            uint64_t t283 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                            memcpy((uint8_t *)&t279 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t282 * 40), 40);
                            gm_tdr8bh2bm3fc8 t284 = t279;
                            uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&t284) + 0));
                            gm_l40 = t287;
                            gm_toa2vhe3l4hfr t288 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)&t284) + 8));
                            F.l41 = t288;
                            {
                                uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&F.l41) + 0));
                                if (t291 != ((uint8_t)0ULL)) goto ie290;
                                gm_t88rcucg1b7uk t292 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l41) + 8));
                                gm_l42 = t292;
                                uint64_t t293 = gm_l40;
                                uint64_t t294 = (uintptr_t)t293;
                                uint64_t t295 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                if (t294 >= (uintptr_t)(t295)) gm_trap_index(t294, t295, gm_srckg5f2c0p31ur, 366, 56);
                                uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t294 * 40) + 0));
                                if (t296 != ((uint8_t)0ULL)) goto ie290;
                                gm_t88rcucg1b7uk t297 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t294 * 40) + 8));
                                gm_l43 = t297;
                                {
                                    void * t299 = gm_l0;
                                    gm_t88rcucg1b7uk t300 = gm_l42;
                                    gm_t88rcucg1b7uk t301 = gm_l43;
                                    void * t302 = gm_l3;
                                    gm_b0_1 t303 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t299, t300, t301, t302);
                                    if (gm_unw.kind) {
                                        goto gm_ll277;
                                    }
                                }
                                goto ix290;
ie290:;
ix290:;
                            }
                            goto me285;
mn286:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 365, 17);
me285:;
                        }
lc273:;
                        {
                            uint64_t t305 = gm_l39;
                            uint64_t r306; bool o307 = __builtin_add_overflow(t305, ((uint64_t)1ULL), &r306);
                            uint64_t t308 = r306;
                            gm_l39 = t308;
                        }
                    }
gm_ll277:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb273; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc273; }
                    }
                    goto gm_lad;
lb273:;
                }
            }
            goto ix265;
ie265:;
ix265:;
        }
        goto ix248;
ie248:;
ix248:;
        goto me21;
mn244:;
ma31:;
        uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t310 != ((uint8_t)12ULL)) goto mn309;
        gm_b16_8 t311 = GM_LD(gm_b16_8, ((((uint8_t *)&t20) + 8) + 0));
        gm_l44 = t311;
        uint8_t t313 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t313 != ((uint8_t)12ULL)) goto ie312;
        gm_b16_8 t314 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 8) + 0));
        gm_l45 = t314;
        uint64_t t315 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
        uint64_t t316 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
        uint8_t t317 = ((t315 == t316));
        if (!t317) goto ie312;
        {
            {
                gm_b16_8 t320 = gm_l44;
                gm_l46 = t320;
            }
            {
                gm_l47 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t322 = gm_l47;
                    uint64_t t323 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                    uint8_t t324 = (t322 < t323);
                    if (!t324) goto lb321;
                    {
                        uint64_t t327 = gm_l47;
                        uint64_t t328 = gm_l47;
                        uint64_t t329 = (uintptr_t)t328;
                        uint64_t t330 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                        gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l46)) + t329 * 4));
                        gm_tlcm4lvu3g1a8 t332;
                        GM_ST(uint32_t, (uint8_t *)&t332 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t332 + 0), t327);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t332 + 8), t331);
                        gm_tlcm4lvu3g1a8 t333 = t332;
                        uint64_t t336 = GM_LD(uint64_t, (((uint8_t *)&t333) + 0));
                        gm_l48 = t336;
                        gm_t88rcucg1b7uk t337 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t333) + 8));
                        gm_l49 = t337;
                        {
                            void * t339 = gm_l0;
                            gm_t88rcucg1b7uk t340 = gm_l49;
                            uint64_t t341 = gm_l48;
                            uint64_t t342 = (uintptr_t)t341;
                            uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                            if (t342 >= (uintptr_t)(t343)) gm_trap_index(t342, t343, gm_srckg5f2c0p31ur, 371, 59);
                            gm_t88rcucg1b7uk t344 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t342 * 4));
                            void * t345 = gm_l3;
                            gm_b0_1 t346 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t339, t340, t344, t345);
                            if (gm_unw.kind) {
                                goto gm_ll325;
                            }
                        }
                        goto me334;
mn335:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 371, 13);
me334:;
                    }
lc321:;
                    {
                        uint64_t t348 = gm_l47;
                        uint64_t r349; bool o350 = __builtin_add_overflow(t348, ((uint64_t)1ULL), &r349);
                        uint64_t t351 = r349;
                        gm_l47 = t351;
                    }
                }
gm_ll325:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb321; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc321; }
                }
                goto gm_lad;
lb321:;
            }
        }
        goto ix312;
ie312:;
ix312:;
        goto me21;
mn309:;
ma32:;
        uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t353 != ((uint8_t)15ULL)) goto mn352;
        gm_tddtjfggdat0d t354 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t20) + 8));
        F.l50 = t354;
        uint8_t t359 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t359 != ((uint8_t)15ULL)) goto pa358;
        gm_tddtjfggdat0d t360 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l5) + 8));
        F.l51 = t360;
        goto po357;
pa358:;
        uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t362 != ((uint8_t)16ULL)) goto pa361;
        gm_tddtjfggdat0d t363 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l5) + 8));
        F.l51 = t363;
        goto po357;
pa361:;
        goto mn356;
po357:;
        void * t364 = gm_l0;
        void * t365 = (void *)((uint8_t *)&F.l50);
        void * t366 = (void *)((uint8_t *)&F.l51);
        void * t367 = gm_l3;
        gm_b0_1 t368 = gm_f_match_fn_params_7to7qbv((void *)NULL, cx, NULL, t364, t365, t366, t367);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me355;
mn356:;
        goto me355;
mn369:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 373, 19);
me355:;
        goto me21;
mn352:;
ma33:;
        uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t372 != ((uint8_t)16ULL)) goto mn371;
        gm_tddtjfggdat0d t373 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t20) + 8));
        F.l53 = t373;
        uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t375 != ((uint8_t)16ULL)) goto ie374;
        gm_tddtjfggdat0d t376 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l5) + 8));
        F.l54 = t376;
        {
            void * t378 = gm_l0;
            void * t379 = (void *)((uint8_t *)&F.l53);
            void * t380 = (void *)((uint8_t *)&F.l54);
            void * t381 = gm_l3;
            gm_b0_1 t382 = gm_f_match_fn_params_7to7qbv((void *)NULL, cx, NULL, t378, t379, t380, t381);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix374;
ie374:;
ix374:;
        goto me21;
mn371:;
ma34:;
        uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t384 != ((uint8_t)24ULL)) goto mn383;
        gm_tnpifv20irnph t385 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t20) + 8));
        gm_l55 = t385;
        uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
        if (t387 != ((uint8_t)24ULL)) goto ie386;
        gm_tnpifv20irnph t388 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l5) + 8));
        gm_l56 = t388;
        {
            void * t390 = gm_l0;
            void * t391 = (void *)((uint8_t *)&gm_l55);
            void * t392 = (void *)((uint8_t *)&gm_l56);
            void * t393 = gm_l3;
            gm_b0_1 t394 = gm_f_match_effects_0l1vbuc((void *)NULL, cx, NULL, t390, t391, t392, t393);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix386;
ie386:;
ix386:;
        goto me21;
mn383:;
ma35:;
        goto me21;
mn395:;
ma36:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 334, 5);
me21:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_not_first_class_dve75oi F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* part */
    GM_STACK_CHECK(&F, 391u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = t3;
        gm_t88rcucg1b7uk t6 = t4;
        uint8_t t7 = (gm_eq9m6mvtmgde(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = gm_l1;
            gm_b16_8 t11 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t12;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
            gm_tabb710d9mq2e t13;
            gm_f_format_ii7qb9k((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srckg5f2c0p31ur, 194, 27);
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
            gm_b16_8 t16 = t15;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t19;
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l1;
        gm_b16_8 t22 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t22);
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l2;
        gm_b16_8 t25 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_9h7i0ht((void *)&t26, (void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srckg5f2c0p31ur, 195, 12);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_b16_8 t29 = t28;
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_has_2r09oc7 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l5; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint32_t t3 = gm_l1;
            uint64_t t4 = (uint64_t)t3;
            uint64_t t5 = (uintptr_t)t4;
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 1128) + 0) + 8);
            if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srckg5f2c0p31ur, 44, 14);
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 1128) + 0)) + t5 * 48) + 8) + 0));
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
                    gm_t9qkh0rsooc8d t20 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 1));
                    gm_l5 = t20;
                    {
                        gm_t9qkh0rsooc8d t23 = gm_l5;
                        gm_t9qkh0rsooc8d t24 = gm_l2;
                        uint8_t t25 = gm_f_implies_ibnta05(t23, t24);
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
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 44, 5);
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
extern __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_constraint_6f1aotv(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_constraint_6f1aotv F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sqri8tum3tpfm, 2) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t5;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[1] = 1;
    t1 = t5;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s7kt2bei71pgo, 3) == 0)) goto mn6;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t8;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[1] = 1;
    t1 = t8;
    goto me2;
mn6:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_snoj0t5lf2lht, 3) == 0)) goto mn9;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
    t1 = t11;
    goto me2;
mn9:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_seggohi50ng09, 3) == 0)) goto mn12;
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)3ULL));
    gm_taari1s0m51a7 t14;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[1] = 1;
    t1 = t14;
    goto me2;
mn12:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_se19nf1421e37, 3) == 0)) goto mn15;
    gm_t9qkh0rsooc8d t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)4ULL));
    gm_taari1s0m51a7 t17;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17), t16);
    ((uint8_t *)&t17)[1] = 1;
    t1 = t17;
    goto me2;
mn15:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s7s25m3nlrif2, 5) == 0)) goto mn18;
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)5ULL));
    gm_taari1s0m51a7 t20;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20), t19);
    ((uint8_t *)&t20)[1] = 1;
    t1 = t20;
    goto me2;
mn18:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sfv26hkjngt1i, 5) == 0)) goto mn21;
    gm_t9qkh0rsooc8d t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)6ULL));
    gm_taari1s0m51a7 t23;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[1] = 1;
    t1 = t23;
    goto me2;
mn21:;
    gm_taari1s0m51a7 t25;
    memset(&t25, 0, sizeof t25);
    t1 = t25;
    goto me2;
mn24:;
    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 3, 56);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plain_fields_i69c54j F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l5; /* ok */
    GM_STACK_CHECK(&F, 367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
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
                    gm_t0mglksrkfa5e t15 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 40));
                    F.l4 = t15;
                    {
                        uint8_t t18;
                        if ((((((uint8_t *)a1) + 40))[4] != 0)) goto mn20;
                        t18 = ((uint8_t)1);
                        goto me19;
mn20:;
                        t18 = ((uint8_t)0);
                        goto me19;
mn21:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 175, 12);
me19:;
                        if (!t18) goto ie17;
                        {
                            void * t24 = gm_l0;
                            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                            uint8_t t26 = gm_f_is_plain_n60iihm(t24, GM_CONV(gm_csguankko4lmhd, t25));
                            uint8_t t27 = (uint8_t)!t26;
                            if (!t27) goto ie23;
                            {
                                uint8_t t29 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix23;
ie23:;
ix23:;
                        }
                        goto ix17;
ie17:;
                        {
                            uint8_t t31 = 0;
                            uint8_t t32 = 0;
                            void * t33 = gm_l0;
                            void * t34 = (void *)(((uint8_t *)t33) + 8);
                            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                            uint8_t t36 = gm_f_is_integer_8d4f9ek(t34, GM_CONV(gm_csguankko4lmhd, t35));
                            if (!t36) {
                                void * t37 = gm_l0;
                                void * t38 = (void *)(((uint8_t *)t37) + 8);
                                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                                gm_t53at80pfdus3 t40;
                                gm_f_kind_jcnt4hm((void *)&t40, t38, GM_CONV(gm_csguankko4lmhd, t39));
                                gm_t53at80pfdus3 t41 = t40;
                                uint8_t t42 = ((GM_LD(uint8_t, ((uint8_t *)&t41) + 0) == ((uint8_t)4ULL)));
                                t32 = t42;
                            } else t32 = 1;
                            if (!t32) {
                                uint8_t t43 = 0;
                                void * t44 = gm_l0;
                                gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                                gm_tkn42u7na3t81 t46 = gm_f_bits_struct_ty_l1kjtap(t44, GM_CONV(gm_csguankko4lmhd, t45));
                                gm_tkn42u7na3t81 t47 = t46;
                                uint8_t t48;
                                if (((((uint8_t *)&t47))[4] != 0)) goto mn50;
                                t48 = ((uint8_t)0);
                                goto me49;
mn50:;
                                t48 = ((uint8_t)1);
                                goto me49;
mn51:;
                                gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 179, 75);
me49:;
                                if (t48) {
                                    void * t52 = gm_l0;
                                    gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 16));
                                    uint8_t t54 = gm_f_is_plain_n60iihm(t52, GM_CONV(gm_csguankko4lmhd, t53));
                                    t43 = t54;
                                } else t43 = 0;
                                t31 = t43;
                            } else t31 = 1;
                            gm_l5 = t31;
                        }
                        {
                            uint8_t t56 = gm_l5;
                            uint8_t t57 = (uint8_t)!t56;
                            if (!t57) goto ie55;
                            {
                                uint8_t t59 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix55;
ie55:;
ix55:;
                        }
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 174, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t61 = gm_l3;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l3 = t64;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t65 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_plain_fields_i69c54j_c(void * a0, gm_tpautr4br7h3l a1) { return gm_f_plain_fields_i69c54j(a0, &a1); }
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_satisfies_p0n2er8 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    uint32_t gm_l3; /* p */
    uint8_t gm_l4; /* is_float */
    GM_STACK_CHECK(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)19ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 8));
        gm_l3 = t9;
        {
            void * t11 = gm_l0;
            uint32_t t12 = gm_l3;
            gm_t9qkh0rsooc8d t13 = gm_l2;
            uint8_t t14 = gm_f_param_has_2r09oc7(t11, t12, t13);
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = (void *)(((uint8_t *)t16) + 8);
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_t53at80pfdus3 t19;
        gm_f_kind_jcnt4hm((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
        gm_t53at80pfdus3 t20 = t19;
        uint8_t t21;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t24 != ((uint8_t)3ULL)) goto mn23;
        t21 = ((uint8_t)1);
        goto me22;
mn23:;
        t21 = ((uint8_t)0);
        goto me22;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 69, 17);
me22:;
        gm_l4 = t21;
    }
    {
        uint8_t t26;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma29; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma31; case ((uint8_t)4ULL): goto ma32; case ((uint8_t)5ULL): goto ma33; case ((uint8_t)6ULL): goto ma34; default: goto ma35; }
ma28:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t38 != ((uint8_t)0ULL)) goto mn37;
        void * t39 = gm_l0;
        gm_t88rcucg1b7uk t40 = gm_l1;
        uint8_t t41 = gm_f_has_eq_iba53hs((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t26 = t41;
        goto me27;
mn37:;
ma29:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t43 != ((uint8_t)1ULL)) goto mn42;
        void * t44 = gm_l0;
        gm_t88rcucg1b7uk t45 = gm_l1;
        uint8_t t46 = gm_f_has_ord_1io9knm(t44, GM_CONV(gm_csguankko4lmhd, t45));
        t26 = t46;
        goto me27;
mn42:;
ma30:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t48 != ((uint8_t)2ULL)) goto mn47;
        uint8_t t49 = 0;
        void * t50 = gm_l0;
        void * t51 = (void *)(((uint8_t *)t50) + 8);
        gm_t88rcucg1b7uk t52 = gm_l1;
        uint8_t t53 = gm_f_is_integer_8d4f9ek(t51, GM_CONV(gm_csguankko4lmhd, t52));
        if (!t53) {
            uint8_t t54 = gm_l4;
            t49 = t54;
        } else t49 = 1;
        t26 = t49;
        goto me27;
mn47:;
ma31:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t56 != ((uint8_t)3ULL)) goto mn55;
        void * t57 = gm_l0;
        void * t58 = (void *)(((uint8_t *)t57) + 8);
        gm_t88rcucg1b7uk t59 = gm_l1;
        uint8_t t60 = gm_f_is_integer_8d4f9ek(t58, GM_CONV(gm_csguankko4lmhd, t59));
        t26 = t60;
        goto me27;
mn55:;
ma32:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t62 != ((uint8_t)4ULL)) goto mn61;
        uint8_t t63 = 0;
        void * t64 = gm_l0;
        void * t65 = (void *)(((uint8_t *)t64) + 8);
        gm_t88rcucg1b7uk t66 = gm_l1;
        uint8_t t67 = gm_f_is_signed_integer_s66djm7(t65, GM_CONV(gm_csguankko4lmhd, t66));
        if (!t67) {
            uint8_t t68 = gm_l4;
            t63 = t68;
        } else t63 = 1;
        t26 = t63;
        goto me27;
mn61:;
ma33:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t70 != ((uint8_t)5ULL)) goto mn69;
        uint8_t t71 = gm_l4;
        t26 = t71;
        goto me27;
mn69:;
ma34:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t73 != ((uint8_t)6ULL)) goto mn72;
        void * t74 = gm_l0;
        gm_t88rcucg1b7uk t75 = gm_l1;
        uint8_t t76 = gm_f_is_plain_n60iihm(t74, GM_CONV(gm_csguankko4lmhd, t75));
        t26 = t76;
        goto me27;
mn72:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 70, 12);
me27:;
        uint8_t t77 = 0;
        F.ret = t26;
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
inline __attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_scope_type_params_2di4dm5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* params */
    gm_t66j1im9ip128 gm_l2; /* before */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l6; /* pid */
    GM_STACK_CHECK(&F, 431u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l2, t2);
    }
    {
        {
            gm_b16_8 t4 = gm_l1;
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
                        gm_t9805qtonbsag t18 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l5) + 40));
                        gm_t9805qtonbsag t19 = t18;
                        uint32_t t20;
                        if (((((uint8_t *)&t19))[4] != 0)) {
                            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&t19) + 0));
                            t20 = t21;
                        } else {
                            goto lc5;
                        }
                        gm_l6 = t20;
                    }
                    {
                        uint32_t t23 = gm_l6;
                        uint64_t t24 = (uint64_t)t23;
                        uint64_t t25 = (uintptr_t)t24;
                        void * t26 = gm_l0;
                        uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 1128) + 0) + 8);
                        if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srckg5f2c0p31ur, 235, 12);
                        uint8_t t28;
                        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 1128) + 0)) + t25 * 48) + 0) + 0));
                        if (t31 != ((uint8_t)2ULL)) goto mn30;
                        t28 = ((uint8_t)1);
                        goto me29;
mn30:;
                        t28 = ((uint8_t)0);
                        goto me29;
mn32:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 235, 12);
me29:;
                        if (!t28) goto ie22;
                        {
                            void * t34 = gm_l0;
                            void * t35 = (void *)(((uint8_t *)t34) + 1528);
                            gm_tommf4j9upomc t36;
                            memset((uint8_t *)&t36 + 20, 0, 4);
                            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t37);
                            uint32_t t38 = gm_l6;
                            GM_ST(uint32_t, (uint8_t *)&t36 + 16, t38);
                            gm_tftb1odc739t5 t39 = gm_f_push_sp4si0f(t35, &t36);
                            gm_tftb1odc739t5 t40 = t39;
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srckg5f2c0p31ur, 236, 13);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        goto ix22;
ie22:;
                        {
                            void * t43 = gm_l0;
                            void * t44 = (void *)(((uint8_t *)t43) + 1488);
                            gm_tep2drsl9408b t45;
                            memset((uint8_t *)&t45 + 20, 0, 4);
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t45 + 0, t46);
                            void * t47 = gm_l0;
                            void * t48 = (void *)(((uint8_t *)t47) + 8);
                            uint32_t t49 = gm_l6;
                            gm_t88rcucg1b7uk t50 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t45 + 16, t50);
                            gm_tftb1odc739t5 t51 = gm_f_push_hv483nm(t44, &t45);
                            gm_tftb1odc739t5 t52 = t51;
                            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srckg5f2c0p31ur, 238, 13);
                            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                        }
ix22:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 233, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t55 = gm_l4;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l4 = t58;
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
        memmove(gm_out, ((uint8_t *)&gm_l2), 32);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_second_class_part_k6i54mk F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t11 != ((uint8_t)15ULL)) goto mn10;
        t8 = ((uint8_t)1);
        goto me9;
mn10:;
        t8 = ((uint8_t)0);
        goto me9;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 201, 8);
me9:;
        if (!t8) goto ie2;
        {
            gm_t88rcucg1b7uk t14 = gm_l1;
            gm_tkn42u7na3t81 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[4] = 1;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_tkn42u7na3t81 t19 = gm_f_inner_second_class_part_ufhro3f(t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t20 = 0;
        F.ret = t19;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_soa_const_view_od97rln(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unsatisfied_jnmtj8v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, uint32_t a3, uint32_t a4) {
    struct gm_fr_unsatisfied_jnmtj8v F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t9qkh0rsooc8d gm_l2; /* c */
    uint32_t gm_l3; /* p */
    uint32_t gm_l4; /* template */
    GM_STACK_CHECK(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_t0uuhaljsf410 t2;
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_b16_8 t5 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
        gm_t9qkh0rsooc8d t6 = gm_l2;
        gm_b16_8 t7 = gm_f_constraint_name_ombut3o(t6);
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t7);
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        uint32_t t10 = gm_l3;
        gm_b16_8 t11 = gm_f_param_name_rbnop5l(t9, t10);
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 32, t11);
        uint32_t t12 = gm_l4;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1088) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srckg5f2c0p31ur, 63, 126);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t14 * 184) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 48, t17);
        gm_tabb710d9mq2e t18;
        gm_f_format_980ugfo((void *)&t18, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srckg5f2c0p31ur, 63, 12);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        uint8_t t23 = 0;
        F.ret = t22;
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
extern __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep_c(void * a0, gm_t66j1im9ip128 a1) { gm_f_unscope_type_params_bofhhep(a0, &a1); }
__attribute__((aligned(4))) void gm_f_value_at_sthggua(void *gm_out, const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_at_sthggua F;
    gm_tkaapcii86udk gm_l0; /* p */
    uint64_t gm_l1; /* k */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* x */
    void * gm_l7; /* a */
    uint64_t gm_l8; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 850u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)a0) + 8) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l1;
        uint8_t t6 = (!(t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie2;
        {
            gm_tjoi8o798ne9k t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = GM_LD(void *, (((uint8_t *)a0) + 0));
        gm_t6evimt84lndq t11 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t10) + 0));
        F.l2 = t11;
    }
    {
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l3 = t13;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l4;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l4;
                    uint64_t t21 = gm_l4;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 8));
                    gm_t2lqu8jsj511d t25;
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 8), t24);
                    gm_t2lqu8jsj511d t26 = t25;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&t26) + 0));
                    gm_l5 = t29;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t26) + 8));
                    gm_l6 = t30;
                    {
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                        if (t33 != ((uint8_t)3ULL)) goto ie32;
                        void * t34 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                        gm_l7 = t34;
                        {
                            uint64_t t36;
                            uint64_t t38 = gm_l5;
                            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                            if (o40) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 590, 17);
                            uint64_t t41 = r39;
                            uint64_t t42 = GM_LD(uintptr_t, (((uint8_t *)a0) + 8) + 8);
                            uint8_t t43 = ((t41 == t42));
                            if (!t43) goto ie37;
                            uint64_t t45 = gm_l6;
                            uint64_t t46 = gm_l1;
                            uint64_t r47; bool o48 = __builtin_add_overflow(t45, t46, &r47);
                            if (o48) gm_trap("integer overflow in `+`", gm_srckg5f2c0p31ur, 590, 39);
                            uint64_t t49 = r47;
                            uint64_t t50 = t49;
                            t36 = t50;
                            goto ix37;
ie37:;
                            uint64_t t52 = gm_l6;
                            uint64_t t53 = t52;
                            t36 = t53;
ix37:;
                            gm_l8 = t36;
                        }
                        {
                            uint64_t t55 = gm_l8;
                            void * t56 = gm_l7;
                            uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)t56) + 0) + 8);
                            uint64_t t58 = (uint64_t)t57;
                            uint8_t t59 = (t55 >= t58);
                            if (!t59) goto ie54;
                            {
                                gm_tjoi8o798ne9k t61;
                                memset(&t61, 0, sizeof t61);
                                uint8_t t62 = 0;
                                F.ret = t61;
                                goto gm_fret;
                            }
                            goto ix54;
ie54:;
ix54:;
                        }
                        {
                            void * t63 = gm_l7;
                            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t63) + 0));
                            gm_b16_8 t65 = t64;
                            uint64_t t66 = gm_l8;
                            uint64_t t67 = (uint64_t)t66;
                            uint64_t t68 = (uintptr_t)t67;
                            gm_b16_8 t69 = t65;
                            uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&t69) + 8);
                            if (t68 >= (uintptr_t)(t70)) gm_trap_index(t68, t70, gm_srckg5f2c0p31ur, 592, 13);
                            gm_t6evimt84lndq t71 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t69)) + t68 * 48));
                            F.l2 = t71;
                        }
                        goto ix32;
ie32:;
                        {
                            gm_tjoi8o798ne9k t73;
                            memset(&t73, 0, sizeof t73);
                            uint8_t t74 = 0;
                            F.ret = t73;
                            goto gm_fret;
                        }
ix32:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 588, 5);
me27:;
                }
lc14:;
                {
                    uint64_t t76 = gm_l4;
                    uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                    uint64_t t79 = r77;
                    gm_l4 = t79;
                }
            }
lb14:;
        }
    }
    {
        gm_t6evimt84lndq t80 = F.l2;
        gm_tjoi8o798ne9k t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 52, 0);
        GM_ST(uint64_t, (uint8_t *)&t81 + 56, 0);
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t81), t80);
        ((uint8_t *)&t81)[48] = 1;
        uint8_t t82 = 0;
        F.ret = t81;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tjoi8o798ne9k gm_f_value_at_sthggua_c(gm_tkaapcii86udk a0, uint64_t a1) { gm_tjoi8o798ne9k gm_r; gm_f_value_at_sthggua(&gm_r, &a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_value_key_vki574i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_value_key_vki574i F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    unsigned __int128 gm_l5; /* bits */
    uint16_t gm_l6; /* b */
    uint32_t gm_l7; /* w */
    uint8_t gm_l9; /* b */
    uint32_t gm_l11; /* f */
    gm_to6glrduves6o gm_l12; /* sl */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* k */
    unsigned __int128 gm_l17; /* b */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    GM_STACK_CHECK(&F, 2019u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; default: goto ma11; }
ma5:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        unsigned __int128 t16 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l5 = t16;
        void * t18 = gm_l4;
        gm_t88rcucg1b7uk t19 = gm_l2;
        uint8_t t20 = gm_f_is_integer_8d4f9ek(t18, GM_CONV(gm_csguankko4lmhd, t19));
        if (!t20) goto ie17;
        {
            void * t22 = gm_l4;
            gm_t88rcucg1b7uk t23 = gm_l2;
            gm_t53at80pfdus3 t24;
            gm_f_kind_jcnt4hm((void *)&t24, t22, GM_CONV(gm_csguankko4lmhd, t23));
            gm_t53at80pfdus3 t25 = t24;
            uint32_t t26;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
            if (t29 != ((uint8_t)0ULL)) goto mn28;
            uint16_t t30 = GM_LD(uint16_t, ((((uint8_t *)&t25) + 8) + 2));
            gm_l6 = t30;
            uint16_t t31 = gm_l6;
            uint32_t t32 = (uint32_t)t31;
            t26 = t32;
            goto me27;
mn28:;
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
            void * t36 = (void *)(((uint8_t *)t35) + 88);
            uint32_t t37 = gm_f_ptr_bits_glps5i7(t36);
            t26 = t37;
            goto me27;
mn33:;
            gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 474, 22);
me27:;
            gm_l7 = t26;
        }
        {
            unsigned __int128 t38 = gm_l5;
            gm_f_from_u128_gl5fk0t((void *)&F.l8, (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t40 = gm_l4;
            gm_t88rcucg1b7uk t41 = gm_l2;
            uint8_t t42 = gm_f_is_signed_integer_s66djm7(t40, GM_CONV(gm_csguankko4lmhd, t41));
            if (!t42) goto ie39;
            uint32_t t43 = gm_l7;
            uint8_t t44 = (t43 > ((uint32_t)0ULL));
            if (!t44) goto ie39;
            unsigned __int128 t45 = gm_l5;
            uint32_t t46 = gm_l7;
            uint32_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint32_t)1ULL), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_srckg5f2c0p31ur, 479, 70);
            uint32_t t49 = r47;
            unsigned __int128 t50 = (unsigned __int128)t49;
            unsigned __int128 n51 = (unsigned __int128)t50;
            if (n51 >= 128) gm_trap_shift((uint64_t)n51, "u128", gm_srckg5f2c0p31ur, 479, 57);
            unsigned __int128 t52 = (unsigned __int128)(t45 >> (int)n51);
            unsigned __int128 t53 = (unsigned __int128)(t52 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
            uint8_t t54 = ((t53 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
            if (!t54) goto ie39;
            {
                gm_tbci5ospk5nnn t56 = F.l8;
                gm_tbci5ospk5nnn t57;
                gm_f_one_08qg1op((void *)&t57, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t58 = gm_l7;
                uint64_t t59 = (uint64_t)t58;
                gm_tbci5ospk5nnn t60;
                gm_f_shl_qqecl81((void *)&t60, (void *)NULL, cx, NULL, &t57, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), (void *)NULL, cx, NULL, &t56, &t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix39;
ie39:;
ix39:;
        }
        {
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l2;
            gm_t428ue2u3hf5g t63;
            gm_f_int_key_dmcraup((void *)&t63, (void *)NULL, cx, NULL, t61, t62, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trgmlgt2gmmjs t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 36, 0);
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t64), t63);
            ((uint8_t *)&t64)[32] = 1;
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
        goto me4;
mn14:;
ma6:;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t67 != ((uint8_t)2ULL)) goto mn66;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
        gm_l9 = t68;
        {
            gm_f_list_uvfu1k1((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t70 = (void *)((uint8_t *)&F.l10);
            uint8_t t71;
            uint8_t t73 = gm_l9;
            if (!t73) goto ie72;
            uint8_t t75 = ((uint8_t)1ULL);
            t71 = t75;
            goto ix72;
ie72:;
            uint8_t t77 = ((uint8_t)0ULL);
            t71 = t77;
ix72:;
            gm_tftb1odc739t5 t78 = gm_f_push_t83rg2m(t70, t71);
            gm_tftb1odc739t5 t79 = t78;
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_srckg5f2c0p31ur, 484, 13);
            gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
        }
        {
            gm_tcmkompbl09il t81;
            memset((uint8_t *)&t81 + 4, 0, 4);
            gm_t88rcucg1b7uk t82 = gm_l2;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t81 + 0, t82);
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t84 = t83;
            gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 8, t85);
            gm_t428ue2u3hf5g t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t86 + 8), t81);
            gm_trgmlgt2gmmjs t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 36, 0);
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t87), t86);
            ((uint8_t *)&t87)[32] = 1;
            uint8_t t88 = 0;
            F.ret = t87;
            goto gm_fret;
        }
        goto me4;
mn66:;
ma7:;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t90 != ((uint8_t)11ULL)) goto mn89;
        uint32_t t91 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
        gm_l11 = t91;
        gm_tiilmfd16v3fc t92;
        memset((uint8_t *)&t92 + 4, 0, 4);
        uint32_t t93 = gm_l11;
        GM_ST(uint32_t, (uint8_t *)&t92 + 0, t93);
        uint32_t t94 = gm_l11;
        uint64_t t95 = (uint64_t)t94;
        uint64_t t96 = (uintptr_t)t95;
        void * t97 = gm_l0;
        uint64_t t98 = GM_LD(uintptr_t, ((((uint8_t *)t97) + 344) + 0) + 8);
        if (t96 >= (uintptr_t)(t98)) gm_trap_index(t96, t98, gm_srckg5f2c0p31ur, 487, 33);
        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t97) + 344) + 0)) + t96 * 136) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t92 + 8, t99);
        gm_t428ue2u3hf5g t100;
        GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t100 + 8), t92);
        gm_trgmlgt2gmmjs t101;
        GM_ST(uint8_t, (uint8_t *)&t101 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t101 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t101 + 36, 0);
        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t101), t100);
        ((uint8_t *)&t101)[32] = 1;
        uint8_t t102 = 0;
        F.ret = t101;
        goto gm_fret;
        goto me4;
mn89:;
ma8:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t104 != ((uint8_t)6ULL)) goto mn103;
        gm_t428ue2u3hf5g t105;
        GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t105 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t105 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t105 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)2ULL));
        gm_trgmlgt2gmmjs t106;
        GM_ST(uint8_t, (uint8_t *)&t106 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t106 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t106 + 36, 0);
        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t106), t105);
        ((uint8_t *)&t106)[32] = 1;
        uint8_t t107 = 0;
        F.ret = t106;
        goto gm_fret;
        goto me4;
mn103:;
ma9:;
        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t109 != ((uint8_t)5ULL)) goto mn108;
        gm_to6glrduves6o t110 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l1) + 16));
        gm_l12 = t110;
        gm_t88rcucg1b7uk t112 = gm_l2;
        void * t113 = gm_l4;
        gm_t88rcucg1b7uk t114 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t115 = t112;
        gm_t88rcucg1b7uk t116 = t114;
        uint8_t t117 = (gm_eq9m6mvtmgde(((uint8_t *)&t115), ((uint8_t *)&t116)));
        if (!t117) goto ie111;
        {
            gm_f_list_uvfu1k1((void *)&F.l13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 24));
                gm_l14 = t120;
            }
            {
                for (;;) {
                    uint64_t t122 = gm_l15;
                    uint64_t t123 = gm_l14;
                    uint8_t t124 = (t122 < t123);
                    if (!t124) goto lb121;
                    {
                        uint64_t t127 = gm_l15;
                        uint64_t t128 = t127;
                        uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)&t128));
                        gm_l16 = t131;
                        {
                            uint64_t t134 = gm_l16;
                            gm_tjoi8o798ne9k t135;
                            gm_f_value_at_sthggua((void *)&t135, &(*(gm_tkaapcii86udk *)(((uint8_t *)&gm_l12) + 0)), t134);
                            gm_tjoi8o798ne9k t136 = t135;
                            if (!((((uint8_t *)&t136))[48] != 0)) goto ie133;
                            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&t136) + 0));
                            if (t137 != ((uint8_t)0ULL)) goto ie133;
                            unsigned __int128 t138 = GM_LD(unsigned __int128, (((uint8_t *)&t136) + 16));
                            gm_l17 = t138;
                            {
                                void * t140 = (void *)((uint8_t *)&F.l13);
                                unsigned __int128 t141 = gm_l17;
                                uint8_t t142 = (uint8_t)t141;
                                gm_tftb1odc739t5 t143 = gm_f_push_t83rg2m(t140, t142);
                                gm_tftb1odc739t5 t144 = t143;
                                if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srckg5f2c0p31ur, 496, 17);
                                gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
                            }
                            goto ix133;
ie133:;
                            {
                                {
                                    void * t148 = gm_l0;
                                    gm_tl874t2atnlh3 t149 = gm_l3;
                                    gm_b16_8 t150;
                                    GM_ST(void *, (uint8_t *)&t150, (void *)(gm_sicfped8s368m));
                                    GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(57));
                                    gm_b0_1 t151 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t148, t149, t150);
                                    if (gm_unw.kind) {
                                        goto gm_ll125;
                                    }
                                }
                                {
                                    gm_trgmlgt2gmmjs t152;
                                    memset(&t152, 0, sizeof t152);
                                    uint8_t t153 = 0;
                                    F.ret = t152;
                                    goto gm_fret;
                                }
                            }
ix133:;
                        }
                        goto me129;
mn130:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 491, 13);
me129:;
                    }
lc121:;
                    {
                        uint64_t t155 = gm_l15;
                        uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                        uint64_t t158 = r156;
                        gm_l15 = t158;
                    }
                }
gm_ll125:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb121; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc121; }
                }
                goto gm_lad;
lb121:;
            }
        }
        {
            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t160 = t159;
            gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
            gm_t428ue2u3hf5g t162;
            GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t162 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)3ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t162 + 8), t161);
            gm_trgmlgt2gmmjs t163;
            GM_ST(uint8_t, (uint8_t *)&t163 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t163 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t163 + 36, 0);
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t163), t162);
            ((uint8_t *)&t163)[32] = 1;
            uint8_t t164 = 0;
            F.ret = t163;
            goto gm_fret;
        }
        goto ix111;
ie111:;
ix111:;
        goto me4;
mn108:;
ma10:;
        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t168 != ((uint8_t)1ULL)) goto pa167;
        goto po166;
pa167:;
        uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t170 != ((uint8_t)3ULL)) goto pa169;
        goto po166;
pa169:;
        goto mn165;
po166:;
        {
            gm_f_list_uvfu1k1((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                void * t173 = gm_l0;
                gm_t88rcucg1b7uk t174 = gm_l2;
                gm_tbccvikg6b96e t175;
                gm_f_layout_a5ovnrm((void *)&t175, (void *)NULL, cx, NULL, t173, t174);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbccvikg6b96e t176 = t175;
                uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&t176) + 0));
                uint64_t t178 = (uint64_t)t177;
                gm_l19 = t178;
            }
            {
                for (;;) {
                    uint64_t t180 = gm_l20;
                    uint64_t t181 = gm_l19;
                    uint8_t t182 = (t180 < t181);
                    if (!t182) goto lb179;
                    {
                        uint64_t t185 = gm_l20;
                        uint64_t t186 = t185;
                        {
                            void * t190 = (void *)((uint8_t *)&F.l18);
                            gm_tftb1odc739t5 t191 = gm_f_push_t83rg2m(t190, ((uint8_t)0ULL));
                            gm_tftb1odc739t5 t192 = t191;
                            if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_srckg5f2c0p31ur, 503, 58);
                            gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
                        }
                        goto me187;
mn188:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 503, 13);
me187:;
                    }
lc179:;
                    {
                        uint64_t t195 = gm_l20;
                        uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                        uint64_t t198 = r196;
                        gm_l20 = t198;
                    }
                }
lb179:;
            }
        }
        {
            void * t200 = gm_l0;
            gm_t88rcucg1b7uk t201 = gm_l2;
            gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            uint8_t t203 = gm_f_canonical_bytes_trb762l((void *)NULL, cx, NULL, t200, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l1)), t201, t202);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t203) goto ie199;
            {
                gm_tcmkompbl09il t205;
                memset((uint8_t *)&t205 + 4, 0, 4);
                gm_t88rcucg1b7uk t206 = gm_l2;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t205 + 0, t206);
                gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                gm_b16_8 t208 = t207;
                gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                GM_ST(gm_b16_8, (uint8_t *)&t205 + 8, t209);
                gm_t428ue2u3hf5g t210;
                GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t210 + 8), t205);
                gm_trgmlgt2gmmjs t211;
                GM_ST(uint8_t, (uint8_t *)&t211 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t211 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t211 + 36, 0);
                GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t211), t210);
                ((uint8_t *)&t211)[32] = 1;
                uint8_t t212 = 0;
                F.ret = t211;
                goto gm_fret;
            }
            goto ix199;
ie199:;
ix199:;
        }
        goto me4;
mn165:;
ma11:;
        goto me4;
mn213:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srckg5f2c0p31ur, 472, 5);
me4:;
    }
    {
        void * t215 = gm_l0;
        gm_tl874t2atnlh3 t216 = gm_l3;
        gm_b16_8 t217;
        GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sd2s2op6i577j));
        GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(35));
        gm_b0_1 t218 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t215, t216, t217);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_trgmlgt2gmmjs t219;
        memset(&t219, 0, sizeof t219);
        uint8_t t220 = 0;
        F.ret = t219;
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

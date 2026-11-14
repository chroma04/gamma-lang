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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
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
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tmn7n0n9raou5 f0; uint8_t f1; uint8_t pe[3]; } gm_tbgiuvo2j5vcs;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { gm_tmn7n0n9raou5 f0; gm_t88rcucg1b7uk f1; } gm_tm7p68clol9c3;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_b16_8 v1; } f1; } gm_tvbp5g7fh5fh0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgiuvo2j5vcs) == 12 && _Alignof(gm_tbgiuvo2j5vcs) == 4 && offsetof(gm_tbgiuvo2j5vcs, f0) == 0 && offsetof(gm_tbgiuvo2j5vcs, f1) == 8, "layout of `?Coercion`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tm7p68clol9c3) == 12 && _Alignof(gm_tm7p68clol9c3) == 4 && offsetof(gm_tm7p68clol9c3, f0) == 0 && offsetof(gm_tm7p68clol9c3, f1) == 8, "layout of `Step`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvbp5g7fh5fh0) == 24 && _Alignof(gm_tvbp5g7fh5fh0) == 8 && offsetof(gm_tvbp5g7fh5fh0, f0) == 0 && offsetof(gm_tvbp5g7fh5fh0, f1) == 8, "layout of `LiftResult`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
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
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adds_const_e6d4bkb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_apply_steps_fd8cr97 {
    gm_frame h;
    void * ret;
};
struct gm_fr_base_coercion_t3tgsf0 {
    gm_frame h;
    gm_tddtjfggdat0d l25; /* a */
    gm_tddtjfggdat0d l26; /* b */
    gm_tbgiuvo2j5vcs ret;
};
struct gm_fr_coerce_sudc6c1 {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_tddtjfggdat0d l25; /* b */
    gm_tddtjfggdat0d l26; /* a */
    gm_tddtjfggdat0d l30; /* a */
    void * ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mcu2r7e {
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
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_6canco6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_pointer_coercion_a03b7p1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_shape_m0o6aok {
    gm_frame h;
    uint8_t ret;
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
struct gm_fr_src_array_ok_og10umr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_steps_for_768gb3b {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_steps_into_vts72l5 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b);
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_adds_const_e6d4bkb(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_base_coercion_t3tgsf0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_embeds_2jpqm45(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_dr0nbiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_held_temp_d00dm1p(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_lift_variant_ptko7np(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_list_6canco6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pointer_coercion_a03b7p1(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f21bjke(void * a0, gm_tm7p68clol9c3 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_joldpsp(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_shape_m0o6aok(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_set_contains_8v9ep9m(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_coerce_c57qnm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_steps_into_vts72l5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_sn1h26ucppu4i[110];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4vq75vtnd3bq[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
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
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    for (uintptr_t i = 0; i < n; i++) if (!gm_eq0iqiubfm7e(pa + i * 16, pb + i * 16)) return false;
    return true;
}
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq9m6mvtmgde(a, b);
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 60u);
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
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coercion_steps_ibqvhta F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_f_steps_for_768gb3b((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_complex_of_kh32tib F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0rphlcbno91v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tkn42u7na3t81 t4 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mcu2r7e F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
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
inline __attribute__((aligned(4))) void gm_f_list_6canco6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6canco6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_mcu2r7e();
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
inline __attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_src_array_ok_og10umr F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tnnjeerolos0a gm_l2; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 159u);
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
        if (t7 != ((uint8_t)11ULL)) goto ie2;
        gm_tnnjeerolos0a t8 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t6) + 8) + 12));
        gm_l2 = t8;
        {
            uint8_t t10;
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t13 != ((uint8_t)1ULL)) goto mn12;
            t10 = ((uint8_t)0);
            goto me11;
mn12:;
            t10 = ((uint8_t)1);
            goto me11;
mn14:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 273, 12);
me11:;
            uint8_t t15 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_adds_const_e6d4bkb(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adds_const_e6d4bkb F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sc */
    gm_t88rcucg1b7uk gm_l5; /* p */
    uint8_t gm_l6; /* tc */
    gm_t88rcucg1b7uk gm_l7; /* q */
    uint8_t gm_l8; /* sc */
    gm_t88rcucg1b7uk gm_l9; /* p */
    uint8_t gm_l10; /* tc */
    gm_t88rcucg1b7uk gm_l11; /* q */
    uint8_t gm_l12; /* sc */
    gm_t88rcucg1b7uk gm_l13; /* p */
    uint8_t gm_l14; /* tc */
    gm_t88rcucg1b7uk gm_l15; /* q */
    gm_t88rcucg1b7uk gm_l16; /* a */
    gm_t88rcucg1b7uk gm_l17; /* b */
    gm_t88rcucg1b7uk gm_l18; /* e1 */
    gm_t88rcucg1b7uk gm_l19; /* a */
    gm_t88rcucg1b7uk gm_l20; /* e2 */
    gm_t88rcucg1b7uk gm_l21; /* b */
    GM_STACK_CHECK(&F, 1029u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma9:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t18 != ((uint8_t)8ULL)) goto mn17;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l4 = t19;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l5 = t20;
        void * t22 = gm_l3;
        gm_t88rcucg1b7uk t23 = gm_l2;
        gm_t53at80pfdus3 t24;
        gm_f_kind_jcnt4hm((void *)&t24, t22, GM_CONV(gm_csguankko4lmhd, t23));
        gm_t53at80pfdus3 t25 = t24;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t26 != ((uint8_t)8ULL)) goto ie21;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)&t25) + 8) + 0));
        gm_l6 = t27;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t25) + 8) + 4));
        gm_l7 = t28;
        {
            uint8_t t30 = 0;
            uint8_t t31 = 0;
            gm_t88rcucg1b7uk t32 = gm_l5;
            gm_t88rcucg1b7uk t33 = gm_l7;
            gm_t88rcucg1b7uk t34 = t32;
            gm_t88rcucg1b7uk t35 = t33;
            uint8_t t36 = (gm_eq9m6mvtmgde(((uint8_t *)&t34), ((uint8_t *)&t35)));
            if (t36) {
                uint8_t t37 = gm_l6;
                t31 = t37;
            } else t31 = 0;
            if (t31) {
                uint8_t t38 = gm_l4;
                uint8_t t39 = (uint8_t)!t38;
                t30 = t39;
            } else t30 = 0;
            uint8_t t40 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
        goto me8;
mn17:;
ma10:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t42 != ((uint8_t)9ULL)) goto mn41;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l8 = t43;
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l9 = t44;
        void * t46 = gm_l3;
        gm_t88rcucg1b7uk t47 = gm_l2;
        gm_t53at80pfdus3 t48;
        gm_f_kind_jcnt4hm((void *)&t48, t46, GM_CONV(gm_csguankko4lmhd, t47));
        gm_t53at80pfdus3 t49 = t48;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t50 != ((uint8_t)9ULL)) goto ie45;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)&t49) + 8) + 0));
        gm_l10 = t51;
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t49) + 8) + 4));
        gm_l11 = t52;
        {
            uint8_t t54 = 0;
            uint8_t t55 = 0;
            gm_t88rcucg1b7uk t56 = gm_l9;
            gm_t88rcucg1b7uk t57 = gm_l11;
            gm_t88rcucg1b7uk t58 = t56;
            gm_t88rcucg1b7uk t59 = t57;
            uint8_t t60 = (gm_eq9m6mvtmgde(((uint8_t *)&t58), ((uint8_t *)&t59)));
            if (t60) {
                uint8_t t61 = gm_l10;
                t55 = t61;
            } else t55 = 0;
            if (t55) {
                uint8_t t62 = gm_l8;
                uint8_t t63 = (uint8_t)!t62;
                t54 = t63;
            } else t54 = 0;
            uint8_t t64 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
        goto me8;
mn41:;
ma11:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t66 != ((uint8_t)10ULL)) goto mn65;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l12 = t67;
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l13 = t68;
        void * t70 = gm_l3;
        gm_t88rcucg1b7uk t71 = gm_l2;
        gm_t53at80pfdus3 t72;
        gm_f_kind_jcnt4hm((void *)&t72, t70, GM_CONV(gm_csguankko4lmhd, t71));
        gm_t53at80pfdus3 t73 = t72;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
        if (t74 != ((uint8_t)10ULL)) goto ie69;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)&t73) + 8) + 0));
        gm_l14 = t75;
        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t73) + 8) + 4));
        gm_l15 = t76;
        {
            uint8_t t78 = 0;
            uint8_t t79 = 0;
            gm_t88rcucg1b7uk t80 = gm_l13;
            gm_t88rcucg1b7uk t81 = gm_l15;
            gm_t88rcucg1b7uk t82 = t80;
            gm_t88rcucg1b7uk t83 = t81;
            uint8_t t84 = (gm_eq9m6mvtmgde(((uint8_t *)&t82), ((uint8_t *)&t83)));
            if (t84) {
                uint8_t t85 = gm_l14;
                t79 = t85;
            } else t79 = 0;
            if (t79) {
                uint8_t t86 = gm_l12;
                uint8_t t87 = (uint8_t)!t86;
                t78 = t87;
            } else t78 = 0;
            uint8_t t88 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto ix69;
ie69:;
ix69:;
        goto me8;
mn65:;
ma12:;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t90 != ((uint8_t)13ULL)) goto mn89;
        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l16 = t91;
        void * t93 = gm_l3;
        gm_t88rcucg1b7uk t94 = gm_l2;
        gm_t53at80pfdus3 t95;
        gm_f_kind_jcnt4hm((void *)&t95, t93, GM_CONV(gm_csguankko4lmhd, t94));
        gm_t53at80pfdus3 t96 = t95;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t96) + 0));
        if (t97 != ((uint8_t)13ULL)) goto ie92;
        gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t96) + 8));
        gm_l17 = t98;
        {
            void * t100 = gm_l0;
            gm_t88rcucg1b7uk t101 = gm_l16;
            gm_t88rcucg1b7uk t102 = gm_l17;
            uint8_t t103 = gm_f_adds_const_e6d4bkb(t100, GM_CONV(gm_csguankko4lmhd, t101), GM_CONV(gm_csguankko4lmhd, t102));
            uint8_t t104 = 0;
            F.ret = t103;
            goto gm_fret;
        }
        goto ix92;
ie92:;
ix92:;
        goto me8;
mn89:;
ma13:;
        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t106 != ((uint8_t)14ULL)) goto mn105;
        gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 0));
        gm_l18 = t107;
        gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l19 = t108;
        void * t110 = gm_l3;
        gm_t88rcucg1b7uk t111 = gm_l2;
        gm_t53at80pfdus3 t112;
        gm_f_kind_jcnt4hm((void *)&t112, t110, GM_CONV(gm_csguankko4lmhd, t111));
        gm_t53at80pfdus3 t113 = t112;
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&t113) + 0));
        if (t114 != ((uint8_t)14ULL)) goto ie109;
        gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t113) + 8) + 0));
        gm_l20 = t115;
        gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t113) + 8) + 4));
        gm_l21 = t116;
        {
            uint8_t t118 = 0;
            gm_t88rcucg1b7uk t119 = gm_l18;
            gm_t88rcucg1b7uk t120 = gm_l20;
            gm_t88rcucg1b7uk t121 = t119;
            gm_t88rcucg1b7uk t122 = t120;
            uint8_t t123 = (gm_eq9m6mvtmgde(((uint8_t *)&t121), ((uint8_t *)&t122)));
            if (t123) {
                void * t124 = gm_l0;
                gm_t88rcucg1b7uk t125 = gm_l19;
                gm_t88rcucg1b7uk t126 = gm_l21;
                uint8_t t127 = gm_f_adds_const_e6d4bkb(t124, GM_CONV(gm_csguankko4lmhd, t125), GM_CONV(gm_csguankko4lmhd, t126));
                t118 = t127;
            } else t118 = 0;
            uint8_t t128 = 0;
            F.ret = t118;
            goto gm_fret;
        }
        goto ix109;
ie109:;
ix109:;
        goto me8;
mn105:;
ma14:;
        goto me8;
mn129:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 198, 5);
me8:;
    }
    {
        uint8_t t131 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_apply_steps_fd8cr97 F;
    void * gm_l0; /* e0 */
    gm_b16_8 gm_l1; /* steps */
    void * gm_l2; /* e */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tm7p68clol9c3 gm_l5; /* s */
    GM_STACK_CHECK(&F, 314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_l2 = t2;
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
                    gm_tm7p68clol9c3 t16 = GM_LD(gm_tm7p68clol9c3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 12));
                    gm_l5 = t16;
                    {
                        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                        gm_tmn7n0n9raou5 t19 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l5) + 0));
                        void * t20 = gm_l2;
                        gm_t8jsc4n9vp2ib t21;
                        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t21 + 0), t19);
                        GM_ST(void *, ((uint8_t *)&t21 + 8), t20);
                        gm_teti411eujvf4 t22;
                        memset(&t22, 0, sizeof t22);
                        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)32ULL));
                        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t22 + 8), t21);
                        void * t23 = gm_l2;
                        gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t23) + 72));
                        void * t25 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t18, &t22, t24);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_l2 = t25;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 136, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
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
        void * t31 = gm_l2;
        uint8_t t32 = 0;
        F.ret = t31;
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
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_base_coercion_t3tgsf0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_base_coercion_t3tgsf0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sa */
    uint16_t gm_l5; /* ba */
    uint8_t gm_l6; /* sb */
    uint16_t gm_l7; /* bb */
    uint8_t gm_l8; /* ok */
    uint16_t gm_l9; /* a */
    uint16_t gm_l10; /* b */
    gm_b16_8 gm_l11; /* a */
    gm_b16_8 gm_l12; /* na */
    gm_b16_8 gm_l13; /* b */
    gm_b16_8 gm_l14; /* nb */
    uint8_t gm_l15; /* same */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_t88rcucg1b7uk gm_l19; /* a */
    gm_t88rcucg1b7uk gm_l20; /* b */
    gm_t88rcucg1b7uk gm_l21; /* e1 */
    gm_t88rcucg1b7uk gm_l22; /* a */
    gm_t88rcucg1b7uk gm_l23; /* e2 */
    gm_t88rcucg1b7uk gm_l24; /* b */
    uint8_t gm_l27; /* same */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_tiilmfd16v3fc gm_l33; /* x */
    gm_t88rcucg1b7uk gm_l34; /* pointee */
    GM_STACK_CHECK(&F, 2961u);
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
        void * t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t18) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma16; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma16; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma16; case ((uint8_t)20ULL): goto ma16; case ((uint8_t)21ULL): goto ma16; case ((uint8_t)22ULL): goto ma16; case ((uint8_t)23ULL): goto ma16; case ((uint8_t)24ULL): goto ma16; case ((uint8_t)25ULL): goto ma16; case ((uint8_t)26ULL): goto ma16; default: goto ma16; }
ma9:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t20 != ((uint8_t)5ULL)) goto mn19;
        gm_tmn7n0n9raou5 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)5ULL));
        gm_tbgiuvo2j5vcs t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t22), t21);
        ((uint8_t *)&t22)[8] = 1;
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
        goto me8;
mn19:;
ma10:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t25 != ((uint8_t)0ULL)) goto mn24;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l4 = t26;
        uint16_t t27 = GM_LD(uint16_t, ((((uint8_t *)&t7) + 8) + 2));
        gm_l5 = t27;
        void * t28 = gm_l3;
        gm_t88rcucg1b7uk t29 = gm_l2;
        gm_t53at80pfdus3 t30;
        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
        gm_t53at80pfdus3 t31 = t30;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        switch (t38) { case ((uint8_t)0ULL): goto ma33; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma35; case ((uint8_t)3ULL): goto ma36; case ((uint8_t)4ULL): goto ma36; case ((uint8_t)5ULL): goto ma36; case ((uint8_t)6ULL): goto ma36; case ((uint8_t)7ULL): goto ma36; case ((uint8_t)8ULL): goto ma36; case ((uint8_t)9ULL): goto ma36; case ((uint8_t)10ULL): goto ma36; case ((uint8_t)11ULL): goto ma36; case ((uint8_t)12ULL): goto ma36; case ((uint8_t)13ULL): goto ma36; case ((uint8_t)14ULL): goto ma36; case ((uint8_t)15ULL): goto ma36; case ((uint8_t)16ULL): goto ma36; case ((uint8_t)17ULL): goto ma36; case ((uint8_t)18ULL): goto ma36; case ((uint8_t)19ULL): goto ma36; case ((uint8_t)20ULL): goto ma36; case ((uint8_t)21ULL): goto ma36; case ((uint8_t)22ULL): goto ma36; case ((uint8_t)23ULL): goto ma36; case ((uint8_t)24ULL): goto ma36; case ((uint8_t)25ULL): goto ma36; case ((uint8_t)26ULL): goto ma36; default: goto ma36; }
ma33:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t40 != ((uint8_t)0ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&t31) + 8) + 0));
        gm_l6 = t41;
        uint16_t t42 = GM_LD(uint16_t, ((((uint8_t *)&t31) + 8) + 2));
        gm_l7 = t42;
        {
            uint8_t t44;
            uint8_t t46 = gm_l4;
            uint8_t t47 = gm_l6;
            uint8_t t48 = ((t46 == t47));
            if (!t48) goto ie45;
            uint16_t t50 = gm_l7;
            uint16_t t51 = gm_l5;
            uint8_t t52 = (t50 >= t51);
            uint8_t t53 = t52;
            t44 = t53;
            goto ix45;
ie45:;
            uint8_t t54;
            uint8_t t56 = gm_l4;
            uint8_t t57 = (uint8_t)!t56;
            if (!t57) goto ie55;
            uint16_t t59 = gm_l7;
            uint16_t t60 = gm_l5;
            uint8_t t61 = (t59 > t60);
            uint8_t t62 = t61;
            t54 = t62;
            goto ix55;
ie55:;
            uint8_t t64 = ((uint8_t)0);
            t54 = t64;
ix55:;
            t44 = t54;
ix45:;
            gm_l8 = t44;
        }
        {
            gm_tbgiuvo2j5vcs t65;
            uint8_t t67 = gm_l8;
            if (!t67) goto ie66;
            gm_tmn7n0n9raou5 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
            gm_tmn7n0n9raou5 t70 = t69;
            gm_tbgiuvo2j5vcs t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t71), t70);
            ((uint8_t *)&t71)[8] = 1;
            t65 = t71;
            goto ix66;
ie66:;
            gm_tbgiuvo2j5vcs t73;
            memset(&t73, 0, sizeof t73);
            gm_tbgiuvo2j5vcs t74 = t73;
            t65 = t74;
ix66:;
            uint8_t t75 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto me32;
mn39:;
ma34:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t77 != ((uint8_t)1ULL)) goto mn76;
        gm_tbgiuvo2j5vcs t78;
        uint8_t t80 = gm_l4;
        uint8_t t81 = (uint8_t)!t80;
        if (!t81) goto ie79;
        uint16_t t82 = gm_l5;
        uint8_t t83 = (t82 <= ((uint16_t)32ULL));
        if (!t83) goto ie79;
        gm_tmn7n0n9raou5 t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)0ULL));
        gm_tmn7n0n9raou5 t86 = t85;
        gm_tbgiuvo2j5vcs t87;
        GM_ST(uint8_t, (uint8_t *)&t87 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t87 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t87), t86);
        ((uint8_t *)&t87)[8] = 1;
        t78 = t87;
        goto ix79;
ie79:;
        gm_tbgiuvo2j5vcs t89;
        memset(&t89, 0, sizeof t89);
        gm_tbgiuvo2j5vcs t90 = t89;
        t78 = t90;
ix79:;
        uint8_t t91 = 0;
        F.ret = t78;
        goto gm_fret;
        goto me32;
mn76:;
ma35:;
        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t93 != ((uint8_t)2ULL)) goto mn92;
        gm_tbgiuvo2j5vcs t94;
        uint8_t t96 = 0;
        uint8_t t97 = 0;
        uint8_t t98 = gm_l4;
        uint8_t t99 = (uint8_t)!t98;
        if (t99) {
            uint16_t t100 = gm_l5;
            uint8_t t101 = (t100 <= ((uint16_t)31ULL));
            t97 = t101;
        } else t97 = 0;
        if (!t97) {
            uint8_t t102 = 0;
            uint8_t t103 = gm_l4;
            if (t103) {
                uint16_t t104 = gm_l5;
                uint8_t t105 = (t104 <= ((uint16_t)32ULL));
                t102 = t105;
            } else t102 = 0;
            t96 = t102;
        } else t96 = 1;
        if (!t96) goto ie95;
        gm_tmn7n0n9raou5 t107;
        GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)0ULL));
        gm_tmn7n0n9raou5 t108 = t107;
        gm_tbgiuvo2j5vcs t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t109), t108);
        ((uint8_t *)&t109)[8] = 1;
        t94 = t109;
        goto ix95;
ie95:;
        gm_tbgiuvo2j5vcs t111;
        memset(&t111, 0, sizeof t111);
        gm_tbgiuvo2j5vcs t112 = t111;
        t94 = t112;
ix95:;
        uint8_t t113 = 0;
        F.ret = t94;
        goto gm_fret;
        goto me32;
mn92:;
ma36:;
        goto me32;
mn114:;
ma37:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 145, 25);
me32:;
        goto me8;
mn24:;
ma11:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t117 != ((uint8_t)3ULL)) goto mn116;
        uint16_t t118 = GM_LD(uint16_t, (((uint8_t *)&t7) + 8));
        gm_l9 = t118;
        void * t120 = gm_l3;
        gm_t88rcucg1b7uk t121 = gm_l2;
        gm_t53at80pfdus3 t122;
        gm_f_kind_jcnt4hm((void *)&t122, t120, GM_CONV(gm_csguankko4lmhd, t121));
        gm_t53at80pfdus3 t123 = t122;
        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&t123) + 0));
        if (t124 != ((uint8_t)3ULL)) goto ie119;
        uint16_t t125 = GM_LD(uint16_t, (((uint8_t *)&t123) + 8));
        gm_l10 = t125;
        {
            gm_tbgiuvo2j5vcs t127;
            uint16_t t129 = gm_l10;
            uint16_t t130 = gm_l9;
            uint8_t t131 = (t129 > t130);
            if (!t131) goto ie128;
            gm_tmn7n0n9raou5 t133;
            GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)1ULL));
            gm_tmn7n0n9raou5 t134 = t133;
            gm_tbgiuvo2j5vcs t135;
            GM_ST(uint8_t, (uint8_t *)&t135 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t135 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t135), t134);
            ((uint8_t *)&t135)[8] = 1;
            t127 = t135;
            goto ix128;
ie128:;
            gm_tbgiuvo2j5vcs t137;
            memset(&t137, 0, sizeof t137);
            gm_tbgiuvo2j5vcs t138 = t137;
            t127 = t138;
ix128:;
            uint8_t t139 = 0;
            F.ret = t127;
            goto gm_fret;
        }
        goto ix119;
ie119:;
ix119:;
        goto me8;
mn116:;
ma12:;
        uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t141 != ((uint8_t)12ULL)) goto mn140;
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 0));
        gm_l11 = t142;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 16));
        gm_l12 = t143;
        void * t145 = gm_l3;
        gm_t88rcucg1b7uk t146 = gm_l2;
        gm_t53at80pfdus3 t147;
        gm_f_kind_jcnt4hm((void *)&t147, t145, GM_CONV(gm_csguankko4lmhd, t146));
        gm_t53at80pfdus3 t148 = t147;
        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t148) + 0));
        if (t149 != ((uint8_t)12ULL)) goto ie144;
        gm_b16_8 t150 = GM_LD(gm_b16_8, ((((uint8_t *)&t148) + 8) + 0));
        gm_l13 = t150;
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((((uint8_t *)&t148) + 8) + 16));
        gm_l14 = t151;
        {
            uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
            uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
            uint8_t t155 = ((t153 == t154));
            gm_l15 = t155;
        }
        {
            uint8_t t157 = gm_l15;
            if (!t157) goto ie156;
            {
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_l16 = t160;
                }
                {
                    for (;;) {
                        uint64_t t162 = gm_l17;
                        uint64_t t163 = gm_l16;
                        uint8_t t164 = (t162 < t163);
                        if (!t164) goto lb161;
                        {
                            uint64_t t167 = gm_l17;
                            uint64_t t168 = t167;
                            uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&t168));
                            gm_l18 = t171;
                            {
                                uint64_t t174 = gm_l18;
                                uint64_t t175 = (uintptr_t)t174;
                                uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_src4vq75vtnd3bq, 159, 24);
                                gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t175 * 4));
                                uint64_t t178 = gm_l18;
                                uint64_t t179 = (uintptr_t)t178;
                                uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                if (t179 >= (uintptr_t)(t180)) gm_trap_index(t179, t180, gm_src4vq75vtnd3bq, 159, 36);
                                gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t179 * 4));
                                gm_t88rcucg1b7uk t182 = t177;
                                gm_t88rcucg1b7uk t183 = t181;
                                uint8_t t184 = (!gm_eq9m6mvtmgde(((uint8_t *)&t182), ((uint8_t *)&t183)));
                                if (!t184) goto ie173;
                                {
                                    gm_l15 = ((uint8_t)0);
                                }
                                goto ix173;
ie173:;
ix173:;
                            }
                            goto me169;
mn170:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 158, 17);
me169:;
                        }
lc161:;
                        {
                            uint64_t t187 = gm_l17;
                            uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                            uint64_t t190 = r188;
                            gm_l17 = t190;
                        }
                    }
lb161:;
                }
            }
            goto ix156;
ie156:;
ix156:;
        }
        {
            gm_tbgiuvo2j5vcs t191;
            uint8_t t193 = gm_l15;
            if (!t193) goto ie192;
            gm_b16_8 t194 = gm_l12;
            gm_b16_8 t195 = gm_l14;
            gm_b16_8 t196 = t194;
            gm_b16_8 t197 = t195;
            uint8_t t198 = (!gm_eqq39g9duc08(((uint8_t *)&t196), ((uint8_t *)&t197)));
            if (!t198) goto ie192;
            uint8_t t199 = 0;
            uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
            uint8_t t201 = ((t200 == ((uint64_t)0ULL)));
            if (!t201) {
                uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t203 = ((t202 == ((uint64_t)0ULL)));
                t199 = t203;
            } else t199 = 1;
            if (!t199) goto ie192;
            gm_tmn7n0n9raou5 t205;
            GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)8ULL));
            gm_tmn7n0n9raou5 t206 = t205;
            gm_tbgiuvo2j5vcs t207;
            GM_ST(uint8_t, (uint8_t *)&t207 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t207 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t207), t206);
            ((uint8_t *)&t207)[8] = 1;
            t191 = t207;
            goto ix192;
ie192:;
            gm_tbgiuvo2j5vcs t209;
            memset(&t209, 0, sizeof t209);
            gm_tbgiuvo2j5vcs t210 = t209;
            t191 = t210;
ix192:;
            uint8_t t211 = 0;
            F.ret = t191;
            goto gm_fret;
        }
        goto ix144;
ie144:;
ix144:;
        goto me8;
mn140:;
ma13:;
        uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t213 != ((uint8_t)13ULL)) goto mn212;
        gm_t88rcucg1b7uk t214 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l19 = t214;
        void * t216 = gm_l3;
        gm_t88rcucg1b7uk t217 = gm_l2;
        gm_t53at80pfdus3 t218;
        gm_f_kind_jcnt4hm((void *)&t218, t216, GM_CONV(gm_csguankko4lmhd, t217));
        gm_t53at80pfdus3 t219 = t218;
        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&t219) + 0));
        if (t220 != ((uint8_t)13ULL)) goto ie215;
        gm_t88rcucg1b7uk t221 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t219) + 8));
        gm_l20 = t221;
        {
            void * t224 = gm_l0;
            gm_t88rcucg1b7uk t225 = gm_l19;
            uint8_t t226 = gm_f_is_nullable_pointer_nkie06s(t224, GM_CONV(gm_csguankko4lmhd, t225));
            if (!t226) goto ie223;
            {
                gm_tbgiuvo2j5vcs t228;
                void * t230 = gm_l0;
                gm_t88rcucg1b7uk t231 = gm_l19;
                gm_t88rcucg1b7uk t232 = gm_l20;
                uint8_t t233 = gm_f_pointer_coercion_a03b7p1(t230, GM_CONV(gm_csguankko4lmhd, t231), GM_CONV(gm_csguankko4lmhd, t232));
                if (!t233) goto ie229;
                gm_tmn7n0n9raou5 t235;
                GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t235 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)11ULL));
                gm_tmn7n0n9raou5 t236 = t235;
                gm_tbgiuvo2j5vcs t237;
                GM_ST(uint8_t, (uint8_t *)&t237 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t237 + 10, 0);
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t237), t236);
                ((uint8_t *)&t237)[8] = 1;
                t228 = t237;
                goto ix229;
ie229:;
                gm_tbgiuvo2j5vcs t239;
                memset(&t239, 0, sizeof t239);
                gm_tbgiuvo2j5vcs t240 = t239;
                t228 = t240;
ix229:;
                uint8_t t241 = 0;
                F.ret = t228;
                goto gm_fret;
            }
            goto ix223;
ie223:;
ix223:;
        }
        {
            void * t243 = gm_l0;
            gm_t88rcucg1b7uk t244 = gm_l19;
            gm_t88rcucg1b7uk t245 = gm_l20;
            uint8_t t246 = gm_f_adds_const_e6d4bkb(t243, GM_CONV(gm_csguankko4lmhd, t244), GM_CONV(gm_csguankko4lmhd, t245));
            if (!t246) goto ie242;
            {
                gm_tmn7n0n9raou5 t248;
                GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t248 + 0), ((uint8_t)6ULL));
                gm_tbgiuvo2j5vcs t249;
                GM_ST(uint8_t, (uint8_t *)&t249 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t249 + 10, 0);
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t249), t248);
                ((uint8_t *)&t249)[8] = 1;
                uint8_t t250 = 0;
                F.ret = t249;
                goto gm_fret;
            }
            goto ix242;
ie242:;
ix242:;
        }
        goto ix215;
ie215:;
ix215:;
        goto me8;
mn212:;
ma14:;
        uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t252 != ((uint8_t)14ULL)) goto mn251;
        gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 0));
        gm_l21 = t253;
        gm_t88rcucg1b7uk t254 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l22 = t254;
        void * t256 = gm_l3;
        gm_t88rcucg1b7uk t257 = gm_l2;
        gm_t53at80pfdus3 t258;
        gm_f_kind_jcnt4hm((void *)&t258, t256, GM_CONV(gm_csguankko4lmhd, t257));
        gm_t53at80pfdus3 t259 = t258;
        uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&t259) + 0));
        if (t260 != ((uint8_t)14ULL)) goto ie255;
        gm_t88rcucg1b7uk t261 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t259) + 8) + 0));
        gm_l23 = t261;
        gm_t88rcucg1b7uk t262 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t259) + 8) + 4));
        gm_l24 = t262;
        gm_t88rcucg1b7uk t263 = gm_l21;
        gm_t88rcucg1b7uk t264 = gm_l23;
        gm_t88rcucg1b7uk t265 = t263;
        gm_t88rcucg1b7uk t266 = t264;
        uint8_t t267 = (gm_eq9m6mvtmgde(((uint8_t *)&t265), ((uint8_t *)&t266)));
        if (!t267) goto ie255;
        void * t268 = gm_l0;
        gm_t88rcucg1b7uk t269 = gm_l22;
        gm_t88rcucg1b7uk t270 = gm_l24;
        uint8_t t271 = gm_f_adds_const_e6d4bkb(t268, GM_CONV(gm_csguankko4lmhd, t269), GM_CONV(gm_csguankko4lmhd, t270));
        if (!t271) goto ie255;
        {
            gm_tmn7n0n9raou5 t273;
            GM_ST(uint8_t, (uint8_t *)&t273 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t273 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t273 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t273 + 0), ((uint8_t)6ULL));
            gm_tbgiuvo2j5vcs t274;
            GM_ST(uint8_t, (uint8_t *)&t274 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t274 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t274), t273);
            ((uint8_t *)&t274)[8] = 1;
            uint8_t t275 = 0;
            F.ret = t274;
            goto gm_fret;
        }
        goto ix255;
ie255:;
ix255:;
        goto me8;
mn251:;
ma15:;
        uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t277 != ((uint8_t)16ULL)) goto mn276;
        gm_tddtjfggdat0d t278 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t7) + 8));
        F.l25 = t278;
        void * t280 = gm_l3;
        gm_t88rcucg1b7uk t281 = gm_l2;
        gm_t53at80pfdus3 t282;
        gm_f_kind_jcnt4hm((void *)&t282, t280, GM_CONV(gm_csguankko4lmhd, t281));
        gm_t53at80pfdus3 t283 = t282;
        uint8_t t284 = GM_LD(uint8_t, (((uint8_t *)&t283) + 0));
        if (t284 != ((uint8_t)16ULL)) goto ie279;
        gm_tddtjfggdat0d t285 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t283) + 8));
        F.l26 = t285;
        {
            uint8_t t287 = 0;
            uint8_t t288 = 0;
            uint64_t t289 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
            uint64_t t290 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 0) + 8);
            uint8_t t291 = ((t289 == t290));
            if (t291) {
                gm_t88rcucg1b7uk t292 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l25) + 16));
                gm_t88rcucg1b7uk t293 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l26) + 16));
                gm_t88rcucg1b7uk t294 = t292;
                gm_t88rcucg1b7uk t295 = t293;
                uint8_t t296 = (gm_eq9m6mvtmgde(((uint8_t *)&t294), ((uint8_t *)&t295)));
                t288 = t296;
            } else t288 = 0;
            if (t288) {
                uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 40));
                uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 40));
                uint8_t t299 = ((t297 == t298));
                t287 = t299;
            } else t287 = 0;
            gm_l27 = t287;
        }
        {
            uint8_t t301 = gm_l27;
            if (!t301) goto ie300;
            {
                {
                    gm_l29 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t304 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                    gm_l28 = t304;
                }
                {
                    for (;;) {
                        uint64_t t306 = gm_l29;
                        uint64_t t307 = gm_l28;
                        uint8_t t308 = (t306 < t307);
                        if (!t308) goto lb305;
                        {
                            uint64_t t311 = gm_l29;
                            uint64_t t312 = t311;
                            uint64_t t315 = GM_LD(uint64_t, ((uint8_t *)&t312));
                            gm_l30 = t315;
                            {
                                uint64_t t318 = gm_l30;
                                uint64_t t319 = (uintptr_t)t318;
                                uint64_t t320 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                                if (t319 >= (uintptr_t)(t320)) gm_trap_index(t319, t320, gm_src4vq75vtnd3bq, 173, 24);
                                gm_t88rcucg1b7uk t321 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t319 * 4));
                                uint64_t t322 = gm_l30;
                                uint64_t t323 = (uintptr_t)t322;
                                uint64_t t324 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 0) + 8);
                                if (t323 >= (uintptr_t)(t324)) gm_trap_index(t323, t324, gm_src4vq75vtnd3bq, 173, 43);
                                gm_t88rcucg1b7uk t325 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 0)) + t323 * 4));
                                gm_t88rcucg1b7uk t326 = t321;
                                gm_t88rcucg1b7uk t327 = t325;
                                uint8_t t328 = (!gm_eq9m6mvtmgde(((uint8_t *)&t326), ((uint8_t *)&t327)));
                                if (!t328) goto ie317;
                                {
                                    gm_l27 = ((uint8_t)0);
                                }
                                goto ix317;
ie317:;
ix317:;
                            }
                            goto me313;
mn314:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 172, 17);
me313:;
                        }
lc305:;
                        {
                            uint64_t t331 = gm_l29;
                            uint64_t r332; bool o333 = __builtin_add_overflow(t331, ((uint64_t)1ULL), &r332);
                            uint64_t t334 = r332;
                            gm_l29 = t334;
                        }
                    }
lb305:;
                }
            }
            goto ix300;
ie300:;
ix300:;
        }
        {
            uint8_t t336 = 0;
            uint8_t t337 = gm_l27;
            uint8_t t338 = (uint8_t)!t337;
            if (!t338) {
                uint64_t t339 = GM_LD(uintptr_t, ((((uint8_t *)&F.l25) + 24) + 0) + 8);
                uint64_t t340 = GM_LD(uintptr_t, ((((uint8_t *)&F.l26) + 24) + 0) + 8);
                uint8_t t341 = (t339 >= t340);
                t336 = t341;
            } else t336 = 1;
            if (!t336) goto ie335;
            {
                gm_tbgiuvo2j5vcs t343;
                memset(&t343, 0, sizeof t343);
                uint8_t t344 = 0;
                F.ret = t343;
                goto gm_fret;
            }
            goto ix335;
ie335:;
ix335:;
        }
        {
            {
                gm_b16_8 t346 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l25) + 24) + 0));
                gm_l31 = t346;
            }
            {
                gm_l32 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t348 = gm_l32;
                    uint64_t t349 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                    uint8_t t350 = (t348 < t349);
                    if (!t350) goto lb347;
                    {
                        uint64_t t353 = gm_l32;
                        uint64_t t354 = (uintptr_t)t353;
                        uint64_t t355 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        gm_tiilmfd16v3fc t358 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t354 * 24));
                        gm_l33 = t358;
                        {
                            void * t361 = (void *)(((uint8_t *)&F.l26) + 24);
                            void * t362 = (void *)((uint8_t *)&gm_l33);
                            uint8_t t363 = gm_f_set_contains_8v9ep9m(t361, t362);
                            uint8_t t364 = (uint8_t)!t363;
                            if (!t364) goto ie360;
                            {
                                gm_tbgiuvo2j5vcs t366;
                                memset(&t366, 0, sizeof t366);
                                uint8_t t367 = 0;
                                F.ret = t366;
                                goto gm_fret;
                            }
                            goto ix360;
ie360:;
ix360:;
                        }
                        goto me356;
mn357:;
                        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 177, 13);
me356:;
                    }
lc347:;
                    {
                        uint64_t t369 = gm_l32;
                        uint64_t r370; bool o371 = __builtin_add_overflow(t369, ((uint64_t)1ULL), &r370);
                        uint64_t t372 = r370;
                        gm_l32 = t372;
                    }
                }
lb347:;
            }
        }
        {
            gm_tmn7n0n9raou5 t373;
            GM_ST(uint8_t, (uint8_t *)&t373 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t373 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t373 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t373 + 0), ((uint8_t)10ULL));
            gm_tbgiuvo2j5vcs t374;
            GM_ST(uint8_t, (uint8_t *)&t374 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t374 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t374), t373);
            ((uint8_t *)&t374)[8] = 1;
            uint8_t t375 = 0;
            F.ret = t374;
            goto gm_fret;
        }
        goto ix279;
ie279:;
ix279:;
        goto me8;
mn276:;
ma16:;
        goto me8;
mn376:;
ma17:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 143, 5);
me8:;
    }
    {
        void * t379 = gm_l0;
        gm_t88rcucg1b7uk t380 = gm_l1;
        gm_t88rcucg1b7uk t381 = gm_l2;
        uint8_t t382 = gm_f_pointer_coercion_a03b7p1(t379, GM_CONV(gm_csguankko4lmhd, t380), GM_CONV(gm_csguankko4lmhd, t381));
        if (!t382) goto ie378;
        {
            void * t385 = gm_l3;
            gm_t88rcucg1b7uk t386 = gm_l1;
            gm_t53at80pfdus3 t387;
            gm_f_kind_jcnt4hm((void *)&t387, t385, GM_CONV(gm_csguankko4lmhd, t386));
            gm_t53at80pfdus3 t388 = t387;
            uint8_t t389 = GM_LD(uint8_t, (((uint8_t *)&t388) + 0));
            if (t389 != ((uint8_t)8ULL)) goto ie384;
            gm_t88rcucg1b7uk t390 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t388) + 8) + 4));
            gm_l34 = t390;
            void * t391 = gm_l3;
            gm_t88rcucg1b7uk t392 = gm_l2;
            gm_t53at80pfdus3 t393;
            gm_f_kind_jcnt4hm((void *)&t393, t391, GM_CONV(gm_csguankko4lmhd, t392));
            gm_t53at80pfdus3 t394 = t393;
            uint8_t t395;
            uint8_t t398 = GM_LD(uint8_t, (((uint8_t *)&t394) + 0));
            if (t398 != ((uint8_t)10ULL)) goto mn397;
            t395 = ((uint8_t)1);
            goto me396;
mn397:;
            t395 = ((uint8_t)0);
            goto me396;
mn399:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 185, 50);
me396:;
            if (!t395) goto ie384;
            void * t400 = gm_l3;
            gm_t88rcucg1b7uk t401 = gm_l34;
            gm_t53at80pfdus3 t402;
            gm_f_kind_jcnt4hm((void *)&t402, t400, GM_CONV(gm_csguankko4lmhd, t401));
            gm_t53at80pfdus3 t403 = t402;
            uint8_t t404;
            uint8_t t410 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
            switch (t410) { case ((uint8_t)0ULL): goto ma408; case ((uint8_t)1ULL): goto ma408; case ((uint8_t)2ULL): goto ma408; case ((uint8_t)3ULL): goto ma408; case ((uint8_t)4ULL): goto ma408; case ((uint8_t)5ULL): goto ma408; case ((uint8_t)6ULL): goto ma408; case ((uint8_t)7ULL): goto ma408; case ((uint8_t)8ULL): goto ma408; case ((uint8_t)9ULL): goto ma408; case ((uint8_t)10ULL): goto ma408; case ((uint8_t)11ULL): goto ma406; case ((uint8_t)12ULL): goto ma408; case ((uint8_t)13ULL): goto ma408; case ((uint8_t)14ULL): goto ma408; case ((uint8_t)15ULL): goto ma408; case ((uint8_t)16ULL): goto ma408; case ((uint8_t)17ULL): goto ma408; case ((uint8_t)18ULL): goto ma408; case ((uint8_t)19ULL): goto ma408; case ((uint8_t)20ULL): goto ma407; case ((uint8_t)21ULL): goto ma408; case ((uint8_t)22ULL): goto ma408; case ((uint8_t)23ULL): goto ma408; case ((uint8_t)24ULL): goto ma408; case ((uint8_t)25ULL): goto ma408; case ((uint8_t)26ULL): goto ma408; default: goto ma408; }
ma406:;
            uint8_t t412 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
            if (t412 != ((uint8_t)11ULL)) goto mn411;
            t404 = ((uint8_t)1);
            goto me405;
mn411:;
ma407:;
            uint8_t t414 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
            if (t414 != ((uint8_t)20ULL)) goto mn413;
            t404 = ((uint8_t)1);
            goto me405;
mn413:;
ma408:;
            t404 = ((uint8_t)0);
            goto me405;
mn415:;
ma409:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 185, 84);
me405:;
            if (!t404) goto ie384;
            {
                gm_tmn7n0n9raou5 t417;
                GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)7ULL));
                gm_tbgiuvo2j5vcs t418;
                GM_ST(uint8_t, (uint8_t *)&t418 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t418 + 10, 0);
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t418), t417);
                ((uint8_t *)&t418)[8] = 1;
                uint8_t t419 = 0;
                F.ret = t418;
                goto gm_fret;
            }
            goto ix384;
ie384:;
ix384:;
        }
        {
            gm_tmn7n0n9raou5 t420;
            GM_ST(uint8_t, (uint8_t *)&t420 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t420 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t420 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t420 + 0), ((uint8_t)6ULL));
            gm_tbgiuvo2j5vcs t421;
            GM_ST(uint8_t, (uint8_t *)&t421 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t421 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t421), t420);
            ((uint8_t *)&t421)[8] = 1;
            uint8_t t422 = 0;
            F.ret = t421;
            goto gm_fret;
        }
        goto ix378;
ie378:;
ix378:;
    }
    {
        gm_tbgiuvo2j5vcs t423;
        memset(&t423, 0, sizeof t423);
        uint8_t t424 = 0;
        F.ret = t423;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coerce_sudc6c1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* target0 */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* src */
    gm_t88rcucg1b7uk gm_l5; /* target */
    gm_t88rcucg1b7uk gm_l6; /* a */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* b */
    uint8_t gm_l9; /* sc */
    uint8_t gm_l10; /* tc */
    void * gm_l12; /* c */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    void * gm_l14; /* p */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l16; /* na */
    gm_b16_8 gm_l17; /* b */
    gm_b16_8 gm_l18; /* nb */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_t88rcucg1b7uk gm_l22; /* found */
    gm_t88rcucg1b7uk gm_l23; /* s */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tiilmfd16v3fc gm_l29; /* x */
    uint8_t gm_l31; /* subset */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tiilmfd16v3fc gm_l34; /* x */
    gm_b16_8 gm_l35; /* steps */
    GM_STACK_CHECK(&F, 5115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 96);
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_t88rcucg1b7uk t17 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t13, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t17;
    }
    {
        uint8_t t19 = 0;
        uint8_t t20 = 0;
        gm_t88rcucg1b7uk t21 = gm_l4;
        gm_t88rcucg1b7uk t22 = gm_l5;
        gm_t88rcucg1b7uk t23 = t21;
        gm_t88rcucg1b7uk t24 = t22;
        uint8_t t25 = (gm_eq9m6mvtmgde(((uint8_t *)&t23), ((uint8_t *)&t24)));
        if (!t25) {
            void * t26 = gm_l3;
            gm_t88rcucg1b7uk t27 = gm_l4;
            uint8_t t28 = gm_f_is_error_8k27gd1(t26, GM_CONV(gm_csguankko4lmhd, t27));
            t20 = t28;
        } else t20 = 1;
        if (!t20) {
            void * t29 = gm_l3;
            gm_t88rcucg1b7uk t30 = gm_l5;
            uint8_t t31 = gm_f_is_error_8k27gd1(t29, GM_CONV(gm_csguankko4lmhd, t30));
            t19 = t31;
        } else t19 = 1;
        if (!t19) goto ie18;
        {
            void * t33 = gm_l1;
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t36 = gm_l3;
        gm_t88rcucg1b7uk t37 = gm_l4;
        gm_t53at80pfdus3 t38;
        gm_f_kind_jcnt4hm((void *)&t38, t36, GM_CONV(gm_csguankko4lmhd, t37));
        gm_t53at80pfdus3 t39 = t38;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
        if (t40 != ((uint8_t)11ULL)) goto ie35;
        gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t39) + 8) + 8));
        gm_l6 = t41;
        void * t42 = gm_l3;
        gm_t88rcucg1b7uk t43 = gm_l5;
        gm_t53at80pfdus3 t44;
        gm_f_kind_jcnt4hm((void *)&t44, t42, GM_CONV(gm_csguankko4lmhd, t43));
        gm_t53at80pfdus3 t45 = t44;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t46 != ((uint8_t)10ULL)) goto ie35;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)&t45) + 8) + 0));
        gm_l7 = t47;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t45) + 8) + 4));
        gm_l8 = t48;
        uint8_t t49 = gm_l7;
        if (!t49) goto ie35;
        void * t50 = gm_l3;
        gm_t88rcucg1b7uk t51 = gm_l4;
        uint8_t t52 = gm_f_is_positional_array_6sufsk8(t50, GM_CONV(gm_csguankko4lmhd, t51));
        if (!t52) goto ie35;
        void * t53 = gm_l0;
        void * t54 = (void *)(((uint8_t *)t53) + 96);
        void * t55 = gm_l3;
        gm_t88rcucg1b7uk t56 = gm_l6;
        gm_t88rcucg1b7uk t57 = gm_l8;
        gm_t9qkh0rsooc8d t58 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t54, t55, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t59 = t58;
        uint8_t t60;
        if (!((((uint8_t *)&t59))[0] != 0)) goto mn62;
        t60 = ((uint8_t)0);
        goto me61;
mn62:;
        t60 = ((uint8_t)1);
        goto me61;
mn63:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 10, 121);
me61:;
        if (!t60) goto ie35;
        {
            void * t65 = gm_l0;
            void * t66 = gm_l0;
            void * t67 = gm_l1;
            gm_t88rcucg1b7uk t68 = gm_l5;
            void * t69 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t70 = gm_l5;
            void * t71 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t65, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        void * t74 = gm_l3;
        gm_t88rcucg1b7uk t75 = gm_l4;
        gm_t53at80pfdus3 t76;
        gm_f_kind_jcnt4hm((void *)&t76, t74, GM_CONV(gm_csguankko4lmhd, t75));
        gm_t53at80pfdus3 t77 = t76;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
        if (t78 != ((uint8_t)10ULL)) goto ie73;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)&t77) + 8) + 0));
        gm_l9 = t79;
        void * t80 = gm_l3;
        gm_t88rcucg1b7uk t81 = gm_l5;
        gm_t53at80pfdus3 t82;
        gm_f_kind_jcnt4hm((void *)&t82, t80, GM_CONV(gm_csguankko4lmhd, t81));
        gm_t53at80pfdus3 t83 = t82;
        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t83) + 0));
        if (t84 != ((uint8_t)10ULL)) goto ie73;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)&t83) + 8) + 0));
        gm_l10 = t85;
        uint8_t t86 = gm_l9;
        if (!t86) goto ie73;
        uint8_t t87 = gm_l10;
        uint8_t t88 = (uint8_t)!t87;
        if (!t88) goto ie73;
        void * t89 = gm_l1;
        uint8_t t90 = gm_f_is_held_temp_d00dm1p(t89);
        if (!t90) goto ie73;
        {
            void * t92 = gm_l1;
            gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t92) + 72));
            gm_t6j6smj7qoctu t94;
            void * t95 = gm_l0;
            gm_t88rcucg1b7uk t96 = gm_l5;
            gm_b16_8 t97 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t95, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t97);
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l4;
            gm_b16_8 t100 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 16, t100);
            gm_tabb710d9mq2e t101;
            gm_f_format_ns6dben((void *)&t101, (void *)NULL, cx, NULL, &t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t102 = t101;
            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_src4vq75vtnd3bq, 14, 33);
            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
            gm_b16_8 t104 = t103;
            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
            gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t93, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t106 = (void *)((uint8_t *)&F.l11);
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sn1h26ucppu4i));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(109));
            gm_f_add_note_46v502s(t106, t107);
        }
        {
            void * t108 = gm_l0;
            void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
            gm_b0_1 t110 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t109, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t111 = gm_l1;
            uint8_t t112 = 0;
            F.ret = t111;
            goto gm_fret;
        }
        goto ix73;
ie73:;
ix73:;
    }
    {
        void * t114 = gm_l0;
        void * t115 = gm_l1;
        gm_t88rcucg1b7uk t116 = gm_l4;
        gm_t88rcucg1b7uk t117 = gm_l5;
        void * t118 = gm_f_soa_coerce_c57qnm7((void *)NULL, cx, NULL, t114, t115, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t119 = t118;
        if (!(GM_LD(void *, ((uint8_t *)&t119)) != 0)) goto ie113;
        void * t120 = GM_LD(void *, ((uint8_t *)&t119));
        gm_l12 = t120;
        {
            void * t122 = gm_l12;
            uint8_t t123 = 0;
            F.ret = t122;
            goto gm_fret;
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        void * t125 = gm_l0;
        void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
        gm_t88rcucg1b7uk t127 = gm_l5;
        gm_tkn42u7na3t81 t128 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t129 = t128;
        if (!((((uint8_t *)&t129))[4] != 0)) goto ie124;
        gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t129));
        gm_l13 = t130;
        {
            void * t133 = gm_l0;
            void * t134 = gm_l1;
            gm_t88rcucg1b7uk t135 = gm_l5;
            gm_t88rcucg1b7uk t136 = gm_l13;
            void * t137 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t133, t134, t135, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t138 = t137;
            if (!(GM_LD(void *, ((uint8_t *)&t138)) != 0)) goto ie132;
            void * t139 = GM_LD(void *, ((uint8_t *)&t138));
            gm_l14 = t139;
            {
                void * t141 = gm_l14;
                uint8_t t142 = 0;
                F.ret = t141;
                goto gm_fret;
            }
            goto ix132;
ie132:;
ix132:;
        }
        goto ix124;
ie124:;
ix124:;
    }
    {
        void * t144 = gm_l3;
        gm_t88rcucg1b7uk t145 = gm_l4;
        gm_t53at80pfdus3 t146;
        gm_f_kind_jcnt4hm((void *)&t146, t144, GM_CONV(gm_csguankko4lmhd, t145));
        gm_t53at80pfdus3 t147 = t146;
        uint8_t t148 = ((GM_LD(uint8_t, ((uint8_t *)&t147) + 0) == ((uint8_t)5ULL)));
        if (!t148) goto ie143;
        {
            gm_t88rcucg1b7uk t150 = gm_l5;
            gm_tmn7n0n9raou5 t151;
            GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)5ULL));
            void * t152 = gm_l1;
            gm_t8jsc4n9vp2ib t153;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t153 + 0), t151);
            GM_ST(void *, ((uint8_t *)&t153 + 8), t152);
            gm_teti411eujvf4 t154;
            memset(&t154, 0, sizeof t154);
            GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t154 + 8), t153);
            void * t155 = gm_l1;
            gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t155) + 72));
            void * t157 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t150, &t154, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t158 = 0;
            F.ret = t157;
            goto gm_fret;
        }
        goto ix143;
ie143:;
ix143:;
    }
    {
        void * t160 = gm_l3;
        gm_t88rcucg1b7uk t161 = gm_l4;
        gm_t53at80pfdus3 t162;
        gm_f_kind_jcnt4hm((void *)&t162, t160, GM_CONV(gm_csguankko4lmhd, t161));
        gm_t53at80pfdus3 t163 = t162;
        uint8_t t164;
        uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&t163) + 0));
        if (t167 != ((uint8_t)23ULL)) goto mn166;
        t164 = ((uint8_t)1);
        goto me165;
mn166:;
        t164 = ((uint8_t)0);
        goto me165;
mn168:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 25, 8);
me165:;
        if (!t164) goto ie159;
        {
            void * t170 = gm_l0;
            void * t171 = (void *)(((uint8_t *)t170) + 280);
            gm_thsh8t5u55gps t172;
            gm_t88rcucg1b7uk t173 = gm_l4;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t172 + 0, t173);
            gm_t88rcucg1b7uk t174 = gm_l5;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t172 + 4, t174);
            void * t175 = gm_l1;
            gm_tl874t2atnlh3 t176 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t175) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t172 + 8, t176);
            gm_tftb1odc739t5 t177 = gm_f_push_joldpsp(t171, &t172);
            gm_tftb1odc739t5 t178 = t177;
            if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 1, gm_src4vq75vtnd3bq, 28, 9);
            gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 1));
        }
        {
            gm_t88rcucg1b7uk t180 = gm_l5;
            gm_tmn7n0n9raou5 t181;
            GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t181 + 0), ((uint8_t)12ULL));
            void * t182 = gm_l1;
            gm_t8jsc4n9vp2ib t183;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t183 + 0), t181);
            GM_ST(void *, ((uint8_t *)&t183 + 8), t182);
            gm_teti411eujvf4 t184;
            memset(&t184, 0, sizeof t184);
            GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t184 + 8), t183);
            void * t185 = gm_l1;
            gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t185) + 72));
            void * t187 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t180, &t184, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t188 = 0;
            F.ret = t187;
            goto gm_fret;
        }
        goto ix159;
ie159:;
ix159:;
    }
    {
        void * t190 = gm_l3;
        gm_t88rcucg1b7uk t191 = gm_l4;
        gm_t53at80pfdus3 t192;
        gm_f_kind_jcnt4hm((void *)&t192, t190, GM_CONV(gm_csguankko4lmhd, t191));
        gm_t53at80pfdus3 t193 = t192;
        uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&t193) + 0));
        if (t194 != ((uint8_t)12ULL)) goto ie189;
        gm_b16_8 t195 = GM_LD(gm_b16_8, ((((uint8_t *)&t193) + 8) + 0));
        gm_l15 = t195;
        gm_b16_8 t196 = GM_LD(gm_b16_8, ((((uint8_t *)&t193) + 8) + 16));
        gm_l16 = t196;
        void * t197 = gm_l3;
        gm_t88rcucg1b7uk t198 = gm_l5;
        gm_t53at80pfdus3 t199;
        gm_f_kind_jcnt4hm((void *)&t199, t197, GM_CONV(gm_csguankko4lmhd, t198));
        gm_t53at80pfdus3 t200 = t199;
        uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t200) + 0));
        if (t201 != ((uint8_t)12ULL)) goto ie189;
        gm_b16_8 t202 = GM_LD(gm_b16_8, ((((uint8_t *)&t200) + 8) + 0));
        gm_l17 = t202;
        gm_b16_8 t203 = GM_LD(gm_b16_8, ((((uint8_t *)&t200) + 8) + 16));
        gm_l18 = t203;
        {
            uint8_t t206 = 0;
            void * t207 = gm_l3;
            gm_t88rcucg1b7uk t208 = gm_l4;
            uint8_t t209 = gm_f_has_infer_3o82a0b(t207, GM_CONV(gm_csguankko4lmhd, t208));
            if (!t209) {
                void * t210 = gm_l3;
                gm_t88rcucg1b7uk t211 = gm_l5;
                uint8_t t212 = gm_f_has_infer_3o82a0b(t210, GM_CONV(gm_csguankko4lmhd, t211));
                t206 = t212;
            } else t206 = 1;
            if (!t206) goto ie205;
            uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
            uint8_t t215 = ((t213 == t214));
            if (!t215) goto ie205;
            gm_b16_8 t216 = gm_l16;
            gm_b16_8 t217 = gm_l18;
            gm_b16_8 t218 = t216;
            gm_b16_8 t219 = t217;
            uint8_t t220 = (!gm_eqq39g9duc08(((uint8_t *)&t218), ((uint8_t *)&t219)));
            if (!t220) goto ie205;
            uint8_t t221 = 0;
            uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
            uint8_t t223 = ((t222 == ((uint64_t)0ULL)));
            if (!t223) {
                uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t225 = ((t224 == ((uint64_t)0ULL)));
                t221 = t225;
            } else t221 = 1;
            if (!t221) goto ie205;
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t228 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_l19 = t228;
                }
                {
                    for (;;) {
                        uint64_t t230 = gm_l20;
                        uint64_t t231 = gm_l19;
                        uint8_t t232 = (t230 < t231);
                        if (!t232) goto lb229;
                        {
                            uint64_t t235 = gm_l20;
                            uint64_t t236 = t235;
                            uint64_t t239 = GM_LD(uint64_t, ((uint8_t *)&t236));
                            gm_l21 = t239;
                            {
                                void * t242 = gm_l0;
                                void * t243 = (void *)(((uint8_t *)t242) + 96);
                                void * t244 = gm_l3;
                                uint64_t t245 = gm_l21;
                                uint64_t t246 = (uintptr_t)t245;
                                uint64_t t247 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                if (t246 >= (uintptr_t)(t247)) gm_trap_index(t246, t247, gm_src4vq75vtnd3bq, 35, 40);
                                gm_t88rcucg1b7uk t248 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t246 * 4));
                                uint64_t t249 = gm_l21;
                                uint64_t t250 = (uintptr_t)t249;
                                uint64_t t251 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                if (t250 >= (uintptr_t)(t251)) gm_trap_index(t250, t251, gm_src4vq75vtnd3bq, 35, 46);
                                gm_t88rcucg1b7uk t252 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t250 * 4));
                                gm_t9qkh0rsooc8d t253 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t243, t244, t248, t252);
                                if (gm_unw.kind) {
                                    goto gm_ll233;
                                }
                                gm_t9qkh0rsooc8d t254 = t253;
                                uint8_t t255;
                                if (!((((uint8_t *)&t254))[0] != 0)) goto mn257;
                                t255 = ((uint8_t)1);
                                goto me256;
mn257:;
                                t255 = ((uint8_t)0);
                                goto me256;
mn258:;
                                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 35, 20);
me256:;
                                if (!t255) goto ie241;
                                {
                                    void * t260 = gm_l0;
                                    void * t261 = (void *)(((uint8_t *)t260) + 96);
                                    void * t262 = gm_l0;
                                    void * t263 = GM_LD(void *, (((uint8_t *)t262) + 0));
                                    gm_t88rcucg1b7uk t264 = gm_l4;
                                    gm_t88rcucg1b7uk t265 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t261, t263, t264);
                                    if (gm_unw.kind) {
                                        goto gm_ll233;
                                    }
                                    gm_l22 = t265;
                                }
                                {
                                    void * t266 = gm_l0;
                                    void * t267 = gm_l1;
                                    gm_tl874t2atnlh3 t268 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t267) + 72));
                                    gm_t6j6smj7qoctu t269;
                                    void * t270 = gm_l0;
                                    gm_t88rcucg1b7uk t271 = gm_l5;
                                    gm_b16_8 t272 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t270, t271);
                                    if (gm_unw.kind) {
                                        goto gm_ll233;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t269 + 0, t272);
                                    void * t273 = gm_l0;
                                    gm_t88rcucg1b7uk t274 = gm_l22;
                                    gm_b16_8 t275 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t273, t274);
                                    if (gm_unw.kind) {
                                        goto gm_ll233;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t269 + 16, t275);
                                    gm_tabb710d9mq2e t276;
                                    gm_f_format_ns6dben((void *)&t276, (void *)NULL, cx, NULL, &t269);
                                    if (gm_unw.kind) {
                                        goto gm_ll233;
                                    }
                                    gm_tabb710d9mq2e t277 = t276;
                                    if (((((uint8_t *)&t277))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t277) + 8, gm_src4vq75vtnd3bq, 37, 38);
                                    gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&t277) + 8));
                                    gm_b16_8 t279 = t278;
                                    gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
                                    gm_b0_1 t281 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t266, t268, t280);
                                    if (gm_unw.kind) {
                                        goto gm_ll233;
                                    }
                                }
                                {
                                    void * t282 = gm_l1;
                                    uint8_t t283 = 0;
                                    F.ret = t282;
                                    goto gm_fret;
                                }
                                goto ix241;
ie241:;
ix241:;
                            }
                            goto me237;
mn238:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 34, 13);
me237:;
                        }
lc229:;
                        {
                            uint64_t t285 = gm_l20;
                            uint64_t r286; bool o287 = __builtin_add_overflow(t285, ((uint64_t)1ULL), &r286);
                            uint64_t t288 = r286;
                            gm_l20 = t288;
                        }
                    }
gm_ll233:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb229; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc229; }
                    }
                    goto gm_lad;
lb229:;
                }
            }
            {
                gm_t88rcucg1b7uk t289 = gm_l5;
                gm_tmn7n0n9raou5 t290;
                GM_ST(uint8_t, (uint8_t *)&t290 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t290 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t290 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)8ULL));
                void * t291 = gm_l1;
                gm_t8jsc4n9vp2ib t292;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t292 + 0), t290);
                GM_ST(void *, ((uint8_t *)&t292 + 8), t291);
                gm_teti411eujvf4 t293;
                memset(&t293, 0, sizeof t293);
                GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t293 + 8), t292);
                void * t294 = gm_l1;
                gm_tl874t2atnlh3 t295 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t294) + 72));
                void * t296 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t289, &t293, t295);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t297 = 0;
                F.ret = t296;
                goto gm_fret;
            }
            goto ix205;
ie205:;
ix205:;
        }
        goto ix189;
ie189:;
ix189:;
    }
    {
        uint8_t t299 = 0;
        void * t300 = gm_l3;
        gm_t88rcucg1b7uk t301 = gm_l4;
        uint8_t t302 = gm_f_has_infer_3o82a0b(t300, GM_CONV(gm_csguankko4lmhd, t301));
        if (!t302) {
            void * t303 = gm_l3;
            gm_t88rcucg1b7uk t304 = gm_l5;
            uint8_t t305 = gm_f_has_infer_3o82a0b(t303, GM_CONV(gm_csguankko4lmhd, t304));
            t299 = t305;
        } else t299 = 1;
        if (!t299) goto ie298;
        {
            void * t308 = gm_l0;
            void * t309 = (void *)(((uint8_t *)t308) + 96);
            void * t310 = gm_l3;
            gm_t88rcucg1b7uk t311 = gm_l4;
            gm_t88rcucg1b7uk t312 = gm_l5;
            gm_t9qkh0rsooc8d t313 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t309, t310, t311, t312);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t314 = t313;
            uint8_t t315;
            if (!((((uint8_t *)&t314))[0] != 0)) goto mn317;
            t315 = ((uint8_t)1);
            goto me316;
mn317:;
            t315 = ((uint8_t)0);
            goto me316;
mn318:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 45, 12);
me316:;
            if (!t315) goto ie307;
            {
                void * t320 = gm_l0;
                void * t321 = (void *)(((uint8_t *)t320) + 96);
                void * t322 = gm_l0;
                void * t323 = GM_LD(void *, (((uint8_t *)t322) + 0));
                gm_t88rcucg1b7uk t324 = gm_l4;
                gm_t88rcucg1b7uk t325 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t321, t323, t324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t325;
            }
            {
                void * t326 = gm_l0;
                void * t327 = (void *)(((uint8_t *)t326) + 96);
                void * t328 = gm_l0;
                void * t329 = GM_LD(void *, (((uint8_t *)t328) + 0));
                gm_t88rcucg1b7uk t330 = gm_l5;
                gm_t88rcucg1b7uk t331 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t327, t329, t330);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t331;
            }
            {
                void * t332 = gm_l0;
                void * t333 = gm_l1;
                gm_tl874t2atnlh3 t334 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t333) + 72));
                gm_t6j6smj7qoctu t335;
                void * t336 = gm_l0;
                gm_t88rcucg1b7uk t337 = gm_l24;
                gm_b16_8 t338 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t336, t337);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t335 + 0, t338);
                void * t339 = gm_l0;
                gm_t88rcucg1b7uk t340 = gm_l23;
                gm_b16_8 t341 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t339, t340);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t335 + 16, t341);
                gm_tabb710d9mq2e t342;
                gm_f_format_ns6dben((void *)&t342, (void *)NULL, cx, NULL, &t335);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t343 = t342;
                if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 8, gm_src4vq75vtnd3bq, 48, 30);
                gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&t343) + 8));
                gm_b16_8 t345 = t344;
                gm_b16_8 t346 = GM_LD(gm_b16_8, ((uint8_t *)&t345));
                gm_b0_1 t347 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t332, t334, t346);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix307;
ie307:;
ix307:;
        }
        {
            void * t348 = gm_l1;
            uint8_t t349 = 0;
            F.ret = t348;
            goto gm_fret;
        }
        goto ix298;
ie298:;
ix298:;
    }
    {
        void * t351 = gm_l3;
        gm_t88rcucg1b7uk t352 = gm_l5;
        gm_t53at80pfdus3 t353;
        gm_f_kind_jcnt4hm((void *)&t353, t351, GM_CONV(gm_csguankko4lmhd, t352));
        gm_t53at80pfdus3 t354 = t353;
        uint8_t t355 = GM_LD(uint8_t, (((uint8_t *)&t354) + 0));
        if (t355 != ((uint8_t)15ULL)) goto ie350;
        gm_tddtjfggdat0d t356 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t354) + 8));
        F.l25 = t356;
        {
            void * t358 = gm_l3;
            gm_t88rcucg1b7uk t359 = gm_l4;
            gm_t53at80pfdus3 t360;
            gm_f_kind_jcnt4hm((void *)&t360, t358, GM_CONV(gm_csguankko4lmhd, t359));
            gm_t53at80pfdus3 t361 = t360;
            uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&t361) + 0));
            switch (t367) { case ((uint8_t)0ULL): goto ma365; case ((uint8_t)1ULL): goto ma365; case ((uint8_t)2ULL): goto ma365; case ((uint8_t)3ULL): goto ma365; case ((uint8_t)4ULL): goto ma365; case ((uint8_t)5ULL): goto ma365; case ((uint8_t)6ULL): goto ma365; case ((uint8_t)7ULL): goto ma365; case ((uint8_t)8ULL): goto ma365; case ((uint8_t)9ULL): goto ma365; case ((uint8_t)10ULL): goto ma365; case ((uint8_t)11ULL): goto ma365; case ((uint8_t)12ULL): goto ma365; case ((uint8_t)13ULL): goto ma365; case ((uint8_t)14ULL): goto ma365; case ((uint8_t)15ULL): goto ma364; case ((uint8_t)16ULL): goto ma363; case ((uint8_t)17ULL): goto ma365; case ((uint8_t)18ULL): goto ma365; case ((uint8_t)19ULL): goto ma365; case ((uint8_t)20ULL): goto ma365; case ((uint8_t)21ULL): goto ma365; case ((uint8_t)22ULL): goto ma365; case ((uint8_t)23ULL): goto ma365; case ((uint8_t)24ULL): goto ma365; case ((uint8_t)25ULL): goto ma365; case ((uint8_t)26ULL): goto ma365; default: goto ma365; }
ma363:;
            uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&t361) + 0));
            if (t369 != ((uint8_t)16ULL)) goto mn368;
            gm_tddtjfggdat0d t370 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t361) + 8));
            F.l26 = t370;
            void * t372 = (void *)((uint8_t *)&F.l26);
            void * t373 = (void *)((uint8_t *)&F.l25);
            uint8_t t374 = gm_f_same_shape_m0o6aok(t372, t373);
            if (!t374) goto ie371;
            {
                uint8_t t377 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 40));
                if (!t377) goto ie376;
                {
                    void * t379 = gm_l0;
                    void * t380 = gm_l1;
                    gm_tl874t2atnlh3 t381 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t380) + 72));
                    void * t382 = gm_l0;
                    gm_t88rcucg1b7uk t383 = gm_l4;
                    gm_b16_8 t384 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t382, t383);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t385;
                    GM_ST(gm_b16_8, ((uint8_t *)&t385 + 0), t384);
                    gm_tabb710d9mq2e t386;
                    gm_f_format_dr0nbiu((void *)&t386, (void *)NULL, cx, NULL, t385);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t387 = t386;
                    if (((((uint8_t *)&t387))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t387) + 8, gm_src4vq75vtnd3bq, 59, 38);
                    gm_b16_8 t388 = GM_LD(gm_b16_8, (((uint8_t *)&t387) + 8));
                    gm_b16_8 t389 = t388;
                    gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&t389));
                    gm_b0_1 t391 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t379, t381, t390);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t392 = gm_l1;
                    uint8_t t393 = 0;
                    F.ret = t392;
                    goto gm_fret;
                }
                goto ix376;
ie376:;
ix376:;
            }
            {
                {
                    gm_b16_8 t395 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 24) + 0));
                    gm_l27 = t395;
                }
                {
                    gm_l28 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t397 = gm_l28;
                        uint64_t t398 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        uint8_t t399 = (t397 < t398);
                        if (!t399) goto lb396;
                        {
                            uint64_t t402 = gm_l28;
                            uint64_t t403 = (uintptr_t)t402;
                            uint64_t t404 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                            gm_tiilmfd16v3fc t407 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t403 * 24));
                            gm_l29 = t407;
                            {
                                void * t410 = (void *)(((uint8_t *)&F.l25) + 24);
                                void * t411 = (void *)((uint8_t *)&gm_l29);
                                uint8_t t412 = gm_f_set_contains_8v9ep9m(t410, t411);
                                uint8_t t413 = (uint8_t)!t412;
                                if (!t413) goto ie409;
                                {
                                    void * t415 = gm_l0;
                                    void * t416 = (void *)(((uint8_t *)t415) + 192);
                                    gm_tj44oivvogvtl t417;
                                    memset((uint8_t *)&t417 + 36, 0, 4);
                                    memcpy((uint8_t *)&t417 + 0, ((uint8_t *)&gm_l29), 24);
                                    void * t418 = gm_l1;
                                    gm_tl874t2atnlh3 t419 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t418) + 72));
                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t417 + 24, t419);
                                    gm_tftb1odc739t5 t420 = gm_f_push_3onuaru(t416, &t417);
                                    gm_tftb1odc739t5 t421 = t420;
                                    if (((((uint8_t *)&t421))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t421) + 1, gm_src4vq75vtnd3bq, 63, 59);
                                    gm_b0_1 t422 = GM_LD(gm_b0_1, (((uint8_t *)&t421) + 1));
                                }
                                goto ix409;
ie409:;
ix409:;
                            }
                            goto me405;
mn406:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 62, 17);
me405:;
                        }
lc396:;
                        {
                            uint64_t t424 = gm_l28;
                            uint64_t r425; bool o426 = __builtin_add_overflow(t424, ((uint64_t)1ULL), &r425);
                            uint64_t t427 = r425;
                            gm_l28 = t427;
                        }
                    }
lb396:;
                }
            }
            {
                gm_t88rcucg1b7uk t428 = gm_l5;
                gm_tmn7n0n9raou5 t429;
                GM_ST(uint8_t, (uint8_t *)&t429 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t429 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t429 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t429 + 0), ((uint8_t)9ULL));
                void * t430 = gm_l1;
                gm_t8jsc4n9vp2ib t431;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t431 + 0), t429);
                GM_ST(void *, ((uint8_t *)&t431 + 8), t430);
                gm_teti411eujvf4 t432;
                memset(&t432, 0, sizeof t432);
                GM_ST(uint8_t, ((uint8_t *)&t432 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t432 + 8), t431);
                void * t433 = gm_l1;
                gm_tl874t2atnlh3 t434 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t433) + 72));
                void * t435 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t428, &t432, t434);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t436 = 0;
                F.ret = t435;
                goto gm_fret;
            }
            goto ix371;
ie371:;
ix371:;
            goto me362;
mn368:;
ma364:;
            uint8_t t438 = GM_LD(uint8_t, (((uint8_t *)&t361) + 0));
            if (t438 != ((uint8_t)15ULL)) goto mn437;
            gm_tddtjfggdat0d t439 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t361) + 8));
            F.l30 = t439;
            void * t441 = (void *)((uint8_t *)&F.l30);
            void * t442 = (void *)((uint8_t *)&F.l25);
            uint8_t t443 = gm_f_same_shape_m0o6aok(t441, t442);
            if (!t443) goto ie440;
            {
                gm_l31 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t446 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l30) + 24) + 0));
                    gm_l32 = t446;
                }
                {
                    gm_l33 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t448 = gm_l33;
                        uint64_t t449 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        uint8_t t450 = (t448 < t449);
                        if (!t450) goto lb447;
                        {
                            uint64_t t453 = gm_l33;
                            uint64_t t454 = (uintptr_t)t453;
                            uint64_t t455 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                            gm_tiilmfd16v3fc t458 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t454 * 24));
                            gm_l34 = t458;
                            {
                                void * t461 = (void *)(((uint8_t *)&F.l25) + 24);
                                void * t462 = (void *)((uint8_t *)&gm_l34);
                                uint8_t t463 = gm_f_set_contains_8v9ep9m(t461, t462);
                                uint8_t t464 = (uint8_t)!t463;
                                if (!t464) goto ie460;
                                {
                                    gm_l31 = ((uint8_t)0);
                                }
                                goto ix460;
ie460:;
ix460:;
                            }
                            goto me456;
mn457:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 69, 17);
me456:;
                        }
lc447:;
                        {
                            uint64_t t467 = gm_l33;
                            uint64_t r468; bool o469 = __builtin_add_overflow(t467, ((uint64_t)1ULL), &r468);
                            uint64_t t470 = r468;
                            gm_l33 = t470;
                        }
                    }
lb447:;
                }
            }
            {
                uint8_t t472 = gm_l31;
                if (!t472) goto ie471;
                {
                    gm_t88rcucg1b7uk t474 = gm_l5;
                    gm_tmn7n0n9raou5 t475;
                    GM_ST(uint8_t, (uint8_t *)&t475 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t475 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t475 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t475 + 0), ((uint8_t)9ULL));
                    void * t476 = gm_l1;
                    gm_t8jsc4n9vp2ib t477;
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t477 + 0), t475);
                    GM_ST(void *, ((uint8_t *)&t477 + 8), t476);
                    gm_teti411eujvf4 t478;
                    memset(&t478, 0, sizeof t478);
                    GM_ST(uint8_t, ((uint8_t *)&t478 + 0), ((uint8_t)32ULL));
                    GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t478 + 8), t477);
                    void * t479 = gm_l1;
                    gm_tl874t2atnlh3 t480 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t479) + 72));
                    void * t481 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t474, &t478, t480);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t482 = 0;
                    F.ret = t481;
                    goto gm_fret;
                }
                goto ix471;
ie471:;
ix471:;
            }
            goto ix440;
ie440:;
ix440:;
            goto me362;
mn437:;
ma365:;
            goto me362;
mn483:;
ma366:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 56, 9);
me362:;
        }
        goto ix350;
ie350:;
ix350:;
    }
    {
        void * t486 = gm_l0;
        gm_t88rcucg1b7uk t487 = gm_l4;
        gm_t88rcucg1b7uk t488 = gm_l5;
        uint8_t t489 = gm_f_embeds_2jpqm45(t486, GM_CONV(gm_csguankko4lmhd, t487), GM_CONV(gm_csguankko4lmhd, t488));
        if (!t489) goto ie485;
        {
            gm_t88rcucg1b7uk t491 = gm_l5;
            gm_tmn7n0n9raou5 t492;
            GM_ST(uint8_t, (uint8_t *)&t492 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t492 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t492 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t492 + 0), ((uint8_t)6ULL));
            void * t493 = gm_l1;
            gm_t8jsc4n9vp2ib t494;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t494 + 0), t492);
            GM_ST(void *, ((uint8_t *)&t494 + 8), t493);
            gm_teti411eujvf4 t495;
            memset(&t495, 0, sizeof t495);
            GM_ST(uint8_t, ((uint8_t *)&t495 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t495 + 8), t494);
            void * t496 = gm_l1;
            gm_tl874t2atnlh3 t497 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t496) + 72));
            void * t498 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t491, &t495, t497);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t499 = 0;
            F.ret = t498;
            goto gm_fret;
        }
        goto ix485;
ie485:;
ix485:;
    }
    {
        void * t500 = gm_l0;
        gm_t88rcucg1b7uk t501 = gm_l4;
        gm_t88rcucg1b7uk t502 = gm_l5;
        gm_th2lqbo77pvsc t503;
        gm_f_coercion_steps_ibqvhta((void *)&t503, (void *)NULL, cx, NULL, t500, t501, t502);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t504 = t503;
        gm_b16_8 t505;
        if (((((uint8_t *)&t504))[16] != 0)) {
            gm_b16_8 t506 = GM_LD(gm_b16_8, (((uint8_t *)&t504) + 0));
            t505 = t506;
        } else {
            {
                void * t508 = gm_l0;
                void * t509 = gm_l1;
                gm_tl874t2atnlh3 t510 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t509) + 72));
                gm_t6j6smj7qoctu t511;
                void * t512 = gm_l0;
                gm_t88rcucg1b7uk t513 = gm_l5;
                gm_b16_8 t514 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t512, t513);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t511 + 0, t514);
                void * t515 = gm_l0;
                gm_t88rcucg1b7uk t516 = gm_l4;
                gm_b16_8 t517 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t515, t516);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t511 + 16, t517);
                gm_tabb710d9mq2e t518;
                gm_f_format_ns6dben((void *)&t518, (void *)NULL, cx, NULL, &t511);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t519 = t518;
                if (((((uint8_t *)&t519))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t519) + 8, gm_src4vq75vtnd3bq, 81, 26);
                gm_b16_8 t520 = GM_LD(gm_b16_8, (((uint8_t *)&t519) + 8));
                gm_b16_8 t521 = t520;
                gm_b16_8 t522 = GM_LD(gm_b16_8, ((uint8_t *)&t521));
                gm_b0_1 t523 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t508, t510, t522);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t524 = gm_l1;
            uint8_t t525 = 0;
            F.ret = t524;
            goto gm_fret;
        }
        gm_l35 = t505;
    }
    {
        void * t526 = gm_l1;
        gm_b16_8 t527 = gm_l35;
        gm_b16_8 t528 = t527;
        gm_b16_8 t529 = GM_LD(gm_b16_8, ((uint8_t *)&t528));
        void * t530 = gm_f_apply_steps_fd8cr97((void *)NULL, cx, NULL, t526, t529);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t531 = 0;
        F.ret = t530;
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
extern __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_pointer_coercion_a03b7p1(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pointer_coercion_a03b7p1 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sc */
    gm_t88rcucg1b7uk gm_l5; /* se */
    uint8_t gm_l6; /* sk */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* p */
    uint8_t gm_l9; /* c */
    gm_t88rcucg1b7uk gm_l10; /* p */
    uint8_t gm_l11; /* c */
    gm_t88rcucg1b7uk gm_l12; /* p */
    uint8_t gm_l13; /* tc */
    gm_t88rcucg1b7uk gm_l14; /* te */
    uint8_t gm_l15; /* tk */
    uint8_t gm_l16; /* c */
    gm_t88rcucg1b7uk gm_l17; /* p */
    uint8_t gm_l18; /* c */
    gm_t88rcucg1b7uk gm_l19; /* p */
    uint8_t gm_l20; /* c */
    gm_t88rcucg1b7uk gm_l21; /* p */
    gm_tkn42u7na3t81 gm_l22; /* array_elem */
    gm_t88rcucg1b7uk gm_l23; /* elem */
    gm_t88rcucg1b7uk gm_l24; /* elem */
    GM_STACK_CHECK_SHALLOW(&F, 862u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_l5 = t4;
    }
    {
        gm_l6 = ((uint8_t)0ULL);
    }
    {
        void * t5 = gm_l3;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma10:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t17 != ((uint8_t)8ULL)) goto mn16;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l7 = t18;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l8 = t19;
        {
            uint8_t t21 = gm_l7;
            gm_l4 = t21;
        }
        {
            gm_t88rcucg1b7uk t22 = gm_l8;
            gm_l5 = t22;
        }
        {
            gm_l6 = ((uint8_t)0ULL);
        }
        goto me9;
mn16:;
ma11:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t24 != ((uint8_t)9ULL)) goto mn23;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l9 = t25;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l10 = t26;
        {
            uint8_t t28 = gm_l9;
            gm_l4 = t28;
        }
        {
            gm_t88rcucg1b7uk t29 = gm_l10;
            gm_l5 = t29;
        }
        {
            gm_l6 = ((uint8_t)1ULL);
        }
        goto me9;
mn23:;
ma12:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t31 != ((uint8_t)10ULL)) goto mn30;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l11 = t32;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l12 = t33;
        {
            uint8_t t35 = gm_l11;
            gm_l4 = t35;
        }
        {
            gm_t88rcucg1b7uk t36 = gm_l12;
            gm_l5 = t36;
        }
        {
            gm_l6 = ((uint8_t)2ULL);
        }
        goto me9;
mn30:;
ma13:;
        uint8_t t38 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me9;
mn37:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 215, 5);
me9:;
    }
    {
        void * t40 = gm_l3;
        gm_t88rcucg1b7uk t41 = gm_l2;
        gm_t53at80pfdus3 t42;
        gm_f_kind_jcnt4hm((void *)&t42, t40, GM_CONV(gm_csguankko4lmhd, t41));
        gm_t53at80pfdus3 t43 = t42;
        uint8_t t44 = ((GM_LD(uint8_t, ((uint8_t *)&t43) + 0) == ((uint8_t)7ULL)));
        if (!t44) goto ie39;
        {
            uint8_t t46 = gm_l6;
            uint8_t t47 = (!(t46 == ((uint8_t)2ULL)));
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        gm_l13 = ((uint8_t)0);
    }
    {
        gm_t88rcucg1b7uk t49 = gm_l2;
        gm_l14 = t49;
    }
    {
        gm_l15 = ((uint8_t)0ULL);
    }
    {
        void * t50 = gm_l3;
        gm_t88rcucg1b7uk t51 = gm_l2;
        gm_t53at80pfdus3 t52;
        gm_f_kind_jcnt4hm((void *)&t52, t50, GM_CONV(gm_csguankko4lmhd, t51));
        gm_t53at80pfdus3 t53 = t52;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        switch (t60) { case ((uint8_t)0ULL): goto ma58; case ((uint8_t)1ULL): goto ma58; case ((uint8_t)2ULL): goto ma58; case ((uint8_t)3ULL): goto ma58; case ((uint8_t)4ULL): goto ma58; case ((uint8_t)5ULL): goto ma58; case ((uint8_t)6ULL): goto ma58; case ((uint8_t)7ULL): goto ma58; case ((uint8_t)8ULL): goto ma55; case ((uint8_t)9ULL): goto ma56; case ((uint8_t)10ULL): goto ma57; case ((uint8_t)11ULL): goto ma58; case ((uint8_t)12ULL): goto ma58; case ((uint8_t)13ULL): goto ma58; case ((uint8_t)14ULL): goto ma58; case ((uint8_t)15ULL): goto ma58; case ((uint8_t)16ULL): goto ma58; case ((uint8_t)17ULL): goto ma58; case ((uint8_t)18ULL): goto ma58; case ((uint8_t)19ULL): goto ma58; case ((uint8_t)20ULL): goto ma58; case ((uint8_t)21ULL): goto ma58; case ((uint8_t)22ULL): goto ma58; case ((uint8_t)23ULL): goto ma58; case ((uint8_t)24ULL): goto ma58; case ((uint8_t)25ULL): goto ma58; case ((uint8_t)26ULL): goto ma58; default: goto ma58; }
ma55:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t62 != ((uint8_t)8ULL)) goto mn61;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)&t53) + 8) + 0));
        gm_l16 = t63;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t53) + 8) + 4));
        gm_l17 = t64;
        {
            uint8_t t66 = gm_l16;
            gm_l13 = t66;
        }
        {
            gm_t88rcucg1b7uk t67 = gm_l17;
            gm_l14 = t67;
        }
        {
            gm_l15 = ((uint8_t)0ULL);
        }
        goto me54;
mn61:;
ma56:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t69 != ((uint8_t)9ULL)) goto mn68;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)&t53) + 8) + 0));
        gm_l18 = t70;
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t53) + 8) + 4));
        gm_l19 = t71;
        {
            uint8_t t73 = gm_l18;
            gm_l13 = t73;
        }
        {
            gm_t88rcucg1b7uk t74 = gm_l19;
            gm_l14 = t74;
        }
        {
            gm_l15 = ((uint8_t)1ULL);
        }
        goto me54;
mn68:;
ma57:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t76 != ((uint8_t)10ULL)) goto mn75;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)&t53) + 8) + 0));
        gm_l20 = t77;
        gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t53) + 8) + 4));
        gm_l21 = t78;
        {
            uint8_t t80 = gm_l20;
            gm_l13 = t80;
        }
        {
            gm_t88rcucg1b7uk t81 = gm_l21;
            gm_l14 = t81;
        }
        {
            gm_l15 = ((uint8_t)2ULL);
        }
        goto me54;
mn75:;
ma58:;
        uint8_t t83 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me54;
mn82:;
ma59:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 237, 5);
me54:;
    }
    {
        uint8_t t85 = gm_l4;
        if (!t85) goto ie84;
        uint8_t t86 = gm_l13;
        uint8_t t87 = (uint8_t)!t86;
        if (!t87) goto ie84;
        {
            uint8_t t89 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix84;
ie84:;
ix84:;
    }
    {
        uint8_t t91 = gm_l6;
        uint8_t t92 = gm_l15;
        uint8_t t93 = ((t91 == t92));
        if (!t93) goto ie90;
        {
            gm_t88rcucg1b7uk t95 = gm_l5;
            gm_t88rcucg1b7uk t96 = gm_l14;
            gm_t88rcucg1b7uk t97 = t95;
            gm_t88rcucg1b7uk t98 = t96;
            uint8_t t99 = (gm_eq9m6mvtmgde(((uint8_t *)&t97), ((uint8_t *)&t98)));
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
        }
        goto ix90;
ie90:;
ix90:;
    }
    {
        gm_tkn42u7na3t81 t101;
        memset(&t101, 0, sizeof t101);
        gm_l22 = t101;
    }
    {
        void * t102 = gm_l3;
        gm_t88rcucg1b7uk t103 = gm_l5;
        gm_t53at80pfdus3 t104;
        gm_f_kind_jcnt4hm((void *)&t104, t102, GM_CONV(gm_csguankko4lmhd, t103));
        gm_t53at80pfdus3 t105 = t104;
        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&t105) + 0));
        switch (t111) { case ((uint8_t)0ULL): goto ma109; case ((uint8_t)1ULL): goto ma109; case ((uint8_t)2ULL): goto ma109; case ((uint8_t)3ULL): goto ma109; case ((uint8_t)4ULL): goto ma109; case ((uint8_t)5ULL): goto ma109; case ((uint8_t)6ULL): goto ma109; case ((uint8_t)7ULL): goto ma109; case ((uint8_t)8ULL): goto ma109; case ((uint8_t)9ULL): goto ma109; case ((uint8_t)10ULL): goto ma109; case ((uint8_t)11ULL): goto ma107; case ((uint8_t)12ULL): goto ma109; case ((uint8_t)13ULL): goto ma109; case ((uint8_t)14ULL): goto ma109; case ((uint8_t)15ULL): goto ma109; case ((uint8_t)16ULL): goto ma109; case ((uint8_t)17ULL): goto ma109; case ((uint8_t)18ULL): goto ma109; case ((uint8_t)19ULL): goto ma109; case ((uint8_t)20ULL): goto ma108; case ((uint8_t)21ULL): goto ma109; case ((uint8_t)22ULL): goto ma109; case ((uint8_t)23ULL): goto ma109; case ((uint8_t)24ULL): goto ma109; case ((uint8_t)25ULL): goto ma109; case ((uint8_t)26ULL): goto ma109; default: goto ma109; }
ma107:;
        uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&t105) + 0));
        if (t113 != ((uint8_t)11ULL)) goto mn112;
        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t105) + 8) + 8));
        gm_l23 = t114;
        void * t116 = gm_l3;
        gm_t88rcucg1b7uk t117 = gm_l5;
        uint8_t t118 = gm_f_src_array_ok_og10umr(t116, GM_CONV(gm_csguankko4lmhd, t117));
        if (!t118) goto ie115;
        {
            gm_t88rcucg1b7uk t120 = gm_l23;
            gm_tkn42u7na3t81 t121;
            GM_ST(uint8_t, (uint8_t *)&t121 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t121 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t121), t120);
            ((uint8_t *)&t121)[4] = 1;
            gm_l22 = t121;
        }
        goto ix115;
ie115:;
ix115:;
        goto me106;
mn112:;
ma108:;
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&t105) + 0));
        if (t123 != ((uint8_t)20ULL)) goto mn122;
        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t105) + 8) + 4));
        gm_l24 = t124;
        {
            gm_t88rcucg1b7uk t126 = gm_l24;
            gm_tkn42u7na3t81 t127;
            GM_ST(uint8_t, (uint8_t *)&t127 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t127 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t127), t126);
            ((uint8_t *)&t127)[4] = 1;
            gm_l22 = t127;
        }
        goto me106;
mn122:;
ma109:;
        goto me106;
mn128:;
ma110:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 259, 5);
me106:;
    }
    {
        uint8_t t131 = gm_l6;
        uint8_t t132 = ((t131 == ((uint8_t)0ULL)));
        if (!t132) goto ie130;
        uint8_t t133 = gm_l15;
        uint8_t t134 = ((t133 == ((uint8_t)1ULL)));
        if (!t134) goto ie130;
        {
            uint8_t t136 = 0;
            gm_t88rcucg1b7uk t137 = gm_l5;
            gm_t88rcucg1b7uk t138 = gm_l14;
            gm_t88rcucg1b7uk t139 = t137;
            gm_t88rcucg1b7uk t140 = t138;
            uint8_t t141 = (gm_eq9m6mvtmgde(((uint8_t *)&t139), ((uint8_t *)&t140)));
            if (!t141) {
                gm_tkn42u7na3t81 t142 = gm_l22;
                gm_t88rcucg1b7uk t143 = gm_l14;
                gm_tkn42u7na3t81 t144;
                GM_ST(uint8_t, (uint8_t *)&t144 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t144 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t144), t143);
                ((uint8_t *)&t144)[4] = 1;
                gm_tkn42u7na3t81 t145 = t142;
                gm_tkn42u7na3t81 t146 = t144;
                uint8_t t147 = (gm_eqs65r2l0loh(((uint8_t *)&t145), ((uint8_t *)&t146)));
                t136 = t147;
            } else t136 = 1;
            uint8_t t148 = 0;
            F.ret = t136;
            goto gm_fret;
        }
        goto ix130;
ie130:;
ix130:;
    }
    {
        uint8_t t150 = gm_l6;
        uint8_t t151 = ((t150 == ((uint8_t)0ULL)));
        if (!t151) goto ie149;
        uint8_t t152 = gm_l15;
        uint8_t t153 = ((t152 == ((uint8_t)2ULL)));
        if (!t153) goto ie149;
        {
            gm_tkn42u7na3t81 t155 = gm_l22;
            gm_t88rcucg1b7uk t156 = gm_l14;
            gm_tkn42u7na3t81 t157;
            GM_ST(uint8_t, (uint8_t *)&t157 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t157 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t157), t156);
            ((uint8_t *)&t157)[4] = 1;
            gm_tkn42u7na3t81 t158 = t155;
            gm_tkn42u7na3t81 t159 = t157;
            uint8_t t160 = (gm_eqs65r2l0loh(((uint8_t *)&t158), ((uint8_t *)&t159)));
            uint8_t t161 = 0;
            F.ret = t160;
            goto gm_fret;
        }
        goto ix149;
ie149:;
ix149:;
    }
    {
        uint8_t t162 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_shape_m0o6aok(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_shape_m0o6aok F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        void * t6 = gm_l1;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 0) + 8);
        uint8_t t8 = (!(t5 == t7));
        if (!t8) {
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 16));
            void * t11 = gm_l1;
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 16));
            gm_t88rcucg1b7uk t13 = t10;
            gm_t88rcucg1b7uk t14 = t12;
            uint8_t t15 = (!gm_eq9m6mvtmgde(((uint8_t *)&t13), ((uint8_t *)&t14)));
            t3 = t15;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t19 = gm_l0;
            uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
            gm_l2 = t20;
        }
        {
            for (;;) {
                uint64_t t22 = gm_l3;
                uint64_t t23 = gm_l2;
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t t28 = t27;
                    uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&t28));
                    gm_l4 = t31;
                    {
                        uint64_t t34 = gm_l4;
                        uint64_t t35 = (uintptr_t)t34;
                        void * t36 = gm_l0;
                        uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
                        if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_src4vq75vtnd3bq, 280, 12);
                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 0)) + t35 * 4));
                        uint64_t t39 = gm_l4;
                        uint64_t t40 = (uintptr_t)t39;
                        void * t41 = gm_l1;
                        uint64_t t42 = GM_LD(uintptr_t, (((uint8_t *)t41) + 0) + 8);
                        if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_src4vq75vtnd3bq, 280, 31);
                        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t41) + 0)) + t40 * 4));
                        gm_t88rcucg1b7uk t44 = t38;
                        gm_t88rcucg1b7uk t45 = t43;
                        uint8_t t46 = (!gm_eq9m6mvtmgde(((uint8_t *)&t44), ((uint8_t *)&t45)));
                        if (!t46) goto ie33;
                        {
                            uint8_t t48 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 279, 5);
me29:;
                }
lc21:;
                {
                    uint64_t t50 = gm_l3;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l3 = t53;
                }
            }
lb21:;
        }
    }
    {
        uint8_t t54 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3) {
    struct gm_fr_steps_for_768gb3b F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    uint8_t gm_l3; /* lift */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_6canco6((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        uint8_t t6 = gm_l3;
        void * t7 = (void *)((uint8_t *)&F.l4);
        uint8_t t8 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t3, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t8) goto ie2;
        {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_th2lqbo77pvsc t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[16] = 1;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_th2lqbo77pvsc t13;
        memset(&t13, 0, sizeof t13);
        uint8_t t14 = 0;
        F.ret = t13;
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
__attribute__((aligned(4))) uint8_t gm_f_steps_into_vts72l5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3, void * a4) {
    struct gm_fr_steps_into_vts72l5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    uint8_t gm_l3; /* lift */
    void * gm_l4; /* out */
    gm_tmn7n0n9raou5 gm_l5; /* k */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* e */
    gm_t88rcucg1b7uk gm_l8; /* o1 */
    gm_t88rcucg1b7uk gm_l9; /* f */
    gm_t88rcucg1b7uk gm_l10; /* o2 */
    uint32_t gm_l11; /* i */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    GM_STACK_CHECK(&F, 866u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = t3;
        gm_t88rcucg1b7uk t6 = t4;
        uint8_t t7 = (gm_eq9m6mvtmgde(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        gm_t88rcucg1b7uk t13 = gm_l1;
        gm_t88rcucg1b7uk t14 = gm_l2;
        gm_tbgiuvo2j5vcs t15 = gm_f_base_coercion_t3tgsf0((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbgiuvo2j5vcs t16 = t15;
        if (!((((uint8_t *)&t16))[8] != 0)) goto ie10;
        gm_tmn7n0n9raou5 t17 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&t16));
        gm_l5 = t17;
        {
            void * t19 = gm_l4;
            gm_tmn7n0n9raou5 t20 = gm_l5;
            gm_t88rcucg1b7uk t21 = gm_l2;
            gm_tm7p68clol9c3 t22;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t22 + 0), t20);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22 + 8), t21);
            gm_tftb1odc739t5 t23 = gm_f_push_f21bjke(t19, t22);
            gm_tftb1odc739t5 t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src4vq75vtnd3bq, 108, 9);
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
        }
        {
            uint8_t t26 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        void * t29 = (void *)(((uint8_t *)t28) + 8);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = gm_l3;
        if (!t31) goto ie30;
        void * t32 = gm_l6;
        gm_t88rcucg1b7uk t33 = gm_l1;
        gm_t53at80pfdus3 t34;
        gm_f_kind_jcnt4hm((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
        gm_t53at80pfdus3 t35 = t34;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t36 != ((uint8_t)14ULL)) goto ie30;
        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t35) + 8) + 0));
        gm_l7 = t37;
        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t35) + 8) + 4));
        gm_l8 = t38;
        void * t39 = gm_l6;
        gm_t88rcucg1b7uk t40 = gm_l2;
        gm_t53at80pfdus3 t41;
        gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
        gm_t53at80pfdus3 t42 = t41;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
        if (t43 != ((uint8_t)14ULL)) goto ie30;
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t42) + 8) + 0));
        gm_l9 = t44;
        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t42) + 8) + 4));
        gm_l10 = t45;
        gm_t88rcucg1b7uk t46 = gm_l8;
        gm_t88rcucg1b7uk t47 = gm_l10;
        gm_t88rcucg1b7uk t48 = t46;
        gm_t88rcucg1b7uk t49 = t47;
        uint8_t t50 = (gm_eq9m6mvtmgde(((uint8_t *)&t48), ((uint8_t *)&t49)));
        if (!t50) goto ie30;
        {
            void * t53 = gm_l0;
            gm_t88rcucg1b7uk t54 = gm_l7;
            gm_t88rcucg1b7uk t55 = gm_l9;
            gm_tvbp5g7fh5fh0 t56;
            gm_f_lift_variant_ptko7np((void *)&t56, (void *)NULL, cx, NULL, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvbp5g7fh5fh0 t57 = t56;
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
            if (t58 != ((uint8_t)0ULL)) goto ie52;
            uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&t57) + 8));
            gm_l11 = t59;
            {
                void * t61 = gm_l4;
                uint32_t t62 = gm_l11;
                gm_tmn7n0n9raou5 t63;
                GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)4ULL));
                GM_ST(uint32_t, ((uint8_t *)&t63 + 4), t62);
                gm_t88rcucg1b7uk t64 = gm_l2;
                gm_tm7p68clol9c3 t65;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t65 + 0), t63);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t65 + 8), t64);
                gm_tftb1odc739t5 t66 = gm_f_push_f21bjke(t61, t65);
                gm_tftb1odc739t5 t67 = t66;
                if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_src4vq75vtnd3bq, 115, 13);
                gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
            }
            {
                uint8_t t69 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix52;
ie52:;
ix52:;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t70 = gm_l6;
        gm_t88rcucg1b7uk t71 = gm_l2;
        gm_t53at80pfdus3 t72;
        gm_f_kind_jcnt4hm((void *)&t72, t70, GM_CONV(gm_csguankko4lmhd, t71));
        gm_t53at80pfdus3 t73 = t72;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
        switch (t79) { case ((uint8_t)0ULL): goto ma77; case ((uint8_t)1ULL): goto ma77; case ((uint8_t)2ULL): goto ma77; case ((uint8_t)3ULL): goto ma77; case ((uint8_t)4ULL): goto ma77; case ((uint8_t)5ULL): goto ma77; case ((uint8_t)6ULL): goto ma77; case ((uint8_t)7ULL): goto ma77; case ((uint8_t)8ULL): goto ma77; case ((uint8_t)9ULL): goto ma77; case ((uint8_t)10ULL): goto ma77; case ((uint8_t)11ULL): goto ma77; case ((uint8_t)12ULL): goto ma77; case ((uint8_t)13ULL): goto ma75; case ((uint8_t)14ULL): goto ma76; case ((uint8_t)15ULL): goto ma77; case ((uint8_t)16ULL): goto ma77; case ((uint8_t)17ULL): goto ma77; case ((uint8_t)18ULL): goto ma77; case ((uint8_t)19ULL): goto ma77; case ((uint8_t)20ULL): goto ma77; case ((uint8_t)21ULL): goto ma77; case ((uint8_t)22ULL): goto ma77; case ((uint8_t)23ULL): goto ma77; case ((uint8_t)24ULL): goto ma77; case ((uint8_t)25ULL): goto ma77; case ((uint8_t)26ULL): goto ma77; default: goto ma77; }
ma75:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
        if (t81 != ((uint8_t)13ULL)) goto mn80;
        gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t73) + 8));
        gm_l12 = t82;
        {
            void * t85 = gm_l0;
            gm_t88rcucg1b7uk t86 = gm_l1;
            gm_t88rcucg1b7uk t87 = gm_l12;
            uint8_t t88 = gm_l3;
            void * t89 = gm_l4;
            uint8_t t90 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t85, t86, t87, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t91 = (uint8_t)!t90;
            if (!t91) goto ie84;
            {
                uint8_t t93 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix84;
ie84:;
ix84:;
        }
        {
            void * t94 = gm_l4;
            gm_tmn7n0n9raou5 t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t96 = gm_l2;
            gm_tm7p68clol9c3 t97;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t97 + 0), t95);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t97 + 8), t96);
            gm_tftb1odc739t5 t98 = gm_f_push_f21bjke(t94, t97);
            gm_tftb1odc739t5 t99 = t98;
            if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 1, gm_src4vq75vtnd3bq, 122, 13);
            gm_b0_1 t100 = GM_LD(gm_b0_1, (((uint8_t *)&t99) + 1));
        }
        {
            uint8_t t101 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me74;
mn80:;
ma76:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
        if (t103 != ((uint8_t)14ULL)) goto mn102;
        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t73) + 8) + 4));
        gm_l13 = t104;
        {
            void * t107 = gm_l0;
            gm_t88rcucg1b7uk t108 = gm_l1;
            gm_t88rcucg1b7uk t109 = gm_l13;
            uint8_t t110 = gm_l3;
            void * t111 = gm_l4;
            uint8_t t112 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t107, t108, t109, t110, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t113 = (uint8_t)!t112;
            if (!t113) goto ie106;
            {
                uint8_t t115 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix106;
ie106:;
ix106:;
        }
        {
            void * t116 = gm_l4;
            gm_tmn7n0n9raou5 t117;
            GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)3ULL));
            gm_t88rcucg1b7uk t118 = gm_l2;
            gm_tm7p68clol9c3 t119;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t119 + 0), t117);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t119 + 8), t118);
            gm_tftb1odc739t5 t120 = gm_f_push_f21bjke(t116, t119);
            gm_tftb1odc739t5 t121 = t120;
            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_src4vq75vtnd3bq, 127, 13);
            gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
        }
        {
            uint8_t t123 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me74;
mn102:;
ma77:;
        uint8_t t125 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me74;
mn124:;
ma78:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 119, 5);
me74:;
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

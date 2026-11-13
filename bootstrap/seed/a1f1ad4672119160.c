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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; uint8_t f4; uint8_t pe[7]; } gm_tg9l3k9j9m9b9;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?FloatSuffix`");
typedef struct { void * f0; uint8_t f1; uint8_t p2[7]; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_t4c7ipjj9s3fp f5; gm_trqfvmjtt4sij f6; } gm_tad8216qc9dro;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg9l3k9j9m9b9) == 72 && _Alignof(gm_tg9l3k9j9m9b9) == 8 && offsetof(gm_tg9l3k9j9m9b9, f0) == 0 && offsetof(gm_tg9l3k9j9m9b9, f1) == 16 && offsetof(gm_tg9l3k9j9m9b9, f2) == 32 && offsetof(gm_tg9l3k9j9m9b9, f3) == 48 && offsetof(gm_tg9l3k9j9m9b9, f4) == 64, "layout of `Dangling`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tad8216qc9dro) == 248 && _Alignof(gm_tad8216qc9dro) == 8 && offsetof(gm_tad8216qc9dro, f0) == 0 && offsetof(gm_tad8216qc9dro, f1) == 8 && offsetof(gm_tad8216qc9dro, f2) == 16 && offsetof(gm_tad8216qc9dro, f3) == 72 && offsetof(gm_tad8216qc9dro, f4) == 112 && offsetof(gm_tad8216qc9dro, f5) == 152 && offsetof(gm_tad8216qc9dro, f6) == 208, "layout of `Checked`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ta42iqot1auae v0; gm_tghmb53v8qi1e v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t6e9d80inoppc;
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t6e9d80inoppc) == 112 && _Alignof(gm_t6e9d80inoppc) == 8 && offsetof(gm_t6e9d80inoppc, f0) == 0 && offsetof(gm_t6e9d80inoppc, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_analyze_np1m50h {
    gm_frame h;
    gm_tad8216qc9dro l4; /* c */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_assigned_53ipnmg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_binds_busqeo0 {
    gm_frame h;
    void * l0; /* c */
    uint32_t l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_block_stmts_c2nnk6m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bool_lit_q44074e {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_file_ok_4j31vl0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_259eng6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o40bhnh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_array_p7ehais {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mutate_oi1s4vs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_needless_pub_jqsnq13 {
    gm_frame h;
    gm_tkacitepg5fk0 l11; /* a */
    gm_tv2uf4leil5hq l12; /* b */
    gm_tagrkbqh054h7 l13; /* state */
    gm_t6qooom8qa2do l14; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_path_eq_l3qbb7v {
    gm_frame h;
    gm_tbci5ospk5nnn l20; /* x */
    gm_tbci5ospk5nnn l21; /* y */
    uint8_t ret;
};
struct gm_fr_root_oq23mha {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_run_checks_c17ks6r {
    gm_frame h;
    gm_b16_8 l1; /* scoped */
    gm_trqfvmjtt4sij l4; /* fn_decl */
    gm_t6qooom8qa2do l10; /* v */
    gm_trqfvmjtt4sij l17; /* calls */
    gm_trqfvmjtt4sij l29; /* calls */
    gm_trqfvmjtt4sij l37; /* growth */
    gm_trqfvmjtt4sij l42; /* dangling */
    gm_tg9l3k9j9m9b9 l45; /* d */
    gm_tsoie1ppgdvcg l51; /* l */
    gm_tv2uf4leil5hq l65; /* b */
    gm_tkacitepg5fk0 l69; /* a */
    gm_tagrkbqh054h7 l70; /* state */
    gm_t6qooom8qa2do l73; /* v */
    gm_trqfvmjtt4sij l74; /* kept */
    gm_tj5f8vajf35o7 l77; /* f */
    gm_b0_1 ret;
};
struct gm_fr_same_value_7pehjnc {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_use_of_q9agaul {
    gm_frame h;
    void * ret;
};
struct gm_fr_visit_unu9ms6 {
    gm_frame h;
    void * l0; /* c */
    uint32_t l1; /* f */
    void * l3; /* calls */
    gm_tsth9qu1dv0g3 l9; /* callee */
    gm_t710lsrq1m8gk l23; /* x */
    gm_t710lsrq1m8gk l40; /* x */
    gm_b0_1 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
bool gm_eqbqb4gb1d92_2(const uint8_t *a, const uint8_t *b);
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_assigned_53ipnmg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_binds_busqeo0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_stmts_c2nnk6m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_contains_14ai471(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_file_ok_4j31vl0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_get_ptr_1a4voen(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_q3uemcr(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_growth_summaries_dguakce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_tvpcibu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_is_array_p7ehais(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_list_1aqn740(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_fsi5en1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_copies_vc6lpss(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_needless_pub_jqsnq13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_path_eq_l3qbb7v(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_possible_dangling_addres_d403vr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_present_only_bindings_ug2i6dq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s4q134v(void * a0, gm_tf5srh4mp4b3i a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3g1tdpr(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_he2q4rq(void * a0, gm_tcc1rq4n1avb2 a1, const void * a2);
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_root_oq23mha(void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_run_checks_c17ks6r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_same_value_7pehjnc(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_stale_element_pointers_jjgn0ll(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void * gm_f_use_of_q9agaul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_unu9ms6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3);
extern uint8_t gm_c9b4lh7gft4[4] __attribute__((aligned(4)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1ft2nrvnhcbp[10];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_sa0amdcmvej1l[14];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_skr6p9lfc5ftp[11];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const char gm_srco9vbah234t6e[];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svqdrhms9i231[7];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2) {
    struct gm_fr_analyze_np1m50h F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* program */
    gm_b16_8 gm_l2; /* scoped */
    uint64_t gm_l3; /* cn */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l3;
            void * t4 = gm_l0;
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 1968) + 0) + 8);
            uint8_t t6 = (t3 < t5);
            if (!t6) goto lb2;
            {
                uint64_t t10 = gm_l3;
                uint64_t t11 = (uintptr_t)t10;
                void * t12 = gm_l0;
                uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 1968) + 0) + 8);
                if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srco9vbah234t6e, 77, 12);
                uint8_t t14 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t12) + 1968) + 0)) + t11 * 88) + 72) + 0) == ((uint8_t)0ULL)));
                if (!t14) goto ie9;
                {
                    {
                        void * t17 = gm_l0;
                        uint64_t t18 = gm_l3;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 1968) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srco9vbah234t6e, 78, 38);
                        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 1968) + 0)) + t19 * 88) + 16));
                        uint64_t t23 = gm_l3;
                        uint64_t t24 = (uintptr_t)t23;
                        void * t25 = gm_l0;
                        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 1968) + 0) + 8);
                        if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srco9vbah234t6e, 78, 63);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 1968) + 0)) + t24 * 88) + 28));
                        void * t28 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t17, t22, t27);
                        if (gm_unw.kind) {
                            goto gm_ll7;
                        }
                    }
                }
                goto ix9;
ie9:;
ix9:;
            }
            {
                uint64_t t29 = gm_l3;
                uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srco9vbah234t6e, 80, 9);
                uint64_t t32 = r30;
                gm_l3 = t32;
            }
lc2:;
        }
gm_ll7:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb2; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc2; }
        }
        goto gm_lad;
lb2:;
    }
    {
        memset((uint8_t *)&F.l4 + 9, 0, 7);
        void * t33 = gm_l0;
        GM_ST(void *, (uint8_t *)&F.l4 + 0, t33);
        uint8_t t34 = gm_l1;
        GM_ST(uint8_t, (uint8_t *)&F.l4 + 8, t34);
        gm_f_hash_map_tvpcibu((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l4 + 16)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 72)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 112)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_hash_map_t9h7o8p((void *)&(*(gm_t4c7ipjj9s3fp *)((uint8_t *)&F.l4 + 152)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_1aqn740((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4 + 208)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
            uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 240) + 0) + 8);
            gm_l5 = t38;
        }
        {
            for (;;) {
                uint64_t t40 = gm_l6;
                uint64_t t41 = gm_l5;
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l6;
                    uint64_t t46 = t45;
                    {
                        void * t50 = (void *)(((uint8_t *)&F.l4) + 72);
                        gm_tftb1odc739t5 t51 = gm_f_push_vd1290p(t50, ((uint8_t)0));
                        gm_tftb1odc739t5 t52 = t51;
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srco9vbah234t6e, 91, 39);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                    }
                    goto me47;
mn48:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 91, 5);
me47:;
                }
lc39:;
                {
                    uint64_t t55 = gm_l6;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l6 = t58;
                }
            }
lb39:;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t60 = gm_l0;
            uint64_t t61 = GM_LD(uintptr_t, ((((uint8_t *)t60) + 344) + 0) + 8);
            gm_l7 = t61;
        }
        {
            for (;;) {
                uint64_t t63 = gm_l8;
                uint64_t t64 = gm_l7;
                uint8_t t65 = (t63 < t64);
                if (!t65) goto lb62;
                {
                    uint64_t t68 = gm_l8;
                    uint64_t t69 = t68;
                    {
                        void * t73 = (void *)(((uint8_t *)&F.l4) + 112);
                        gm_tftb1odc739t5 t74 = gm_f_push_vd1290p(t73, ((uint8_t)0));
                        gm_tftb1odc739t5 t75 = t74;
                        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srco9vbah234t6e, 92, 35);
                        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
                    }
                    goto me70;
mn71:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 92, 5);
me70:;
                }
lc62:;
                {
                    uint64_t t78 = gm_l8;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l8 = t81;
                }
            }
lb62:;
        }
    }
    {
        void * t82 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t83 = gm_l2;
        gm_b0_1 t84 = gm_f_run_checks_c17ks6r((void *)NULL, cx, NULL, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_trqfvmjtt4sij t85 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&F.l4) + 208));
        uint8_t t86 = 0;
        F.ret = t85;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_assigned_53ipnmg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_assigned_53ipnmg F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* place */
    gm_tcc1rq4n1avb2 gm_l3; /* k */
    void * gm_l4; /* u */
    uint8_t gm_l5; /* seen */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tl874t2atnlh3 gm_l8; /* w */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        void * t4 = gm_l2;
        gm_b0_1 t5 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t7 = 0;
        void * t8 = gm_l2;
        uint8_t t9;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t12 != ((uint8_t)15ULL)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 378, 8);
me10:;
        if (!t9) {
            void * t14 = gm_l2;
            uint8_t t15;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            if (t18 != ((uint8_t)16ULL)) goto mn17;
            t15 = ((uint8_t)1);
            goto me16;
mn17:;
            t15 = ((uint8_t)0);
            goto me16;
mn19:;
            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 378, 35);
me16:;
            t7 = t15;
        } else t7 = 1;
        if (!t7) goto ie6;
        {
            void * t22 = gm_l0;
            uint32_t t23 = gm_l1;
            void * t24 = gm_l2;
            gm_t736djc66po0l t25 = gm_f_root_oq23mha(t22, t23, t24);
            gm_t736djc66po0l t26 = t25;
            if (!((((uint8_t *)&t26))[8] != 0)) goto ie21;
            gm_tcc1rq4n1avb2 t27 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t26));
            gm_l3 = t27;
            {
                void * t29 = gm_l0;
                gm_tcc1rq4n1avb2 t30 = gm_l3;
                void * t31 = gm_f_use_of_q9agaul((void *)NULL, cx, NULL, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t31;
            }
            {
                gm_l5 = ((uint8_t)0);
            }
            {
                {
                    void * t33 = gm_l4;
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t33) + 0) + 0));
                    gm_l6 = t34;
                }
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t36 = gm_l7;
                        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t38 = (t36 < t37);
                        if (!t38) goto lb35;
                        {
                            uint64_t t41 = gm_l7;
                            uint64_t t42 = (uintptr_t)t41;
                            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t42 * 12));
                            gm_l8 = t46;
                            {
                                gm_tl874t2atnlh3 t49 = gm_l8;
                                void * t50 = gm_l2;
                                gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 208));
                                gm_tl874t2atnlh3 t52 = t49;
                                gm_tl874t2atnlh3 t53 = t51;
                                uint8_t t54 = (gm_eqff85ac10o3(((uint8_t *)&t52), ((uint8_t *)&t53)));
                                if (!t54) goto ie48;
                                {
                                    gm_l5 = ((uint8_t)1);
                                }
                                goto ix48;
ie48:;
ix48:;
                            }
                            goto me44;
mn45:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 382, 13);
me44:;
                        }
lc35:;
                        {
                            uint64_t t57 = gm_l7;
                            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                            uint64_t t60 = r58;
                            gm_l7 = t60;
                        }
                    }
lb35:;
                }
            }
            {
                uint8_t t62 = gm_l5;
                uint8_t t63 = (uint8_t)!t62;
                if (!t63) goto ie61;
                {
                    void * t65 = gm_l4;
                    void * t66 = (void *)(((uint8_t *)t65) + 0);
                    void * t67 = gm_l2;
                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 208));
                    gm_tftb1odc739t5 t69 = gm_f_push_0mk7udh(t66, GM_CONV(gm_cseahvg8v7p0vs, t68));
                    gm_tftb1odc739t5 t70 = t69;
                    if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srco9vbah234t6e, 385, 27);
                    gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                }
                goto ix61;
ie61:;
ix61:;
            }
            goto ix21;
ie21:;
ix21:;
        }
        goto ix6;
ie6:;
ix6:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_binds_busqeo0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_binds_busqeo0 F;
    void * gm_l2; /* p */
    uint32_t gm_l3; /* l */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t5 != ((uint8_t)1ULL)) goto mn4;
        uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t6;
        void * t7 = F.l0;
        void * t8 = (void *)(((uint8_t *)t7) + 152);
        uint32_t t9 = F.l1;
        uint32_t t10 = gm_l3;
        gm_tcc1rq4n1avb2 t11;
        GM_ST(uint32_t, ((uint8_t *)&t11 + 0), t9);
        GM_ST(uint32_t, ((uint8_t *)&t11 + 4), t10);
        gm_tftb1odc739t5 t12 = gm_f_put_3g1tdpr(t8, t11, ((uint8_t)1));
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srco9vbah234t6e, 345, 21);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        goto me3;
mn4:;
        void * t16 = gm_l2;
        gm_closure t17 = ((gm_closure){(void *)gm_f_fn_o40bhnh, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 344, 5);
me3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_block_stmts_c2nnk6m(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_block_stmts_c2nnk6m F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* stmts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    uint32_t gm_l6; /* local */
    gm_b16_8 gm_l7; /* locals */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t9805qtonbsag gm_l10; /* l */
    uint32_t gm_l11; /* id */
    void * gm_l12; /* place */
    void * gm_l13; /* value */
    gm_b16_8 gm_l14; /* places */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* p */
    void * gm_l18; /* x */
    GM_STACK_CHECK(&F, 975u);
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
                    gm_trcmmd12cefb1 t15 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 32));
                    gm_l5 = t15;
                    {
                        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        switch (t24) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; default: goto ma22; }
ma18:;
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t26 != ((uint8_t)0ULL)) goto mn25;
                        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l6 = t27;
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 152);
                        uint32_t t30 = gm_l1;
                        uint32_t t31 = gm_l6;
                        gm_tcc1rq4n1avb2 t32;
                        GM_ST(uint32_t, ((uint8_t *)&t32 + 0), t30);
                        GM_ST(uint32_t, ((uint8_t *)&t32 + 4), t31);
                        gm_tftb1odc739t5 t33 = gm_f_put_3g1tdpr(t29, t32, ((uint8_t)1));
                        gm_tftb1odc739t5 t34 = t33;
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srco9vbah234t6e, 354, 31);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                        goto me17;
mn25:;
ma19:;
                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t37 != ((uint8_t)1ULL)) goto mn36;
                        gm_b16_8 t38 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t38;
                        {
                            {
                                gm_b16_8 t41 = gm_l7;
                                gm_l8 = t41;
                            }
                            {
                                gm_l9 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t43 = gm_l9;
                                    uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    uint8_t t45 = (t43 < t44);
                                    if (!t45) goto lb42;
                                    {
                                        uint64_t t48 = gm_l9;
                                        uint64_t t49 = (uintptr_t)t48;
                                        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                        gm_t9805qtonbsag t53 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t49 * 8));
                                        gm_l10 = t53;
                                        {
                                            if (!((((uint8_t *)&gm_l10))[4] != 0)) goto ie55;
                                            uint32_t t56 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                                            gm_l11 = t56;
                                            {
                                                void * t58 = gm_l0;
                                                void * t59 = (void *)(((uint8_t *)t58) + 152);
                                                uint32_t t60 = gm_l1;
                                                uint32_t t61 = gm_l11;
                                                gm_tcc1rq4n1avb2 t62;
                                                GM_ST(uint32_t, ((uint8_t *)&t62 + 0), t60);
                                                GM_ST(uint32_t, ((uint8_t *)&t62 + 4), t61);
                                                gm_tftb1odc739t5 t63 = gm_f_put_3g1tdpr(t59, t62, ((uint8_t)1));
                                                gm_tftb1odc739t5 t64 = t63;
                                                if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srco9vbah234t6e, 357, 34);
                                                gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                                            }
                                            goto ix55;
ie55:;
ix55:;
                                        }
                                        goto me51;
mn52:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 356, 17);
me51:;
                                    }
lc42:;
                                    {
                                        uint64_t t67 = gm_l9;
                                        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                                        uint64_t t70 = r68;
                                        gm_l9 = t70;
                                    }
                                }
lb42:;
                            }
                        }
                        goto me17;
mn36:;
ma20:;
                        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t72 != ((uint8_t)2ULL)) goto mn71;
                        void * t73 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l12 = t73;
                        void * t74 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l13 = t74;
                        {
                            void * t76 = gm_l0;
                            uint32_t t77 = gm_l1;
                            void * t78 = gm_l12;
                            gm_b0_1 t79 = gm_f_assigned_53ipnmg((void *)NULL, cx, NULL, t76, t77, t78);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t81 = gm_l0;
                            void * t82 = gm_l12;
                            void * t83 = gm_l13;
                            uint8_t t84 = gm_f_same_value_7pehjnc(t81, t82, t83);
                            if (!t84) goto ie80;
                            void * t85 = gm_l12;
                            gm_tl874t2atnlh3 t86 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t85) + 208));
                            gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            gm_tl874t2atnlh3 t88 = t86;
                            gm_tl874t2atnlh3 t89 = t87;
                            uint8_t t90 = (!gm_eqff85ac10o3(((uint8_t *)&t88), ((uint8_t *)&t89)));
                            if (!t90) goto ie80;
                            void * t91 = gm_l13;
                            gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t91) + 208));
                            gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                            gm_tl874t2atnlh3 t94 = t92;
                            gm_tl874t2atnlh3 t95 = t93;
                            uint8_t t96 = (!gm_eqff85ac10o3(((uint8_t *)&t94), ((uint8_t *)&t95)));
                            if (!t96) goto ie80;
                            {
                                void * t98 = gm_l0;
                                void * t99 = (void *)(((uint8_t *)t98) + 208);
                                void * t100 = gm_l12;
                                gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t100) + 208));
                                void * t102 = gm_l13;
                                gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t102) + 208));
                                gm_tl874t2atnlh3 t104 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t101), GM_CONV(gm_cseahvg8v7p0vs, t103)));
                                gm_tj5f8vajf35o7 t105;
                                memset(&t105, 0, sizeof t105);
                                GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)5ULL));
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t105 + 8), t104);
                                gm_tftb1odc739t5 t106 = gm_f_push_rqlbv1e(t99, &t105);
                                gm_tftb1odc739t5 t107 = t106;
                                if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srco9vbah234t6e, 363, 21);
                                gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                            }
                            goto ix80;
ie80:;
ix80:;
                        }
                        goto me17;
mn71:;
ma21:;
                        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t110 != ((uint8_t)3ULL)) goto mn109;
                        gm_b16_8 t111 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l14 = t111;
                        {
                            {
                                gm_b16_8 t114 = gm_l14;
                                gm_l15 = t114;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t116 = gm_l16;
                                    uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t118 = (t116 < t117);
                                    if (!t118) goto lb115;
                                    {
                                        uint64_t t121 = gm_l16;
                                        uint64_t t122 = (uintptr_t)t121;
                                        uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        void * t124 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t122 * 8));
                                        void * t125 = t124;
                                        void * t128 = GM_LD(void *, ((uint8_t *)&t125));
                                        gm_l17 = t128;
                                        {
                                            void * t131 = gm_l17;
                                            void * t132 = t131;
                                            if (!(GM_LD(void *, ((uint8_t *)&t132)) != 0)) goto ie130;
                                            void * t133 = GM_LD(void *, ((uint8_t *)&t132));
                                            gm_l18 = t133;
                                            {
                                                void * t135 = gm_l0;
                                                uint32_t t136 = gm_l1;
                                                void * t137 = gm_l18;
                                                gm_b0_1 t138 = gm_f_assigned_53ipnmg((void *)NULL, cx, NULL, t135, t136, t137);
                                                if (gm_unw.kind) {
                                                    goto gm_ll119;
                                                }
                                            }
                                            goto ix130;
ie130:;
ix130:;
                                        }
                                        goto me126;
mn127:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 367, 17);
me126:;
                                    }
lc115:;
                                    {
                                        uint64_t t140 = gm_l16;
                                        uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                                        uint64_t t143 = r141;
                                        gm_l16 = t143;
                                    }
                                }
gm_ll119:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb115; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc115; }
                                }
                                goto gm_ll8;
lb115:;
                            }
                        }
                        goto me17;
mn109:;
ma22:;
                        goto me17;
mn144:;
ma23:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 353, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 352, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t147 = gm_l4;
                    uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                    uint64_t t150 = r148;
                    gm_l4 = t150;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_bool_lit_q44074e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bool_lit_q44074e F;
    void * gm_l0; /* x */
    uint8_t gm_l1; /* b */
    GM_STACK_CHECK(&F, 30u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)4ULL)) goto ie2;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            uint8_t t7 = gm_l1;
            gm_tqs5p4n2tq19v t8;
            GM_ST(uint8_t, ((uint8_t *)&t8), t7);
            ((uint8_t *)&t8)[1] = 1;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tqs5p4n2tq19v t10;
        memset(&t10, 0, sizeof t10);
        uint8_t t11 = 0;
        F.ret = t10;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_file_ok_4j31vl0(void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0) {
    struct gm_fr_file_ok_4j31vl0 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    GM_STACK_CHECK(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    gm_tl874t2atnlh3 t3 = gm_l0;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
    gm_tl874t2atnlh3 t5 = t3;
    gm_tl874t2atnlh3 t6 = t4;
    uint8_t t7 = (!gm_eqff85ac10o3(((uint8_t *)&t5), ((uint8_t *)&t6)));
    if (t7) {
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1) + 8);
        uint32_t t10 = (uint32_t)t9;
        uint8_t t11 = (t8 < t10);
        t2 = t11;
    } else t2 = 0;
    if (t2) {
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srco9vbah234t6e, 99, 116);
        uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_checks_c17ks6r *)((&F.h)->up))->l1)) + t14 * 1));
        t1 = t16;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_259eng6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_259eng6 F;
    void * gm_l0; /* child */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l0));
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l1));
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_visit_unu9ms6 *)((&F.h)->up))->l3));
        gm_b0_1 t6 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o40bhnh(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_o40bhnh F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_binds_busqeo0 *)((&F.h)->up))->l0));
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_binds_busqeo0 *)((&F.h)->up))->l1));
        void * t4 = gm_l0;
        gm_b0_1 t5 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_array_p7ehais(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_array_p7ehais F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
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
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t11 != ((uint8_t)11ULL)) goto pa10;
    goto po9;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t13 != ((uint8_t)20ULL)) goto pa12;
    goto po9;
pa12:;
    goto mn8;
po9:;
    t6 = ((uint8_t)1);
    goto me7;
mn8:;
    t6 = ((uint8_t)0);
    goto me7;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 265, 61);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mutate_oi1s4vs(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_mutate_oi1s4vs F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* place */
    gm_tcc1rq4n1avb2 gm_l3; /* k */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        void * t5 = gm_l2;
        gm_t736djc66po0l t6 = gm_f_root_oq23mha(t3, t4, t5);
        gm_t736djc66po0l t7 = t6;
        if (!((((uint8_t *)&t7))[8] != 0)) goto ie2;
        gm_tcc1rq4n1avb2 t8 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            void * t10 = gm_l0;
            gm_tcc1rq4n1avb2 t11 = gm_l3;
            void * t12 = gm_f_use_of_q9agaul((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (((uint8_t *)t12) + 40), ((uint8_t)1));
        }
        goto ix2;
ie2:;
ix2:;
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
__attribute__((aligned(4))) void gm_f_needless_pub_jqsnq13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_needless_pub_jqsnq13 F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* scoped */
    void * gm_l3; /* s */
    void * gm_l4; /* decl */
    gm_b16_8 gm_l5; /* name */
    uint8_t gm_l6; /* candidate */
    gm_tl874t2atnlh3 gm_l7; /* span */
    uint8_t gm_l8; /* file_ok */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 1209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        uint64_t t4 = gm_l1;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l3;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 240) + 0) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_srco9vbah234t6e, 223, 14);
        void * t9 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 240) + 0)) + t5 * 216);
        gm_l4 = t9;
    }
    {
        void * t10 = gm_l4;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 0) + 0));
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l4;
        uint8_t t13;
        uint8_t t19 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma17; default: goto ma18; }
ma15:;
        uint8_t t21 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        if (t21 != ((uint8_t)0ULL)) goto mn20;
        t13 = ((uint8_t)0);
        goto me14;
mn20:;
ma16:;
        uint8_t t23 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        if (t23 != ((uint8_t)1ULL)) goto mn22;
        t13 = ((uint8_t)1);
        goto me14;
mn22:;
ma17:;
        uint8_t t25 = GM_LD(uint8_t, (((((uint8_t *)t12) + 40) + 56) + 0));
        if (t25 != ((uint8_t)2ULL)) goto mn24;
        void * t26 = gm_l0;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t26) + 8));
        t13 = t27;
        goto me14;
mn24:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 225, 18);
me14:;
        gm_l6 = t13;
    }
    {
        void * t28 = gm_l4;
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t28) + 0) + 16));
        gm_l7 = t29;
    }
    {
        uint8_t t30 = 0;
        uint8_t t31 = 0;
        gm_tl874t2atnlh3 t32 = gm_l7;
        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tl874t2atnlh3 t34 = t32;
        gm_tl874t2atnlh3 t35 = t33;
        uint8_t t36 = (!gm_eqff85ac10o3(((uint8_t *)&t34), ((uint8_t *)&t35)));
        if (t36) {
            uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint32_t t39 = (uint32_t)t38;
            uint8_t t40 = (t37 < t39);
            t31 = t40;
        } else t31 = 0;
        if (t31) {
            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
            uint64_t t42 = (uint64_t)t41;
            uint64_t t43 = (uintptr_t)t42;
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srco9vbah234t6e, 231, 83);
            uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t43 * 1));
            t30 = t45;
        } else t30 = 0;
        gm_l8 = t30;
    }
    {
        uint8_t t47 = 0;
        uint8_t t48 = 0;
        uint8_t t49 = 0;
        uint8_t t50 = 0;
        uint8_t t51 = gm_l6;
        uint8_t t52 = (uint8_t)!t51;
        if (!t52) {
            uint8_t t53 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l5)), gm_sog4rksrmsqd2, 4) == 0));
            t50 = t53;
        } else t50 = 1;
        if (!t50) {
            void * t54 = gm_l4;
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 32));
            uint64_t t56 = (uintptr_t)t55;
            void * t57 = gm_l3;
            void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
            uint64_t t59 = GM_LD(uintptr_t, ((((uint8_t *)t58) + 200) + 0) + 8);
            if (t56 >= (uintptr_t)(t59)) gm_trap_index(t56, t59, gm_srco9vbah234t6e, 232, 43);
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t58) + 200) + 0)) + t56 * 184) + 112));
            t49 = t60;
        } else t49 = 1;
        if (!t49) {
            uint8_t t61 = gm_l8;
            uint8_t t62 = (uint8_t)!t61;
            t48 = t62;
        } else t48 = 1;
        if (!t48) {
            void * t63 = gm_l3;
            uint64_t t64 = gm_l1;
            uint8_t t65 = gm_f_is_used_outside_tbk81ks(t63, t64);
            t47 = t65;
        } else t47 = 1;
        if (!t47) goto ie46;
        {
            gm_th2lqbo77pvsc t67;
            memset(&t67, 0, sizeof t67);
            uint8_t t68 = 0;
            F.ret = t67;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        {
            void * t70 = gm_l4;
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((((uint8_t *)t70) + 40) + 40));
            gm_l9 = t71;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t73 = gm_l10;
                uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t75 = (t73 < t74);
                if (!t75) goto lb72;
                {
                    uint64_t t78 = gm_l10;
                    uint64_t t79 = (uintptr_t)t78;
                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_tkacitepg5fk0 t83 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t79 * 72));
                    F.l11 = t83;
                    {
                        gm_b16_8 t86 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 0) + 0));
                        gm_t6j6smj7qoctu t87;
                        gm_b16_8 t88;
                        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_spe8t3e79vnal));
                        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
                        GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t88);
                        gm_b16_8 t89;
                        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s1ft2nrvnhcbp));
                        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(9));
                        GM_ST(gm_b16_8, (uint8_t *)&t87 + 16, t89);
                        gm_t6j6smj7qoctu t90 = t87;
                        uint8_t t91 = 0;
                        gm_b16_8 t92 = t86;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t90) + 0, ((uint8_t *)&t92))) t91 = 1;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t90) + 16, ((uint8_t *)&t92))) t91 = 1;
                        uint8_t t93 = (uint8_t)(t91);
                        if (!t93) goto ie85;
                        {
                            gm_th2lqbo77pvsc t95;
                            memset(&t95, 0, sizeof t95);
                            uint8_t t96 = 0;
                            F.ret = t95;
                            goto gm_fret;
                        }
                        goto ix85;
ie85:;
ix85:;
                    }
                    goto me81;
mn82:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 235, 5);
me81:;
                }
lc72:;
                {
                    uint64_t t98 = gm_l10;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l10 = t101;
                }
            }
lb72:;
        }
    }
    {
        void * t103 = gm_l4;
        uint8_t t104 = GM_LD(uint8_t, (((((uint8_t *)t103) + 40) + 64) + 0));
        if (t104 != ((uint8_t)0ULL)) goto ie102;
        gm_tv2uf4leil5hq t105 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t103) + 40) + 64) + 8));
        F.l12 = t105;
        {
            void * t107 = gm_l3;
            uint64_t t108 = gm_l1;
            gm_f_state_a5bnsjb((void *)&F.l13, (void *)NULL, cx, NULL, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
            if (t110 != ((uint8_t)2ULL)) goto ie109;
            gm_t6qooom8qa2do t111 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&F.l13) + 8));
            F.l14 = t111;
            {
                void * t114 = GM_LD(void *, (((uint8_t *)&F.l12) + 40));
                uint8_t t115;
                uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t114) + 8) + 0));
                if (t118 != ((uint8_t)36ULL)) goto mn117;
                t115 = ((uint8_t)1);
                goto me116;
mn117:;
                t115 = ((uint8_t)0);
                goto me116;
mn119:;
                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 241, 8);
me116:;
                if (!t115) goto ie113;
                {
                    uint8_t t122;
                    uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                    if (t125 != ((uint8_t)5ULL)) goto mn124;
                    t122 = ((uint8_t)1);
                    goto me123;
mn124:;
                    t122 = ((uint8_t)0);
                    goto me123;
mn126:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 242, 12);
me123:;
                    if (!t122) goto ie121;
                    {
                        gm_b16_8 t128;
                        GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sq0dqiq83b924));
                        GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(8));
                        gm_th2lqbo77pvsc t129;
                        GM_ST(uint8_t, (uint8_t *)&t129 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t129 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t129 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t129), t128);
                        ((uint8_t *)&t129)[16] = 1;
                        uint8_t t130 = 0;
                        F.ret = t129;
                        goto gm_fret;
                    }
                    goto ix121;
ie121:;
ix121:;
                }
                goto ix113;
ie113:;
ix113:;
            }
            {
                uint8_t t132 = 0;
                uint8_t t133;
                uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                if (t136 != ((uint8_t)7ULL)) goto mn135;
                t133 = ((uint8_t)1);
                goto me134;
mn135:;
                t133 = ((uint8_t)0);
                goto me134;
mn137:;
                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 244, 8);
me134:;
                if (!t133) {
                    uint8_t t138;
                    uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&F.l14) + 0));
                    if (t141 != ((uint8_t)8ULL)) goto mn140;
                    t138 = ((uint8_t)1);
                    goto me139;
mn140:;
                    t138 = ((uint8_t)0);
                    goto me139;
mn142:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 244, 26);
me139:;
                    t132 = t138;
                } else t132 = 1;
                if (!t132) goto ie131;
                {
                    gm_b16_8 t144;
                    GM_ST(void *, (uint8_t *)&t144, (void *)(gm_svklio0qebpog));
                    GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(8));
                    gm_th2lqbo77pvsc t145;
                    GM_ST(uint8_t, (uint8_t *)&t145 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t145 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t145 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t145), t144);
                    ((uint8_t *)&t145)[16] = 1;
                    uint8_t t146 = 0;
                    F.ret = t145;
                    goto gm_fret;
                }
                goto ix131;
ie131:;
ix131:;
            }
            {
                gm_th2lqbo77pvsc t147;
                memset(&t147, 0, sizeof t147);
                uint8_t t148 = 0;
                F.ret = t147;
                goto gm_fret;
            }
            goto ix109;
ie109:;
            {
                gm_th2lqbo77pvsc t150;
                memset(&t150, 0, sizeof t150);
                uint8_t t151 = 0;
                F.ret = t150;
                goto gm_fret;
            }
ix109:;
        }
        goto ix102;
ie102:;
        {
            gm_th2lqbo77pvsc t153;
            memset(&t153, 0, sizeof t153);
            uint8_t t154 = 0;
            F.ret = t153;
            goto gm_fret;
        }
ix102:;
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
__attribute__((aligned(4))) uint8_t gm_f_path_eq_l3qbb7v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_path_eq_l3qbb7v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint32_t gm_l2; /* x */
    uint32_t gm_l3; /* y */
    uint32_t gm_l4; /* l1 */
    uint32_t gm_l5; /* x */
    uint32_t gm_l6; /* l2 */
    uint32_t gm_l7; /* y */
    uint64_t gm_l8; /* x */
    uint64_t gm_l9; /* y */
    void * gm_l10; /* b1 */
    uint32_t gm_l11; /* x1 */
    void * gm_l12; /* b2 */
    uint32_t gm_l13; /* x2 */
    void * gm_l14; /* x */
    void * gm_l15; /* y */
    void * gm_l16; /* b1 */
    void * gm_l17; /* x1 */
    void * gm_l18; /* b2 */
    void * gm_l19; /* x2 */
    GM_STACK_CHECK(&F, 565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma10; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma10; case ((uint8_t)57ULL): goto ma10; case ((uint8_t)58ULL): goto ma10; default: goto ma10; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)15ULL)) goto mn13;
        uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        {
            void * t18 = gm_l1;
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t19 != ((uint8_t)15ULL)) goto ie17;
            uint32_t t20 = GM_LD(uint32_t, ((((uint8_t *)t18) + 8) + 8));
            gm_l3 = t20;
            {
                uint32_t t22 = gm_l2;
                uint32_t t23 = gm_l3;
                uint8_t t24 = ((t22 == t23));
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            uint8_t t26 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn13:;
ma5:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t28 != ((uint8_t)16ULL)) goto mn27;
        uint32_t t29 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t29;
        uint32_t t30 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 4));
        gm_l5 = t30;
        {
            void * t33 = gm_l1;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            if (t34 != ((uint8_t)16ULL)) goto ie32;
            uint32_t t35 = GM_LD(uint32_t, (((((uint8_t *)t33) + 8) + 8) + 0));
            gm_l6 = t35;
            uint32_t t36 = GM_LD(uint32_t, (((((uint8_t *)t33) + 8) + 8) + 4));
            gm_l7 = t36;
            {
                uint8_t t38 = 0;
                uint32_t t39 = gm_l4;
                uint32_t t40 = gm_l6;
                uint8_t t41 = ((t39 == t40));
                if (t41) {
                    uint32_t t42 = gm_l5;
                    uint32_t t43 = gm_l7;
                    uint8_t t44 = ((t42 == t43));
                    t38 = t44;
                } else t38 = 0;
                uint8_t t45 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            uint8_t t46 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn27:;
ma6:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)17ULL)) goto mn47;
        uint64_t t49 = GM_LD(uint64_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l8 = t49;
        {
            void * t52 = gm_l1;
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t52) + 8) + 0));
            if (t53 != ((uint8_t)17ULL)) goto ie51;
            uint64_t t54 = GM_LD(uint64_t, (((((uint8_t *)t52) + 8) + 8) + 16));
            gm_l9 = t54;
            {
                uint64_t t56 = gm_l8;
                uint64_t t57 = gm_l9;
                uint8_t t58 = ((t56 == t57));
                uint8_t t59 = 0;
                F.ret = t58;
                goto gm_fret;
            }
            goto ix51;
ie51:;
ix51:;
        }
        {
            uint8_t t60 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn47:;
ma7:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t62 != ((uint8_t)19ULL)) goto mn61;
        void * t63 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l10 = t63;
        uint32_t t64 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l11 = t64;
        {
            void * t67 = gm_l1;
            uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t67) + 8) + 0));
            if (t68 != ((uint8_t)19ULL)) goto ie66;
            void * t69 = GM_LD(void *, (((((uint8_t *)t67) + 8) + 8) + 0));
            gm_l12 = t69;
            uint32_t t70 = GM_LD(uint32_t, (((((uint8_t *)t67) + 8) + 8) + 8));
            gm_l13 = t70;
            {
                uint8_t t72 = 0;
                uint32_t t73 = gm_l11;
                uint32_t t74 = gm_l13;
                uint8_t t75 = ((t73 == t74));
                if (t75) {
                    void * t76 = gm_l10;
                    void * t77 = gm_l12;
                    uint8_t t78 = gm_f_path_eq_l3qbb7v(t76, t77);
                    t72 = t78;
                } else t72 = 0;
                uint8_t t79 = 0;
                F.ret = t72;
                goto gm_fret;
            }
            goto ix66;
ie66:;
ix66:;
        }
        {
            uint8_t t80 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn61:;
ma8:;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t82 != ((uint8_t)18ULL)) goto mn81;
        void * t83 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l14 = t83;
        {
            void * t86 = gm_l1;
            uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t86) + 8) + 0));
            if (t87 != ((uint8_t)18ULL)) goto ie85;
            void * t88 = GM_LD(void *, ((((uint8_t *)t86) + 8) + 8));
            gm_l15 = t88;
            {
                void * t90 = gm_l14;
                void * t91 = gm_l15;
                uint8_t t92 = gm_f_path_eq_l3qbb7v(t90, t91);
                uint8_t t93 = 0;
                F.ret = t92;
                goto gm_fret;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            uint8_t t94 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn81:;
ma9:;
        uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t96 != ((uint8_t)20ULL)) goto mn95;
        void * t97 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l16 = t97;
        void * t98 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l17 = t98;
        {
            void * t101 = gm_l1;
            uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t101) + 8) + 0));
            if (t102 != ((uint8_t)20ULL)) goto ie100;
            void * t103 = GM_LD(void *, (((((uint8_t *)t101) + 8) + 8) + 0));
            gm_l18 = t103;
            void * t104 = GM_LD(void *, (((((uint8_t *)t101) + 8) + 8) + 8));
            gm_l19 = t104;
            {
                void * t107 = gm_l16;
                void * t108 = gm_l18;
                uint8_t t109 = gm_f_path_eq_l3qbb7v(t107, t108);
                uint8_t t110 = (uint8_t)!t109;
                if (!t110) goto ie106;
                {
                    uint8_t t112 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix106;
ie106:;
ix106:;
            }
            {
                void * t114 = gm_l17;
                uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t114) + 8) + 0));
                if (t115 != ((uint8_t)0ULL)) goto ie113;
                gm_tbci5ospk5nnn t116 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t114) + 8) + 8));
                F.l20 = t116;
                {
                    void * t119 = gm_l19;
                    uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t119) + 8) + 0));
                    if (t120 != ((uint8_t)0ULL)) goto ie118;
                    gm_tbci5ospk5nnn t121 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t119) + 8) + 8));
                    F.l21 = t121;
                    {
                        uint8_t t123 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l20)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l21)));
                        uint8_t t124 = 0;
                        F.ret = t123;
                        goto gm_fret;
                    }
                    goto ix118;
ie118:;
ix118:;
                }
                goto ix113;
ie113:;
ix113:;
            }
            {
                void * t125 = gm_l17;
                void * t126 = gm_l19;
                uint8_t t127 = gm_f_path_eq_l3qbb7v(t125, t126);
                uint8_t t128 = 0;
                F.ret = t127;
                goto gm_fret;
            }
            goto ix100;
ie100:;
ix100:;
        }
        {
            uint8_t t129 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn95:;
ma10:;
        uint8_t t131 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn130:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 404, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_root_oq23mha(void * a0, uint32_t a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_root_oq23mha F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* l */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* local */
    uint32_t gm_l6; /* fr */
    void * gm_l7; /* base */
    void * gm_l8; /* base */
    gm_tao63pk3r18v5 gm_l9; /* sl */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma9; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma9; default: goto ma9; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t13 != ((uint8_t)15ULL)) goto mn12;
        uint32_t t14 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t14;
        uint32_t t15 = gm_l1;
        uint32_t t16 = gm_l3;
        gm_tcc1rq4n1avb2 t17;
        GM_ST(uint32_t, ((uint8_t *)&t17 + 0), t15);
        GM_ST(uint32_t, ((uint8_t *)&t17 + 4), t16);
        gm_t736djc66po0l t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 10, 0);
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t18), t17);
        ((uint8_t *)&t18)[8] = 1;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
        goto me3;
mn12:;
ma5:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)16ULL)) goto mn20;
        uint32_t t22 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t22;
        uint32_t t23 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 4));
        gm_l5 = t23;
        {
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            uint32_t t27 = gm_l1;
            uint32_t t28 = gm_l4;
            gm_t9805qtonbsag t29 = gm_f_frame_up_h0ddp1d(t26, t27, t28);
            gm_t9805qtonbsag t30 = t29;
            uint32_t t31;
            if (((((uint8_t *)&t30))[4] != 0)) {
                uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&t30) + 0));
                t31 = t32;
            } else {
                gm_t736djc66po0l t33;
                memset(&t33, 0, sizeof t33);
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            gm_l6 = t31;
        }
        {
            uint32_t t35 = gm_l6;
            uint32_t t36 = gm_l5;
            gm_tcc1rq4n1avb2 t37;
            GM_ST(uint32_t, ((uint8_t *)&t37 + 0), t35);
            GM_ST(uint32_t, ((uint8_t *)&t37 + 4), t36);
            gm_t736djc66po0l t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 10, 0);
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[8] = 1;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto me3;
mn20:;
ma6:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)19ULL)) goto mn40;
        void * t42 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l7 = t42;
        void * t43 = gm_l0;
        uint32_t t44 = gm_l1;
        void * t45 = gm_l7;
        gm_t736djc66po0l t46 = gm_f_root_oq23mha(t43, t44, t45);
        uint8_t t47 = 0;
        F.ret = t46;
        goto gm_fret;
        goto me3;
mn40:;
ma7:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t49 != ((uint8_t)20ULL)) goto mn48;
        void * t50 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t50;
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
        void * t54 = gm_l8;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
        uint8_t t56 = gm_f_is_array_p7ehais(t53, GM_CONV(gm_csguankko4lmhd, t55));
        if (!t56) goto ie51;
        {
            void * t58 = gm_l0;
            uint32_t t59 = gm_l1;
            void * t60 = gm_l8;
            gm_t736djc66po0l t61 = gm_f_root_oq23mha(t58, t59, t60);
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
        goto me3;
mn48:;
ma8:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t64 != ((uint8_t)21ULL)) goto mn63;
        gm_tao63pk3r18v5 t65 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t65;
        void * t67 = gm_l0;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
        void * t69 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 0));
        uint8_t t71 = gm_f_is_array_p7ehais(t68, GM_CONV(gm_csguankko4lmhd, t70));
        if (!t71) goto ie66;
        {
            void * t73 = gm_l0;
            uint32_t t74 = gm_l1;
            void * t75 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
            gm_t736djc66po0l t76 = gm_f_root_oq23mha(t73, t74, t75);
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto ix66;
ie66:;
ix66:;
        goto me3;
mn63:;
ma9:;
        goto me3;
mn78:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 251, 5);
me3:;
    }
    {
        gm_t736djc66po0l t80;
        memset(&t80, 0, sizeof t80);
        uint8_t t81 = 0;
        F.ret = t80;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_run_checks_c17ks6r(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_run_checks_c17ks6r F;
    void * gm_l0; /* c */
    void * gm_l2; /* s */
    gm_closure gm_l3; /* file_ok */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l11; /* f */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint32_t gm_l15; /* f */
    void * gm_l16; /* body */
    uint32_t gm_l18; /* outer */
    gm_tf5srh4mp4b3i gm_l19; /* me */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint32_t gm_l22; /* callee */
    uint32_t gm_l23; /* g */
    uint64_t gm_l24; /* d */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    void * gm_l28; /* init */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint32_t gm_l32; /* callee */
    uint64_t gm_l33; /* d */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint32_t gm_l36; /* f */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    uint32_t gm_l41; /* f */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    void * gm_l46; /* def */
    uint8_t gm_l47; /* own_params */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l50; /* j */
    uint32_t gm_l52; /* lid */
    uint8_t gm_l53; /* param */
    uint64_t gm_l54; /* writes */
    uint8_t gm_l55; /* mutated */
    void * gm_l56; /* u */
    uint64_t gm_l57; /* uses */
    uint32_t gm_l58; /* n */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* <tmp> */
    uint64_t gm_l61; /* i */
    void * gm_l62; /* d */
    gm_b16_8 gm_l63; /* name */
    gm_b16_8 gm_l64; /* what */
    uint8_t gm_l66; /* skip */
    gm_b16_8 gm_l67; /* <tmp> */
    uint64_t gm_l68; /* <tmp> */
    gm_b16_8 gm_l71; /* what */
    void * gm_l72; /* inner */
    gm_b16_8 gm_l75; /* <tmp> */
    uint64_t gm_l76; /* <tmp> */
    gm_tl874t2atnlh3 gm_l78; /* span */
    gm_tl874t2atnlh3 gm_l79; /* span */
    gm_tl874t2atnlh3 gm_l80; /* span */
    uint8_t gm_l81; /* keep */
    GM_STACK_CHECK(&F, 6903u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        gm_closure t4 = ((gm_closure){(void *)gm_f_file_ok_4j31vl0, (void *)&F.h, cx});
        gm_l3 = t4;
    }
    {
        gm_f_list_fsi5en1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
            gm_l5 = t7;
        }
        {
            for (;;) {
                uint64_t t9 = gm_l6;
                uint64_t t10 = gm_l5;
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l6;
                    uint64_t t15 = t14;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l4);
                        gm_tf5srh4mp4b3i t20;
                        memset(&t20, 0, sizeof t20);
                        gm_tftb1odc739t5 t21 = gm_f_push_s4q134v(t19, t20);
                        gm_tftb1odc739t5 t22 = t21;
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srco9vbah234t6e, 101, 35);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 101, 5);
me16:;
                }
lc8:;
                {
                    uint64_t t25 = gm_l6;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l6 = t28;
                }
            }
lb8:;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t30 = gm_l2;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 240) + 0) + 8);
            gm_l7 = t32;
        }
        {
            for (;;) {
                uint64_t t34 = gm_l8;
                uint64_t t35 = gm_l7;
                uint8_t t36 = (t34 < t35);
                if (!t36) goto lb33;
                {
                    uint64_t t39 = gm_l8;
                    uint64_t t40 = t39;
                    uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&t40));
                    gm_l9 = t43;
                    {
                        void * t46 = gm_l2;
                        uint64_t t47 = gm_l9;
                        gm_tagrkbqh054h7 t48;
                        gm_f_state_a5bnsjb((void *)&t48, (void *)NULL, cx, NULL, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_ll37;
                        }
                        gm_tagrkbqh054h7 t49 = t48;
                        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                        if (t50 != ((uint8_t)2ULL)) goto ie45;
                        gm_t6qooom8qa2do t51 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&t49) + 8));
                        F.l10 = t51;
                        {
                            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                            if (t54 != ((uint8_t)5ULL)) goto ie53;
                            uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&F.l10) + 8));
                            gm_l11 = t55;
                            {
                                uint32_t t58 = gm_l11;
                                uint64_t t59 = (uint64_t)t58;
                                uint64_t t60 = (uintptr_t)t59;
                                uint64_t t61 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                                if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_srco9vbah234t6e, 105, 20);
                                uint8_t t62;
                                if (((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t60 * 16))[8] != 0)) goto mn64;
                                t62 = ((uint8_t)1);
                                goto me63;
mn64:;
                                t62 = ((uint8_t)0);
                                goto me63;
mn65:;
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 105, 20);
me63:;
                                if (!t62) goto ie57;
                                {
                                    uint32_t t67 = gm_l11;
                                    uint64_t t68 = (uint64_t)t67;
                                    uint64_t t69 = (uintptr_t)t68;
                                    uint64_t t70 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                                    if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_srco9vbah234t6e, 105, 54);
                                    uint64_t t71 = gm_l9;
                                    gm_tf5srh4mp4b3i t72;
                                    GM_ST(uint8_t, (uint8_t *)&t72 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t72 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t72 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t72), t71);
                                    ((uint8_t *)&t72)[8] = 1;
                                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t69 * 16), t72);
                                }
                                goto ix57;
ie57:;
ix57:;
                            }
                            goto ix53;
ie53:;
ix53:;
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    goto me41;
mn42:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 102, 5);
me41:;
                }
lc33:;
                {
                    uint64_t t74 = gm_l8;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l8 = t77;
                }
            }
gm_ll37:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb33; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc33; }
            }
            goto gm_lad;
lb33:;
        }
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            void * t79 = gm_l2;
            uint64_t t80 = GM_LD(uintptr_t, ((((uint8_t *)t79) + 384) + 0) + 8);
            gm_l12 = t80;
        }
        {
            for (;;) {
                uint64_t t82 = gm_l13;
                uint64_t t83 = gm_l12;
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l13;
                    uint64_t t88 = t87;
                    uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&t88));
                    gm_l14 = t91;
                    {
                        uint64_t t93 = gm_l14;
                        uint32_t t94 = (uint32_t)t93;
                        gm_l15 = t94;
                    }
                    {
                        uint64_t t96 = gm_l14;
                        uint64_t t97 = (uintptr_t)t96;
                        void * t98 = gm_l2;
                        uint64_t t99 = GM_LD(uintptr_t, ((((uint8_t *)t98) + 344) + 0) + 8);
                        if (t97 >= (uintptr_t)(t99)) gm_trap_index(t97, t99, gm_srco9vbah234t6e, 112, 12);
                        uint8_t t100;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t98) + 344) + 0)) + t97 * 136) + 96))[24] != 0)) goto mn102;
                        t100 = ((uint8_t)0);
                        goto me101;
mn102:;
                        t100 = ((uint8_t)1);
                        goto me101;
mn103:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 112, 12);
me101:;
                        if (!t100) goto ie95;
                        {
                            goto lc81;
                        }
                        goto ix95;
ie95:;
ix95:;
                    }
                    {
                        uint64_t t105 = gm_l14;
                        uint64_t t106 = (uintptr_t)t105;
                        void * t107 = gm_l2;
                        uint64_t t108 = GM_LD(uintptr_t, ((((uint8_t *)t107) + 384) + 0) + 8);
                        if (t106 >= (uintptr_t)(t108)) gm_trap_index(t106, t108, gm_srco9vbah234t6e, 113, 17);
                        void * t109 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t107) + 384) + 0)) + t106 * 64) + 40));
                        void * t110 = t109;
                        void * t111;
                        if ((GM_LD(void *, ((uint8_t *)&t110)) != 0)) {
                            void * t112 = GM_LD(void *, (((uint8_t *)&t110) + 0));
                            t111 = t112;
                        } else {
                            goto lc81;
                        }
                        gm_l16 = t111;
                    }
                    {
                        gm_f_list_go2qg0b((void *)&F.l17, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                    }
                    {
                        void * t113 = gm_l0;
                        uint32_t t114 = gm_l15;
                        void * t115 = gm_l16;
                        void * t116 = (void *)((uint8_t *)&F.l17);
                        gm_b0_1 t117 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t113, t114, t115, t116);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                    }
                    {
                        void * t118 = gm_l2;
                        void * t119 = (void *)(((uint8_t *)t118) + 2736);
                        uint32_t t120 = gm_l15;
                        gm_t9805qtonbsag t121 = gm_f_get_q3uemcr(t119, t120);
                        gm_t9805qtonbsag t122 = t121;
                        uint32_t t123;
                        if (((((uint8_t *)&t122))[4] != 0)) {
                            uint32_t t124 = GM_LD(uint32_t, (((uint8_t *)&t122) + 0));
                            t123 = t124;
                        } else {
                            uint32_t t125 = gm_l15;
                            t123 = t125;
                        }
                        gm_l18 = t123;
                    }
                    {
                        uint32_t t126 = gm_l18;
                        uint64_t t127 = (uint64_t)t126;
                        uint64_t t128 = (uintptr_t)t127;
                        uint64_t t129 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                        if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_srco9vbah234t6e, 117, 15);
                        gm_tf5srh4mp4b3i t130 = GM_LD(gm_tf5srh4mp4b3i, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t128 * 16));
                        gm_l19 = t130;
                    }
                    {
                        {
                            gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                            gm_l20 = t132;
                        }
                        {
                            gm_l21 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t134 = gm_l21;
                                uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                uint8_t t136 = (t134 < t135);
                                if (!t136) goto lb133;
                                {
                                    uint64_t t139 = gm_l21;
                                    uint64_t t140 = (uintptr_t)t139;
                                    uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    uint32_t t142 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t140 * 4));
                                    uint32_t t143 = t142;
                                    uint32_t t146 = GM_LD(uint32_t, ((uint8_t *)&t143));
                                    gm_l22 = t146;
                                    {
                                        void * t148 = gm_l2;
                                        uint32_t t149 = gm_l22;
                                        uint32_t t150 = gm_f_generic_of_1861hm9(t148, t149);
                                        gm_l23 = t150;
                                    }
                                    {
                                        uint32_t t152 = gm_l23;
                                        uint64_t t153 = (uint64_t)t152;
                                        uint64_t t154 = (uintptr_t)t153;
                                        uint64_t t155 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                                        if (t154 >= (uintptr_t)(t155)) gm_trap_index(t154, t155, gm_srco9vbah234t6e, 120, 21);
                                        if (!((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t154 * 16))[8] != 0)) goto ie151;
                                        uint64_t t156 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t154 * 16));
                                        gm_l24 = t156;
                                        {
                                            gm_tf5srh4mp4b3i t159 = gm_l19;
                                            uint64_t t160 = gm_l24;
                                            gm_tf5srh4mp4b3i t161;
                                            GM_ST(uint8_t, (uint8_t *)&t161 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t161 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t161 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t161), t160);
                                            ((uint8_t *)&t161)[8] = 1;
                                            gm_tf5srh4mp4b3i t162 = t159;
                                            gm_tf5srh4mp4b3i t163 = t161;
                                            uint8_t t164 = (!gm_eq7mri15hk78(((uint8_t *)&t162), ((uint8_t *)&t163)));
                                            if (!t164) goto ie158;
                                            {
                                                uint64_t t166 = gm_l24;
                                                uint64_t t167 = (uintptr_t)t166;
                                                void * t168 = gm_l0;
                                                uint64_t t169 = GM_LD(uintptr_t, ((((uint8_t *)t168) + 72) + 0) + 8);
                                                if (t167 >= (uintptr_t)(t169)) gm_trap_index(t167, t169, gm_srco9vbah234t6e, 121, 34);
                                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t168) + 72) + 0)) + t167 * 1), ((uint8_t)1));
                                            }
                                            goto ix158;
ie158:;
ix158:;
                                        }
                                        goto ix151;
ie151:;
ix151:;
                                    }
                                    goto me144;
mn145:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 118, 9);
me144:;
                                }
lc133:;
                                {
                                    uint64_t t171 = gm_l21;
                                    uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                                    uint64_t t174 = r172;
                                    gm_l21 = t174;
                                }
                            }
lb133:;
                        }
                    }
                    goto me89;
mn90:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 110, 5);
me89:;
                }
lc81:;
                {
                    uint64_t t176 = gm_l13;
                    uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                    uint64_t t179 = r177;
                    gm_l13 = t179;
                }
            }
gm_ll85:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
            }
            goto gm_lad;
lb81:;
        }
    }
    {
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            void * t181 = gm_l2;
            uint64_t t182 = GM_LD(uintptr_t, ((((uint8_t *)t181) + 1968) + 0) + 8);
            gm_l25 = t182;
        }
        {
            for (;;) {
                uint64_t t184 = gm_l26;
                uint64_t t185 = gm_l25;
                uint8_t t186 = (t184 < t185);
                if (!t186) goto lb183;
                {
                    uint64_t t189 = gm_l26;
                    uint64_t t190 = t189;
                    uint64_t t193 = GM_LD(uint64_t, ((uint8_t *)&t190));
                    gm_l27 = t193;
                    {
                        uint64_t t195 = gm_l27;
                        uint64_t t196 = (uintptr_t)t195;
                        void * t197 = gm_l2;
                        uint64_t t198 = GM_LD(uintptr_t, ((((uint8_t *)t197) + 1968) + 0) + 8);
                        if (t196 >= (uintptr_t)(t198)) gm_trap_index(t196, t198, gm_srco9vbah234t6e, 126, 17);
                        void * t199 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t197) + 1968) + 0)) + t196 * 88) + 64));
                        void * t200 = t199;
                        void * t201;
                        if ((GM_LD(void *, ((uint8_t *)&t200)) != 0)) {
                            void * t202 = GM_LD(void *, (((uint8_t *)&t200) + 0));
                            t201 = t202;
                        } else {
                            goto lc183;
                        }
                        gm_l28 = t201;
                    }
                    {
                        gm_f_list_go2qg0b((void *)&F.l29, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll187;
                        }
                    }
                    {
                        void * t203 = gm_l0;
                        uint32_t t204 = GM_LD(uint32_t, gm_c9b4lh7gft4);
                        void * t205 = gm_l28;
                        void * t206 = (void *)((uint8_t *)&F.l29);
                        gm_b0_1 t207 = gm_f_visit_unu9ms6((void *)NULL, cx, NULL, t203, t204, t205, t206);
                        if (gm_unw.kind) {
                            goto gm_ll187;
                        }
                    }
                    {
                        {
                            gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                            gm_l30 = t209;
                        }
                        {
                            gm_l31 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t211 = gm_l31;
                                uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                uint8_t t213 = (t211 < t212);
                                if (!t213) goto lb210;
                                {
                                    uint64_t t216 = gm_l31;
                                    uint64_t t217 = (uintptr_t)t216;
                                    uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                    uint32_t t219 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t217 * 4));
                                    uint32_t t220 = t219;
                                    uint32_t t223 = GM_LD(uint32_t, ((uint8_t *)&t220));
                                    gm_l32 = t223;
                                    {
                                        gm_b16_8 t226 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                        gm_b16_8 t227 = t226;
                                        void * t228 = gm_l2;
                                        uint32_t t229 = gm_l32;
                                        uint32_t t230 = gm_f_generic_of_1861hm9(t228, t229);
                                        uint64_t t231 = (uint64_t)t230;
                                        uint64_t t232 = (uintptr_t)t231;
                                        gm_b16_8 t233 = t227;
                                        uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&t233) + 8);
                                        if (t232 >= (uintptr_t)(t234)) gm_trap_index(t232, t234, gm_srco9vbah234t6e, 130, 21);
                                        if (!((((uint8_t *)GM_LD(void *, ((uint8_t *)&t233)) + t232 * 16))[8] != 0)) goto ie225;
                                        uint64_t t235 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t233)) + t232 * 16));
                                        gm_l33 = t235;
                                        {
                                            uint64_t t237 = gm_l33;
                                            uint64_t t238 = (uintptr_t)t237;
                                            void * t239 = gm_l0;
                                            uint64_t t240 = GM_LD(uintptr_t, ((((uint8_t *)t239) + 72) + 0) + 8);
                                            if (t238 >= (uintptr_t)(t240)) gm_trap_index(t238, t240, gm_srco9vbah234t6e, 130, 72);
                                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t239) + 72) + 0)) + t238 * 1), ((uint8_t)1));
                                        }
                                        goto ix225;
ie225:;
ix225:;
                                    }
                                    goto me221;
mn222:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 129, 9);
me221:;
                                }
lc210:;
                                {
                                    uint64_t t242 = gm_l31;
                                    uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                                    uint64_t t245 = r243;
                                    gm_l31 = t245;
                                }
                            }
lb210:;
                        }
                    }
                    goto me191;
mn192:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 125, 5);
me191:;
                }
lc183:;
                {
                    uint64_t t247 = gm_l26;
                    uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                    uint64_t t250 = r248;
                    gm_l26 = t250;
                }
            }
gm_ll187:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb183; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc183; }
            }
            goto gm_lad;
lb183:;
        }
    }
    {
        {
            void * t252 = gm_l2;
            gm_b16_8 t253 = GM_LD(gm_b16_8, ((((uint8_t *)t252) + 920) + 0));
            gm_l34 = t253;
        }
        {
            gm_l35 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t255 = gm_l35;
                uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                uint8_t t257 = (t255 < t256);
                if (!t257) goto lb254;
                {
                    uint64_t t260 = gm_l35;
                    uint64_t t261 = (uintptr_t)t260;
                    uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    uint32_t t263 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t261 * 4));
                    uint32_t t264 = t263;
                    uint32_t t267 = GM_LD(uint32_t, ((uint8_t *)&t264));
                    gm_l36 = t267;
                    {
                        uint32_t t269 = gm_l36;
                        uint64_t t270 = (uint64_t)t269;
                        uint64_t t271 = (uintptr_t)t270;
                        void * t272 = gm_l0;
                        uint64_t t273 = GM_LD(uintptr_t, ((((uint8_t *)t272) + 112) + 0) + 8);
                        if (t271 >= (uintptr_t)(t273)) gm_trap_index(t271, t273, gm_srco9vbah234t6e, 133, 35);
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t272) + 112) + 0)) + t271 * 1), ((uint8_t)1));
                    }
                    goto me265;
mn266:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 133, 5);
me265:;
                }
lc254:;
                {
                    uint64_t t275 = gm_l35;
                    uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                    uint64_t t278 = r276;
                    gm_l35 = t278;
                }
            }
lb254:;
        }
    }
    {
        void * t279 = gm_l2;
        gm_f_growth_summaries_dguakce((void *)&F.l37, (void *)NULL, cx, NULL, t279);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l39 = ((uint64_t)0ULL);
        }
        {
            void * t281 = gm_l2;
            uint64_t t282 = GM_LD(uintptr_t, ((((uint8_t *)t281) + 384) + 0) + 8);
            gm_l38 = t282;
        }
        {
            for (;;) {
                uint64_t t284 = gm_l39;
                uint64_t t285 = gm_l38;
                uint8_t t286 = (t284 < t285);
                if (!t286) goto lb283;
                {
                    uint64_t t289 = gm_l39;
                    uint64_t t290 = t289;
                    uint64_t t293 = GM_LD(uint64_t, ((uint8_t *)&t290));
                    gm_l40 = t293;
                    {
                        uint64_t t295 = gm_l40;
                        uint32_t t296 = (uint32_t)t295;
                        gm_l41 = t296;
                    }
                    {
                        uint8_t t298 = 0;
                        uint8_t t299 = 0;
                        uint64_t t300 = gm_l40;
                        uint64_t t301 = (uintptr_t)t300;
                        void * t302 = gm_l2;
                        uint64_t t303 = GM_LD(uintptr_t, ((((uint8_t *)t302) + 344) + 0) + 8);
                        if (t301 >= (uintptr_t)(t303)) gm_trap_index(t301, t303, gm_srco9vbah234t6e, 138, 12);
                        uint8_t t304;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t302) + 344) + 0)) + t301 * 136) + 96))[24] != 0)) goto mn306;
                        t304 = ((uint8_t)0);
                        goto me305;
mn306:;
                        t304 = ((uint8_t)1);
                        goto me305;
mn307:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 138, 12);
me305:;
                        if (!t304) {
                            uint64_t t308 = gm_l40;
                            uint64_t t309 = (uintptr_t)t308;
                            void * t310 = gm_l2;
                            uint64_t t311 = GM_LD(uintptr_t, ((((uint8_t *)t310) + 384) + 0) + 8);
                            if (t309 >= (uintptr_t)(t311)) gm_trap_index(t309, t311, gm_srco9vbah234t6e, 138, 51);
                            void * t312 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t310) + 384) + 0)) + t309 * 64) + 40));
                            void * t313 = t312;
                            uint8_t t314;
                            if ((GM_LD(void *, ((uint8_t *)&t313)) != 0)) goto mn316;
                            t314 = ((uint8_t)1);
                            goto me315;
mn316:;
                            t314 = ((uint8_t)0);
                            goto me315;
mn317:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 138, 51);
me315:;
                            t299 = t314;
                        } else t299 = 1;
                        if (!t299) {
                            gm_closure t318 = gm_l3;
                            uint64_t t319 = gm_l40;
                            uint64_t t320 = (uintptr_t)t319;
                            void * t321 = gm_l2;
                            uint64_t t322 = GM_LD(uintptr_t, ((((uint8_t *)t321) + 344) + 0) + 8);
                            if (t320 >= (uintptr_t)(t322)) gm_trap_index(t320, t322, gm_srco9vbah234t6e, 138, 95);
                            gm_tl874t2atnlh3 t323 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t321) + 344) + 0)) + t320 * 136) + 16));
                            gm_closure t324 = t318;
                            gm_node *ccx325 = t324.cx;
                            void * t326 = t324.code;
                            uint8_t t327;
                            if (GM_PTR_TAGGED(t326)) {
                                t327 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t326))(GM_CONV(gm_cseahvg8v7p0vs, t323));
                            } else {
                                t327 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t326)(t324.env, ccx325, NULL, t323);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll287;
                            }
                            uint8_t t328 = (uint8_t)!t327;
                            t298 = t328;
                        } else t298 = 1;
                        if (!t298) goto ie297;
                        {
                            goto lc283;
                        }
                        goto ix297;
ie297:;
ix297:;
                    }
                    {
                        void * t330 = gm_l2;
                        uint64_t t331 = gm_l40;
                        gm_f_possible_dangling_addres_d403vr0((void *)&F.l42, (void *)NULL, cx, NULL, t330, t331);
                        if (gm_unw.kind) {
                            goto gm_ll287;
                        }
                    }
                    {
                        {
                            gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 0));
                            gm_l43 = t333;
                        }
                        {
                            gm_l44 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t335 = gm_l44;
                                uint64_t t336 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                uint8_t t337 = (t335 < t336);
                                if (!t337) goto lb334;
                                {
                                    uint64_t t340 = gm_l44;
                                    uint64_t t341 = (uintptr_t)t340;
                                    uint64_t t342 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                    gm_tg9l3k9j9m9b9 t345 = GM_LD(gm_tg9l3k9j9m9b9, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t341 * 72));
                                    F.l45 = t345;
                                    {
                                        uint8_t t348 = GM_LD(uint8_t, (((uint8_t *)&F.l45) + 64));
                                        uint8_t t349 = (uint8_t)!t348;
                                        if (!t349) goto ie347;
                                        {
                                            void * t351 = gm_l0;
                                            void * t352 = (void *)(((uint8_t *)t351) + 208);
                                            gm_tllos8achgacq t353;
                                            memset((uint8_t *)&t353 + 12, 0, 4);
                                            memset((uint8_t *)&t353 + 44, 0, 4);
                                            gm_tl874t2atnlh3 t354 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l45) + 0));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t353 + 0, t354);
                                            gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
                                            GM_ST(gm_b16_8, (uint8_t *)&t353 + 16, t355);
                                            gm_tl874t2atnlh3 t356 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l45) + 32));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t353 + 32, t356);
                                            gm_b16_8 t357 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 48));
                                            GM_ST(gm_b16_8, (uint8_t *)&t353 + 48, t357);
                                            gm_tj5f8vajf35o7 t358;
                                            GM_ST(uint8_t, (uint8_t *)&t358 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t358 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t358 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t358 + 0), ((uint8_t)7ULL));
                                            GM_ST(gm_tllos8achgacq, ((uint8_t *)&t358 + 8), t353);
                                            gm_tftb1odc739t5 t359 = gm_f_push_rqlbv1e(t352, &t358);
                                            gm_tftb1odc739t5 t360 = t359;
                                            if (((((uint8_t *)&t360))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t360) + 1, gm_srco9vbah234t6e, 141, 32);
                                            gm_b0_1 t361 = GM_LD(gm_b0_1, (((uint8_t *)&t360) + 1));
                                        }
                                        goto ix347;
ie347:;
ix347:;
                                    }
                                    goto me343;
mn344:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 140, 9);
me343:;
                                }
lc334:;
                                {
                                    uint64_t t363 = gm_l44;
                                    uint64_t r364; bool o365 = __builtin_add_overflow(t363, ((uint64_t)1ULL), &r364);
                                    uint64_t t366 = r364;
                                    gm_l44 = t366;
                                }
                            }
lb334:;
                        }
                    }
                    {
                        void * t367 = gm_l2;
                        uint64_t t368 = gm_l40;
                        void * t369 = gm_l0;
                        void * t370 = (void *)(((uint8_t *)t369) + 208);
                        gm_b0_1 t371 = gm_f_mutated_copies_vc6lpss((void *)NULL, cx, NULL, t367, t368, t370);
                        if (gm_unw.kind) {
                            goto gm_ll287;
                        }
                    }
                    {
                        void * t372 = gm_l2;
                        uint64_t t373 = gm_l40;
                        gm_b16_8 t374 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                        gm_b16_8 t375 = t374;
                        gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&t375));
                        void * t377 = gm_l0;
                        void * t378 = (void *)(((uint8_t *)t377) + 208);
                        gm_b0_1 t379 = gm_f_stale_element_pointers_jjgn0ll((void *)NULL, cx, NULL, t372, t373, t376, t378);
                        if (gm_unw.kind) {
                            goto gm_ll287;
                        }
                    }
                    {
                        uint64_t t380 = gm_l40;
                        uint64_t t381 = (uintptr_t)t380;
                        void * t382 = gm_l2;
                        uint64_t t383 = GM_LD(uintptr_t, ((((uint8_t *)t382) + 384) + 0) + 8);
                        if (t381 >= (uintptr_t)(t383)) gm_trap_index(t381, t383, gm_srco9vbah234t6e, 145, 17);
                        void * t384 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t382) + 384) + 0)) + t381 * 64);
                        gm_l46 = t384;
                    }
                    {
                        uint8_t t385 = 0;
                        uint8_t t386 = 0;
                        void * t387 = gm_l46;
                        uint8_t t388 = ((GM_LD(uint8_t, (((uint8_t *)t387) + 0) + 0) == ((uint8_t)0ULL)));
                        if (t388) {
                            uint64_t t389 = gm_l40;
                            uint64_t t390 = (uintptr_t)t389;
                            void * t391 = gm_l0;
                            uint64_t t392 = GM_LD(uintptr_t, ((((uint8_t *)t391) + 112) + 0) + 8);
                            if (t390 >= (uintptr_t)(t392)) gm_trap_index(t390, t392, gm_srco9vbah234t6e, 146, 50);
                            uint8_t t393 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t391) + 112) + 0)) + t390 * 1));
                            uint8_t t394 = (uint8_t)!t393;
                            t386 = t394;
                        } else t386 = 0;
                        if (t386) {
                            uint64_t t395 = gm_l40;
                            uint64_t t396 = (uintptr_t)t395;
                            void * t397 = gm_l2;
                            uint64_t t398 = GM_LD(uintptr_t, ((((uint8_t *)t397) + 344) + 0) + 8);
                            if (t396 >= (uintptr_t)(t398)) gm_trap_index(t396, t398, gm_srco9vbah234t6e, 146, 74);
                            uint8_t t399 = (!((GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t397) + 344) + 0)) + t396 * 136) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t397) + 344) + 0)) + t396 * 136) + 0)), gm_sog4rksrmsqd2, 4) == 0));
                            t385 = t399;
                        } else t385 = 0;
                        gm_l47 = t385;
                    }
                    {
                        {
                            void * t401 = gm_l46;
                            gm_b16_8 t402 = GM_LD(gm_b16_8, (((uint8_t *)t401) + 24));
                            gm_b16_8 t403 = t402;
                            gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
                            gm_l48 = t404;
                        }
                        {
                            gm_l49 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t406 = gm_l49;
                                uint64_t t407 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                                uint8_t t408 = (t406 < t407);
                                if (!t408) goto lb405;
                                {
                                    gm_t5im2v50m3hk6 t411;
                                    uint64_t t412 = gm_l49;
                                    GM_ST(uint64_t, (uint8_t *)&t411 + 0, t412);
                                    uint64_t t413 = gm_l49;
                                    uint64_t t414 = (uintptr_t)t413;
                                    uint64_t t415 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                                    memcpy((uint8_t *)&t411 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l48)) + t414 * 40), 40);
                                    gm_t5im2v50m3hk6 t416 = t411;
                                    uint64_t t419 = GM_LD(uint64_t, (((uint8_t *)&t416) + 0));
                                    gm_l50 = t419;
                                    gm_tsoie1ppgdvcg t420 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)&t416) + 8));
                                    F.l51 = t420;
                                    {
                                        uint64_t t422 = gm_l50;
                                        uint32_t t423 = (uint32_t)t422;
                                        gm_l52 = t423;
                                    }
                                    {
                                        uint8_t t425 = 0;
                                        uint8_t t426 = 0;
                                        uint8_t t427 = 0;
                                        gm_tl874t2atnlh3 t428 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                        gm_tl874t2atnlh3 t429 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                                        gm_tl874t2atnlh3 t430 = t428;
                                        gm_tl874t2atnlh3 t431 = t429;
                                        uint8_t t432 = (gm_eqff85ac10o3(((uint8_t *)&t430), ((uint8_t *)&t431)));
                                        if (!t432) {
                                            gm_b16_8 t433 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            gm_b16_8 t434;
                                            GM_ST(void *, (uint8_t *)&t434, (void *)(gm_s1f440st4mnkp));
                                            GM_ST(uintptr_t, (uint8_t *)&t434 + 8, (uintptr_t)(1));
                                            uint8_t t435 = gm_f_starts_with_6vvt01e(t433, t434);
                                            t427 = t435;
                                        } else t427 = 1;
                                        if (!t427) {
                                            gm_b16_8 t436 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            gm_b16_8 t437;
                                            GM_ST(void *, (uint8_t *)&t437, (void *)(gm_sfdbfrv46lihd));
                                            GM_ST(uintptr_t, (uint8_t *)&t437 + 8, (uintptr_t)(1));
                                            uint8_t t438 = gm_f_starts_with_6vvt01e(t436, t437);
                                            t426 = t438;
                                        } else t426 = 1;
                                        if (!t426) {
                                            gm_closure t439 = gm_l3;
                                            gm_tl874t2atnlh3 t440 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                            gm_closure t441 = t439;
                                            gm_node *ccx442 = t441.cx;
                                            void * t443 = t441.code;
                                            uint8_t t444;
                                            if (GM_PTR_TAGGED(t443)) {
                                                t444 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t443))(GM_CONV(gm_cseahvg8v7p0vs, t440));
                                            } else {
                                                t444 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t443)(t441.env, ccx442, NULL, t440);
                                            }
                                            if (gm_unw.kind) {
                                                goto gm_ll409;
                                            }
                                            uint8_t t445 = (uint8_t)!t444;
                                            t425 = t445;
                                        } else t425 = 1;
                                        if (!t425) goto ie424;
                                        {
                                            goto lc405;
                                        }
                                        goto ix424;
ie424:;
ix424:;
                                    }
                                    {
                                        uint32_t t447 = gm_l52;
                                        void * t448 = gm_l46;
                                        gm_b16_8 t449 = GM_LD(gm_b16_8, (((uint8_t *)t448) + 8));
                                        gm_b16_8 t450 = t449;
                                        uint8_t t451 = 0;
                                        uint32_t t452 = t447;
                                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t450) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t450))) + i * 4, ((uint8_t *)&t452))) { t451 = 1; break; }
                                        uint8_t t453 = (uint8_t)(t451);
                                        gm_l53 = t453;
                                    }
                                    {
                                        uint8_t t455 = gm_l53;
                                        if (!t455) goto ie454;
                                        uint8_t t456 = gm_l47;
                                        uint8_t t457 = (uint8_t)!t456;
                                        if (!t457) goto ie454;
                                        {
                                            goto lc405;
                                        }
                                        goto ix454;
ie454:;
ix454:;
                                    }
                                    {
                                        uint8_t t460 = gm_l53;
                                        uint8_t t461 = (uint8_t)!t460;
                                        if (!t461) goto ie459;
                                        void * t462 = gm_l0;
                                        void * t463 = (void *)(((uint8_t *)t462) + 152);
                                        uint32_t t464 = gm_l41;
                                        uint32_t t465 = gm_l52;
                                        gm_tcc1rq4n1avb2 t466;
                                        GM_ST(uint32_t, ((uint8_t *)&t466 + 0), t464);
                                        GM_ST(uint32_t, ((uint8_t *)&t466 + 4), t465);
                                        uint8_t t467 = gm_f_contains_14ai471(t463, t466);
                                        uint8_t t468 = (uint8_t)!t467;
                                        if (!t468) goto ie459;
                                        {
                                            goto lc405;
                                        }
                                        goto ix459;
ie459:;
ix459:;
                                    }
                                    {
                                        gm_l54 = ((uint64_t)0ULL);
                                    }
                                    {
                                        gm_l55 = ((uint8_t)0);
                                    }
                                    {
                                        void * t471 = gm_l0;
                                        void * t472 = (void *)(((uint8_t *)t471) + 16);
                                        uint32_t t473 = gm_l41;
                                        uint32_t t474 = gm_l52;
                                        gm_tcc1rq4n1avb2 t475;
                                        GM_ST(uint32_t, ((uint8_t *)&t475 + 0), t473);
                                        GM_ST(uint32_t, ((uint8_t *)&t475 + 4), t474);
                                        void * t476 = gm_f_get_ptr_1a4voen(t472, t475);
                                        void * t477 = t476;
                                        if (!(GM_LD(void *, ((uint8_t *)&t477)) != 0)) goto ie470;
                                        void * t478 = GM_LD(void *, ((uint8_t *)&t477));
                                        gm_l56 = t478;
                                        {
                                            void * t480 = gm_l56;
                                            uint64_t t481 = GM_LD(uintptr_t, ((((uint8_t *)t480) + 0) + 0) + 8);
                                            gm_l54 = t481;
                                        }
                                        {
                                            void * t482 = gm_l56;
                                            uint8_t t483 = GM_LD(uint8_t, (((uint8_t *)t482) + 40));
                                            gm_l55 = t483;
                                        }
                                        goto ix470;
ie470:;
ix470:;
                                    }
                                    {
                                        gm_l57 = ((uint64_t)0ULL);
                                    }
                                    {
                                        void * t485 = gm_l2;
                                        void * t486 = (void *)(((uint8_t *)t485) + 2680);
                                        gm_tl874t2atnlh3 t487 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                        gm_t9805qtonbsag t488 = gm_f_get_rlnefnq(t486, GM_CONV(gm_cseahvg8v7p0vs, t487));
                                        gm_t9805qtonbsag t489 = t488;
                                        if (!((((uint8_t *)&t489))[4] != 0)) goto ie484;
                                        uint32_t t490 = GM_LD(uint32_t, ((uint8_t *)&t489));
                                        gm_l58 = t490;
                                        {
                                            uint32_t t492 = gm_l58;
                                            uint64_t t493 = (uint64_t)t492;
                                            gm_l57 = t493;
                                        }
                                        goto ix484;
ie484:;
ix484:;
                                    }
                                    {
                                        uint64_t t495 = gm_l57;
                                        uint64_t t496 = gm_l54;
                                        uint8_t t497 = ((t495 == t496));
                                        if (!t497) goto ie494;
                                        {
                                            void * t499 = gm_l0;
                                            void * t500 = (void *)(((uint8_t *)t499) + 208);
                                            gm_tppdoi1slt2pr t501;
                                            memset((uint8_t *)&t501 + 30, 0, 2);
                                            gm_b16_8 t502 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t501 + 0, t502);
                                            gm_tl874t2atnlh3 t503 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t501 + 16, t503);
                                            uint8_t t504 = gm_l53;
                                            GM_ST(uint8_t, (uint8_t *)&t501 + 28, t504);
                                            uint64_t t505 = gm_l54;
                                            uint8_t t506 = (t505 > ((uint64_t)0ULL));
                                            GM_ST(uint8_t, (uint8_t *)&t501 + 29, t506);
                                            gm_tj5f8vajf35o7 t507;
                                            memset(&t507, 0, sizeof t507);
                                            GM_ST(uint8_t, ((uint8_t *)&t507 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tppdoi1slt2pr, ((uint8_t *)&t507 + 8), t501);
                                            gm_tftb1odc739t5 t508 = gm_f_push_rqlbv1e(t500, &t507);
                                            gm_tftb1odc739t5 t509 = t508;
                                            if (((((uint8_t *)&t509))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t509) + 1, gm_srco9vbah234t6e, 162, 17);
                                            gm_b0_1 t510 = GM_LD(gm_b0_1, (((uint8_t *)&t509) + 1));
                                        }
                                        goto ix494;
ie494:;
                                        uint8_t t512 = GM_LD(uint8_t, (((uint8_t *)&F.l51) + 20));
                                        if (!t512) goto ie511;
                                        uint8_t t513 = gm_l53;
                                        uint8_t t514 = (uint8_t)!t513;
                                        if (!t514) goto ie511;
                                        uint8_t t515 = gm_l55;
                                        uint8_t t516 = (uint8_t)!t515;
                                        if (!t516) goto ie511;
                                        uint64_t t517 = gm_l54;
                                        uint8_t t518 = ((t517 == ((uint64_t)0ULL)));
                                        if (!t518) goto ie511;
                                        {
                                            void * t520 = gm_l0;
                                            void * t521 = (void *)(((uint8_t *)t520) + 208);
                                            gm_t08olq97rscn3 t522;
                                            memset((uint8_t *)&t522 + 28, 0, 4);
                                            gm_b16_8 t523 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t522 + 0, t523);
                                            gm_tl874t2atnlh3 t524 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l51) + 24));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t522 + 16, t524);
                                            gm_tj5f8vajf35o7 t525;
                                            memset(&t525, 0, sizeof t525);
                                            GM_ST(uint8_t, ((uint8_t *)&t525 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t525 + 8), t522);
                                            gm_tftb1odc739t5 t526 = gm_f_push_rqlbv1e(t521, &t525);
                                            gm_tftb1odc739t5 t527 = t526;
                                            if (((((uint8_t *)&t527))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t527) + 1, gm_srco9vbah234t6e, 164, 17);
                                            gm_b0_1 t528 = GM_LD(gm_b0_1, (((uint8_t *)&t527) + 1));
                                        }
                                        goto ix511;
ie511:;
ix511:;
ix494:;
                                    }
                                    goto me417;
mn418:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 147, 9);
me417:;
                                }
lc405:;
                                {
                                    uint64_t t530 = gm_l49;
                                    uint64_t r531; bool o532 = __builtin_add_overflow(t530, ((uint64_t)1ULL), &r531);
                                    uint64_t t533 = r531;
                                    gm_l49 = t533;
                                }
                            }
gm_ll409:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb405; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc405; }
                            }
                            goto gm_ll287;
lb405:;
                        }
                    }
                    goto me291;
mn292:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 136, 5);
me291:;
                }
lc283:;
                {
                    uint64_t t535 = gm_l39;
                    uint64_t r536; bool o537 = __builtin_add_overflow(t535, ((uint64_t)1ULL), &r536);
                    uint64_t t538 = r536;
                    gm_l39 = t538;
                }
            }
gm_ll287:;
            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb283; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc283; }
            }
            goto gm_lad;
lb283:;
        }
    }
    {
        {
            gm_l60 = ((uint64_t)0ULL);
        }
        {
            void * t540 = gm_l2;
            void * t541 = GM_LD(void *, (((uint8_t *)t540) + 0));
            uint64_t t542 = GM_LD(uintptr_t, ((((uint8_t *)t541) + 240) + 0) + 8);
            gm_l59 = t542;
        }
        {
            for (;;) {
                uint64_t t544 = gm_l60;
                uint64_t t545 = gm_l59;
                uint8_t t546 = (t544 < t545);
                if (!t546) goto lb543;
                {
                    uint64_t t549 = gm_l60;
                    uint64_t t550 = t549;
                    uint64_t t553 = GM_LD(uint64_t, ((uint8_t *)&t550));
                    gm_l61 = t553;
                    {
                        uint64_t t555 = gm_l61;
                        uint64_t t556 = (uintptr_t)t555;
                        void * t557 = gm_l2;
                        void * t558 = GM_LD(void *, (((uint8_t *)t557) + 0));
                        uint64_t t559 = GM_LD(uintptr_t, ((((uint8_t *)t558) + 240) + 0) + 8);
                        if (t556 >= (uintptr_t)(t559)) gm_trap_index(t556, t559, gm_srco9vbah234t6e, 170, 15);
                        void * t560 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t558) + 240) + 0)) + t556 * 216);
                        gm_l62 = t560;
                    }
                    {
                        void * t561 = gm_l62;
                        gm_b16_8 t562 = GM_LD(gm_b16_8, ((((uint8_t *)t561) + 0) + 0));
                        gm_l63 = t562;
                    }
                    {
                        void * t564 = gm_l0;
                        uint64_t t565 = gm_l61;
                        gm_b16_8 t566 = F.l1;
                        gm_th2lqbo77pvsc t567;
                        gm_f_needless_pub_jqsnq13((void *)&t567, (void *)NULL, cx, NULL, t564, t565, t566);
                        if (gm_unw.kind) {
                            goto gm_ll547;
                        }
                        gm_th2lqbo77pvsc t568 = t567;
                        if (!((((uint8_t *)&t568))[16] != 0)) goto ie563;
                        gm_b16_8 t569 = GM_LD(gm_b16_8, ((uint8_t *)&t568));
                        gm_l64 = t569;
                        {
                            void * t571 = gm_l0;
                            void * t572 = (void *)(((uint8_t *)t571) + 208);
                            gm_t26d2uc07m5o2 t573;
                            memset((uint8_t *)&t573 + 29, 0, 3);
                            void * t574 = gm_l62;
                            gm_b16_8 t575 = GM_LD(gm_b16_8, ((((uint8_t *)t574) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t573 + 0, t575);
                            void * t576 = gm_l62;
                            gm_tl874t2atnlh3 t577 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t576) + 0) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t573 + 16, t577);
                            void * t578 = gm_l62;
                            gm_t9qkh0rsooc8d t579 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t578) + 40) + 56));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t573 + 28, t579);
                            gm_b16_8 t580 = gm_l64;
                            GM_ST(gm_b16_8, (uint8_t *)&t573 + 32, t580);
                            gm_tj5f8vajf35o7 t581;
                            GM_ST(uint8_t, (uint8_t *)&t581 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t581 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t581 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t581 + 56, 0);
                            GM_ST(uint64_t, (uint8_t *)&t581 + 64, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t581 + 0), ((uint8_t)6ULL));
                            GM_ST(gm_t26d2uc07m5o2, ((uint8_t *)&t581 + 8), t573);
                            gm_tftb1odc739t5 t582 = gm_f_push_rqlbv1e(t572, &t581);
                            gm_tftb1odc739t5 t583 = t582;
                            if (((((uint8_t *)&t583))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t583) + 1, gm_srco9vbah234t6e, 173, 13);
                            gm_b0_1 t584 = GM_LD(gm_b0_1, (((uint8_t *)&t583) + 1));
                        }
                        goto ix563;
ie563:;
ix563:;
                    }
                    {
                        uint8_t t586 = 0;
                        uint8_t t587 = 0;
                        uint8_t t588 = 0;
                        uint8_t t589 = 0;
                        uint8_t t590 = 0;
                        uint8_t t591 = 0;
                        void * t592 = gm_l62;
                        uint8_t t593 = ((GM_LD(uint8_t, ((((uint8_t *)t592) + 40) + 56) + 0) == ((uint8_t)2ULL)));
                        if (!t593) {
                            uint8_t t594 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l63) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l63)), gm_sog4rksrmsqd2, 4) == 0));
                            t591 = t594;
                        } else t591 = 1;
                        if (!t591) {
                            gm_b16_8 t595 = gm_l63;
                            gm_b16_8 t596;
                            GM_ST(void *, (uint8_t *)&t596, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t596 + 8, (uintptr_t)(1));
                            uint8_t t597 = gm_f_starts_with_6vvt01e(t595, t596);
                            t590 = t597;
                        } else t590 = 1;
                        if (!t590) {
                            void * t598 = gm_l62;
                            uint64_t t599 = GM_LD(uint64_t, (((uint8_t *)t598) + 32));
                            uint64_t t600 = (uintptr_t)t599;
                            void * t601 = gm_l2;
                            void * t602 = GM_LD(void *, (((uint8_t *)t601) + 0));
                            uint64_t t603 = GM_LD(uintptr_t, ((((uint8_t *)t602) + 200) + 0) + 8);
                            if (t600 >= (uintptr_t)(t603)) gm_trap_index(t600, t603, gm_srco9vbah234t6e, 175, 80);
                            uint8_t t604 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t602) + 200) + 0)) + t600 * 184) + 112));
                            t589 = t604;
                        } else t589 = 1;
                        if (!t589) {
                            gm_closure t605 = gm_l3;
                            void * t606 = gm_l62;
                            gm_tl874t2atnlh3 t607 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t606) + 0) + 16));
                            gm_closure t608 = t605;
                            gm_node *ccx609 = t608.cx;
                            void * t610 = t608.code;
                            uint8_t t611;
                            if (GM_PTR_TAGGED(t610)) {
                                t611 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t610))(GM_CONV(gm_cseahvg8v7p0vs, t607));
                            } else {
                                t611 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t610)(t608.env, ccx609, NULL, t607);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll547;
                            }
                            uint8_t t612 = (uint8_t)!t611;
                            t588 = t612;
                        } else t588 = 1;
                        if (!t588) {
                            void * t613 = gm_l2;
                            uint64_t t614 = gm_l61;
                            uint8_t t615 = gm_f_is_decl_used_oldf2ja(t613, t614);
                            t587 = t615;
                        } else t587 = 1;
                        if (!t587) {
                            uint64_t t616 = gm_l61;
                            uint64_t t617 = (uintptr_t)t616;
                            void * t618 = gm_l0;
                            uint64_t t619 = GM_LD(uintptr_t, ((((uint8_t *)t618) + 72) + 0) + 8);
                            if (t617 >= (uintptr_t)(t619)) gm_trap_index(t617, t619, gm_srco9vbah234t6e, 175, 171);
                            uint8_t t620 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t618) + 72) + 0)) + t617 * 1));
                            t586 = t620;
                        } else t586 = 1;
                        if (!t586) goto ie585;
                        {
                            goto lc543;
                        }
                        goto ix585;
ie585:;
ix585:;
                    }
                    {
                        void * t623 = gm_l62;
                        uint8_t t624 = GM_LD(uint8_t, (((((uint8_t *)t623) + 40) + 64) + 0));
                        if (t624 != ((uint8_t)0ULL)) goto ie622;
                        gm_tv2uf4leil5hq t625 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t623) + 40) + 64) + 8));
                        F.l65 = t625;
                        {
                            gm_l66 = ((uint8_t)0);
                        }
                        {
                            {
                                void * t628 = gm_l62;
                                gm_b16_8 t629 = GM_LD(gm_b16_8, ((((uint8_t *)t628) + 40) + 40));
                                gm_l67 = t629;
                            }
                            {
                                gm_l68 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t631 = gm_l68;
                                    uint64_t t632 = GM_LD(uintptr_t, ((uint8_t *)&gm_l67) + 8);
                                    uint8_t t633 = (t631 < t632);
                                    if (!t633) goto lb630;
                                    {
                                        uint64_t t636 = gm_l68;
                                        uint64_t t637 = (uintptr_t)t636;
                                        uint64_t t638 = GM_LD(uintptr_t, ((uint8_t *)&gm_l67) + 8);
                                        gm_tkacitepg5fk0 t641 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l67)) + t637 * 72));
                                        F.l69 = t641;
                                        {
                                            gm_b16_8 t644 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l69) + 0) + 0));
                                            gm_t6j6smj7qoctu t645;
                                            gm_b16_8 t646;
                                            GM_ST(void *, (uint8_t *)&t646, (void *)(gm_spe8t3e79vnal));
                                            GM_ST(uintptr_t, (uint8_t *)&t646 + 8, (uintptr_t)(6));
                                            GM_ST(gm_b16_8, (uint8_t *)&t645 + 0, t646);
                                            gm_b16_8 t647;
                                            GM_ST(void *, (uint8_t *)&t647, (void *)(gm_s1ft2nrvnhcbp));
                                            GM_ST(uintptr_t, (uint8_t *)&t647 + 8, (uintptr_t)(9));
                                            GM_ST(gm_b16_8, (uint8_t *)&t645 + 16, t647);
                                            gm_t6j6smj7qoctu t648 = t645;
                                            uint8_t t649 = 0;
                                            gm_b16_8 t650 = t644;
                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t648) + 0, ((uint8_t *)&t650))) t649 = 1;
                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t648) + 16, ((uint8_t *)&t650))) t649 = 1;
                                            uint8_t t651 = (uint8_t)(t649);
                                            if (!t651) goto ie643;
                                            {
                                                gm_l66 = ((uint8_t)1);
                                            }
                                            goto ix643;
ie643:;
ix643:;
                                        }
                                        goto me639;
mn640:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 178, 9);
me639:;
                                    }
lc630:;
                                    {
                                        uint64_t t654 = gm_l68;
                                        uint64_t r655; bool o656 = __builtin_add_overflow(t654, ((uint64_t)1ULL), &r655);
                                        uint64_t t657 = r655;
                                        gm_l68 = t657;
                                    }
                                }
lb630:;
                            }
                        }
                        {
                            uint8_t t659 = gm_l66;
                            if (!t659) goto ie658;
                            {
                                goto lc543;
                            }
                            goto ix658;
ie658:;
ix658:;
                        }
                        {
                            void * t661 = gm_l2;
                            uint64_t t662 = gm_l61;
                            gm_f_state_a5bnsjb((void *)&F.l70, (void *)NULL, cx, NULL, t661, t662);
                            if (gm_unw.kind) {
                                goto gm_ll547;
                            }
                        }
                        {
                            uint8_t t664;
                            uint8_t t667 = GM_LD(uint8_t, (((uint8_t *)&F.l70) + 0));
                            if (t667 != ((uint8_t)2ULL)) goto mn666;
                            t664 = ((uint8_t)0);
                            goto me665;
mn666:;
                            t664 = ((uint8_t)1);
                            goto me665;
mn668:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 183, 12);
me665:;
                            if (!t664) goto ie663;
                            {
                                goto lc543;
                            }
                            goto ix663;
ie663:;
ix663:;
                        }
                        {
                            gm_b16_8 t670;
                            GM_ST(void *, (uint8_t *)&t670, (void *)(gm_svklio0qebpog));
                            GM_ST(uintptr_t, (uint8_t *)&t670 + 8, (uintptr_t)(8));
                            gm_l71 = t670;
                        }
                        {
                            void * t671 = GM_LD(void *, (((uint8_t *)&F.l65) + 40));
                            gm_t6e9d80inoppc t672 = GM_LD(gm_t6e9d80inoppc, (((uint8_t *)t671) + 8));
                            gm_t6e9d80inoppc t673 = t672;
                            uint8_t t683 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            switch (t683) { case ((uint8_t)0ULL): goto ma681; case ((uint8_t)1ULL): goto ma681; case ((uint8_t)2ULL): goto ma681; case ((uint8_t)3ULL): goto ma681; case ((uint8_t)4ULL): goto ma681; case ((uint8_t)5ULL): goto ma681; case ((uint8_t)6ULL): goto ma681; case ((uint8_t)7ULL): goto ma681; case ((uint8_t)8ULL): goto ma681; case ((uint8_t)9ULL): goto ma681; case ((uint8_t)10ULL): goto ma681; case ((uint8_t)11ULL): goto ma681; case ((uint8_t)12ULL): goto ma681; case ((uint8_t)13ULL): goto ma681; case ((uint8_t)14ULL): goto ma681; case ((uint8_t)15ULL): goto ma681; case ((uint8_t)16ULL): goto ma681; case ((uint8_t)17ULL): goto ma681; case ((uint8_t)18ULL): goto ma681; case ((uint8_t)19ULL): goto ma681; case ((uint8_t)20ULL): goto ma681; case ((uint8_t)21ULL): goto ma681; case ((uint8_t)22ULL): goto ma681; case ((uint8_t)23ULL): goto ma681; case ((uint8_t)24ULL): goto ma681; case ((uint8_t)25ULL): goto ma681; case ((uint8_t)26ULL): goto ma681; case ((uint8_t)27ULL): goto ma681; case ((uint8_t)28ULL): goto ma681; case ((uint8_t)29ULL): goto ma681; case ((uint8_t)30ULL): goto ma681; case ((uint8_t)31ULL): goto ma681; case ((uint8_t)32ULL): goto ma678; case ((uint8_t)33ULL): goto ma678; case ((uint8_t)34ULL): goto ma679; case ((uint8_t)35ULL): goto ma680; case ((uint8_t)36ULL): goto ma675; case ((uint8_t)37ULL): goto ma681; case ((uint8_t)38ULL): goto ma676; case ((uint8_t)39ULL): goto ma677; case ((uint8_t)40ULL): goto ma681; case ((uint8_t)41ULL): goto ma681; case ((uint8_t)42ULL): goto ma681; case ((uint8_t)43ULL): goto ma681; case ((uint8_t)44ULL): goto ma681; case ((uint8_t)45ULL): goto ma681; case ((uint8_t)46ULL): goto ma681; case ((uint8_t)47ULL): goto ma681; case ((uint8_t)48ULL): goto ma681; case ((uint8_t)49ULL): goto ma681; case ((uint8_t)50ULL): goto ma681; default: goto ma681; }
ma675:;
                            uint8_t t685 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            if (t685 != ((uint8_t)36ULL)) goto mn684;
                            {
                                gm_b16_8 t687;
                                GM_ST(void *, (uint8_t *)&t687, (void *)(gm_sq0dqiq83b924));
                                GM_ST(uintptr_t, (uint8_t *)&t687 + 8, (uintptr_t)(8));
                                gm_l71 = t687;
                            }
                            goto me674;
mn684:;
ma676:;
                            uint8_t t689 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            if (t689 != ((uint8_t)38ULL)) goto mn688;
                            {
                                gm_b16_8 t691;
                                GM_ST(void *, (uint8_t *)&t691, (void *)(gm_sg3kpa3h16no3));
                                GM_ST(uintptr_t, (uint8_t *)&t691 + 8, (uintptr_t)(6));
                                gm_l71 = t691;
                            }
                            goto me674;
mn688:;
ma677:;
                            uint8_t t693 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            if (t693 != ((uint8_t)39ULL)) goto mn692;
                            {
                                gm_b16_8 t695;
                                GM_ST(void *, (uint8_t *)&t695, (void *)(gm_sa0amdcmvej1l));
                                GM_ST(uintptr_t, (uint8_t *)&t695 + 8, (uintptr_t)(13));
                                gm_l71 = t695;
                            }
                            goto me674;
mn692:;
ma678:;
                            uint8_t t699 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            if (t699 != ((uint8_t)32ULL)) goto pa698;
                            goto po697;
pa698:;
                            uint8_t t701 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            if (t701 != ((uint8_t)33ULL)) goto pa700;
                            goto po697;
pa700:;
                            goto mn696;
po697:;
                            {
                                gm_b16_8 t703;
                                GM_ST(void *, (uint8_t *)&t703, (void *)(gm_s2737i1iq57nf));
                                GM_ST(uintptr_t, (uint8_t *)&t703 + 8, (uintptr_t)(4));
                                gm_l71 = t703;
                            }
                            goto me674;
mn696:;
ma679:;
                            uint8_t t705 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            if (t705 != ((uint8_t)34ULL)) goto mn704;
                            {
                                gm_b16_8 t707;
                                GM_ST(void *, (uint8_t *)&t707, (void *)(gm_svqdrhms9i231));
                                GM_ST(uintptr_t, (uint8_t *)&t707 + 8, (uintptr_t)(6));
                                gm_l71 = t707;
                            }
                            goto me674;
mn704:;
ma680:;
                            uint8_t t709 = GM_LD(uint8_t, (((uint8_t *)&t673) + 0));
                            if (t709 != ((uint8_t)35ULL)) goto mn708;
                            void * t710 = GM_LD(void *, ((((uint8_t *)&t673) + 8) + 16));
                            gm_l72 = t710;
                            uint8_t t711 = 0;
                            void * t712 = gm_l72;
                            uint8_t t713;
                            uint8_t t716 = GM_LD(uint8_t, ((((uint8_t *)t712) + 8) + 0));
                            if (t716 != ((uint8_t)32ULL)) goto mn715;
                            t713 = ((uint8_t)1);
                            goto me714;
mn715:;
                            t713 = ((uint8_t)0);
                            goto me714;
mn717:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 191, 38);
me714:;
                            if (!t713) {
                                void * t718 = gm_l72;
                                uint8_t t719;
                                uint8_t t722 = GM_LD(uint8_t, ((((uint8_t *)t718) + 8) + 0));
                                if (t722 != ((uint8_t)33ULL)) goto mn721;
                                t719 = ((uint8_t)1);
                                goto me720;
mn721:;
                                t719 = ((uint8_t)0);
                                goto me720;
mn723:;
                                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 191, 66);
me720:;
                                t711 = t719;
                            } else t711 = 1;
                            if (!t711) goto mn708;
                            {
                                gm_b16_8 t725;
                                GM_ST(void *, (uint8_t *)&t725, (void *)(gm_s2737i1iq57nf));
                                GM_ST(uintptr_t, (uint8_t *)&t725 + 8, (uintptr_t)(4));
                                gm_l71 = t725;
                            }
                            goto me674;
mn708:;
ma681:;
                            {
                                uint8_t t729 = GM_LD(uint8_t, (((uint8_t *)&F.l70) + 0));
                                if (t729 != ((uint8_t)2ULL)) goto ie728;
                                gm_t6qooom8qa2do t730 = GM_LD(gm_t6qooom8qa2do, (((uint8_t *)&F.l70) + 8));
                                F.l73 = t730;
                                {
                                    uint8_t t739 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    switch (t739) { case ((uint8_t)0ULL): goto ma733; case ((uint8_t)1ULL): goto ma733; case ((uint8_t)2ULL): goto ma737; case ((uint8_t)3ULL): goto ma734; case ((uint8_t)4ULL): goto ma734; case ((uint8_t)5ULL): goto ma737; case ((uint8_t)6ULL): goto ma737; case ((uint8_t)7ULL): goto ma737; case ((uint8_t)8ULL): goto ma737; case ((uint8_t)9ULL): goto ma736; case ((uint8_t)10ULL): goto ma735; case ((uint8_t)11ULL): goto ma737; default: goto ma737; }
ma733:;
                                    uint8_t t743 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t743 != ((uint8_t)0ULL)) goto pa742;
                                    goto po741;
pa742:;
                                    uint8_t t745 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t745 != ((uint8_t)1ULL)) goto pa744;
                                    goto po741;
pa744:;
                                    goto mn740;
po741:;
                                    {
                                        gm_b16_8 t747;
                                        GM_ST(void *, (uint8_t *)&t747, (void *)(gm_skr6p9lfc5ftp));
                                        GM_ST(uintptr_t, (uint8_t *)&t747 + 8, (uintptr_t)(10));
                                        gm_l71 = t747;
                                    }
                                    goto me732;
mn740:;
ma734:;
                                    uint8_t t751 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t751 != ((uint8_t)4ULL)) goto pa750;
                                    goto po749;
pa750:;
                                    uint8_t t753 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t753 != ((uint8_t)3ULL)) goto pa752;
                                    goto po749;
pa752:;
                                    goto mn748;
po749:;
                                    {
                                        gm_b16_8 t755;
                                        GM_ST(void *, (uint8_t *)&t755, (void *)(gm_svqdrhms9i231));
                                        GM_ST(uintptr_t, (uint8_t *)&t755 + 8, (uintptr_t)(6));
                                        gm_l71 = t755;
                                    }
                                    goto me732;
mn748:;
ma735:;
                                    uint8_t t757 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t757 != ((uint8_t)10ULL)) goto mn756;
                                    {
                                        gm_b16_8 t759;
                                        GM_ST(void *, (uint8_t *)&t759, (void *)(gm_sptnm8c6c52dr));
                                        GM_ST(uintptr_t, (uint8_t *)&t759 + 8, (uintptr_t)(6));
                                        gm_l71 = t759;
                                    }
                                    goto me732;
mn756:;
ma736:;
                                    uint8_t t761 = GM_LD(uint8_t, (((uint8_t *)&F.l73) + 0));
                                    if (t761 != ((uint8_t)9ULL)) goto mn760;
                                    {
                                        gm_b16_8 t763;
                                        GM_ST(void *, (uint8_t *)&t763, (void *)(gm_sg3kpa3h16no3));
                                        GM_ST(uintptr_t, (uint8_t *)&t763 + 8, (uintptr_t)(6));
                                        gm_l71 = t763;
                                    }
                                    goto me732;
mn760:;
ma737:;
                                    goto me732;
mn764:;
ma738:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 194, 21);
me732:;
                                }
                                goto ix728;
ie728:;
ix728:;
                            }
                            goto me674;
mn726:;
ma682:;
                            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 185, 9);
me674:;
                        }
                        {
                            void * t766 = gm_l0;
                            void * t767 = (void *)(((uint8_t *)t766) + 208);
                            gm_tt384v6n8ojib t768;
                            void * t769 = gm_l62;
                            gm_b16_8 t770 = GM_LD(gm_b16_8, ((((uint8_t *)t769) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t768 + 0, t770);
                            void * t771 = gm_l62;
                            gm_tl874t2atnlh3 t772 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t771) + 0) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t768 + 16, t772);
                            void * t773 = gm_l62;
                            gm_tl874t2atnlh3 t774 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t773) + 40) + 4));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t768 + 28, t774);
                            gm_b16_8 t775 = gm_l71;
                            GM_ST(gm_b16_8, (uint8_t *)&t768 + 40, t775);
                            gm_tj5f8vajf35o7 t776;
                            GM_ST(uint8_t, (uint8_t *)&t776 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t776 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t776 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t776 + 64, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t776 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_tt384v6n8ojib, ((uint8_t *)&t776 + 8), t768);
                            gm_tftb1odc739t5 t777 = gm_f_push_rqlbv1e(t767, &t776);
                            gm_tftb1odc739t5 t778 = t777;
                            if (((((uint8_t *)&t778))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t778) + 1, gm_srco9vbah234t6e, 204, 9);
                            gm_b0_1 t779 = GM_LD(gm_b0_1, (((uint8_t *)&t778) + 1));
                        }
                        goto ix622;
ie622:;
                        {
                            goto lc543;
                        }
ix622:;
                    }
                    goto me551;
mn552:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 169, 5);
me551:;
                }
lc543:;
                {
                    uint64_t t782 = gm_l60;
                    uint64_t r783; bool o784 = __builtin_add_overflow(t782, ((uint64_t)1ULL), &r783);
                    uint64_t t785 = r783;
                    gm_l60 = t785;
                }
            }
gm_ll547:;
            if (gm_unw.frame == &F.h && gm_unw.label == 10) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb543; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc543; }
            }
            goto gm_lad;
lb543:;
        }
    }
    {
        gm_f_list_1aqn740((void *)&F.l74, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t787 = gm_l0;
            gm_b16_8 t788 = GM_LD(gm_b16_8, ((((uint8_t *)t787) + 208) + 0));
            gm_l75 = t788;
        }
        {
            gm_l76 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t790 = gm_l76;
                uint64_t t791 = GM_LD(uintptr_t, ((uint8_t *)&gm_l75) + 8);
                uint8_t t792 = (t790 < t791);
                if (!t792) goto lb789;
                {
                    uint64_t t795 = gm_l76;
                    uint64_t t796 = (uintptr_t)t795;
                    uint64_t t797 = GM_LD(uintptr_t, ((uint8_t *)&gm_l75) + 8);
                    gm_tj5f8vajf35o7 t800 = GM_LD(gm_tj5f8vajf35o7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l75)) + t796 * 72));
                    F.l77 = t800;
                    {
                        uint8_t t802;
                        uint8_t t809 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        switch (t809) { case ((uint8_t)0ULL): goto ma807; case ((uint8_t)1ULL): goto ma807; case ((uint8_t)2ULL): goto ma807; case ((uint8_t)3ULL): goto ma804; case ((uint8_t)4ULL): goto ma805; case ((uint8_t)5ULL): goto ma806; case ((uint8_t)6ULL): goto ma807; case ((uint8_t)7ULL): goto ma807; case ((uint8_t)8ULL): goto ma807; case ((uint8_t)9ULL): goto ma807; case ((uint8_t)10ULL): goto ma807; default: goto ma807; }
ma804:;
                        uint8_t t811 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        if (t811 != ((uint8_t)3ULL)) goto mn810;
                        gm_tl874t2atnlh3 t812 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l77) + 8) + 0));
                        gm_l78 = t812;
                        gm_closure t813 = gm_l3;
                        gm_tl874t2atnlh3 t814 = gm_l78;
                        gm_closure t815 = t813;
                        gm_node *ccx816 = t815.cx;
                        void * t817 = t815.code;
                        uint8_t t818;
                        if (GM_PTR_TAGGED(t817)) {
                            t818 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t817))(GM_CONV(gm_cseahvg8v7p0vs, t814));
                        } else {
                            t818 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t817)(t815.env, ccx816, NULL, t814);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll793;
                        }
                        t802 = t818;
                        goto me803;
mn810:;
ma805:;
                        uint8_t t820 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        if (t820 != ((uint8_t)4ULL)) goto mn819;
                        gm_tl874t2atnlh3 t821 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l77) + 8) + 0));
                        gm_l79 = t821;
                        gm_closure t822 = gm_l3;
                        gm_tl874t2atnlh3 t823 = gm_l79;
                        gm_closure t824 = t822;
                        gm_node *ccx825 = t824.cx;
                        void * t826 = t824.code;
                        uint8_t t827;
                        if (GM_PTR_TAGGED(t826)) {
                            t827 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t826))(GM_CONV(gm_cseahvg8v7p0vs, t823));
                        } else {
                            t827 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t826)(t824.env, ccx825, NULL, t823);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll793;
                        }
                        t802 = t827;
                        goto me803;
mn819:;
ma806:;
                        uint8_t t829 = GM_LD(uint8_t, (((uint8_t *)&F.l77) + 0));
                        if (t829 != ((uint8_t)5ULL)) goto mn828;
                        gm_tl874t2atnlh3 t830 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l77) + 8));
                        gm_l80 = t830;
                        gm_closure t831 = gm_l3;
                        gm_tl874t2atnlh3 t832 = gm_l80;
                        gm_closure t833 = t831;
                        gm_node *ccx834 = t833.cx;
                        void * t835 = t833.code;
                        uint8_t t836;
                        if (GM_PTR_TAGGED(t835)) {
                            t836 = ((uint8_t (*)(gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t835))(GM_CONV(gm_cseahvg8v7p0vs, t832));
                        } else {
                            t836 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_tl874t2atnlh3))t835)(t833.env, ccx834, NULL, t832);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll793;
                        }
                        t802 = t836;
                        goto me803;
mn828:;
ma807:;
                        t802 = ((uint8_t)1);
                        goto me803;
mn837:;
ma808:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 208, 17);
me803:;
                        gm_l81 = t802;
                    }
                    {
                        uint8_t t839 = gm_l81;
                        if (!t839) goto ie838;
                        {
                            void * t841 = (void *)((uint8_t *)&F.l74);
                            gm_tftb1odc739t5 t842 = gm_f_push_rqlbv1e(t841, &(*(gm_tj5f8vajf35o7 *)((uint8_t *)&F.l77)));
                            gm_tftb1odc739t5 t843 = t842;
                            if (((((uint8_t *)&t843))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t843) + 1, gm_srco9vbah234t6e, 214, 19);
                            gm_b0_1 t844 = GM_LD(gm_b0_1, (((uint8_t *)&t843) + 1));
                        }
                        goto ix838;
ie838:;
ix838:;
                    }
                    goto me798;
mn799:;
                    gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 207, 5);
me798:;
                }
lc789:;
                {
                    uint64_t t846 = gm_l76;
                    uint64_t r847; bool o848 = __builtin_add_overflow(t846, ((uint64_t)1ULL), &r847);
                    uint64_t t849 = r847;
                    gm_l76 = t849;
                }
            }
gm_ll793:;
            if (gm_unw.frame == &F.h && gm_unw.label == 12) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb789; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc789; }
            }
            goto gm_lad;
lb789:;
        }
    }
    {
        void * t850 = gm_l0;
        gm_trqfvmjtt4sij t851 = F.l74;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t850) + 208), t851);
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_value_7pehjnc(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_value_7pehjnc F;
    void * gm_l0; /* c */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint8_t gm_l5; /* plain */
    GM_STACK_CHECK(&F, 301u);
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
        void * t5 = gm_l1;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l3;
        gm_t88rcucg1b7uk t8 = gm_l4;
        uint8_t t9 = gm_f_is_integer_8d4f9ek(t7, GM_CONV(gm_csguankko4lmhd, t8));
        gm_l5 = t9;
    }
    {
        void * t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = gm_l4;
        gm_t53at80pfdus3 t12;
        gm_f_kind_jcnt4hm((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
        gm_t53at80pfdus3 t13 = t12;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t18 != ((uint8_t)4ULL)) goto pa17;
        goto po16;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t20 != ((uint8_t)8ULL)) goto pa19;
        goto po16;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t22 != ((uint8_t)9ULL)) goto pa21;
        goto po16;
pa21:;
        goto mn15;
po16:;
        {
            gm_l5 = ((uint8_t)1);
        }
        goto me14;
mn15:;
        goto me14;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 396, 5);
me14:;
    }
    {
        uint8_t t26 = 0;
        uint8_t t27 = 0;
        uint8_t t28 = gm_l5;
        if (t28) {
            void * t29 = gm_l1;
            gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 208));
            void * t31 = gm_l2;
            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 208));
            gm_tl874t2atnlh3 t33 = t30;
            gm_tl874t2atnlh3 t34 = t32;
            uint8_t t35 = (!gm_eqff85ac10o3(((uint8_t *)&t33), ((uint8_t *)&t34)));
            t27 = t35;
        } else t27 = 0;
        if (t27) {
            void * t36 = gm_l1;
            void * t37 = gm_l2;
            uint8_t t38 = gm_f_path_eq_l3qbb7v(t36, t37);
            t26 = t38;
        } else t26 = 0;
        uint8_t t39 = 0;
        F.ret = t26;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_use_of_q9agaul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1) {
    struct gm_fr_use_of_q9agaul F;
    void * gm_l0; /* c */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    GM_STACK_CHECK(&F, 198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 16);
        gm_tcc1rq4n1avb2 t5 = gm_l1;
        void * t6 = gm_f_get_ptr_1a4voen(t4, t5);
        void * t7 = t6;
        uint8_t t8;
        if ((GM_LD(void *, ((uint8_t *)&t7)) != 0)) goto mn10;
        t8 = ((uint8_t)1);
        goto me9;
mn10:;
        t8 = ((uint8_t)0);
        goto me9;
mn11:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 276, 8);
me9:;
        if (!t8) goto ie2;
        {
            void * t13 = gm_l0;
            void * t14 = (void *)(((uint8_t *)t13) + 16);
            gm_tcc1rq4n1avb2 t15 = gm_l1;
            gm_tqetrgo0q24b5 t16;
            memset((uint8_t *)&t16 + 41, 0, 7);
            gm_f_list_7s4om10((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t16 + 0)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&t16 + 40, ((uint8_t)0));
            gm_tftb1odc739t5 t17 = gm_f_put_he2q4rq(t14, t15, &t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srco9vbah234t6e, 276, 36);
            gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 16);
        gm_tcc1rq4n1avb2 t22 = gm_l1;
        void * t23 = gm_f_get_ptr_1a4voen(t21, t22);
        void * t24 = t23;
        if (!(GM_LD(void *, ((uint8_t *)&t24)) != 0)) gm_trap("unwrap of null", gm_srco9vbah234t6e, 277, 12);
        void * t25 = GM_LD(void *, ((uint8_t *)&t24));
        uint8_t t26 = 0;
        F.ret = t25;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_unu9ms6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3) {
    struct gm_fr_visit_unu9ms6 F;
    void * gm_l2; /* e */
    void * gm_l4; /* tys */
    void * gm_l5; /* p */
    uint8_t gm_l6; /* is_const */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    uint8_t gm_l8; /* is_const */
    uint32_t gm_l10; /* g */
    uint32_t gm_l11; /* g */
    uint32_t gm_l12; /* g */
    void * gm_l13; /* b */
    gm_b16_8 gm_l14; /* arms */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l17; /* a */
    gm_trl2khmuphncd gm_l18; /* x */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tlr9dp305htm7 gm_l21; /* cp */
    void * gm_l22; /* pat */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_tlr9dp305htm7 gm_l26; /* cp */
    void * gm_l27; /* pat */
    gm_t3dcoc7077t1q gm_l28; /* x */
    uint32_t gm_l29; /* l */
    void * gm_l30; /* lhs */
    void * gm_l31; /* rhs */
    uint8_t gm_l32; /* negated */
    gm_tqs5p4n2tq19v gm_l33; /* lb */
    gm_tqs5p4n2tq19v gm_l34; /* rb */
    uint8_t gm_l35; /* b */
    void * gm_l36; /* other */
    gm_ts6oqppjh0ba3 gm_l37; /* op */
    void * gm_l38; /* lhs */
    void * gm_l39; /* rhs */
    GM_STACK_CHECK(&F, 2650u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    {
        void * t2 = F.l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l2;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma18; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma18; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma18; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma18; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma18; case ((uint8_t)23ULL): goto ma18; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma17; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma18; case ((uint8_t)33ULL): goto ma18; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma18; case ((uint8_t)37ULL): goto ma18; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma18; case ((uint8_t)40ULL): goto ma18; case ((uint8_t)41ULL): goto ma15; case ((uint8_t)42ULL): goto ma18; case ((uint8_t)43ULL): goto ma16; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma18; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma14; case ((uint8_t)49ULL): goto ma18; case ((uint8_t)50ULL): goto ma18; case ((uint8_t)51ULL): goto ma18; case ((uint8_t)52ULL): goto ma18; case ((uint8_t)53ULL): goto ma18; case ((uint8_t)54ULL): goto ma18; case ((uint8_t)55ULL): goto ma18; case ((uint8_t)56ULL): goto ma18; case ((uint8_t)57ULL): goto ma18; case ((uint8_t)58ULL): goto ma18; default: goto ma18; }
ma7:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t22 != ((uint8_t)24ULL)) goto mn21;
        void * t23 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l5 = t23;
        void * t25 = gm_l4;
        void * t26 = gm_l2;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
        gm_t53at80pfdus3 t28;
        gm_f_kind_jcnt4hm((void *)&t28, t25, GM_CONV(gm_csguankko4lmhd, t27));
        gm_t53at80pfdus3 t29 = t28;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t30 != ((uint8_t)8ULL)) goto ie24;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)&t29) + 8) + 0));
        gm_l6 = t31;
        {
            uint8_t t34 = gm_l6;
            uint8_t t35 = (uint8_t)!t34;
            if (!t35) goto ie33;
            {
                void * t37 = F.l0;
                uint32_t t38 = F.l1;
                void * t39 = gm_l5;
                gm_b0_1 t40 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t37, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix33;
ie33:;
ix33:;
        }
        goto ix24;
ie24:;
ix24:;
        goto me6;
mn21:;
ma8:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t42 != ((uint8_t)21ULL)) goto mn41;
        gm_tao63pk3r18v5 t43 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t5) + 8) + 8));
        gm_l7 = t43;
        void * t45 = gm_l4;
        void * t46 = gm_l2;
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 0));
        gm_t53at80pfdus3 t48;
        gm_f_kind_jcnt4hm((void *)&t48, t45, GM_CONV(gm_csguankko4lmhd, t47));
        gm_t53at80pfdus3 t49 = t48;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t50 != ((uint8_t)10ULL)) goto ie44;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)&t49) + 8) + 0));
        gm_l8 = t51;
        {
            uint8_t t54 = gm_l8;
            uint8_t t55 = (uint8_t)!t54;
            if (!t55) goto ie53;
            {
                void * t57 = F.l0;
                uint32_t t58 = F.l1;
                void * t59 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
                gm_b0_1 t60 = gm_f_mutate_oi1s4vs((void *)NULL, cx, NULL, t57, t58, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix53;
ie53:;
ix53:;
        }
        goto ix44;
ie44:;
ix44:;
        goto me6;
mn41:;
ma9:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t62 != ((uint8_t)34ULL)) goto mn61;
        gm_tsth9qu1dv0g3 t63 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t5) + 8) + 8) + 0));
        F.l9 = t63;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
        if (t65 != ((uint8_t)0ULL)) goto ie64;
        uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 8));
        gm_l10 = t66;
        {
            void * t68 = F.l3;
            uint32_t t69 = gm_l10;
            gm_tftb1odc739t5 t70 = gm_f_push_im9d4ro(t68, t69);
            gm_tftb1odc739t5 t71 = t70;
            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srco9vbah234t6e, 291, 51);
            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
        }
        goto ix64;
ie64:;
ix64:;
        goto me6;
mn61:;
ma10:;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t76 != ((uint8_t)26ULL)) goto pa75;
        uint32_t t77 = GM_LD(uint32_t, ((((uint8_t *)t5) + 8) + 8));
        gm_l11 = t77;
        goto po74;
pa75:;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t79 != ((uint8_t)27ULL)) goto pa78;
        uint32_t t80 = GM_LD(uint32_t, ((((uint8_t *)t5) + 8) + 8));
        gm_l11 = t80;
        goto po74;
pa78:;
        goto mn73;
po74:;
        {
            void * t82 = F.l3;
            uint32_t t83 = gm_l11;
            gm_tftb1odc739t5 t84 = gm_f_push_im9d4ro(t82, t83);
            gm_tftb1odc739t5 t85 = t84;
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srco9vbah234t6e, 293, 13);
            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
        }
        {
            void * t87 = F.l0;
            gm_b16_8 t88 = GM_LD(gm_b16_8, ((((uint8_t *)t87) + 112) + 0));
            gm_b16_8 t89 = t88;
            void * t90 = F.l0;
            void * t91 = GM_LD(void *, (((uint8_t *)t90) + 0));
            uint32_t t92 = gm_l11;
            uint32_t t93 = gm_f_generic_of_1861hm9(t91, t92);
            uint64_t t94 = (uint64_t)t93;
            uint64_t t95 = (uintptr_t)t94;
            gm_b16_8 t96 = t89;
            uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&t96) + 8);
            if (t95 >= (uintptr_t)(t97)) gm_trap_index(t95, t97, gm_srco9vbah234t6e, 294, 13);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t96)) + t95 * 1), ((uint8_t)1));
        }
        goto me6;
mn73:;
ma11:;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t99 != ((uint8_t)46ULL)) goto mn98;
        void * t100 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l13 = t100;
        void * t101 = F.l0;
        uint32_t t102 = F.l1;
        void * t103 = gm_l13;
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)t103) + 0));
        gm_b16_8 t105 = t104;
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
        gm_b0_1 t107 = gm_f_block_stmts_c2nnk6m((void *)NULL, cx, NULL, t101, t102, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn98:;
ma12:;
        uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t109 != ((uint8_t)44ULL)) goto mn108;
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l14 = t110;
        {
            {
                gm_b16_8 t113 = gm_l14;
                gm_l15 = t113;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t115 = gm_l16;
                    uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t117 = (t115 < t116);
                    if (!t117) goto lb114;
                    {
                        uint64_t t120 = gm_l16;
                        uint64_t t121 = (uintptr_t)t120;
                        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_tbbqefuhv2r8h t125 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t121 * 24));
                        gm_l17 = t125;
                        {
                            void * t127 = F.l0;
                            uint32_t t128 = F.l1;
                            void * t129 = GM_LD(void *, (((uint8_t *)&gm_l17) + 0));
                            gm_b0_1 t130 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t127, t128, t129);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                        }
                        goto me123;
mn124:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 298, 13);
me123:;
                    }
lc114:;
                    {
                        uint64_t t132 = gm_l16;
                        uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                        uint64_t t135 = r133;
                        gm_l16 = t135;
                    }
                }
gm_ll118:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb114; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc114; }
                }
                goto gm_lad;
lb114:;
            }
        }
        {
            uint32_t t137 = F.l1;
            uint64_t t138 = (uint64_t)t137;
            void * t139 = F.l0;
            void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
            uint64_t t141 = GM_LD(uintptr_t, ((((uint8_t *)t140) + 384) + 0) + 8);
            uint8_t t142 = (t138 < t141);
            if (!t142) goto ie136;
            {
                uint32_t t144 = F.l1;
                uint64_t t145 = (uint64_t)t144;
                uint64_t t146 = (uintptr_t)t145;
                void * t147 = F.l0;
                void * t148 = GM_LD(void *, (((uint8_t *)t147) + 0));
                uint64_t t149 = GM_LD(uintptr_t, ((((uint8_t *)t148) + 384) + 0) + 8);
                if (t146 >= (uintptr_t)(t149)) gm_trap_index(t146, t149, gm_srco9vbah234t6e, 299, 71);
                void * t150 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t148) + 384) + 0)) + t146 * 64);
                gm_b16_8 t151 = gm_l14;
                gm_b16_8 t152 = t151;
                gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                void * t154 = F.l0;
                void * t155 = (void *)(((uint8_t *)t154) + 208);
                gm_b0_1 t156 = gm_f_present_only_bindings_ug2i6dq((void *)NULL, cx, NULL, t150, t153, t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix136;
ie136:;
ix136:;
        }
        goto me6;
mn108:;
ma13:;
        uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t158 != ((uint8_t)47ULL)) goto mn157;
        gm_trl2khmuphncd t159 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t5) + 8) + 8));
        gm_l18 = t159;
        {
            {
                gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                gm_l19 = t162;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t164 = gm_l20;
                    uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t166 = (t164 < t165);
                    if (!t166) goto lb163;
                    {
                        uint64_t t169 = gm_l20;
                        uint64_t t170 = (uintptr_t)t169;
                        uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_tlr9dp305htm7 t174 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t170 * 24));
                        gm_l21 = t174;
                        {
                            uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                            if (t177 != ((uint8_t)1ULL)) goto ie176;
                            void * t178 = GM_LD(void *, ((((uint8_t *)&gm_l21) + 8) + 0));
                            gm_l22 = t178;
                            {
                                void * t180 = F.l0;
                                uint32_t t181 = F.l1;
                                void * t182 = gm_l22;
                                gm_b0_1 t183 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t180, t181, t182);
                                if (gm_unw.kind) {
                                    goto gm_ll167;
                                }
                            }
                            goto ix176;
ie176:;
ix176:;
                        }
                        goto me172;
mn173:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 302, 13);
me172:;
                    }
lc163:;
                    {
                        uint64_t t185 = gm_l20;
                        uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                        uint64_t t188 = r186;
                        gm_l20 = t188;
                    }
                }
gm_ll167:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb163; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc163; }
                }
                goto gm_lad;
lb163:;
            }
        }
        goto me6;
mn157:;
ma14:;
        uint8_t t190 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t190 != ((uint8_t)48ULL)) goto mn189;
        gm_t710lsrq1m8gk t191 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t5) + 8) + 8));
        F.l23 = t191;
        {
            {
                gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 8));
                gm_l24 = t194;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t196 = gm_l25;
                    uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t198 = (t196 < t197);
                    if (!t198) goto lb195;
                    {
                        uint64_t t201 = gm_l25;
                        uint64_t t202 = (uintptr_t)t201;
                        uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        gm_tlr9dp305htm7 t206 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t202 * 24));
                        gm_l26 = t206;
                        {
                            uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                            if (t209 != ((uint8_t)1ULL)) goto ie208;
                            void * t210 = GM_LD(void *, ((((uint8_t *)&gm_l26) + 8) + 0));
                            gm_l27 = t210;
                            {
                                void * t212 = F.l0;
                                uint32_t t213 = F.l1;
                                void * t214 = gm_l27;
                                gm_b0_1 t215 = gm_f_binds_busqeo0((void *)NULL, cx, NULL, t212, t213, t214);
                                if (gm_unw.kind) {
                                    goto gm_ll199;
                                }
                            }
                            goto ix208;
ie208:;
ix208:;
                        }
                        goto me204;
mn205:;
                        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 307, 13);
me204:;
                    }
lc195:;
                    {
                        uint64_t t217 = gm_l25;
                        uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                        uint64_t t220 = r218;
                        gm_l25 = t220;
                    }
                }
gm_ll199:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb195; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc195; }
                }
                goto gm_lad;
lb195:;
            }
        }
        goto me6;
mn189:;
ma15:;
        uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t222 != ((uint8_t)41ULL)) goto mn221;
        gm_t3dcoc7077t1q t223 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t5) + 8) + 8));
        gm_l28 = t223;
        if (!(((((uint8_t *)&gm_l28) + 8))[4] != 0)) goto ie224;
        uint32_t t225 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 8));
        gm_l29 = t225;
        {
            void * t227 = F.l0;
            void * t228 = (void *)(((uint8_t *)t227) + 152);
            uint32_t t229 = F.l1;
            uint32_t t230 = gm_l29;
            gm_tcc1rq4n1avb2 t231;
            GM_ST(uint32_t, ((uint8_t *)&t231 + 0), t229);
            GM_ST(uint32_t, ((uint8_t *)&t231 + 4), t230);
            gm_tftb1odc739t5 t232 = gm_f_put_3g1tdpr(t228, t231, ((uint8_t)1));
            gm_tftb1odc739t5 t233 = t232;
            if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 1, gm_srco9vbah234t6e, 311, 41);
            gm_b0_1 t234 = GM_LD(gm_b0_1, (((uint8_t *)&t233) + 1));
        }
        goto ix224;
ie224:;
ix224:;
        goto me6;
mn221:;
ma16:;
        uint8_t t236 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t236 != ((uint8_t)43ULL)) goto mn235;
        void * t237 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l30 = t237;
        void * t238 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l31 = t238;
        uint8_t t239 = GM_LD(uint8_t, (((((uint8_t *)t5) + 8) + 8) + 16));
        gm_l32 = t239;
        {
            void * t241 = gm_l30;
            gm_tqs5p4n2tq19v t242 = gm_f_bool_lit_q44074e(t241);
            gm_l33 = t242;
        }
        {
            void * t243 = gm_l31;
            gm_tqs5p4n2tq19v t244 = gm_f_bool_lit_q44074e(t243);
            gm_l34 = t244;
        }
        {
            uint8_t t246;
            if (((((uint8_t *)&gm_l33))[1] != 0)) goto mn248;
            t246 = ((uint8_t)0);
            goto me247;
mn248:;
            t246 = ((uint8_t)1);
            goto me247;
mn249:;
            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 315, 17);
me247:;
            uint8_t t250;
            if (((((uint8_t *)&gm_l34))[1] != 0)) goto mn252;
            t250 = ((uint8_t)0);
            goto me251;
mn252:;
            t250 = ((uint8_t)1);
            goto me251;
mn253:;
            gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 315, 41);
me251:;
            uint8_t t254 = (!(t246 == t250));
            if (!t254) goto ie245;
            {
                gm_tqs5p4n2tq19v t256 = gm_l33;
                gm_tqs5p4n2tq19v t257 = t256;
                uint8_t t258;
                if (((((uint8_t *)&t257))[1] != 0)) {
                    uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&t257) + 0));
                    t258 = t259;
                } else {
                    gm_tqs5p4n2tq19v t260 = gm_l34;
                    gm_tqs5p4n2tq19v t261 = t260;
                    if (!((((uint8_t *)&t261))[1] != 0)) gm_trap("unwrap of null", gm_srco9vbah234t6e, 316, 30);
                    uint8_t t262 = GM_LD(uint8_t, ((uint8_t *)&t261));
                    t258 = t262;
                }
                gm_l35 = t258;
            }
            {
                void * t263;
                uint8_t t265;
                if (((((uint8_t *)&gm_l33))[1] != 0)) goto mn267;
                t265 = ((uint8_t)0);
                goto me266;
mn267:;
                t265 = ((uint8_t)1);
                goto me266;
mn268:;
                gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 317, 29);
me266:;
                if (!t265) goto ie264;
                void * t270 = gm_l31;
                void * t271 = t270;
                t263 = t271;
                goto ix264;
ie264:;
                void * t273 = gm_l30;
                void * t274 = t273;
                t263 = t274;
ix264:;
                gm_l36 = t263;
            }
            {
                void * t276 = gm_l4;
                void * t277 = gm_l36;
                gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t277) + 0));
                gm_t53at80pfdus3 t279;
                gm_f_kind_jcnt4hm((void *)&t279, t276, GM_CONV(gm_csguankko4lmhd, t278));
                gm_t53at80pfdus3 t280 = t279;
                uint8_t t281 = ((GM_LD(uint8_t, ((uint8_t *)&t280) + 0) == ((uint8_t)4ULL)));
                if (!t281) goto ie275;
                void * t282 = gm_l2;
                gm_tl874t2atnlh3 t283 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t282) + 208));
                gm_tl874t2atnlh3 t284 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_tl874t2atnlh3 t285 = t283;
                gm_tl874t2atnlh3 t286 = t284;
                uint8_t t287 = (!gm_eqff85ac10o3(((uint8_t *)&t285), ((uint8_t *)&t286)));
                if (!t287) goto ie275;
                {
                    void * t289 = F.l0;
                    void * t290 = (void *)(((uint8_t *)t289) + 208);
                    gm_tt7dbdq4n1egc t291;
                    memset((uint8_t *)&t291 + 25, 0, 3);
                    void * t292 = gm_l2;
                    gm_tl874t2atnlh3 t293 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t292) + 208));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t291 + 0, t293);
                    void * t294 = gm_l36;
                    gm_tl874t2atnlh3 t295 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t294) + 208));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t291 + 12, t295);
                    uint8_t t296 = gm_l35;
                    uint8_t t297 = gm_l32;
                    uint8_t t298 = (!(t296 == t297));
                    GM_ST(uint8_t, (uint8_t *)&t291 + 24, t298);
                    gm_tj5f8vajf35o7 t299;
                    memset(&t299, 0, sizeof t299);
                    GM_ST(uint8_t, ((uint8_t *)&t299 + 0), ((uint8_t)3ULL));
                    GM_ST(gm_tt7dbdq4n1egc, ((uint8_t *)&t299 + 8), t291);
                    gm_tftb1odc739t5 t300 = gm_f_push_rqlbv1e(t290, &t299);
                    gm_tftb1odc739t5 t301 = t300;
                    if (((((uint8_t *)&t301))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t301) + 1, gm_srco9vbah234t6e, 319, 21);
                    gm_b0_1 t302 = GM_LD(gm_b0_1, (((uint8_t *)&t301) + 1));
                }
                goto ix275;
ie275:;
ix275:;
            }
            goto ix245;
ie245:;
            void * t304 = F.l0;
            void * t305 = gm_l30;
            void * t306 = gm_l31;
            uint8_t t307 = gm_f_same_value_7pehjnc(t304, t305, t306);
            if (!t307) goto ie303;
            {
                void * t309 = F.l0;
                void * t310 = (void *)(((uint8_t *)t309) + 208);
                void * t311 = gm_l2;
                gm_tl874t2atnlh3 t312 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t311) + 208));
                uint8_t t313 = gm_l32;
                uint8_t t314 = (uint8_t)!t313;
                gm_toceffb697p0u t315;
                GM_ST(uint8_t, (uint8_t *)&t315 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t315 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t315 + 0), t312);
                GM_ST(uint8_t, ((uint8_t *)&t315 + 12), t314);
                gm_tj5f8vajf35o7 t316;
                memset(&t316, 0, sizeof t316);
                GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)4ULL));
                GM_ST(gm_toceffb697p0u, ((uint8_t *)&t316 + 8), t315);
                gm_tftb1odc739t5 t317 = gm_f_push_rqlbv1e(t310, &t316);
                gm_tftb1odc739t5 t318 = t317;
                if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 1, gm_srco9vbah234t6e, 322, 17);
                gm_b0_1 t319 = GM_LD(gm_b0_1, (((uint8_t *)&t318) + 1));
            }
            goto ix303;
ie303:;
ix303:;
ix245:;
        }
        goto me6;
mn235:;
ma17:;
        uint8_t t321 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t321 != ((uint8_t)30ULL)) goto mn320;
        gm_ts6oqppjh0ba3 t322 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l37 = t322;
        void * t323 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l38 = t323;
        void * t324 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 16));
        gm_l39 = t324;
        {
            gm_ts6oqppjh0ba3 t327 = gm_l37;
            gm_ts6oqppjh0ba3 t328;
            GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t328 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t329;
            GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)11ULL));
            gm_ts6oqppjh0ba3 t330;
            GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)12ULL));
            gm_ts6oqppjh0ba3 t331;
            GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)13ULL));
            gm_t6htphat2tk14 t332;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t332 + 0), t328);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t332 + 2), t329);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t332 + 4), t330);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t332 + 6), t331);
            gm_t6htphat2tk14 t333 = t332;
            uint8_t t334 = 0;
            gm_ts6oqppjh0ba3 t335 = t327;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t333) + 0, ((uint8_t *)&t335))) t334 = 1;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t333) + 2, ((uint8_t *)&t335))) t334 = 1;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t333) + 4, ((uint8_t *)&t335))) t334 = 1;
            if (gm_eqbqb4gb1d92_2(((uint8_t *)&t333) + 6, ((uint8_t *)&t335))) t334 = 1;
            uint8_t t336 = (uint8_t)(t334);
            if (!t336) goto ie326;
            void * t337 = F.l0;
            void * t338 = gm_l38;
            void * t339 = gm_l39;
            uint8_t t340 = gm_f_same_value_7pehjnc(t337, t338, t339);
            if (!t340) goto ie326;
            {
                void * t342 = F.l0;
                void * t343 = (void *)(((uint8_t *)t342) + 208);
                void * t344 = gm_l2;
                gm_tl874t2atnlh3 t345 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t344) + 208));
                gm_ts6oqppjh0ba3 t346 = gm_l37;
                gm_ts6oqppjh0ba3 t347;
                GM_ST(uint8_t, (uint8_t *)&t347 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t347 + 0), ((uint8_t)11ULL));
                gm_ts6oqppjh0ba3 t348;
                GM_ST(uint8_t, (uint8_t *)&t348 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)13ULL));
                gm_t9ldr1q5hn9fq t349;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t349 + 0), t347);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t349 + 2), t348);
                gm_t9ldr1q5hn9fq t350 = t349;
                uint8_t t351 = 0;
                gm_ts6oqppjh0ba3 t352 = t346;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t350) + 0, ((uint8_t *)&t352))) t351 = 1;
                if (gm_eqbqb4gb1d92_2(((uint8_t *)&t350) + 2, ((uint8_t *)&t352))) t351 = 1;
                uint8_t t353 = (uint8_t)(t351);
                gm_toceffb697p0u t354;
                GM_ST(uint8_t, (uint8_t *)&t354 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t354 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t354 + 0), t345);
                GM_ST(uint8_t, ((uint8_t *)&t354 + 12), t353);
                gm_tj5f8vajf35o7 t355;
                memset(&t355, 0, sizeof t355);
                GM_ST(uint8_t, ((uint8_t *)&t355 + 0), ((uint8_t)4ULL));
                GM_ST(gm_toceffb697p0u, ((uint8_t *)&t355 + 8), t354);
                gm_tftb1odc739t5 t356 = gm_f_push_rqlbv1e(t343, &t355);
                gm_tftb1odc739t5 t357 = t356;
                if (((((uint8_t *)&t357))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t357) + 1, gm_srco9vbah234t6e, 327, 17);
                gm_b0_1 t358 = GM_LD(gm_b0_1, (((uint8_t *)&t357) + 1));
            }
            goto ix326;
ie326:;
ix326:;
        }
        goto me6;
mn320:;
ma18:;
        goto me6;
mn359:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srco9vbah234t6e, 284, 5);
me6:;
    }
    {
        void * t362 = gm_l2;
        uint8_t t363 = GM_LD(uint8_t, ((((uint8_t *)t362) + 8) + 0));
        if (t363 != ((uint8_t)48ULL)) goto ie361;
        gm_t710lsrq1m8gk t364 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t362) + 8) + 8));
        F.l40 = t364;
        {
            void * t366 = F.l0;
            uint32_t t367 = F.l1;
            void * t368 = GM_LD(void *, (((uint8_t *)&F.l40) + 24));
            gm_b16_8 t369 = GM_LD(gm_b16_8, (((uint8_t *)t368) + 0));
            gm_b16_8 t370 = t369;
            gm_b16_8 t371 = GM_LD(gm_b16_8, ((uint8_t *)&t370));
            gm_b0_1 t372 = gm_f_block_stmts_c2nnk6m((void *)NULL, cx, NULL, t366, t367, t371);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix361;
ie361:;
ix361:;
    }
    {
        void * t373 = gm_l2;
        gm_closure t374 = ((gm_closure){(void *)gm_f_fn_259eng6, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t373, t374);
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

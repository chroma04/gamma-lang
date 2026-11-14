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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t88rcucg1b7uk a[3]; } gm_t10fqegjtup5b;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t10fqegjtup5b) == 12 && _Alignof(gm_t10fqegjtup5b) == 4 && offsetof(gm_t10fqegjtup5b, a) == 0, "layout of `[3]Ty`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t2i8ne5ibd7v4 f0; uint8_t f1; uint8_t pe[3]; } gm_t8uadrbfpr7kk;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8uadrbfpr7kk) == 20 && _Alignof(gm_t8uadrbfpr7kk) == 4 && offsetof(gm_t8uadrbfpr7kk, f0) == 0 && offsetof(gm_t8uadrbfpr7kk, f1) == 16, "layout of `?(ty: Ty, at: Span)`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t0d02142k4tu8 f0; uint8_t f1; uint8_t pe[7]; } gm_toqf51949ppls;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
_Static_assert(sizeof(gm_toqf51949ppls) == 72 && _Alignof(gm_toqf51949ppls) == 8 && offsetof(gm_toqf51949ppls, f0) == 0 && offsetof(gm_toqf51949ppls, f1) == 64, "layout of `?Pattern`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_counting_loop_s4u2ugd {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* cond */
    gm_trqfvmjtt4sij l10; /* more */
    void * ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ilis91k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
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
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_extend_nkql463 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_for_each_value_5qklbg7 {
    gm_frame h;
    gm_tl874t2atnlh3 l4; /* span */
    gm_trqfvmjtt4sij l18; /* stmts */
    gm_trqfvmjtt4sij l34; /* items */
    gm_trqfvmjtt4sij l41; /* stmts */
    gm_trqfvmjtt4sij l47; /* stmts */
    gm_trqfvmjtt4sij l56; /* stmts */
    void * ret;
};
struct gm_fr_for_loop_b6hjsbq {
    gm_frame h;
    void * ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_in_expr_su8ofmp {
    gm_frame h;
    gm_trqfvmjtt4sij l20; /* vals */
    gm_trqfvmjtt4sij l29; /* members */
    gm_trqfvmjtt4sij l30; /* etys */
    gm_trqfvmjtt4sij l41; /* lets */
    gm_trqfvmjtt4sij l58; /* items */
    gm_trqfvmjtt4sij l59; /* jts */
    gm_trqfvmjtt4sij l65; /* stmts */
    void * ret;
};
struct gm_fr_increment_12n73t5 {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_members_97aedtg {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* all */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6l26q4a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_i91hglv {
    gm_frame h;
    void * ret;
};
struct gm_fr_local_l5o3d47 {
    gm_frame h;
    void * ret;
};
struct gm_fr_loop_with_pattern_d8vl0mj {
    gm_frame h;
    gm_t0d02142k4tu8 l8; /* pat_ast */
    gm_trqfvmjtt4sij l9; /* args */
    gm_t0d02142k4tu8 l12; /* p */
    gm_trqfvmjtt4sij l14; /* binds */
    gm_trqfvmjtt4sij l20; /* arms */
    gm_trqfvmjtt4sij l22; /* stmts */
    void * ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_t69m56meqefs8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l74t9rq {
    gm_frame h;
    gm_tnrgict8sthcv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_loop_44fv3au {
    gm_frame h;
    gm_tl874t2atnlh3 l6; /* span */
    gm_trqfvmjtt4sij l11; /* stmts */
    gm_trqfvmjtt4sij l14; /* stop */
    gm_trqfvmjtt4sij l15; /* more */
    gm_trqfvmjtt4sij l16; /* parts */
    gm_trqfvmjtt4sij l18; /* cond */
    void * ret;
};
struct gm_fr_range_test_tqcppmu {
    gm_frame h;
    gm_t88rcucg1b7uk l5; /* ty */
    gm_tl874t2atnlh3 l8; /* span */
    uint32_t l10; /* tmp */
    gm_trqfvmjtt4sij l14; /* stmts */
    void * ret;
};
struct gm_fr_regrow_4bta8ne_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t5pgl2t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_tuple_elem_join_om8llaj {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_tuple_l3i6trt {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_x_dmu97o4 {
    gm_frame h;
    void * ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_deinit_pal9hnf(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_q21h982(void * a0);
__attribute__((aligned(4))) void gm_f_early_fallback_error_4lq49mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) uint8_t gm_f_eq_allowed_d841u9b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_for_each_value_5qklbg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_for_loop_b6hjsbq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_2bpp75h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ftgbk9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h1n6lc9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t00o2ec(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vc9g56o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_in_expr_su8ofmp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_in_patterns_mi6p7hb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_increment_12n73t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_is_pattern_operand_p51tban(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_members_97aedtg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_type_cck70vp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_local_i91hglv(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void * gm_f_local_l5o3d47(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_loop_with_pattern_d8vl0mj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, void * a3, gm_b16_8 a4, void * a5);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_packed_place_5ck3439(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_range_loop_44fv3au(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_t88rcucg1b7uk a4, uint8_t a5);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_range_test_tqcppmu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3, void * a4, gm_t88rcucg1b7uk a5, uint8_t a6, uint8_t a7, gm_tl874t2atnlh3 a8);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_set_fallback_s66n99d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_for_ovoqsoj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_tuple_elem_join_om8llaj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_x_dmu97o4(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k0nv110r163[40] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_sa4nk3mrdf2d0[36];
extern const uint8_t gm_sg8s3e7n1hsck[34];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_skue5huof65fj[80];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_smdscldu1a8hg[42];
extern const uint8_t gm_so4k586t2v79t[64];
extern const uint8_t gm_sp6ona76fn0di[13];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclfnv05vuf0b8[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_su22guvkav5b5[21];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = gm_l2;
        void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_block_q09sn1p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    uint8_t t3 = gm_l2;
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    void * t5 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t1, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ilis91k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 120)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
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
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_nkql463 F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_h08har5_2(t14, t15, t16);
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6l26q4a F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ilis91k();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fv6i5s8();
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
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lmbdkhb_2();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) void * gm_f_local_i91hglv(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_local_i91hglv F;
    uint32_t gm_l0; /* id */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t1 = gm_l1;
    uint32_t t2 = gm_l0;
    gm_teti411eujvf4 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_range_loop_44fv3au *)((&F.h)->up))->l6));
    void * t5 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t1, &t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void * gm_f_local_l5o3d47(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_local_l5o3d47 F;
    uint32_t gm_l0; /* id */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t1 = gm_l1;
    uint32_t t2 = gm_l0;
    gm_teti411eujvf4 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_for_each_value_5qklbg7 *)((&F.h)->up))->l4));
    void * t5 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t1, &t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
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
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
            gm_td8hfgklo8dil t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_t66j1im9ip128 t17 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&gm_l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out))[32] = 1;
        memmove((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 0, t29, 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
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
            gm_tqetrgo0q24b5 t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t29, 40);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_ruvva9t F;
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
            gm_te5gbhu3us3k4 t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_t8enl5g7b22jb t17 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 48));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 52, 0);
        ((uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out))[48] = 1;
        memmove((uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 0, t29, 48);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 56);
            gm_tqetrgo0q24b5 t5;
            gm_f_pop_dt2bd2s((void *)&t5, t4);
        }
    }
    {
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 616);
            gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
        }
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 656);
        gm_td8hfgklo8dil t13;
        gm_f_pop_8sodrk9((void *)&t13, t12);
        if (!((((uint8_t *)&t13))[32] != 0)) goto ie10;
        gm_l1 = (*(gm_t66j1im9ip128 *)(((uint8_t *)&t13) + 0));
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_f_restore_scope_edqq1v8(t16, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
        }
        goto ix10;
ie10:;
ix10:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7koe9lk F;
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
            gm_b16_8 t11 = gm_f_empty_slice_qm5tp1g();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_emnjbt3(t10, ((uint64_t)1ULL), t12);
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
        gm_tpm220j5qhjke t21 = F.l1;
        GM_ST(gm_tpm220j5qhjke, ((uint8_t *)t20 + t18 * 48), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_3 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h9tbucc_3(t10, ((uint64_t)1ULL), t12);
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
        gm_tbbqefuhv2r8h t21 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)a1));
        GM_ST(gm_tbbqefuhv2r8h, ((uint8_t *)t20 + t18 * 24), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_vj3ckdj_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h08har5_2(t10, ((uint64_t)1ULL), t12);
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
        gm_trcmmd12cefb1 t21 = gm_l1;
        GM_ST(gm_trcmmd12cefb1, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_61fb705_2(t10, ((uint64_t)1ULL), t12);
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
        gm_t69m56meqefs8 t21 = F.l1;
        GM_ST(gm_t69m56meqefs8, ((uint8_t *)t20 + t18 * 88), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 88)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp_2 F;
    void * gm_l0; /* l */
    gm_tlr9dp305htm7 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_lmbdkhb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_4bta8ne_2(t10, ((uint64_t)1ULL), t12);
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
        gm_tlr9dp305htm7 t21 = gm_l1;
        GM_ST(gm_tlr9dp305htm7, ((uint8_t *)t20 + t18 * 24), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4iu2lk(t10, ((uint64_t)1ULL), t12);
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
        gm_t88rcucg1b7uk t21 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t20 + t18 * 4), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l74t9rq F;
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
            gm_b16_8 t11 = gm_f_empty_slice_r9g5q6n();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_t5pgl2t(t10, ((uint64_t)1ULL), t12);
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
        gm_tnrgict8sthcv t21 = F.l1;
        GM_ST(gm_tnrgict8sthcv, ((uint8_t *)t20 + t18 * 120), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_fv6i5s8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_5719f1b(t10, ((uint64_t)1ULL), t12);
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
        void * t21 = gm_l1;
        GM_ST(void *, ((uint8_t *)t20 + t18 * 8), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_3 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t5pgl2t F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_tuple_l3i6trt F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* elems */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_t53at80pfdus3 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t6 + 8), t2);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t6);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
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
inline __attribute__((aligned(4))) void * gm_f_x_dmu97o4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_x_dmu97o4 F;
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t1 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_range_test_tqcppmu *)((&F.h)->up))->l5));
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_range_test_tqcppmu *)((&F.h)->up))->l10));
    gm_teti411eujvf4 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_range_test_tqcppmu *)((&F.h)->up))->l8));
    void * t5 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t1, &t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6) {
    struct gm_fr_counting_loop_s4u2ugd F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint32_t gm_l3; /* i */
    gm_t88rcucg1b7uk gm_l4; /* ity */
    void * gm_l5; /* bound */
    void * gm_l6; /* item */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* lt */
    GM_STACK_CHECK(&F, 547u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
        gm_l7 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trl9d7m1k5lbd t8;
        memset((uint8_t *)&t8 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)10ULL));
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t8 + 0, t9);
        gm_t88rcucg1b7uk t10 = gm_l4;
        uint32_t t11 = gm_l3;
        gm_teti411eujvf4 t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t12 + 8), t11);
        gm_tl874t2atnlh3 t13 = gm_l7;
        void * t14 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t8 + 8, t14);
        void * t15 = gm_l5;
        GM_ST(void *, (uint8_t *)&t8 + 16, t15);
        gm_teti411eujvf4 t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)30ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t16 + 8), t8);
        gm_tl874t2atnlh3 t17 = gm_l7;
        void * t18 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t7, &t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t18;
    }
    {
        gm_f_list_v5d1o9b_2((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t19 = (void *)((uint8_t *)&F.l9);
        void * t20 = gm_l8;
        gm_tlr9dp305htm7 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t21 + 8), t20);
        gm_tftb1odc739t5 t22 = gm_f_push_eb7kbhp_2(t19, &t21);
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srclfnv05vuf0b8, 266, 5);
        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l10);
        uint32_t t25 = gm_l3;
        gm_t88rcucg1b7uk t26 = gm_l4;
        gm_tl874t2atnlh3 t27 = gm_l7;
        gm_trcmmd12cefb1 t28;
        gm_f_increment_12n73t5((void *)&t28, (void *)NULL, cx, NULL, t25, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t29 = gm_f_push_d6c76ud_2(t24, &t28);
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srclfnv05vuf0b8, 268, 5);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        gm_t88rcucg1b7uk t33 = gm_l2;
        void * t34 = gm_l6;
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_tl874t2atnlh3 t39 = gm_l7;
        void * t40 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t37, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t41 = gm_f_loop_with_pattern_d8vl0mj((void *)NULL, cx, NULL, t31, t32, t33, t34, t35, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t42 = 0;
        F.ret = t41;
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
__attribute__((aligned(4))) void * gm_f_for_each_value_5qklbg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_for_each_value_5qklbg7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* src0 */
    void * gm_l3; /* tys */
    void * gm_l5; /* src */
    gm_tjpqvfd72o62u gm_l6; /* info */
    gm_t88rcucg1b7uk gm_l7; /* st */
    void * gm_l8; /* slice */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t88rcucg1b7uk gm_l10; /* e */
    gm_t88rcucg1b7uk gm_l11; /* e */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_tqs5p4n2tq19v gm_l13; /* m */
    uint8_t gm_l14; /* x */
    uint32_t gm_l15; /* t */
    gm_t88rcucg1b7uk gm_l16; /* sty */
    void * gm_l17; /* s */
    uint8_t gm_l19; /* c */
    gm_t88rcucg1b7uk gm_l20; /* pointee */
    gm_t88rcucg1b7uk gm_l21; /* e */
    gm_t88rcucg1b7uk gm_l22; /* e */
    void * gm_l23; /* arr */
    gm_b16_8 gm_l24; /* elems */
    gm_t88rcucg1b7uk gm_l25; /* e0 */
    gm_tkn42u7na3t81 gm_l26; /* joined */
    gm_t88rcucg1b7uk gm_l27; /* j */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* k */
    gm_t88rcucg1b7uk gm_l31; /* a */
    gm_t88rcucg1b7uk gm_l32; /* b */
    uint32_t gm_l33; /* t */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    void * gm_l38; /* field */
    void * gm_l39; /* arr */
    void * gm_l40; /* lp */
    gm_tbngattl1fo51 gm_l42; /* r */
    uint32_t gm_l43; /* t */
    void * gm_l44; /* lo */
    void * gm_l45; /* hi */
    void * gm_l46; /* lp */
    gm_t88rcucg1b7uk gm_l48; /* sty */
    gm_t88rcucg1b7uk gm_l49; /* usize_t */
    uint32_t gm_l50; /* s */
    uint32_t gm_l51; /* i */
    gm_closure gm_l52; /* local */
    void * gm_l53; /* len */
    void * gm_l54; /* item */
    void * gm_l55; /* lp */
    GM_STACK_CHECK(&F, 4659u);
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
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t5) + 104));
        F.l4 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l2;
        void * t9 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t9;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l5;
        gm_t8cc1mtbrhlpt t13;
        gm_f_soa_of_jufdcj2((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t13))[32] != 0)) goto ie10;
        gm_l6 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t13) + 0));
        {
            void * t15 = gm_l0;
            void * t16 = gm_l1;
            void * t17 = gm_l5;
            void * t18 = gm_f_soa_for_ovoqsoj((void *)NULL, cx, NULL, t15, t16, t17, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 96);
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        void * t24 = gm_l5;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t24) + 0));
        gm_t88rcucg1b7uk t26 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t21, t23, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t26;
    }
    {
        void * t27 = gm_l5;
        gm_l8 = t27;
    }
    {
        gm_t88rcucg1b7uk t28 = gm_l7;
        gm_l9 = t28;
    }
    {
        void * t29 = gm_l3;
        gm_t88rcucg1b7uk t30 = gm_l7;
        gm_t53at80pfdus3 t31;
        gm_f_kind_jcnt4hm((void *)&t31, t29, GM_CONV(gm_csguankko4lmhd, t30));
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        switch (t41) { case ((uint8_t)0ULL): goto ma39; case ((uint8_t)1ULL): goto ma39; case ((uint8_t)2ULL): goto ma39; case ((uint8_t)3ULL): goto ma39; case ((uint8_t)4ULL): goto ma39; case ((uint8_t)5ULL): goto ma39; case ((uint8_t)6ULL): goto ma39; case ((uint8_t)7ULL): goto ma39; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma39; case ((uint8_t)10ULL): goto ma33; case ((uint8_t)11ULL): goto ma34; case ((uint8_t)12ULL): goto ma37; case ((uint8_t)13ULL): goto ma39; case ((uint8_t)14ULL): goto ma39; case ((uint8_t)15ULL): goto ma39; case ((uint8_t)16ULL): goto ma39; case ((uint8_t)17ULL): goto ma38; case ((uint8_t)18ULL): goto ma39; case ((uint8_t)19ULL): goto ma39; case ((uint8_t)20ULL): goto ma34; case ((uint8_t)21ULL): goto ma39; case ((uint8_t)22ULL): goto ma39; case ((uint8_t)23ULL): goto ma39; case ((uint8_t)24ULL): goto ma39; case ((uint8_t)25ULL): goto ma39; case ((uint8_t)26ULL): goto ma36; default: goto ma39; }
ma33:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t43 != ((uint8_t)10ULL)) goto mn42;
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 4));
        gm_l10 = t44;
        {
            gm_t88rcucg1b7uk t46 = gm_l10;
            gm_l9 = t46;
        }
        goto me32;
mn42:;
ma34:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t50 != ((uint8_t)11ULL)) goto pa49;
        gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 8));
        gm_l11 = t51;
        goto po48;
pa49:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t53 != ((uint8_t)20ULL)) goto pa52;
        gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 4));
        gm_l11 = t54;
        goto po48;
pa52:;
        goto mn47;
po48:;
        {
            gm_t88rcucg1b7uk t56 = gm_l11;
            gm_l9 = t56;
        }
        {
            void * t57 = gm_l0;
            void * t58 = gm_l5;
            gm_tqs5p4n2tq19v t59 = gm_f_place_mutability_0tlm4g8(t57, t58);
            gm_l13 = t59;
        }
        {
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
            void * t63 = gm_l5;
            uint8_t t64 = gm_f_packed_place_5ck3439(t62, t63);
            if (!t64) goto ie60;
            {
                gm_tqs5p4n2tq19v t66;
                memset(&t66, 0, sizeof t66);
                gm_l13 = t66;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            if (!((((uint8_t *)&gm_l13))[1] != 0)) goto ie67;
            uint8_t t68 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
            gm_l14 = t68;
            {
                void * t70 = gm_l3;
                uint8_t t71 = gm_l14;
                uint8_t t72 = (uint8_t)!t71;
                gm_t88rcucg1b7uk t73 = gm_l11;
                gm_t88rcucg1b7uk t74 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t70, t72, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tao63pk3r18v5 t75;
                memset((uint8_t *)&t75 + 25, 0, 7);
                void * t76 = gm_l5;
                GM_ST(void *, (uint8_t *)&t75 + 0, t76);
                GM_ST(void *, (uint8_t *)&t75 + 8, ((void *)0));
                GM_ST(void *, (uint8_t *)&t75 + 16, ((void *)0));
                GM_ST(uint8_t, (uint8_t *)&t75 + 24, ((uint8_t)0));
                gm_teti411eujvf4 t77;
                GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t77 + 8), t75);
                gm_tl874t2atnlh3 t78 = F.l4;
                void * t79 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t74, &t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t79;
            }
            goto ix67;
ie67:;
            {
                void * t81 = gm_l0;
                gm_t88rcucg1b7uk t82 = gm_l7;
                gm_tl874t2atnlh3 t83 = F.l4;
                uint32_t t84 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t81, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t84;
            }
            {
                void * t85 = gm_l3;
                gm_t88rcucg1b7uk t86 = gm_l11;
                gm_t88rcucg1b7uk t87 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t85, ((uint8_t)1), t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t87;
            }
            {
                gm_t88rcucg1b7uk t88 = gm_l16;
                gm_tao63pk3r18v5 t89;
                memset((uint8_t *)&t89 + 25, 0, 7);
                gm_t88rcucg1b7uk t90 = gm_l7;
                uint32_t t91 = gm_l15;
                gm_teti411eujvf4 t92;
                memset(&t92, 0, sizeof t92);
                GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t92 + 8), t91);
                gm_tl874t2atnlh3 t93 = F.l4;
                void * t94 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t90, &t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(void *, (uint8_t *)&t89 + 0, t94);
                GM_ST(void *, (uint8_t *)&t89 + 8, ((void *)0));
                GM_ST(void *, (uint8_t *)&t89 + 16, ((void *)0));
                GM_ST(uint8_t, (uint8_t *)&t89 + 24, ((uint8_t)0));
                gm_teti411eujvf4 t95;
                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t95 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t95 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t95 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t95 + 8), t89);
                gm_tl874t2atnlh3 t96 = F.l4;
                void * t97 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t88, &t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t97;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l18, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t98 = (void *)((uint8_t *)&F.l18);
                uint32_t t99 = gm_l15;
                void * t100 = gm_l5;
                gm_ttggq7ng4kp3p t101;
                GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t101 + 0), t99);
                GM_ST(void *, ((uint8_t *)&t101 + 8), t100);
                gm_trcmmd12cefb1 t102;
                GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t102 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t102 + 8), t101);
                gm_tftb1odc739t5 t103 = gm_f_push_d6c76ud_2(t98, &t102);
                if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srclfnv05vuf0b8, 337, 17);
                gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
            }
            {
                gm_t88rcucg1b7uk t105 = gm_l16;
                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                void * t107 = gm_l17;
                gm_tl874t2atnlh3 t108 = F.l4;
                void * t109 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t105, t106, t107, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t109;
            }
ix67:;
        }
        goto me32;
mn47:;
ma35:;
        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t111 != ((uint8_t)8ULL)) goto mn110;
        uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)&t31) + 8) + 0));
        gm_l19 = t112;
        gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 4));
        gm_l20 = t113;
        {
            void * t115 = gm_l3;
            gm_t88rcucg1b7uk t116 = gm_l20;
            gm_t53at80pfdus3 t117;
            gm_f_kind_jcnt4hm((void *)&t117, t115, GM_CONV(gm_csguankko4lmhd, t116));
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&t117) + 0));
            if (t122 != ((uint8_t)11ULL)) goto pa121;
            gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t117) + 8) + 8));
            gm_l21 = t123;
            goto po120;
pa121:;
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&t117) + 0));
            if (t125 != ((uint8_t)20ULL)) goto pa124;
            gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t117) + 8) + 4));
            gm_l21 = t126;
            goto po120;
pa124:;
            goto mn119;
po120:;
            {
                gm_t88rcucg1b7uk t128 = gm_l21;
                gm_l9 = t128;
            }
            {
                gm_t88rcucg1b7uk t129 = gm_l20;
                void * t130 = gm_l5;
                gm_teti411eujvf4 t131;
                memset(&t131, 0, sizeof t131);
                GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)18ULL));
                GM_ST(void *, ((uint8_t *)&t131 + 8), t130);
                gm_tl874t2atnlh3 t132 = F.l4;
                void * t133 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t129, &t131, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t133;
            }
            {
                void * t134 = gm_l3;
                uint8_t t135 = gm_l19;
                gm_t88rcucg1b7uk t136 = gm_l21;
                gm_t88rcucg1b7uk t137 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t134, t135, t136);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tao63pk3r18v5 t138;
                memset((uint8_t *)&t138 + 25, 0, 7);
                void * t139 = gm_l23;
                GM_ST(void *, (uint8_t *)&t138 + 0, t139);
                GM_ST(void *, (uint8_t *)&t138 + 8, ((void *)0));
                GM_ST(void *, (uint8_t *)&t138 + 16, ((void *)0));
                GM_ST(uint8_t, (uint8_t *)&t138 + 24, ((uint8_t)0));
                gm_teti411eujvf4 t140;
                GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t140 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t140 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t140 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t140 + 8), t138);
                gm_tl874t2atnlh3 t141 = F.l4;
                void * t142 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t142;
            }
            goto me118;
mn119:;
            uint8_t t144 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
            goto me118;
mn143:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 342, 13);
me118:;
        }
        goto me32;
mn110:;
ma36:;
        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t146 != ((uint8_t)26ULL)) goto mn145;
        void * t147 = gm_l0;
        void * t148 = GM_LD(void *, (((uint8_t *)t147) + 0));
        gm_tl874t2atnlh3 t149 = F.l4;
        void * t150 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t148, t149);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t151 = 0;
        F.ret = t150;
        goto gm_fret;
        goto me32;
mn145:;
ma37:;
        uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t153 != ((uint8_t)12ULL)) goto mn152;
        gm_b16_8 t154 = GM_LD(gm_b16_8, ((((uint8_t *)&t31) + 8) + 0));
        gm_l24 = t154;
        {
            uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
            uint8_t t158 = ((t157 == ((uint64_t)0ULL)));
            if (!t158) goto ie156;
            {
                uint8_t t160 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            goto ix156;
ie156:;
ix156:;
        }
        {
            uint64_t t161 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
            if (t161 >= (uintptr_t)(t162)) gm_trap_index(t161, t162, gm_srclfnv05vuf0b8, 356, 19);
            gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t161 * 4));
            gm_l25 = t163;
        }
        {
            void * t164 = gm_l0;
            gm_b16_8 t165 = gm_l24;
            gm_tkn42u7na3t81 t166 = gm_f_tuple_elem_join_om8llaj((void *)NULL, cx, NULL, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t166;
        }
        {
            if (!((((uint8_t *)&gm_l26))[4] != 0)) goto ie167;
            gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26));
            gm_l27 = t168;
            {
                gm_t88rcucg1b7uk t170 = gm_l27;
                gm_l25 = t170;
            }
            goto ix167;
ie167:;
ix167:;
        }
        {
            {
                gm_l29 = ((uint64_t)1ULL);
            }
            {
                uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                gm_l28 = t172;
            }
            {
                for (;;) {
                    uint64_t t174 = gm_l29;
                    uint64_t t175 = gm_l28;
                    uint8_t t176 = (t174 < t175);
                    if (!t176) goto lb173;
                    {
                        uint64_t t179 = gm_l29;
                        uint64_t t182 = GM_LD(uint64_t, ((uint8_t *)&t179));
                        gm_l30 = t182;
                        {
                            uint8_t t185;
                            if (((((uint8_t *)&gm_l26))[4] != 0)) goto mn187;
                            t185 = ((uint8_t)1);
                            goto me186;
mn187:;
                            t185 = ((uint8_t)0);
                            goto me186;
mn188:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 361, 20);
me186:;
                            if (!t185) goto ie184;
                            void * t189 = gm_l0;
                            void * t190 = (void *)(((uint8_t *)t189) + 96);
                            void * t191 = gm_l3;
                            gm_t88rcucg1b7uk t192 = gm_l25;
                            uint64_t t193 = gm_l30;
                            uint64_t t194 = (uintptr_t)t193;
                            uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srclfnv05vuf0b8, 361, 63);
                            gm_t88rcucg1b7uk t196 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t194 * 4));
                            gm_t9qkh0rsooc8d t197 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t190, t191, t192, t196);
                            if (gm_unw.kind) {
                                goto gm_ll177;
                            }
                            uint8_t t198;
                            if (!((((uint8_t *)&t197))[0] != 0)) goto mn200;
                            t198 = ((uint8_t)1);
                            goto me199;
mn200:;
                            t198 = ((uint8_t)0);
                            goto me199;
mn201:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 361, 39);
me199:;
                            if (!t198) goto ie184;
                            {
                                void * t203 = gm_l0;
                                void * t204 = (void *)(((uint8_t *)t203) + 96);
                                void * t205 = gm_l0;
                                void * t206 = GM_LD(void *, (((uint8_t *)t205) + 0));
                                gm_t88rcucg1b7uk t207 = gm_l25;
                                gm_t88rcucg1b7uk t208 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t204, t206, t207);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                                gm_l31 = t208;
                            }
                            {
                                void * t209 = gm_l0;
                                void * t210 = (void *)(((uint8_t *)t209) + 96);
                                void * t211 = gm_l0;
                                void * t212 = GM_LD(void *, (((uint8_t *)t211) + 0));
                                uint64_t t213 = gm_l30;
                                uint64_t t214 = (uintptr_t)t213;
                                uint64_t t215 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                if (t214 >= (uintptr_t)(t215)) gm_trap_index(t214, t215, gm_srclfnv05vuf0b8, 363, 49);
                                gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t214 * 4));
                                gm_t88rcucg1b7uk t217 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t210, t212, t216);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                                gm_l32 = t217;
                            }
                            {
                                void * t218 = gm_l0;
                                void * t219 = gm_l1;
                                void * t220 = GM_LD(void *, (((uint8_t *)t219) + 56));
                                gm_tl874t2atnlh3 t221 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t220) + 72));
                                gm_t6j6smj7qoctu t222;
                                void * t223 = gm_l0;
                                gm_t88rcucg1b7uk t224 = gm_l31;
                                gm_b16_8 t225 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t223, t224);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t222 + 0, t225);
                                void * t226 = gm_l0;
                                gm_t88rcucg1b7uk t227 = gm_l32;
                                gm_b16_8 t228 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t226, t227);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t222 + 16, t228);
                                gm_tabb710d9mq2e t229;
                                gm_f_format_t00o2ec((void *)&t229, (void *)NULL, cx, NULL, &t222);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                                if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 8, gm_srclfnv05vuf0b8, 364, 43);
                                gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&t229) + 8));
                                gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&t230));
                                gm_b0_1 t232 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t218, t221, t231);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                            }
                            {
                                void * t233 = gm_l0;
                                void * t234 = GM_LD(void *, (((uint8_t *)t233) + 0));
                                gm_tl874t2atnlh3 t235 = F.l4;
                                void * t236 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t234, t235);
                                if (gm_unw.kind) {
                                    goto gm_ll177;
                                }
                                uint8_t t237 = 0;
                                F.ret = t236;
                                goto gm_fret;
                            }
                            goto ix184;
ie184:;
ix184:;
                        }
                        goto me180;
mn181:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 360, 13);
me180:;
                    }
lc173:;
                    {
                        uint64_t t239 = gm_l29;
                        uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                        uint64_t t242 = r240;
                        gm_l29 = t242;
                    }
                }
gm_ll177:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb173; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc173; }
                }
                goto gm_lad;
lb173:;
            }
        }
        {
            void * t243 = gm_l0;
            gm_t88rcucg1b7uk t244 = gm_l7;
            gm_tl874t2atnlh3 t245 = F.l4;
            uint32_t t246 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t243, t244, t245);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t246;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l36 = ((uint64_t)0ULL);
            }
            {
                uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                gm_l35 = t248;
            }
            {
                for (;;) {
                    uint64_t t250 = gm_l36;
                    uint64_t t251 = gm_l35;
                    uint8_t t252 = (t250 < t251);
                    if (!t252) goto lb249;
                    {
                        uint64_t t255 = gm_l36;
                        uint64_t t258 = GM_LD(uint64_t, ((uint8_t *)&t255));
                        gm_l37 = t258;
                        {
                            uint64_t t260 = gm_l37;
                            uint64_t t261 = (uintptr_t)t260;
                            uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                            if (t261 >= (uintptr_t)(t262)) gm_trap_index(t261, t262, gm_srclfnv05vuf0b8, 371, 33);
                            gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t261 * 4));
                            gm_t88rcucg1b7uk t264 = gm_l7;
                            uint32_t t265 = gm_l33;
                            gm_teti411eujvf4 t266;
                            memset(&t266, 0, sizeof t266);
                            GM_ST(uint8_t, ((uint8_t *)&t266 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t266 + 8), t265);
                            gm_tl874t2atnlh3 t267 = F.l4;
                            void * t268 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t264, &t266, t267);
                            if (gm_unw.kind) {
                                goto gm_ll253;
                            }
                            uint64_t t269 = gm_l37;
                            uint32_t t270 = (uint32_t)t269;
                            gm_tbl8il92l97ut t271;
                            GM_ST(uint32_t, (uint8_t *)&t271 + 12, 0);
                            GM_ST(void *, ((uint8_t *)&t271 + 0), t268);
                            GM_ST(uint32_t, ((uint8_t *)&t271 + 8), t270);
                            gm_teti411eujvf4 t272;
                            memset(&t272, 0, sizeof t272);
                            GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)19ULL));
                            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t272 + 8), t271);
                            gm_tl874t2atnlh3 t273 = F.l4;
                            void * t274 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t263, &t272, t273);
                            if (gm_unw.kind) {
                                goto gm_ll253;
                            }
                            gm_l38 = t274;
                        }
                        {
                            void * t275 = (void *)((uint8_t *)&F.l34);
                            void * t276 = gm_l0;
                            void * t277 = gm_l38;
                            gm_t88rcucg1b7uk t278 = gm_l25;
                            void * t279 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t276, t277, t278);
                            if (gm_unw.kind) {
                                goto gm_ll253;
                            }
                            gm_tftb1odc739t5 t280 = gm_f_push_d943vgp_2(t275, &(*(gm_t69m56meqefs8 *)((uint8_t *)t279)));
                            if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_srclfnv05vuf0b8, 372, 17);
                            gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
                        }
                        goto me256;
mn257:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 370, 13);
me256:;
                    }
lc249:;
                    {
                        uint64_t t283 = gm_l36;
                        uint64_t r284; bool o285 = __builtin_add_overflow(t283, ((uint64_t)1ULL), &r284);
                        uint64_t t286 = r284;
                        gm_l36 = t286;
                    }
                }
gm_ll253:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb249; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc249; }
                }
                goto gm_lad;
lb249:;
            }
        }
        {
            void * t287 = gm_l3;
            uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
            uint64_t t289 = (uint64_t)t288;
            gm_t88rcucg1b7uk t290 = gm_l25;
            gm_t88rcucg1b7uk t291 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t287, t289, t290);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
            gm_teti411eujvf4 t293;
            memset(&t293, 0, sizeof t293);
            GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)36ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 8), t292);
            gm_tl874t2atnlh3 t294 = F.l4;
            void * t295 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t291, &t293, t294);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l39 = t295;
        }
        {
            void * t296 = gm_l0;
            void * t297 = gm_l1;
            void * t298 = gm_l39;
            void * t299 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t296, t297, t298);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t300;
            if ((GM_LD(void *, ((uint8_t *)&t299)) != 0)) {
                void * t301 = GM_LD(void *, (((uint8_t *)&t299) + 0));
                t300 = t301;
            } else {
                uint8_t t302 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l40 = t300;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l41, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t303 = (void *)((uint8_t *)&F.l41);
            uint32_t t304 = gm_l33;
            void * t305 = gm_l5;
            gm_ttggq7ng4kp3p t306;
            GM_ST(uint32_t, (uint8_t *)&t306 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t306 + 0), t304);
            GM_ST(void *, ((uint8_t *)&t306 + 8), t305);
            gm_trcmmd12cefb1 t307;
            GM_ST(uint8_t, (uint8_t *)&t307 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t307 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t307 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t307 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t307 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t307 + 8), t306);
            gm_tftb1odc739t5 t308 = gm_f_push_d6c76ud_2(t303, &t307);
            if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 1, gm_srclfnv05vuf0b8, 377, 13);
            gm_b0_1 t309 = GM_LD(gm_b0_1, (((uint8_t *)&t308) + 1));
        }
        {
            void * t310 = (void *)((uint8_t *)&F.l41);
            void * t311 = gm_l40;
            gm_trcmmd12cefb1 t312;
            GM_ST(uint8_t, (uint8_t *)&t312 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t312 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t312 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t312 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t312 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t312 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t312 + 8), t311);
            gm_tftb1odc739t5 t313 = gm_f_push_d6c76ud_2(t310, &t312);
            if (((((uint8_t *)&t313))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t313) + 1, gm_srclfnv05vuf0b8, 378, 13);
            gm_b0_1 t314 = GM_LD(gm_b0_1, (((uint8_t *)&t313) + 1));
        }
        {
            void * t315 = gm_l0;
            void * t316 = GM_LD(void *, (((uint8_t *)t315) + 0));
            gm_b16_8 t317 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
            gm_tl874t2atnlh3 t318 = F.l4;
            void * t319 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t316, t317, t318);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t320 = 0;
            F.ret = t319;
            goto gm_fret;
        }
        goto me32;
mn152:;
ma38:;
        uint8_t t322 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t322 != ((uint8_t)17ULL)) goto mn321;
        {
            void * t324 = gm_l0;
            void * t325 = GM_LD(void *, (((uint8_t *)t324) + 0));
            gm_t88rcucg1b7uk t326 = gm_l7;
            gm_thetuhtouve7f t327 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t325, t326);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbngattl1fo51 t328;
            if (((((uint8_t *)&t327))[8] != 0)) {
                gm_tbngattl1fo51 t329 = GM_LD(gm_tbngattl1fo51, (((uint8_t *)&t327) + 0));
                t328 = t329;
            } else {
                uint8_t t330 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l42 = t328;
        }
        {
            void * t332 = gm_l3;
            gm_t88rcucg1b7uk t333 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            uint8_t t334 = gm_f_is_integer_8d4f9ek(t332, GM_CONV(gm_csguankko4lmhd, t333));
            uint8_t t335 = (uint8_t)!t334;
            if (!t335) goto ie331;
            {
                void * t337 = gm_l0;
                void * t338 = gm_l1;
                void * t339 = GM_LD(void *, (((uint8_t *)t338) + 56));
                gm_tl874t2atnlh3 t340 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t339) + 72));
                void * t341 = gm_l0;
                gm_t88rcucg1b7uk t342 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
                gm_b16_8 t343 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t341, t342);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t344;
                GM_ST(gm_b16_8, ((uint8_t *)&t344 + 0), t343);
                gm_tabb710d9mq2e t345;
                gm_f_format_h1n6lc9((void *)&t345, (void *)NULL, cx, NULL, t344);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t345))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t345) + 8, gm_srclfnv05vuf0b8, 385, 39);
                gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&t345) + 8));
                gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
                gm_b0_1 t348 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t337, t340, t347);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t349 = gm_l0;
                void * t350 = GM_LD(void *, (((uint8_t *)t349) + 0));
                gm_tl874t2atnlh3 t351 = F.l4;
                void * t352 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t350, t351);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t353 = 0;
                F.ret = t352;
                goto gm_fret;
            }
            goto ix331;
ie331:;
ix331:;
        }
        {
            void * t354 = gm_l0;
            gm_t88rcucg1b7uk t355 = gm_l7;
            gm_tl874t2atnlh3 t356 = F.l4;
            uint32_t t357 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t354, t355, t356);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t357;
        }
        {
            gm_t88rcucg1b7uk t358 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            gm_t88rcucg1b7uk t359 = gm_l7;
            uint32_t t360 = gm_l43;
            gm_teti411eujvf4 t361;
            memset(&t361, 0, sizeof t361);
            GM_ST(uint8_t, ((uint8_t *)&t361 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t361 + 8), t360);
            gm_tl874t2atnlh3 t362 = F.l4;
            void * t363 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t359, &t361, t362);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t364;
            GM_ST(uint32_t, (uint8_t *)&t364 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t364 + 0), t363);
            GM_ST(uint32_t, ((uint8_t *)&t364 + 8), ((uint32_t)0ULL));
            gm_teti411eujvf4 t365;
            memset(&t365, 0, sizeof t365);
            GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t365 + 8), t364);
            gm_tl874t2atnlh3 t366 = F.l4;
            void * t367 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t358, &t365, t366);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t367;
        }
        {
            gm_t88rcucg1b7uk t368 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            gm_t88rcucg1b7uk t369 = gm_l7;
            uint32_t t370 = gm_l43;
            gm_teti411eujvf4 t371;
            memset(&t371, 0, sizeof t371);
            GM_ST(uint8_t, ((uint8_t *)&t371 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t371 + 8), t370);
            gm_tl874t2atnlh3 t372 = F.l4;
            void * t373 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t369, &t371, t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t374;
            GM_ST(uint32_t, (uint8_t *)&t374 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t374 + 0), t373);
            GM_ST(uint32_t, ((uint8_t *)&t374 + 8), ((uint32_t)1ULL));
            gm_teti411eujvf4 t375;
            memset(&t375, 0, sizeof t375);
            GM_ST(uint8_t, ((uint8_t *)&t375 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t375 + 8), t374);
            gm_tl874t2atnlh3 t376 = F.l4;
            void * t377 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t368, &t375, t376);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l45 = t377;
        }
        {
            void * t378 = gm_l0;
            void * t379 = gm_l1;
            void * t380 = gm_l44;
            void * t381 = gm_l45;
            gm_t88rcucg1b7uk t382 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
            void * t384 = gm_f_range_loop_44fv3au((void *)NULL, cx, NULL, t378, t379, t380, t381, t382, t383);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l46 = t384;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l47, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t385 = (void *)((uint8_t *)&F.l47);
            uint32_t t386 = gm_l43;
            void * t387 = gm_l5;
            gm_ttggq7ng4kp3p t388;
            GM_ST(uint32_t, (uint8_t *)&t388 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t388 + 0), t386);
            GM_ST(void *, ((uint8_t *)&t388 + 8), t387);
            gm_trcmmd12cefb1 t389;
            GM_ST(uint8_t, (uint8_t *)&t389 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t389 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t389 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t389 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t389 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t389 + 8), t388);
            gm_tftb1odc739t5 t390 = gm_f_push_d6c76ud_2(t385, &t389);
            if (((((uint8_t *)&t390))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t390) + 1, gm_srclfnv05vuf0b8, 393, 13);
            gm_b0_1 t391 = GM_LD(gm_b0_1, (((uint8_t *)&t390) + 1));
        }
        {
            void * t392 = (void *)((uint8_t *)&F.l47);
            void * t393 = gm_l46;
            gm_trcmmd12cefb1 t394;
            GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t394 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t394 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t394 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t394 + 8), t393);
            gm_tftb1odc739t5 t395 = gm_f_push_d6c76ud_2(t392, &t394);
            if (((((uint8_t *)&t395))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t395) + 1, gm_srclfnv05vuf0b8, 394, 13);
            gm_b0_1 t396 = GM_LD(gm_b0_1, (((uint8_t *)&t395) + 1));
        }
        {
            void * t397 = gm_l0;
            void * t398 = GM_LD(void *, (((uint8_t *)t397) + 0));
            gm_b16_8 t399 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 0));
            gm_tl874t2atnlh3 t400 = F.l4;
            void * t401 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t398, t399, t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t402 = 0;
            F.ret = t401;
            goto gm_fret;
        }
        goto me32;
mn321:;
ma39:;
        uint8_t t404 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me32;
mn403:;
ma40:;
        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 321, 5);
me32:;
    }
    {
        void * t405 = gm_l8;
        gm_t88rcucg1b7uk t406 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t405) + 0));
        gm_l48 = t406;
    }
    {
        void * t407 = gm_l3;
        gm_t88rcucg1b7uk t408 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t407);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l49 = t408;
    }
    {
        void * t409 = gm_l0;
        gm_t88rcucg1b7uk t410 = gm_l48;
        gm_tl874t2atnlh3 t411 = F.l4;
        uint32_t t412 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t409, t410, t411);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l50 = t412;
    }
    {
        void * t413 = gm_l0;
        gm_t88rcucg1b7uk t414 = gm_l49;
        gm_tl874t2atnlh3 t415 = F.l4;
        uint32_t t416 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t413, t414, t415);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l51 = t416;
    }
    {
        gm_closure t417 = ((gm_closure){(void *)gm_f_local_l5o3d47, (void *)&F.h, cx});
        gm_l52 = t417;
    }
    {
        gm_t88rcucg1b7uk t418 = gm_l49;
        gm_closure t419 = gm_l52;
        uint32_t t420 = gm_l50;
        gm_t88rcucg1b7uk t421 = gm_l48;
        gm_closure t422 = t419;
        gm_node *ccx423 = t422.cx;
        gm_node cn424; { gm_node *f = gm_find(cx, 587034433); if (f) { cn424 = *f; cn424.next = ccx423; ccx423 = &cn424; } }
        void * t425 = t422.code;
        void * t426;
        if (GM_PTR_TAGGED(t425)) {
            t426 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t425))(t420, GM_CONV(gm_csguankko4lmhd, t421));
        } else {
            t426 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t425)(t422.env, ccx423, NULL, t420, t421);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t427;
        memset(&t427, 0, sizeof t427);
        GM_ST(uint8_t, ((uint8_t *)&t427 + 0), ((uint8_t)22ULL));
        GM_ST(void *, ((uint8_t *)&t427 + 8), t426);
        gm_tl874t2atnlh3 t428 = F.l4;
        void * t429 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t418, &t427, t428);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l53 = t429;
    }
    {
        gm_t88rcucg1b7uk t430 = gm_l9;
        gm_tlcl4u558l6p5 t431;
        memset((uint8_t *)&t431 + 17, 0, 7);
        gm_closure t432 = gm_l52;
        uint32_t t433 = gm_l50;
        gm_t88rcucg1b7uk t434 = gm_l48;
        gm_closure t435 = t432;
        gm_node *ccx436 = t435.cx;
        gm_node cn437; { gm_node *f = gm_find(cx, 587034433); if (f) { cn437 = *f; cn437.next = ccx436; ccx436 = &cn437; } }
        void * t438 = t435.code;
        void * t439;
        if (GM_PTR_TAGGED(t438)) {
            t439 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t438))(t433, GM_CONV(gm_csguankko4lmhd, t434));
        } else {
            t439 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t438)(t435.env, ccx436, NULL, t433, t434);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t431 + 0, t439);
        gm_closure t440 = gm_l52;
        uint32_t t441 = gm_l51;
        gm_t88rcucg1b7uk t442 = gm_l49;
        gm_closure t443 = t440;
        gm_node *ccx444 = t443.cx;
        gm_node cn445; { gm_node *f = gm_find(cx, 587034433); if (f) { cn445 = *f; cn445.next = ccx444; ccx444 = &cn445; } }
        void * t446 = t443.code;
        void * t447;
        if (GM_PTR_TAGGED(t446)) {
            t447 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t446))(t441, GM_CONV(gm_csguankko4lmhd, t442));
        } else {
            t447 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t446)(t443.env, ccx444, NULL, t441, t442);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t431 + 8, t447);
        GM_ST(uint8_t, (uint8_t *)&t431 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t448;
        memset(&t448, 0, sizeof t448);
        GM_ST(uint8_t, ((uint8_t *)&t448 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t448 + 8), t431);
        gm_tl874t2atnlh3 t449 = F.l4;
        void * t450 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t430, &t448, t449);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l54 = t450;
    }
    {
        void * t451 = gm_l0;
        void * t452 = gm_l1;
        gm_t88rcucg1b7uk t453 = gm_l9;
        uint32_t t454 = gm_l51;
        gm_t88rcucg1b7uk t455 = gm_l49;
        void * t456 = gm_l53;
        void * t457 = gm_l54;
        void * t458 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t451, t452, t453, t454, t455, t456, t457);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l55 = t458;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l56, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t459 = (void *)((uint8_t *)&F.l56);
        uint32_t t460 = gm_l50;
        void * t461 = gm_l8;
        gm_ttggq7ng4kp3p t462;
        GM_ST(uint32_t, (uint8_t *)&t462 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t462 + 0), t460);
        GM_ST(void *, ((uint8_t *)&t462 + 8), t461);
        gm_trcmmd12cefb1 t463;
        GM_ST(uint8_t, (uint8_t *)&t463 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t463 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t463 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t463 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t463 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t463 + 8), t462);
        gm_tftb1odc739t5 t464 = gm_f_push_d6c76ud_2(t459, &t463);
        if (((((uint8_t *)&t464))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t464) + 1, gm_srclfnv05vuf0b8, 409, 5);
        gm_b0_1 t465 = GM_LD(gm_b0_1, (((uint8_t *)&t464) + 1));
    }
    {
        void * t466 = (void *)((uint8_t *)&F.l56);
        uint32_t t467 = gm_l51;
        gm_t88rcucg1b7uk t468 = gm_l49;
        gm_tbci5ospk5nnn t469;
        gm_f_zero_s25l990((void *)&t469, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t470;
        GM_ST(uint8_t, (uint8_t *)&t470 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t470 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t470 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t470 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t470 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t470 + 8), t469);
        gm_tl874t2atnlh3 t471 = F.l4;
        void * t472 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t468, &t470, t471);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t473;
        GM_ST(uint32_t, (uint8_t *)&t473 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t473 + 0), t467);
        GM_ST(void *, ((uint8_t *)&t473 + 8), t472);
        gm_trcmmd12cefb1 t474;
        GM_ST(uint8_t, (uint8_t *)&t474 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t474 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t474 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t474 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t474 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t474 + 8), t473);
        gm_tftb1odc739t5 t475 = gm_f_push_d6c76ud_2(t466, &t474);
        if (((((uint8_t *)&t475))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t475) + 1, gm_srclfnv05vuf0b8, 410, 5);
        gm_b0_1 t476 = GM_LD(gm_b0_1, (((uint8_t *)&t475) + 1));
    }
    {
        void * t477 = (void *)((uint8_t *)&F.l56);
        void * t478 = gm_l55;
        gm_trcmmd12cefb1 t479;
        GM_ST(uint8_t, (uint8_t *)&t479 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t479 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t479 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t479 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t479 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t479 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t479 + 8), t478);
        gm_tftb1odc739t5 t480 = gm_f_push_d6c76ud_2(t477, &t479);
        if (((((uint8_t *)&t480))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t480) + 1, gm_srclfnv05vuf0b8, 411, 5);
        gm_b0_1 t481 = GM_LD(gm_b0_1, (((uint8_t *)&t480) + 1));
    }
    {
        void * t482 = gm_l0;
        void * t483 = GM_LD(void *, (((uint8_t *)t482) + 0));
        gm_b16_8 t484 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 0));
        gm_tl874t2atnlh3 t485 = F.l4;
        void * t486 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t483, t484, t485);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t487 = 0;
        F.ret = t486;
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
__attribute__((aligned(4))) void * gm_f_for_loop_b6hjsbq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_for_loop_b6hjsbq F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint8_t gm_l3; /* is_range */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    void * gm_l5; /* src */
    void * gm_l6; /* e */
    gm_ts6oqppjh0ba3 gm_l7; /* op */
    void * gm_l8; /* lhs */
    void * gm_l9; /* rhs */
    void * gm_l10; /* lo */
    void * gm_l11; /* hi */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_t10fqegjtup5b gm_l13; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* t */
    gm_t2i8ne5ibd7v4 gm_l17; /* c */
    GM_STACK_CHECK(&F, 1329u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
        gm_l2 = t3;
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        void * t5 = gm_l1;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 56));
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
        if (t7 != ((uint8_t)13ULL)) goto ie4;
        gm_ts6oqppjh0ba3 t8 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t6) + 8) + 8) + 0));
        gm_l4 = t8;
        {
            gm_ts6oqppjh0ba3 t10 = gm_l4;
            gm_ts6oqppjh0ba3 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)11ULL));
            gm_t9ldr1q5hn9fq t13;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t13 + 0), t11);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t13 + 2), t12);
            uint8_t t14 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t13) + 0, ((uint8_t *)&t10))) t14 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t13) + 2, ((uint8_t *)&t10))) t14 = 1;
            uint8_t t15 = (uint8_t)(t14);
            gm_l3 = t15;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t17 = gm_l3;
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie16;
        {
            void * t21 = gm_l1;
            void * t22 = GM_LD(void *, (((uint8_t *)t21) + 56));
            uint8_t t23;
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            if (t26 != ((uint8_t)19ULL)) goto mn25;
            t23 = ((uint8_t)1);
            goto me24;
mn25:;
            t23 = ((uint8_t)0);
            goto me24;
mn27:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 194, 12);
me24:;
            if (!t23) goto ie20;
            {
                void * t29 = gm_l0;
                void * t30 = gm_l1;
                void * t31 = gm_f_for_iterator_cc5qo7n((void *)NULL, cx, NULL, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            void * t33 = gm_l0;
            void * t34 = gm_l1;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 56));
            gm_tkn42u7na3t81 t36;
            memset(&t36, 0, sizeof t36);
            void * t37 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t33, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t37;
        }
        {
            void * t39 = gm_l0;
            void * t40 = gm_l1;
            void * t41 = gm_l5;
            void * t42 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t39, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(GM_LD(void *, ((uint8_t *)&t42)) != 0)) goto ie38;
            void * t43 = GM_LD(void *, ((uint8_t *)&t42));
            gm_l6 = t43;
            {
                void * t45 = gm_l6;
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            goto ix38;
ie38:;
ix38:;
        }
        {
            void * t47 = gm_l0;
            void * t48 = gm_l1;
            void * t49 = GM_LD(void *, (((uint8_t *)t48) + 56));
            gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 72));
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_skue5huof65fj));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(79));
            gm_b0_1 t52 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t47, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
            gm_tl874t2atnlh3 t55 = gm_l2;
            void * t56 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t59 = gm_l1;
        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 56));
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
        if (t61 != ((uint8_t)13ULL)) goto ie58;
        gm_ts6oqppjh0ba3 t62 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t60) + 8) + 8) + 0));
        gm_l7 = t62;
        void * t63 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 8));
        gm_l8 = t63;
        void * t64 = GM_LD(void *, (((((uint8_t *)t60) + 8) + 8) + 16));
        gm_l9 = t64;
        {
            void * t66 = gm_l0;
            void * t67 = gm_l8;
            gm_tkn42u7na3t81 t68;
            memset(&t68, 0, sizeof t68);
            void * t69 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t69;
        }
        {
            void * t70 = gm_l0;
            void * t71 = gm_l9;
            gm_tkn42u7na3t81 t72;
            memset(&t72, 0, sizeof t72);
            void * t73 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t70, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t73;
        }
        {
            void * t74 = gm_l0;
            void * t75 = gm_l10;
            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t75) + 0));
            void * t77 = gm_l11;
            gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t77) + 0));
            void * t79 = gm_l1;
            void * t80 = GM_LD(void *, (((uint8_t *)t79) + 56));
            gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t80) + 72));
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sp6ona76fn0di));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(12));
            gm_t88rcucg1b7uk t83 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t74, t76, t78, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t83;
        }
        {
            {
                {
                    gm_t88rcucg1b7uk t86 = gm_l12;
                    void * t87 = gm_l10;
                    gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t87) + 0));
                    void * t89 = gm_l11;
                    gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t89) + 0));
                    gm_t10fqegjtup5b t91;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t91 + 0), t86);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t91 + 4), t88);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t91 + 8), t90);
                    gm_l13 = t91;
                }
                void * t92 = ((uint8_t *)&gm_l13);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srclfnv05vuf0b8, 204, 5);
                gm_b16_8 t93;
                GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t92 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t94 = t93;
                gm_l14 = t94;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t96 = gm_l15;
                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t98 = (t96 < t97);
                    if (!t98) goto lb95;
                    {
                        uint64_t t101 = gm_l15;
                        uint64_t t102 = (uintptr_t)t101;
                        uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t102 * 4));
                        gm_l16 = t106;
                        {
                            void * t109 = gm_l0;
                            void * t110 = (void *)(((uint8_t *)t109) + 96);
                            void * t111 = gm_l0;
                            void * t112 = GM_LD(void *, (((uint8_t *)t111) + 0));
                            void * t113 = (void *)(((uint8_t *)t112) + 8);
                            gm_t88rcucg1b7uk t114 = gm_l16;
                            gm_t9qkh0rsooc8d t115;
                            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)1ULL));
                            gm_t8uadrbfpr7kk t116;
                            gm_f_set_fallback_s66n99d((void *)&t116, (void *)NULL, cx, NULL, t110, t113, t114, t115);
                            if (gm_unw.kind) {
                                goto gm_ll99;
                            }
                            if (!((((uint8_t *)&t116))[16] != 0)) goto ie108;
                            gm_t2i8ne5ibd7v4 t117 = GM_LD(gm_t2i8ne5ibd7v4, ((uint8_t *)&t116));
                            gm_l17 = t117;
                            {
                                void * t119 = gm_l0;
                                void * t120 = GM_LD(void *, (((uint8_t *)t119) + 0));
                                void * t121 = gm_l0;
                                void * t122 = GM_LD(void *, (((uint8_t *)t121) + 0));
                                void * t123 = gm_l1;
                                void * t124 = GM_LD(void *, (((uint8_t *)t123) + 56));
                                gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t124) + 72));
                                gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
                                gm_tl874t2atnlh3 t127 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 4));
                                gm_t4gut2c9fj1kt t128;
                                gm_f_early_fallback_error_4lq49mb((void *)&t128, (void *)NULL, cx, NULL, t122, t125, t126, t127);
                                if (gm_unw.kind) {
                                    goto gm_ll99;
                                }
                                gm_b0_1 t129 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t120, &t128);
                                if (gm_unw.kind) {
                                    goto gm_ll99;
                                }
                            }
                            {
                                goto lb95;
                            }
                            goto ix108;
ie108:;
ix108:;
                        }
                        goto me104;
mn105:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 204, 5);
me104:;
                    }
lc95:;
                    {
                        uint64_t t131 = gm_l15;
                        uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                        uint64_t t134 = r132;
                        gm_l15 = t134;
                    }
                }
gm_ll99:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb95; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc95; }
                }
                goto gm_lad;
lb95:;
            }
        }
        {
            void * t135 = gm_l0;
            void * t136 = (void *)(((uint8_t *)t135) + 320);
            gm_t91vm1g0k2rpm t137;
            memset((uint8_t *)&t137 + 34, 0, 6);
            gm_t88rcucg1b7uk t138 = gm_l12;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t137 + 0, t138);
            void * t139 = gm_l1;
            void * t140 = GM_LD(void *, (((uint8_t *)t139) + 56));
            gm_tl874t2atnlh3 t141 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t140) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t137 + 4, t141);
            gm_b16_8 t142;
            GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sl4ns6hobicht));
            GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t142);
            GM_ST(uint8_t, (uint8_t *)&t137 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t137 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t143;
            GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t143 + 8), t137);
            gm_tftb1odc739t5 t144 = gm_f_push_7koe9lk(t136, &t143);
            if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srclfnv05vuf0b8, 210, 5);
            gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
        }
        {
            void * t146 = gm_l0;
            void * t147 = gm_l1;
            void * t148 = gm_l10;
            void * t149 = gm_l11;
            gm_t88rcucg1b7uk t150 = gm_l12;
            uint8_t t151 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l7) + 0) == ((uint8_t)11ULL)));
            void * t152 = gm_f_range_loop_44fv3au((void *)NULL, cx, NULL, t146, t147, t148, t149, t150, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t153 = 0;
            F.ret = t152;
            goto gm_fret;
        }
        goto ix58;
ie58:;
        {
            void * t155 = gm_l0;
            void * t156 = GM_LD(void *, (((uint8_t *)t155) + 0));
            gm_tl874t2atnlh3 t157 = gm_l2;
            void * t158 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t159 = 0;
            F.ret = t158;
            goto gm_fret;
        }
ix58:;
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
__attribute__((aligned(4))) void * gm_f_in_expr_su8ofmp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_in_expr_su8ofmp F;
    void * gm_l0; /* cx */
    void * gm_l1; /* checked */
    void * gm_l2; /* container */
    uint8_t gm_l3; /* negated */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* elem */
    gm_ts6oqppjh0ba3 gm_l7; /* op */
    void * gm_l8; /* lhs */
    void * gm_l9; /* rhs */
    void * gm_l10; /* lo */
    void * gm_l11; /* hi */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_b16_8 gm_l13; /* elems */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint8_t gm_l17; /* inline */
    gm_b16_8 gm_l18; /* elems */
    gm_b16_8 gm_l19; /* elems */
    gm_t88rcucg1b7uk gm_l21; /* et */
    uint8_t gm_l22; /* wraps */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    void * gm_l26; /* v */
    gm_t88rcucg1b7uk gm_l27; /* vt */
    gm_t88rcucg1b7uk gm_l28; /* joined */
    gm_t88rcucg1b7uk gm_l31; /* jr */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    void * gm_l34; /* v */
    void * gm_l35; /* m */
    gm_t88rcucg1b7uk gm_l36; /* mt */
    void * gm_l37; /* c */
    gm_t88rcucg1b7uk gm_l38; /* ct */
    gm_tbngattl1fo51 gm_l39; /* r */
    uint32_t gm_l40; /* t */
    void * gm_l42; /* lo */
    void * gm_l43; /* hi */
    gm_t88rcucg1b7uk gm_l44; /* item */
    gm_t88rcucg1b7uk gm_l45; /* e */
    gm_t88rcucg1b7uk gm_l46; /* e */
    gm_t88rcucg1b7uk gm_l47; /* e */
    gm_b16_8 gm_l48; /* es */
    gm_b16_8 gm_l49; /* es */
    gm_tkn42u7na3t81 gm_l50; /* joined */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* k */
    gm_t88rcucg1b7uk gm_l54; /* a */
    gm_t88rcucg1b7uk gm_l55; /* b */
    gm_t88rcucg1b7uk gm_l56; /* j */
    uint32_t gm_l57; /* t */
    uint64_t gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    uint64_t gm_l62; /* k */
    void * gm_l63; /* field */
    gm_t88rcucg1b7uk gm_l64; /* jt */
    void * gm_l66; /* agg */
    gm_t88rcucg1b7uk gm_l67; /* rt */
    gm_t88rcucg1b7uk gm_l68; /* ri */
    gm_t88rcucg1b7uk gm_l69; /* et */
    GM_STACK_CHECK(&F, 5395u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l1;
        gm_l6 = t5;
    }
    {
        void * t7 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t8 != ((uint8_t)13ULL)) goto ie6;
        gm_ts6oqppjh0ba3 t9 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t7) + 8) + 8) + 0));
        gm_l7 = t9;
        void * t10 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8));
        gm_l8 = t10;
        void * t11 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 16));
        gm_l9 = t11;
        gm_ts6oqppjh0ba3 t12 = gm_l7;
        gm_ts6oqppjh0ba3 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)10ULL));
        gm_ts6oqppjh0ba3 t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)11ULL));
        gm_t9ldr1q5hn9fq t15;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t15 + 0), t13);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t15 + 2), t14);
        uint8_t t16 = 0;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t15) + 0, ((uint8_t *)&t12))) t16 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t15) + 2, ((uint8_t *)&t12))) t16 = 1;
        uint8_t t17 = (uint8_t)(t16);
        if (!t17) goto ie6;
        {
            void * t19 = gm_l0;
            void * t20 = gm_l8;
            void * t21 = gm_l6;
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
            gm_tkn42u7na3t81 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[4] = 1;
            void * t24 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t19, t20, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t24;
        }
        {
            void * t25 = gm_l0;
            void * t26 = gm_l9;
            void * t27 = gm_l6;
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
            gm_tkn42u7na3t81 t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[4] = 1;
            void * t30 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t25, t26, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t30;
        }
        {
            void * t31 = gm_l0;
            void * t32 = gm_l6;
            gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
            void * t34 = gm_l10;
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
            gm_tl874t2atnlh3 t36 = gm_l4;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_su22guvkav5b5));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(20));
            gm_t88rcucg1b7uk t38 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t31, t33, t35, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t38;
        }
        {
            {
                void * t40 = gm_l0;
                gm_t88rcucg1b7uk t41 = gm_l12;
                void * t42 = gm_l11;
                gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t42) + 0));
                gm_tl874t2atnlh3 t44 = gm_l4;
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_su22guvkav5b5));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(20));
                gm_t88rcucg1b7uk t46 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t40, t41, t43, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t47 = gm_l0;
            void * t48 = (void *)(((uint8_t *)t47) + 320);
            gm_t88rcucg1b7uk t49 = gm_l12;
            gm_tl874t2atnlh3 t50 = gm_l4;
            gm_t2i8ne5ibd7v4 t51;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t51 + 0), t49);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t51 + 4), t50);
            gm_tpm220j5qhjke t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)&t52 + 8), t51);
            gm_tftb1odc739t5 t53 = gm_f_push_7koe9lk(t48, &t52);
            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srclfnv05vuf0b8, 14, 9);
            gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
        }
        {
            void * t55 = gm_l0;
            void * t56 = gm_l6;
            gm_b16_8 t57 = gm_f_empty_slice_vj3ckdj_2();
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            void * t59 = gm_l10;
            void * t60 = gm_l11;
            gm_t88rcucg1b7uk t61 = gm_l12;
            uint8_t t62 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l7) + 0) == ((uint8_t)11ULL)));
            uint8_t t63 = gm_l3;
            gm_tl874t2atnlh3 t64 = gm_l4;
            void * t65 = gm_f_range_test_tqcppmu((void *)NULL, cx, NULL, t55, t56, t58, t59, t60, t61, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t68 = gm_l2;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t68) + 8) + 0));
        if (t69 != ((uint8_t)10ULL)) goto ie67;
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)t68) + 8) + 8));
        gm_l13 = t70;
        gm_b16_8 t71 = gm_l13;
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        uint8_t t73 = gm_f_has_spread_bndtpd6(t72);
        uint8_t t74 = (uint8_t)!t73;
        if (!t74) goto ie67;
        {
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                gm_l14 = t77;
            }
            {
                for (;;) {
                    uint64_t t79 = gm_l15;
                    uint64_t t80 = gm_l14;
                    uint8_t t81 = (t79 < t80);
                    if (!t81) goto lb78;
                    {
                        uint64_t t84 = gm_l15;
                        uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&t84));
                        gm_l16 = t87;
                        {
                            void * t90 = gm_l0;
                            uint64_t t91 = gm_l16;
                            uint64_t t92 = (uintptr_t)t91;
                            uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_srclfnv05vuf0b8, 21, 39);
                            void * t94 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t92 * 136) + 40);
                            void * t95 = gm_l6;
                            gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t95) + 0));
                            gm_toqf51949ppls t97;
                            gm_f_is_pattern_operand_p51tban((void *)&t97, (void *)NULL, cx, NULL, t90, t94, t96);
                            if (gm_unw.kind) {
                                goto gm_ll82;
                            }
                            uint8_t t98;
                            if (((((uint8_t *)&t97))[64] != 0)) goto mn100;
                            t98 = ((uint8_t)0);
                            goto me99;
mn100:;
                            t98 = ((uint8_t)1);
                            goto me99;
mn101:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 21, 16);
me99:;
                            if (!t98) goto ie89;
                            {
                                void * t103 = gm_l0;
                                void * t104 = gm_l6;
                                gm_b16_8 t105 = gm_l13;
                                gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                                uint8_t t107 = gm_l3;
                                gm_tl874t2atnlh3 t108 = gm_l4;
                                void * t109 = gm_f_in_patterns_mi6p7hb((void *)NULL, cx, NULL, t103, t104, t106, t107, t108);
                                if (gm_unw.kind) {
                                    goto gm_ll82;
                                }
                                uint8_t t110 = 0;
                                F.ret = t109;
                                goto gm_fret;
                            }
                            goto ix89;
ie89:;
ix89:;
                        }
                        goto me85;
mn86:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 20, 9);
me85:;
                    }
lc78:;
                    {
                        uint64_t t112 = gm_l15;
                        uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                        uint64_t t115 = r113;
                        gm_l15 = t115;
                    }
                }
gm_ll82:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb78; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc78; }
                }
                goto gm_lad;
lb78:;
            }
        }
        goto ix67;
ie67:;
ix67:;
    }
    {
        gm_l17 = ((uint8_t)0);
    }
    {
        void * t117 = gm_l2;
        uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t117) + 8) + 0));
        if (t118 != ((uint8_t)10ULL)) goto ie116;
        gm_b16_8 t119 = GM_LD(gm_b16_8, ((((uint8_t *)t117) + 8) + 8));
        gm_l18 = t119;
        gm_b16_8 t120 = gm_l18;
        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
        uint8_t t122 = gm_f_has_spread_bndtpd6(t121);
        uint8_t t123 = (uint8_t)!t122;
        if (!t123) goto ie116;
        {
            gm_l17 = ((uint8_t)1);
        }
        goto ix116;
ie116:;
ix116:;
    }
    {
        void * t125;
        uint8_t t127 = gm_l17;
        if (!t127) goto ie126;
        void * t128 = gm_l2;
        uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
        if (t129 != ((uint8_t)10ULL)) goto ie126;
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((((uint8_t *)t128) + 8) + 8));
        gm_l19 = t130;
        {
            gm_f_list_s1kpkpr((void *)&F.l20, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t133 = gm_l0;
            void * t134 = (void *)(((uint8_t *)t133) + 96);
            void * t135 = gm_l0;
            void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
            void * t137 = gm_l6;
            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t137) + 0));
            gm_t88rcucg1b7uk t139 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t134, t136, t138);
            if (gm_unw.kind) {
                goto gm_ld131_1;
            }
            gm_l21 = t139;
        }
        {
            uint8_t t140 = 0;
            void * t141 = gm_l5;
            gm_t88rcucg1b7uk t142 = gm_l21;
            gm_t53at80pfdus3 t143;
            gm_f_kind_jcnt4hm((void *)&t143, t141, GM_CONV(gm_csguankko4lmhd, t142));
            uint8_t t144;
            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
            switch (t150) { case ((uint8_t)0ULL): goto ma148; case ((uint8_t)1ULL): goto ma148; case ((uint8_t)2ULL): goto ma148; case ((uint8_t)3ULL): goto ma148; case ((uint8_t)4ULL): goto ma148; case ((uint8_t)5ULL): goto ma148; case ((uint8_t)6ULL): goto ma148; case ((uint8_t)7ULL): goto ma148; case ((uint8_t)8ULL): goto ma148; case ((uint8_t)9ULL): goto ma148; case ((uint8_t)10ULL): goto ma148; case ((uint8_t)11ULL): goto ma148; case ((uint8_t)12ULL): goto ma148; case ((uint8_t)13ULL): goto ma146; case ((uint8_t)14ULL): goto ma147; case ((uint8_t)15ULL): goto ma148; case ((uint8_t)16ULL): goto ma148; case ((uint8_t)17ULL): goto ma148; case ((uint8_t)18ULL): goto ma148; case ((uint8_t)19ULL): goto ma148; case ((uint8_t)20ULL): goto ma148; case ((uint8_t)21ULL): goto ma148; case ((uint8_t)22ULL): goto ma148; case ((uint8_t)23ULL): goto ma148; case ((uint8_t)24ULL): goto ma148; case ((uint8_t)25ULL): goto ma148; case ((uint8_t)26ULL): goto ma148; default: goto ma148; }
ma146:;
            uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
            if (t152 != ((uint8_t)13ULL)) goto mn151;
            t144 = ((uint8_t)1);
            goto me145;
mn151:;
ma147:;
            uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
            if (t154 != ((uint8_t)14ULL)) goto mn153;
            t144 = ((uint8_t)1);
            goto me145;
mn153:;
ma148:;
            t144 = ((uint8_t)0);
            goto me145;
mn155:;
ma149:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 34, 18);
me145:;
            if (t144) {
                void * t156 = gm_l5;
                gm_t88rcucg1b7uk t157 = gm_l21;
                uint8_t t158 = gm_f_has_infer_3o82a0b(t156, GM_CONV(gm_csguankko4lmhd, t157));
                uint8_t t159 = (uint8_t)!t158;
                t140 = t159;
            } else t140 = 0;
            gm_l22 = t140;
        }
        {
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                gm_l23 = t161;
            }
            {
                for (;;) {
                    uint64_t t163 = gm_l24;
                    uint64_t t164 = gm_l23;
                    uint8_t t165 = (t163 < t164);
                    if (!t165) goto lb162;
                    {
                        uint64_t t168 = gm_l24;
                        uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&t168));
                        gm_l25 = t171;
                        {
                            void * t173 = gm_l0;
                            uint64_t t174 = gm_l25;
                            uint64_t t175 = (uintptr_t)t174;
                            uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                            if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_srclfnv05vuf0b8, 36, 27);
                            void * t177 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t175 * 136) + 40);
                            void * t178 = gm_l6;
                            gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t178) + 0));
                            gm_tkn42u7na3t81 t180;
                            GM_ST(uint8_t, (uint8_t *)&t180 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t180 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t180), t179);
                            ((uint8_t *)&t180)[4] = 1;
                            void * t181 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t173, t177, t180);
                            if (gm_unw.kind) {
                                goto gm_ll166;
                            }
                            gm_l26 = t181;
                        }
                        {
                            void * t182 = gm_l0;
                            void * t183 = (void *)(((uint8_t *)t182) + 96);
                            void * t184 = gm_l0;
                            void * t185 = GM_LD(void *, (((uint8_t *)t184) + 0));
                            void * t186 = gm_l26;
                            gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t186) + 0));
                            gm_t88rcucg1b7uk t188 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t183, t185, t187);
                            if (gm_unw.kind) {
                                goto gm_ll166;
                            }
                            gm_l27 = t188;
                        }
                        {
                            uint8_t t190 = gm_l22;
                            if (!t190) goto ie189;
                            gm_t88rcucg1b7uk t191 = gm_l27;
                            gm_t88rcucg1b7uk t192 = gm_l21;
                            uint8_t t193 = (!gm_eq9m6mvtmgde(((uint8_t *)&t191), ((uint8_t *)&t192)));
                            if (!t193) goto ie189;
                            uint8_t t194 = 0;
                            void * t195 = gm_l5;
                            gm_t88rcucg1b7uk t196 = gm_l27;
                            gm_t53at80pfdus3 t197;
                            gm_f_kind_jcnt4hm((void *)&t197, t195, GM_CONV(gm_csguankko4lmhd, t196));
                            uint8_t t198;
                            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t197) + 0));
                            if (t201 != ((uint8_t)23ULL)) goto mn200;
                            t198 = ((uint8_t)1);
                            goto me199;
mn200:;
                            t198 = ((uint8_t)0);
                            goto me199;
mn202:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 38, 44);
me199:;
                            if (!t198) {
                                void * t203 = gm_l0;
                                gm_t88rcucg1b7uk t204 = gm_l27;
                                gm_t88rcucg1b7uk t205 = gm_l21;
                                gm_th2lqbo77pvsc t206;
                                gm_f_coercion_steps_ibqvhta((void *)&t206, (void *)NULL, cx, NULL, t203, t204, t205);
                                if (gm_unw.kind) {
                                    goto gm_ll166;
                                }
                                uint8_t t207;
                                if (((((uint8_t *)&t206))[16] != 0)) goto mn209;
                                t207 = ((uint8_t)0);
                                goto me208;
mn209:;
                                t207 = ((uint8_t)1);
                                goto me208;
mn210:;
                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 38, 73);
me208:;
                                t194 = t207;
                            } else t194 = 1;
                            if (!t194) goto ie189;
                            {
                                void * t212 = gm_l0;
                                void * t213 = gm_l26;
                                gm_t88rcucg1b7uk t214 = gm_l21;
                                void * t215 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t212, t213, t214);
                                if (gm_unw.kind) {
                                    goto gm_ll166;
                                }
                                gm_l26 = t215;
                            }
                            goto ix189;
ie189:;
ix189:;
                        }
                        {
                            void * t216 = (void *)((uint8_t *)&F.l20);
                            void * t217 = gm_l26;
                            gm_tftb1odc739t5 t218 = gm_f_push_pf02nv6(t216, t217);
                            if (((((uint8_t *)&t218))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t218) + 1, gm_srclfnv05vuf0b8, 41, 13);
                            gm_b0_1 t219 = GM_LD(gm_b0_1, (((uint8_t *)&t218) + 1));
                        }
                        goto me169;
mn170:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 35, 9);
me169:;
                    }
lc162:;
                    {
                        uint64_t t221 = gm_l24;
                        uint64_t r222; bool o223 = __builtin_add_overflow(t221, ((uint64_t)1ULL), &r222);
                        uint64_t t224 = r222;
                        gm_l24 = t224;
                    }
                }
gm_ll166:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb162; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc162; }
                }
                goto gm_ld131_1;
lb162:;
            }
        }
        {
            void * t225 = gm_l0;
            void * t226 = gm_l6;
            gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
            gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
            gm_t88rcucg1b7uk t229 = gm_f_join_members_97aedtg((void *)NULL, cx, NULL, t225, t226, t228);
            if (gm_unw.kind) {
                goto gm_ld131_1;
            }
            gm_l28 = t229;
        }
        {
            gm_t88rcucg1b7uk t231 = gm_l28;
            void * t232 = gm_l6;
            gm_t88rcucg1b7uk t233 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t232) + 0));
            uint8_t t234 = (!gm_eq9m6mvtmgde(((uint8_t *)&t231), ((uint8_t *)&t233)));
            if (!t234) goto ie230;
            {
                void * t236 = gm_l0;
                void * t237 = gm_l6;
                gm_t88rcucg1b7uk t238 = gm_l28;
                void * t239 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t236, t237, t238);
                if (gm_unw.kind) {
                    goto gm_ld131_1;
                }
                gm_l6 = t239;
            }
            goto ix230;
ie230:;
ix230:;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l29, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld131_1;
            }
        }
        {
            gm_f_list_nof7vfj((void *)&F.l30, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld131_1;
            }
        }
        {
            void * t240 = gm_l0;
            void * t241 = (void *)(((uint8_t *)t240) + 96);
            void * t242 = gm_l0;
            void * t243 = GM_LD(void *, (((uint8_t *)t242) + 0));
            gm_t88rcucg1b7uk t244 = gm_l28;
            gm_t88rcucg1b7uk t245 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t241, t243, t244);
            if (gm_unw.kind) {
                goto gm_ld131_1;
            }
            gm_l31 = t245;
        }
        {
            {
                gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                gm_l32 = t247;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t249 = gm_l33;
                    uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t251 = (t249 < t250);
                    if (!t251) goto lb248;
                    {
                        uint64_t t254 = gm_l33;
                        uint64_t t255 = (uintptr_t)t254;
                        uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        void * t257 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t255 * 8));
                        void * t260 = GM_LD(void *, ((uint8_t *)&t257));
                        gm_l34 = t260;
                        {
                            void * t262 = gm_l34;
                            gm_l35 = t262;
                        }
                        {
                            void * t263 = gm_l0;
                            void * t264 = (void *)(((uint8_t *)t263) + 96);
                            void * t265 = gm_l0;
                            void * t266 = GM_LD(void *, (((uint8_t *)t265) + 0));
                            void * t267 = gm_l35;
                            gm_t88rcucg1b7uk t268 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t267) + 0));
                            gm_t88rcucg1b7uk t269 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t264, t266, t268);
                            if (gm_unw.kind) {
                                goto gm_ll252;
                            }
                            gm_l36 = t269;
                        }
                        {
                            gm_t88rcucg1b7uk t271 = gm_l36;
                            gm_t88rcucg1b7uk t272 = gm_l31;
                            uint8_t t273 = (!gm_eq9m6mvtmgde(((uint8_t *)&t271), ((uint8_t *)&t272)));
                            if (!t273) goto ie270;
                            void * t274 = gm_l5;
                            gm_t88rcucg1b7uk t275 = gm_l36;
                            uint8_t t276 = gm_f_has_infer_3o82a0b(t274, GM_CONV(gm_csguankko4lmhd, t275));
                            uint8_t t277 = (uint8_t)!t276;
                            if (!t277) goto ie270;
                            void * t278 = gm_l0;
                            gm_t88rcucg1b7uk t279 = gm_l36;
                            gm_t88rcucg1b7uk t280 = gm_l31;
                            gm_th2lqbo77pvsc t281;
                            gm_f_coercion_steps_ibqvhta((void *)&t281, (void *)NULL, cx, NULL, t278, t279, t280);
                            if (gm_unw.kind) {
                                goto gm_ll252;
                            }
                            uint8_t t282;
                            if (((((uint8_t *)&t281))[16] != 0)) goto mn284;
                            t282 = ((uint8_t)0);
                            goto me283;
mn284:;
                            t282 = ((uint8_t)1);
                            goto me283;
mn285:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 51, 59);
me283:;
                            if (!t282) goto ie270;
                            {
                                void * t287 = gm_l0;
                                void * t288 = gm_l35;
                                gm_t88rcucg1b7uk t289 = gm_l31;
                                void * t290 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t287, t288, t289);
                                if (gm_unw.kind) {
                                    goto gm_ll252;
                                }
                                gm_l35 = t290;
                            }
                            goto ix270;
ie270:;
ix270:;
                        }
                        {
                            {
                                void * t292 = gm_l0;
                                void * t293 = gm_l6;
                                gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t293) + 0));
                                void * t295 = gm_l35;
                                gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t295) + 0));
                                void * t297 = gm_l35;
                                gm_tl874t2atnlh3 t298 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t297) + 72));
                                gm_b16_8 t299;
                                GM_ST(void *, (uint8_t *)&t299, (void *)(gm_smdscldu1a8hg));
                                GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(41));
                                gm_t88rcucg1b7uk t300 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t292, t294, t296, t298, t299);
                                if (gm_unw.kind) {
                                    goto gm_ll252;
                                }
                            }
                        }
                        {
                            void * t301 = (void *)((uint8_t *)&F.l29);
                            void * t302 = gm_l35;
                            gm_tftb1odc739t5 t303 = gm_f_push_d943vgp_2(t301, &(*(gm_t69m56meqefs8 *)((uint8_t *)t302)));
                            if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 1, gm_srclfnv05vuf0b8, 53, 13);
                            gm_b0_1 t304 = GM_LD(gm_b0_1, (((uint8_t *)&t303) + 1));
                        }
                        {
                            void * t305 = (void *)((uint8_t *)&F.l30);
                            void * t306 = gm_l35;
                            gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t306) + 0));
                            gm_tftb1odc739t5 t308 = gm_f_push_k3l9nic(t305, GM_CONV(gm_csguankko4lmhd, t307));
                            if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 1, gm_srclfnv05vuf0b8, 54, 13);
                            gm_b0_1 t309 = GM_LD(gm_b0_1, (((uint8_t *)&t308) + 1));
                        }
                        goto me258;
mn259:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 48, 9);
me258:;
                    }
lc248:;
                    {
                        uint64_t t311 = gm_l33;
                        uint64_t r312; bool o313 = __builtin_add_overflow(t311, ((uint64_t)1ULL), &r312);
                        uint64_t t314 = r312;
                        gm_l33 = t314;
                    }
                }
gm_ll252:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb248; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc248; }
                }
                goto gm_ld131_1;
lb248:;
            }
        }
        void * t315 = gm_l5;
        gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
        gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
        gm_t88rcucg1b7uk t318 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t315, t317);
        if (gm_unw.kind) {
            goto gm_ld131_1;
        }
        gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
        gm_teti411eujvf4 t320;
        memset(&t320, 0, sizeof t320);
        GM_ST(uint8_t, ((uint8_t *)&t320 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t320 + 8), t319);
        void * t321 = gm_l2;
        gm_tl874t2atnlh3 t322 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t321) + 72));
        void * t323 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t318, &t320, t322);
        if (gm_unw.kind) {
            goto gm_ld131_1;
        }
        void * t324 = t323;
        {
            void * t325 = (void *)((uint8_t *)&F.l20);
            gm_f_deinit_pal9hnf(t325);
        }
        goto gm_ls326;
gm_ld131_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t327 = (void *)((uint8_t *)&F.l20);
            gm_f_deinit_pal9hnf(t327);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls326:;
        t125 = t324;
        goto ix126;
ie126:;
        void * t329 = gm_l0;
        void * t330 = gm_l2;
        gm_tkn42u7na3t81 t331;
        memset(&t331, 0, sizeof t331);
        void * t332 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t329, t330, t331);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t333 = t332;
        t125 = t333;
ix126:;
        gm_l37 = t125;
    }
    {
        void * t334 = gm_l0;
        void * t335 = (void *)(((uint8_t *)t334) + 96);
        void * t336 = gm_l0;
        void * t337 = GM_LD(void *, (((uint8_t *)t336) + 0));
        void * t338 = gm_l37;
        gm_t88rcucg1b7uk t339 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t338) + 0));
        gm_t88rcucg1b7uk t340 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t335, t337, t339);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l38 = t340;
    }
    {
        void * t342 = gm_l0;
        void * t343 = GM_LD(void *, (((uint8_t *)t342) + 0));
        gm_t88rcucg1b7uk t344 = gm_l38;
        gm_thetuhtouve7f t345 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t343, t344);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t345))[8] != 0)) goto ie341;
        gm_tbngattl1fo51 t346 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&t345));
        gm_l39 = t346;
        {
            {
                void * t349 = gm_l0;
                void * t350 = gm_l6;
                gm_t88rcucg1b7uk t351 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t350) + 0));
                gm_t88rcucg1b7uk t352 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
                gm_tl874t2atnlh3 t353 = gm_l4;
                gm_b16_8 t354;
                GM_ST(void *, (uint8_t *)&t354, (void *)(gm_sg8s3e7n1hsck));
                GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(33));
                gm_t88rcucg1b7uk t355 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t349, t351, t352, t353, t354);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t356 = gm_l0;
            gm_t88rcucg1b7uk t357 = gm_l38;
            gm_tl874t2atnlh3 t358 = gm_l4;
            uint32_t t359 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t356, t357, t358);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l40 = t359;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l41, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t360 = (void *)((uint8_t *)&F.l41);
            uint32_t t361 = gm_l40;
            void * t362 = gm_l37;
            gm_ttggq7ng4kp3p t363;
            GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t363 + 0), t361);
            GM_ST(void *, ((uint8_t *)&t363 + 8), t362);
            gm_trcmmd12cefb1 t364;
            GM_ST(uint8_t, (uint8_t *)&t364 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t364 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t364 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t364 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t364 + 8), t363);
            gm_tftb1odc739t5 t365 = gm_f_push_d6c76ud_2(t360, &t364);
            if (((((uint8_t *)&t365))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t365) + 1, gm_srclfnv05vuf0b8, 66, 9);
            gm_b0_1 t366 = GM_LD(gm_b0_1, (((uint8_t *)&t365) + 1));
        }
        {
            gm_t88rcucg1b7uk t367 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
            gm_t88rcucg1b7uk t368 = gm_l38;
            uint32_t t369 = gm_l40;
            gm_teti411eujvf4 t370;
            memset(&t370, 0, sizeof t370);
            GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t370 + 8), t369);
            gm_tl874t2atnlh3 t371 = gm_l4;
            void * t372 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t368, &t370, t371);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t373;
            GM_ST(uint32_t, (uint8_t *)&t373 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t373 + 0), t372);
            GM_ST(uint32_t, ((uint8_t *)&t373 + 8), ((uint32_t)0ULL));
            gm_teti411eujvf4 t374;
            memset(&t374, 0, sizeof t374);
            GM_ST(uint8_t, ((uint8_t *)&t374 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t374 + 8), t373);
            gm_tl874t2atnlh3 t375 = gm_l4;
            void * t376 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t367, &t374, t375);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l42 = t376;
        }
        {
            gm_t88rcucg1b7uk t377 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
            gm_t88rcucg1b7uk t378 = gm_l38;
            uint32_t t379 = gm_l40;
            gm_teti411eujvf4 t380;
            memset(&t380, 0, sizeof t380);
            GM_ST(uint8_t, ((uint8_t *)&t380 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t380 + 8), t379);
            gm_tl874t2atnlh3 t381 = gm_l4;
            void * t382 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t378, &t380, t381);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t383;
            GM_ST(uint32_t, (uint8_t *)&t383 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t383 + 0), t382);
            GM_ST(uint32_t, ((uint8_t *)&t383 + 8), ((uint32_t)1ULL));
            gm_teti411eujvf4 t384;
            memset(&t384, 0, sizeof t384);
            GM_ST(uint8_t, ((uint8_t *)&t384 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t384 + 8), t383);
            gm_tl874t2atnlh3 t385 = gm_l4;
            void * t386 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t377, &t384, t385);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t386;
        }
        {
            void * t387 = gm_l0;
            void * t388 = gm_l6;
            gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
            gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&t389));
            void * t391 = gm_l42;
            void * t392 = gm_l43;
            gm_t88rcucg1b7uk t393 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
            uint8_t t394 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 0));
            uint8_t t395 = gm_l3;
            gm_tl874t2atnlh3 t396 = gm_l4;
            void * t397 = gm_f_range_test_tqcppmu((void *)NULL, cx, NULL, t387, t388, t390, t391, t392, t393, t394, t395, t396);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t398 = 0;
            F.ret = t397;
            goto gm_fret;
        }
        goto ix341;
ie341:;
ix341:;
    }
    {
        void * t399 = gm_l6;
        gm_t88rcucg1b7uk t400 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t399) + 0));
        gm_l44 = t400;
    }
    {
        void * t401 = gm_l5;
        gm_t88rcucg1b7uk t402 = gm_l38;
        gm_t53at80pfdus3 t403;
        gm_f_kind_jcnt4hm((void *)&t403, t401, GM_CONV(gm_csguankko4lmhd, t402));
        uint8_t t412 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
        switch (t412) { case ((uint8_t)0ULL): goto ma410; case ((uint8_t)1ULL): goto ma410; case ((uint8_t)2ULL): goto ma410; case ((uint8_t)3ULL): goto ma410; case ((uint8_t)4ULL): goto ma410; case ((uint8_t)5ULL): goto ma410; case ((uint8_t)6ULL): goto ma410; case ((uint8_t)7ULL): goto ma410; case ((uint8_t)8ULL): goto ma410; case ((uint8_t)9ULL): goto ma410; case ((uint8_t)10ULL): goto ma407; case ((uint8_t)11ULL): goto ma405; case ((uint8_t)12ULL): goto ma408; case ((uint8_t)13ULL): goto ma410; case ((uint8_t)14ULL): goto ma410; case ((uint8_t)15ULL): goto ma410; case ((uint8_t)16ULL): goto ma410; case ((uint8_t)17ULL): goto ma410; case ((uint8_t)18ULL): goto ma410; case ((uint8_t)19ULL): goto ma410; case ((uint8_t)20ULL): goto ma406; case ((uint8_t)21ULL): goto ma410; case ((uint8_t)22ULL): goto ma410; case ((uint8_t)23ULL): goto ma410; case ((uint8_t)24ULL): goto ma410; case ((uint8_t)25ULL): goto ma410; case ((uint8_t)26ULL): goto ma409; default: goto ma410; }
ma405:;
        uint8_t t414 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
        if (t414 != ((uint8_t)11ULL)) goto mn413;
        gm_t88rcucg1b7uk t415 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t403) + 8) + 8));
        gm_l45 = t415;
        {
            gm_t88rcucg1b7uk t417 = gm_l45;
            gm_l44 = t417;
        }
        goto me404;
mn413:;
ma406:;
        uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
        if (t419 != ((uint8_t)20ULL)) goto mn418;
        gm_t88rcucg1b7uk t420 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t403) + 8) + 4));
        gm_l46 = t420;
        {
            gm_t88rcucg1b7uk t422 = gm_l46;
            gm_l44 = t422;
        }
        goto me404;
mn418:;
ma407:;
        uint8_t t424 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
        if (t424 != ((uint8_t)10ULL)) goto mn423;
        gm_t88rcucg1b7uk t425 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t403) + 8) + 4));
        gm_l47 = t425;
        {
            gm_t88rcucg1b7uk t427 = gm_l47;
            gm_l44 = t427;
        }
        goto me404;
mn423:;
ma408:;
        uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
        if (t429 != ((uint8_t)12ULL)) goto mn428;
        gm_b16_8 t430 = GM_LD(gm_b16_8, ((((uint8_t *)&t403) + 8) + 0));
        gm_l48 = t430;
        uint64_t t432 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
        uint8_t t433 = (t432 > ((uint64_t)0ULL));
        if (!t433) goto ie431;
        {
            uint64_t t435 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t436 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
            if (t435 >= (uintptr_t)(t436)) gm_trap_index(t435, t436, gm_srclfnv05vuf0b8, 76, 49);
            gm_t88rcucg1b7uk t437 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l48)) + t435 * 4));
            gm_l44 = t437;
        }
        goto ix431;
ie431:;
ix431:;
        goto me404;
mn428:;
ma409:;
        uint8_t t439 = GM_LD(uint8_t, (((uint8_t *)&t403) + 0));
        if (t439 != ((uint8_t)26ULL)) goto mn438;
        void * t440 = gm_l0;
        void * t441 = GM_LD(void *, (((uint8_t *)t440) + 0));
        gm_tl874t2atnlh3 t442 = gm_l4;
        void * t443 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t441, t442);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t444 = 0;
        F.ret = t443;
        goto gm_fret;
        goto me404;
mn438:;
ma410:;
        {
            void * t447 = gm_l0;
            void * t448 = gm_l2;
            gm_tl874t2atnlh3 t449 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t448) + 72));
            void * t450 = gm_l0;
            gm_t88rcucg1b7uk t451 = gm_l38;
            gm_b16_8 t452 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t450, t451);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t453;
            GM_ST(gm_b16_8, ((uint8_t *)&t453 + 0), t452);
            gm_tabb710d9mq2e t454;
            gm_f_format_vc9g56o((void *)&t454, (void *)NULL, cx, NULL, t453);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t454))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t454) + 8, gm_srclfnv05vuf0b8, 79, 38);
            gm_b16_8 t455 = GM_LD(gm_b16_8, (((uint8_t *)&t454) + 8));
            gm_b16_8 t456 = GM_LD(gm_b16_8, ((uint8_t *)&t455));
            gm_b0_1 t457 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t447, t449, t456);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t458 = gm_l0;
            void * t459 = GM_LD(void *, (((uint8_t *)t458) + 0));
            gm_tl874t2atnlh3 t460 = gm_l4;
            void * t461 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t459, t460);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t462 = 0;
            F.ret = t461;
            goto gm_fret;
        }
        goto me404;
mn445:;
ma411:;
        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 72, 5);
me404:;
    }
    {
        void * t464 = gm_l5;
        gm_t88rcucg1b7uk t465 = gm_l38;
        gm_t53at80pfdus3 t466;
        gm_f_kind_jcnt4hm((void *)&t466, t464, GM_CONV(gm_csguankko4lmhd, t465));
        uint8_t t467 = GM_LD(uint8_t, (((uint8_t *)&t466) + 0));
        if (t467 != ((uint8_t)12ULL)) goto ie463;
        gm_b16_8 t468 = GM_LD(gm_b16_8, ((((uint8_t *)&t466) + 8) + 0));
        gm_l49 = t468;
        uint64_t t469 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
        uint8_t t470 = (t469 > ((uint64_t)0ULL));
        if (!t470) goto ie463;
        uint8_t t471 = gm_l17;
        uint8_t t472 = (uint8_t)!t471;
        if (!t472) goto ie463;
        {
            void * t474 = gm_l0;
            gm_b16_8 t475 = gm_l49;
            gm_tkn42u7na3t81 t476 = gm_f_tuple_elem_join_om8llaj((void *)NULL, cx, NULL, t474, t475);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l50 = t476;
        }
        {
            {
                gm_l52 = ((uint64_t)1ULL);
            }
            {
                uint64_t t478 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                gm_l51 = t478;
            }
            {
                for (;;) {
                    uint64_t t480 = gm_l52;
                    uint64_t t481 = gm_l51;
                    uint8_t t482 = (t480 < t481);
                    if (!t482) goto lb479;
                    {
                        uint64_t t485 = gm_l52;
                        uint64_t t488 = GM_LD(uint64_t, ((uint8_t *)&t485));
                        gm_l53 = t488;
                        {
                            uint8_t t491;
                            if (((((uint8_t *)&gm_l50))[4] != 0)) goto mn493;
                            t491 = ((uint8_t)1);
                            goto me492;
mn493:;
                            t491 = ((uint8_t)0);
                            goto me492;
mn494:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 89, 16);
me492:;
                            if (!t491) goto ie490;
                            void * t495 = gm_l0;
                            void * t496 = (void *)(((uint8_t *)t495) + 96);
                            void * t497 = gm_l5;
                            gm_t88rcucg1b7uk t498 = gm_l44;
                            uint64_t t499 = gm_l53;
                            uint64_t t500 = (uintptr_t)t499;
                            uint64_t t501 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                            if (t500 >= (uintptr_t)(t501)) gm_trap_index(t500, t501, gm_srclfnv05vuf0b8, 89, 61);
                            gm_t88rcucg1b7uk t502 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t500 * 4));
                            gm_t9qkh0rsooc8d t503 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t496, t497, t498, t502);
                            if (gm_unw.kind) {
                                goto gm_ll483;
                            }
                            uint8_t t504;
                            if (!((((uint8_t *)&t503))[0] != 0)) goto mn506;
                            t504 = ((uint8_t)1);
                            goto me505;
mn506:;
                            t504 = ((uint8_t)0);
                            goto me505;
mn507:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 89, 35);
me505:;
                            if (!t504) goto ie490;
                            {
                                void * t509 = gm_l0;
                                void * t510 = (void *)(((uint8_t *)t509) + 96);
                                void * t511 = gm_l0;
                                void * t512 = GM_LD(void *, (((uint8_t *)t511) + 0));
                                gm_t88rcucg1b7uk t513 = gm_l44;
                                gm_t88rcucg1b7uk t514 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t510, t512, t513);
                                if (gm_unw.kind) {
                                    goto gm_ll483;
                                }
                                gm_l54 = t514;
                            }
                            {
                                void * t515 = gm_l0;
                                void * t516 = (void *)(((uint8_t *)t515) + 96);
                                void * t517 = gm_l0;
                                void * t518 = GM_LD(void *, (((uint8_t *)t517) + 0));
                                uint64_t t519 = gm_l53;
                                uint64_t t520 = (uintptr_t)t519;
                                uint64_t t521 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                                if (t520 >= (uintptr_t)(t521)) gm_trap_index(t520, t521, gm_srclfnv05vuf0b8, 91, 45);
                                gm_t88rcucg1b7uk t522 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t520 * 4));
                                gm_t88rcucg1b7uk t523 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t516, t518, t522);
                                if (gm_unw.kind) {
                                    goto gm_ll483;
                                }
                                gm_l55 = t523;
                            }
                            {
                                void * t524 = gm_l0;
                                void * t525 = gm_l2;
                                gm_tl874t2atnlh3 t526 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t525) + 72));
                                gm_t6j6smj7qoctu t527;
                                void * t528 = gm_l0;
                                gm_t88rcucg1b7uk t529 = gm_l54;
                                gm_b16_8 t530 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t528, t529);
                                if (gm_unw.kind) {
                                    goto gm_ll483;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t527 + 0, t530);
                                void * t531 = gm_l0;
                                gm_t88rcucg1b7uk t532 = gm_l55;
                                gm_b16_8 t533 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t531, t532);
                                if (gm_unw.kind) {
                                    goto gm_ll483;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t527 + 16, t533);
                                gm_tabb710d9mq2e t534;
                                gm_f_format_ftgbk9j((void *)&t534, (void *)NULL, cx, NULL, &t527);
                                if (gm_unw.kind) {
                                    goto gm_ll483;
                                }
                                if (((((uint8_t *)&t534))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t534) + 8, gm_srclfnv05vuf0b8, 92, 42);
                                gm_b16_8 t535 = GM_LD(gm_b16_8, (((uint8_t *)&t534) + 8));
                                gm_b16_8 t536 = GM_LD(gm_b16_8, ((uint8_t *)&t535));
                                gm_b0_1 t537 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t524, t526, t536);
                                if (gm_unw.kind) {
                                    goto gm_ll483;
                                }
                            }
                            {
                                void * t538 = gm_l0;
                                void * t539 = GM_LD(void *, (((uint8_t *)t538) + 0));
                                gm_tl874t2atnlh3 t540 = gm_l4;
                                void * t541 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t539, t540);
                                if (gm_unw.kind) {
                                    goto gm_ll483;
                                }
                                uint8_t t542 = 0;
                                F.ret = t541;
                                goto gm_fret;
                            }
                            goto ix490;
ie490:;
ix490:;
                        }
                        goto me486;
mn487:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 88, 9);
me486:;
                    }
lc479:;
                    {
                        uint64_t t544 = gm_l52;
                        uint64_t r545; bool o546 = __builtin_add_overflow(t544, ((uint64_t)1ULL), &r545);
                        uint64_t t547 = r545;
                        gm_l52 = t547;
                    }
                }
gm_ll483:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb479; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc479; }
                }
                goto gm_lad;
lb479:;
            }
        }
        {
            if (!((((uint8_t *)&gm_l50))[4] != 0)) goto ie548;
            gm_t88rcucg1b7uk t549 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l50));
            gm_l56 = t549;
            {
                gm_t88rcucg1b7uk t551 = gm_l56;
                gm_l44 = t551;
            }
            {
                void * t552 = gm_l0;
                gm_t88rcucg1b7uk t553 = gm_l38;
                gm_tl874t2atnlh3 t554 = gm_l4;
                uint32_t t555 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t552, t553, t554);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l57 = t555;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l58, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_f_list_nof7vfj((void *)&F.l59, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l61 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t557 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                    gm_l60 = t557;
                }
                {
                    for (;;) {
                        uint64_t t559 = gm_l61;
                        uint64_t t560 = gm_l60;
                        uint8_t t561 = (t559 < t560);
                        if (!t561) goto lb558;
                        {
                            uint64_t t564 = gm_l61;
                            uint64_t t567 = GM_LD(uint64_t, ((uint8_t *)&t564));
                            gm_l62 = t567;
                            {
                                uint64_t t569 = gm_l62;
                                uint64_t t570 = (uintptr_t)t569;
                                uint64_t t571 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                                if (t570 >= (uintptr_t)(t571)) gm_trap_index(t570, t571, gm_srclfnv05vuf0b8, 102, 33);
                                gm_t88rcucg1b7uk t572 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t570 * 4));
                                gm_t88rcucg1b7uk t573 = gm_l38;
                                uint32_t t574 = gm_l57;
                                gm_teti411eujvf4 t575;
                                memset(&t575, 0, sizeof t575);
                                GM_ST(uint8_t, ((uint8_t *)&t575 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t575 + 8), t574);
                                gm_tl874t2atnlh3 t576 = gm_l4;
                                void * t577 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t573, &t575, t576);
                                if (gm_unw.kind) {
                                    goto gm_ll562;
                                }
                                uint64_t t578 = gm_l62;
                                uint32_t t579 = (uint32_t)t578;
                                gm_tbl8il92l97ut t580;
                                GM_ST(uint32_t, (uint8_t *)&t580 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t580 + 0), t577);
                                GM_ST(uint32_t, ((uint8_t *)&t580 + 8), t579);
                                gm_teti411eujvf4 t581;
                                memset(&t581, 0, sizeof t581);
                                GM_ST(uint8_t, ((uint8_t *)&t581 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t581 + 8), t580);
                                gm_tl874t2atnlh3 t582 = gm_l4;
                                void * t583 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t572, &t581, t582);
                                if (gm_unw.kind) {
                                    goto gm_ll562;
                                }
                                gm_l63 = t583;
                            }
                            {
                                void * t584 = (void *)((uint8_t *)&F.l58);
                                void * t585 = gm_l0;
                                void * t586 = gm_l63;
                                gm_t88rcucg1b7uk t587 = gm_l56;
                                void * t588 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t585, t586, t587);
                                if (gm_unw.kind) {
                                    goto gm_ll562;
                                }
                                gm_tftb1odc739t5 t589 = gm_f_push_d943vgp_2(t584, &(*(gm_t69m56meqefs8 *)((uint8_t *)t588)));
                                if (((((uint8_t *)&t589))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t589) + 1, gm_srclfnv05vuf0b8, 103, 17);
                                gm_b0_1 t590 = GM_LD(gm_b0_1, (((uint8_t *)&t589) + 1));
                            }
                            {
                                void * t591 = (void *)((uint8_t *)&F.l59);
                                gm_t88rcucg1b7uk t592 = gm_l56;
                                gm_tftb1odc739t5 t593 = gm_f_push_k3l9nic(t591, GM_CONV(gm_csguankko4lmhd, t592));
                                if (((((uint8_t *)&t593))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t593) + 1, gm_srclfnv05vuf0b8, 104, 17);
                                gm_b0_1 t594 = GM_LD(gm_b0_1, (((uint8_t *)&t593) + 1));
                            }
                            goto me565;
mn566:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 101, 13);
me565:;
                        }
lc558:;
                        {
                            uint64_t t596 = gm_l61;
                            uint64_t r597; bool o598 = __builtin_add_overflow(t596, ((uint64_t)1ULL), &r597);
                            uint64_t t599 = r597;
                            gm_l61 = t599;
                        }
                    }
gm_ll562:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb558; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc558; }
                    }
                    goto gm_lad;
lb558:;
                }
            }
            {
                void * t600 = gm_l5;
                gm_b16_8 t601 = GM_LD(gm_b16_8, (((uint8_t *)&F.l59) + 0));
                gm_b16_8 t602 = GM_LD(gm_b16_8, ((uint8_t *)&t601));
                gm_t88rcucg1b7uk t603 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t600, t602);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l64 = t603;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l65, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t604 = (void *)((uint8_t *)&F.l65);
                uint32_t t605 = gm_l57;
                void * t606 = gm_l37;
                gm_ttggq7ng4kp3p t607;
                GM_ST(uint32_t, (uint8_t *)&t607 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t607 + 0), t605);
                GM_ST(void *, ((uint8_t *)&t607 + 8), t606);
                gm_trcmmd12cefb1 t608;
                GM_ST(uint8_t, (uint8_t *)&t608 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t608 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t608 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t608 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t608 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t608 + 8), t607);
                gm_tftb1odc739t5 t609 = gm_f_push_d6c76ud_2(t604, &t608);
                if (((((uint8_t *)&t609))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t609) + 1, gm_srclfnv05vuf0b8, 108, 13);
                gm_b0_1 t610 = GM_LD(gm_b0_1, (((uint8_t *)&t609) + 1));
            }
            {
                gm_t88rcucg1b7uk t611 = gm_l64;
                gm_b16_8 t612 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 0));
                gm_teti411eujvf4 t613;
                memset(&t613, 0, sizeof t613);
                GM_ST(uint8_t, ((uint8_t *)&t613 + 0), ((uint8_t)35ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t613 + 8), t612);
                gm_tl874t2atnlh3 t614 = gm_l4;
                void * t615 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t611, &t613, t614);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l66 = t615;
            }
            {
                gm_t88rcucg1b7uk t616 = gm_l64;
                gm_b16_8 t617 = GM_LD(gm_b16_8, (((uint8_t *)&F.l65) + 0));
                void * t618 = gm_l66;
                gm_tl874t2atnlh3 t619 = gm_l4;
                void * t620 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t616, t617, t618, t619);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t620;
            }
            goto ix548;
ie548:;
ix548:;
        }
        goto ix463;
ie463:;
ix463:;
    }
    {
        void * t621 = gm_l0;
        void * t622 = (void *)(((uint8_t *)t621) + 96);
        void * t623 = gm_l0;
        void * t624 = GM_LD(void *, (((uint8_t *)t623) + 0));
        void * t625 = gm_l6;
        gm_t88rcucg1b7uk t626 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t625) + 0));
        gm_t88rcucg1b7uk t627 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t622, t624, t626);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l67 = t627;
    }
    {
        void * t628 = gm_l0;
        void * t629 = (void *)(((uint8_t *)t628) + 96);
        void * t630 = gm_l0;
        void * t631 = GM_LD(void *, (((uint8_t *)t630) + 0));
        gm_t88rcucg1b7uk t632 = gm_l44;
        gm_t88rcucg1b7uk t633 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t629, t631, t632);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l68 = t633;
    }
    {
        gm_t88rcucg1b7uk t635 = gm_l67;
        gm_t88rcucg1b7uk t636 = gm_l68;
        uint8_t t637 = (!gm_eq9m6mvtmgde(((uint8_t *)&t635), ((uint8_t *)&t636)));
        if (!t637) goto ie634;
        void * t638 = gm_l5;
        gm_t88rcucg1b7uk t639 = gm_l67;
        uint8_t t640 = gm_f_has_infer_3o82a0b(t638, GM_CONV(gm_csguankko4lmhd, t639));
        uint8_t t641 = (uint8_t)!t640;
        if (!t641) goto ie634;
        void * t642 = gm_l5;
        gm_t88rcucg1b7uk t643 = gm_l68;
        uint8_t t644 = gm_f_has_infer_3o82a0b(t642, GM_CONV(gm_csguankko4lmhd, t643));
        uint8_t t645 = (uint8_t)!t644;
        if (!t645) goto ie634;
        void * t646 = gm_l0;
        gm_t88rcucg1b7uk t647 = gm_l67;
        gm_t88rcucg1b7uk t648 = gm_l68;
        gm_th2lqbo77pvsc t649;
        gm_f_coercion_steps_ibqvhta((void *)&t649, (void *)NULL, cx, NULL, t646, t647, t648);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t650;
        if (((((uint8_t *)&t649))[16] != 0)) goto mn652;
        t650 = ((uint8_t)0);
        goto me651;
mn652:;
        t650 = ((uint8_t)1);
        goto me651;
mn653:;
        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 117, 77);
me651:;
        if (!t650) goto ie634;
        {
            void * t655 = gm_l0;
            void * t656 = gm_l6;
            gm_t88rcucg1b7uk t657 = gm_l68;
            void * t658 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t655, t656, t657);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t658;
        }
        goto ix634;
ie634:;
ix634:;
    }
    {
        {
            void * t660 = gm_l0;
            void * t661 = gm_l6;
            gm_t88rcucg1b7uk t662 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t661) + 0));
            gm_t88rcucg1b7uk t663 = gm_l44;
            gm_tl874t2atnlh3 t664 = gm_l4;
            gm_b16_8 t665;
            GM_ST(void *, (uint8_t *)&t665, (void *)(gm_sa4nk3mrdf2d0));
            GM_ST(uintptr_t, (uint8_t *)&t665 + 8, (uintptr_t)(35));
            gm_t88rcucg1b7uk t666 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t660, t662, t663, t664, t665);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t667 = gm_l0;
        void * t668 = (void *)(((uint8_t *)t667) + 96);
        void * t669 = gm_l0;
        void * t670 = GM_LD(void *, (((uint8_t *)t669) + 0));
        gm_t88rcucg1b7uk t671 = gm_l44;
        gm_t88rcucg1b7uk t672 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t668, t670, t671);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l69 = t672;
    }
    {
        void * t674 = gm_l5;
        gm_t88rcucg1b7uk t675 = gm_l69;
        uint8_t t676 = gm_f_has_infer_3o82a0b(t674, GM_CONV(gm_csguankko4lmhd, t675));
        uint8_t t677 = (uint8_t)!t676;
        if (!t677) goto ie673;
        void * t678 = gm_l5;
        gm_t88rcucg1b7uk t679 = gm_l69;
        uint8_t t680 = gm_f_is_error_8k27gd1(t678, GM_CONV(gm_csguankko4lmhd, t679));
        uint8_t t681 = (uint8_t)!t680;
        if (!t681) goto ie673;
        void * t682 = gm_l0;
        gm_t88rcucg1b7uk t683 = gm_l69;
        uint8_t t684 = gm_f_eq_allowed_d841u9b((void *)NULL, cx, NULL, t682, t683);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t685 = (uint8_t)!t684;
        if (!t685) goto ie673;
        {
            void * t687 = gm_l0;
            gm_tl874t2atnlh3 t688 = gm_l4;
            void * t689 = gm_l0;
            gm_t88rcucg1b7uk t690 = gm_l69;
            gm_b16_8 t691 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t689, t690);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t692;
            GM_ST(gm_b16_8, ((uint8_t *)&t692 + 0), t691);
            gm_tabb710d9mq2e t693;
            gm_f_format_2bpp75h((void *)&t693, (void *)NULL, cx, NULL, t692);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t693))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t693) + 8, gm_srclfnv05vuf0b8, 124, 24);
            gm_b16_8 t694 = GM_LD(gm_b16_8, (((uint8_t *)&t693) + 8));
            gm_b16_8 t695 = GM_LD(gm_b16_8, ((uint8_t *)&t694));
            gm_b0_1 t696 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t687, t688, t695);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix673;
ie673:;
ix673:;
    }
    {
        void * t697 = gm_l5;
        gm_t88rcucg1b7uk t698 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t697);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlcl4u558l6p5 t699;
        memset((uint8_t *)&t699 + 17, 0, 7);
        void * t700 = gm_l6;
        GM_ST(void *, (uint8_t *)&t699 + 0, t700);
        void * t701 = gm_l37;
        GM_ST(void *, (uint8_t *)&t699 + 8, t701);
        uint8_t t702 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&t699 + 16, t702);
        gm_teti411eujvf4 t703;
        memset(&t703, 0, sizeof t703);
        GM_ST(uint8_t, ((uint8_t *)&t703 + 0), ((uint8_t)45ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t703 + 8), t699);
        gm_tl874t2atnlh3 t704 = gm_l4;
        void * t705 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t698, &t703, t704);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t706 = 0;
        F.ret = t705;
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
inline __attribute__((aligned(4))) void gm_f_increment_12n73t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_increment_12n73t5 F;
    uint32_t gm_l0; /* i */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* cur */
    void * gm_l4; /* next */
    GM_STACK_CHECK(&F, 503u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_t88rcucg1b7uk t2 = gm_l1;
        uint32_t t3 = gm_l0;
        gm_teti411eujvf4 t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t3);
        gm_tl874t2atnlh3 t5 = gm_l2;
        void * t6 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t6;
    }
    {
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_trl9d7m1k5lbd t8;
        memset((uint8_t *)&t8 + 2, 0, 6);
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
        gm_ts6oqppjh0ba3 t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 1), t9);
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t8 + 0, t10);
        gm_t88rcucg1b7uk t11 = gm_l1;
        uint32_t t12 = gm_l0;
        gm_teti411eujvf4 t13;
        memset(&t13, 0, sizeof t13);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t13 + 8), t12);
        gm_tl874t2atnlh3 t14 = gm_l2;
        void * t15 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t11, &t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t8 + 8, t15);
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_tbci5ospk5nnn t17;
        gm_f_one_08qg1op((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t18 + 8), t17);
        gm_tl874t2atnlh3 t19 = gm_l2;
        void * t20 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t16, &t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t8 + 16, t20);
        gm_teti411eujvf4 t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)30ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t21 + 8), t8);
        gm_tl874t2atnlh3 t22 = gm_l2;
        void * t23 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t7, &t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t23;
    }
    {
        void * t24 = gm_l3;
        void * t25 = gm_l4;
        gm_t3mf58gv9kf3n t26;
        GM_ST(void *, ((uint8_t *)&t26 + 0), t24);
        GM_ST(void *, ((uint8_t *)&t26 + 8), t25);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 0, ((uint8_t)2ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trcmmd12cefb1 *)gm_out) + 8, t26);
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_members_97aedtg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_join_members_97aedtg F;
    void * gm_l0; /* cx */
    void * gm_l1; /* elem */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l7; /* m */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* joined */
    gm_t88rcucg1b7uk gm_l10; /* jr */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_t88rcucg1b7uk gm_l14; /* r */
    GM_STACK_CHECK(&F, 724u);
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
        gm_f_list_nof7vfj((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t6 = (void *)((uint8_t *)&F.l4);
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        gm_tftb1odc739t5 t9 = gm_f_push_k3l9nic(t6, GM_CONV(gm_csguankko4lmhd, t8));
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srclfnv05vuf0b8, 136, 5);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    {
        {
            gm_b16_8 t12 = gm_l2;
            gm_l5 = t12;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l6;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l6;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    void * t22 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t20 * 8));
                    void * t25 = GM_LD(void *, ((uint8_t *)&t22));
                    gm_l7 = t25;
                    {
                        void * t27 = gm_l0;
                        void * t28 = (void *)(((uint8_t *)t27) + 96);
                        void * t29 = gm_l0;
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                        void * t31 = gm_l7;
                        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 0));
                        gm_t88rcucg1b7uk t33 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t28, t30, t32);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        gm_l8 = t33;
                    }
                    {
                        uint8_t t35 = 0;
                        void * t36 = gm_l3;
                        gm_t88rcucg1b7uk t37 = gm_l8;
                        uint8_t t38 = gm_f_has_infer_3o82a0b(t36, GM_CONV(gm_csguankko4lmhd, t37));
                        if (!t38) {
                            void * t39 = gm_l3;
                            gm_t88rcucg1b7uk t40 = gm_l8;
                            uint8_t t41 = gm_f_is_error_8k27gd1(t39, GM_CONV(gm_csguankko4lmhd, t40));
                            t35 = t41;
                        } else t35 = 1;
                        if (!t35) goto ie34;
                        {
                            void * t43 = gm_l1;
                            gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t43) + 0));
                            uint8_t t45 = 0;
                            F.ret = t44;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t45;
                            goto gm_ll17;
                        }
                        goto ix34;
ie34:;
ix34:;
                    }
                    {
                        void * t46 = (void *)((uint8_t *)&F.l4);
                        gm_t88rcucg1b7uk t47 = gm_l8;
                        gm_tftb1odc739t5 t48 = gm_f_push_k3l9nic(t46, GM_CONV(gm_csguankko4lmhd, t47));
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srclfnv05vuf0b8, 140, 9);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 137, 5);
me23:;
                }
lc13:;
                {
                    uint64_t t51 = gm_l6;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l6 = t54;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_ld1_1;
lb13:;
        }
    }
    {
        void * t56 = gm_l3;
        void * t57 = gm_l0;
        void * t58 = (void *)(((uint8_t *)t57) + 96);
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
        void * t61 = gm_l1;
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t61) + 0));
        gm_t88rcucg1b7uk t63 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t58, t60, t62);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t64 = gm_f_has_infer_3o82a0b(t56, GM_CONV(gm_csguankko4lmhd, t63));
        if (!t64) goto ie55;
        {
            void * t66 = gm_l1;
            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t66) + 0));
            uint8_t t68 = 0;
            F.ret = t67;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t68;
            goto gm_ld1_1;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        void * t69 = gm_l0;
        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
        gm_t88rcucg1b7uk t72 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t69, t71);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l9 = t72;
    }
    {
        void * t73 = gm_l0;
        void * t74 = (void *)(((uint8_t *)t73) + 96);
        void * t75 = gm_l0;
        void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
        gm_t88rcucg1b7uk t77 = gm_l9;
        gm_t88rcucg1b7uk t78 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t74, t76, t77);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l10 = t78;
    }
    {
        {
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l11 = t80;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t82 = gm_l12;
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l12;
                    uint64_t t88 = (uintptr_t)t87;
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t88 * 4));
                    gm_l13 = t92;
                    {
                        void * t94 = gm_l0;
                        void * t95 = (void *)(((uint8_t *)t94) + 96);
                        void * t96 = gm_l0;
                        void * t97 = GM_LD(void *, (((uint8_t *)t96) + 0));
                        gm_t88rcucg1b7uk t98 = gm_l13;
                        gm_t88rcucg1b7uk t99 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t95, t97, t98);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                        gm_l14 = t99;
                    }
                    {
                        gm_t88rcucg1b7uk t101 = gm_l14;
                        gm_t88rcucg1b7uk t102 = gm_l10;
                        uint8_t t103 = (!gm_eq9m6mvtmgde(((uint8_t *)&t101), ((uint8_t *)&t102)));
                        if (!t103) goto ie100;
                        void * t104 = gm_l0;
                        gm_t88rcucg1b7uk t105 = gm_l14;
                        gm_t88rcucg1b7uk t106 = gm_l10;
                        gm_th2lqbo77pvsc t107;
                        gm_f_coercion_steps_ibqvhta((void *)&t107, (void *)NULL, cx, NULL, t104, t105, t106);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                        uint8_t t108;
                        if (((((uint8_t *)&t107))[16] != 0)) goto mn110;
                        t108 = ((uint8_t)1);
                        goto me109;
mn110:;
                        t108 = ((uint8_t)0);
                        goto me109;
mn111:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 148, 28);
me109:;
                        if (!t108) goto ie100;
                        {
                            void * t113 = gm_l1;
                            gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t113) + 0));
                            uint8_t t115 = 0;
                            F.ret = t114;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t115;
                            goto gm_ll85;
                        }
                        goto ix100;
ie100:;
ix100:;
                    }
                    goto me90;
mn91:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 146, 5);
me90:;
                }
lc81:;
                {
                    uint64_t t117 = gm_l12;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l12 = t120;
                }
            }
gm_ll85:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
            }
            goto gm_ld1_1;
lb81:;
        }
    }
    {
        gm_t88rcucg1b7uk t121 = gm_l9;
        uint8_t t122 = 0;
        F.ret = t121;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t122;
        goto gm_ld1_1;
    }
    {
        void * t123 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_q21h982(t123);
    }
    goto gm_ls124;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t125 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_q21h982(t125);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls124:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_local_i91hglv(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) void * gm_f_local_l5o3d47(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_loop_with_pattern_d8vl0mj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, void * a3, gm_b16_8 a4, void * a5) {
    struct gm_fr_loop_with_pattern_d8vl0mj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    void * gm_l3; /* item */
    gm_b16_8 gm_l4; /* cond */
    void * gm_l5; /* step */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_t08olq97rscn3 gm_l7; /* l */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_tl874t2atnlh3 gm_l13; /* sp */
    void * gm_l15; /* pat */
    gm_th2lqbo77pvsc gm_l16; /* label_name */
    gm_t08olq97rscn3 gm_l17; /* l */
    uint32_t gm_l18; /* id */
    void * gm_l19; /* body */
    void * gm_l21; /* m */
    void * gm_l23; /* lb */
    GM_STACK_CHECK(&F, 2155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
        gm_l6 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b0_1 t5 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l1;
        if (!(((((uint8_t *)t7) + 0))[32] != 0)) goto ie6;
        gm_t08olq97rscn3 t8 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t7) + 0));
        gm_l7 = t8;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_t930q5l38hdap t12;
            memset(&t12, 0, sizeof t12);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)2ULL));
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
            gm_b0_1 t14 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t10, t11, &t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
        void * t16 = gm_l1;
        uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 40) + 8);
        if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srclfnv05vuf0b8, 278, 16);
        gm_t0d02142k4tu8 t18 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t16) + 40)) + t15 * 64));
        F.l8 = t18;
    }
    {
        void * t20 = gm_l1;
        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 40) + 8);
        uint8_t t22 = (!(t21 == ((uint64_t)1ULL)));
        if (!t22) goto ie19;
        {
            gm_f_list_ttksl9g((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t25 = gm_l1;
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 40));
                gm_l10 = t26;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t28 = gm_l11;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t30 = (t28 < t29);
                    if (!t30) goto lb27;
                    {
                        uint64_t t33 = gm_l11;
                        uint64_t t34 = (uintptr_t)t33;
                        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t0d02142k4tu8 t38 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t34 * 64));
                        F.l12 = t38;
                        {
                            void * t40 = (void *)((uint8_t *)&F.l9);
                            gm_tnrgict8sthcv t41;
                            memset((uint8_t *)&t41 + 116, 0, 4);
                            gm_t1s75i08qtptd t42;
                            memset(&t42, 0, sizeof t42);
                            GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t41 + 0, t42);
                            memcpy((uint8_t *)&t41 + 40, ((uint8_t *)&F.l12), 64);
                            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 48));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t41 + 104, t43);
                            gm_tftb1odc739t5 t44 = gm_f_push_l74t9rq(t40, &t41);
                            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srclfnv05vuf0b8, 281, 27);
                            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 281, 9);
me36:;
                    }
lc27:;
                    {
                        uint64_t t47 = gm_l11;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                        uint64_t t50 = r48;
                        gm_l11 = t50;
                    }
                }
lb27:;
            }
        }
        {
            uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
            void * t52 = gm_l1;
            uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)t52) + 40) + 8);
            if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_srclfnv05vuf0b8, 282, 25);
            gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t52) + 40)) + t51 * 64) + 48));
            void * t55 = gm_l1;
            uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)t55) + 40) + 8);
            uint64_t r57; bool o58 = __builtin_sub_overflow(t56, ((uint64_t)1ULL), &r57);
            if (o58) gm_trap("integer overflow in `-`", gm_srclfnv05vuf0b8, 282, 48);
            uint64_t t59 = r57;
            uint64_t t60 = (uintptr_t)t59;
            void * t61 = gm_l1;
            uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)t61) + 40) + 8);
            if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_srclfnv05vuf0b8, 282, 41);
            gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t61) + 40)) + t60 * 64) + 48));
            gm_tl874t2atnlh3 t64 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t54), GM_CONV(gm_cseahvg8v7p0vs, t63)));
            gm_l13 = t64;
        }
        {
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 60, 0, 4);
            uint64_t t65 = (uintptr_t)((uint64_t)0ULL);
            void * t66 = gm_l1;
            uint64_t t67 = GM_LD(uintptr_t, (((uint8_t *)t66) + 40) + 8);
            if (t65 >= (uintptr_t)(t67)) gm_trap_index(t65, t67, gm_srclfnv05vuf0b8, 283, 35);
            uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t66) + 40)) + t65 * 64) + 0));
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_to361lgknsu69 t70;
            GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t70 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)8ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t70 + 8), t69);
            gm_tl874t2atnlh3 t71 = gm_l13;
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 0, t68);
            GM_ST(gm_to361lgknsu69, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 8, t70);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 48, t71);
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_f_list_6l26q4a((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t72 = gm_l0;
        void * t73 = (void *)((uint8_t *)&F.l8);
        gm_t88rcucg1b7uk t74 = gm_l2;
        gm_t9qkh0rsooc8d t75;
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)1ULL));
        void * t76 = (void *)((uint8_t *)&F.l14);
        void * t77 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t72, t73, t74, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t77;
    }
    {
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
        void * t81 = (void *)(((uint8_t *)t80) + 8);
        gm_t88rcucg1b7uk t82 = gm_l2;
        uint8_t t83 = gm_f_is_error_8k27gd1(t81, GM_CONV(gm_csguankko4lmhd, t82));
        uint8_t t84 = (uint8_t)!t83;
        if (!t84) goto ie78;
        void * t85 = gm_l0;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        void * t87 = gm_l15;
        uint8_t t88 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t86, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t89 = (uint8_t)!t88;
        if (!t89) goto ie78;
        {
            void * t91 = gm_l0;
            gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 48));
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_so4k586t2v79t));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(63));
            gm_b0_1 t94 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t91, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        void * t95 = gm_l0;
        void * t96 = (void *)((uint8_t *)&F.l14);
        gm_b0_1 t97 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t98;
        memset(&t98, 0, sizeof t98);
        gm_l16 = t98;
    }
    {
        void * t100 = gm_l1;
        if (!(((((uint8_t *)t100) + 0))[32] != 0)) goto ie99;
        gm_t08olq97rscn3 t101 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t100) + 0));
        gm_l17 = t101;
        {
            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l16)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l16)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l16)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l16)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l16)) + 0, t103);
        }
        goto ix99;
ie99:;
ix99:;
    }
    {
        void * t104 = gm_l0;
        gm_th2lqbo77pvsc t105 = gm_l16;
        void * t106 = gm_l0;
        void * t107 = GM_LD(void *, (((uint8_t *)t106) + 0));
        void * t108 = (void *)(((uint8_t *)t107) + 8);
        gm_t88rcucg1b7uk t109 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1qr0p6kln6tf t110;
        GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t110 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t110 + 4), t109);
        uint32_t t111 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t104, &t105, ((uint8_t)1), t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t111;
    }
    {
        void * t112 = gm_l0;
        void * t113 = gm_l1;
        void * t114 = (void *)(((uint8_t *)t113) + 64);
        void * t115 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t112, t114, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t115;
    }
    {
        {
            void * t117 = gm_l0;
            void * t118 = (void *)(((uint8_t *)t117) + 232);
            gm_te5gbhu3us3k4 t119;
            gm_f_pop_ruvva9t((void *)&t119, t118);
        }
    }
    {
        {
            void * t121 = gm_l0;
            uint32_t t122 = gm_l18;
            uint8_t t123 = gm_f_forget_label_breaks_vl4mn72(t121, t122);
        }
    }
    {
        void * t124 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t124);
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t125 = (void *)((uint8_t *)&F.l20);
        gm_tbbqefuhv2r8h t126;
        void * t127 = gm_l15;
        GM_ST(void *, (uint8_t *)&t126 + 0, t127);
        GM_ST(void *, (uint8_t *)&t126 + 8, ((void *)0));
        void * t128 = gm_l19;
        GM_ST(void *, (uint8_t *)&t126 + 16, t128);
        gm_tftb1odc739t5 t129 = gm_f_push_bpovr2k_3(t125, &t126);
        if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srclfnv05vuf0b8, 299, 5);
        gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
    }
    {
        void * t131 = gm_l0;
        void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
        void * t133 = (void *)(((uint8_t *)t132) + 8);
        gm_t88rcucg1b7uk t134 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t133);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkaapcii86udk t135;
        void * t136 = gm_l3;
        GM_ST(void *, (uint8_t *)&t135 + 0, t136);
        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 8, t137);
        gm_teti411eujvf4 t138;
        memset(&t138, 0, sizeof t138);
        GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t138 + 8), t135);
        gm_tl874t2atnlh3 t139 = gm_l6;
        void * t140 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t134, &t138, t139);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l21 = t140;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t141 = (void *)((uint8_t *)&F.l22);
        void * t142 = gm_l21;
        gm_trcmmd12cefb1 t143;
        GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t143 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t143 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t143 + 8), t142);
        gm_tftb1odc739t5 t144 = gm_f_push_d6c76ud_2(t141, &t143);
        if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srclfnv05vuf0b8, 302, 5);
        gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
    }
    {
        gm_tgabaikr08mjg t146;
        memset((uint8_t *)&t146 + 36, 0, 4);
        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t146 + 0, t147);
        GM_ST(void *, (uint8_t *)&t146 + 16, ((void *)0));
        gm_tl874t2atnlh3 t148 = gm_l6;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t146 + 24, t148);
        gm_tq7s98ihonkju t149 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t146, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0nv110r163)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 8, gm_srclfnv05vuf0b8, 303, 11);
        void * t150 = GM_LD(void *, (((uint8_t *)&t149) + 8));
        gm_l23 = t150;
    }
    {
        void * t151 = gm_l0;
        void * t152 = GM_LD(void *, (((uint8_t *)t151) + 0));
        void * t153 = (void *)(((uint8_t *)t152) + 8);
        gm_t88rcucg1b7uk t154 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t153);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t710lsrq1m8gk t155;
        memset((uint8_t *)&t155 + 4, 0, 4);
        uint32_t t156 = gm_l18;
        GM_ST(uint32_t, (uint8_t *)&t155 + 0, t156);
        gm_b16_8 t157 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t155 + 8, t157);
        void * t158 = gm_l23;
        GM_ST(void *, (uint8_t *)&t155 + 24, t158);
        void * t159 = gm_l5;
        GM_ST(void *, (uint8_t *)&t155 + 32, t159);
        gm_teti411eujvf4 t160;
        GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t160 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t160 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)48ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t160 + 8), t155);
        gm_tl874t2atnlh3 t161 = gm_l6;
        void * t162 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t154, &t160, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t163 = 0;
        F.ret = t162;
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
__attribute__((aligned(4))) void * gm_f_range_loop_44fv3au(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_t88rcucg1b7uk a4, uint8_t a5) {
    struct gm_fr_range_loop_44fv3au F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* lo */
    void * gm_l3; /* hi */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    uint8_t gm_l5; /* inclusive */
    gm_t88rcucg1b7uk gm_l7; /* b */
    uint32_t gm_l8; /* hi_t */
    uint32_t gm_l9; /* i */
    gm_closure gm_l10; /* local */
    uint32_t gm_l12; /* going */
    void * gm_l13; /* at_end */
    void * gm_l17; /* step */
    void * gm_l19; /* lp */
    GM_STACK_CHECK(&F, 2185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
        F.l6 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l4;
        gm_tl874t2atnlh3 t10 = F.l6;
        uint32_t t11 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t8, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t11;
    }
    {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = gm_l4;
        gm_tl874t2atnlh3 t14 = F.l6;
        uint32_t t15 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t15;
    }
    {
        gm_closure t16 = ((gm_closure){(void *)gm_f_local_i91hglv, (void *)&F.h, cx});
        gm_l10 = t16;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = (void *)((uint8_t *)&F.l11);
        uint32_t t18 = gm_l9;
        void * t19 = gm_l2;
        gm_ttggq7ng4kp3p t20;
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t20 + 0), t18);
        GM_ST(void *, ((uint8_t *)&t20 + 8), t19);
        gm_trcmmd12cefb1 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t21 + 8), t20);
        gm_tftb1odc739t5 t22 = gm_f_push_d6c76ud_2(t17, &t21);
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srclfnv05vuf0b8, 230, 5);
        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l11);
        uint32_t t25 = gm_l8;
        void * t26 = gm_l3;
        gm_ttggq7ng4kp3p t27;
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t25);
        GM_ST(void *, ((uint8_t *)&t27 + 8), t26);
        gm_trcmmd12cefb1 t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t28 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t28 + 8), t27);
        gm_tftb1odc739t5 t29 = gm_f_push_d6c76ud_2(t24, &t28);
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srclfnv05vuf0b8, 231, 5);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
    }
    {
        uint8_t t32 = gm_l5;
        uint8_t t33 = (uint8_t)!t32;
        if (!t33) goto ie31;
        {
            void * t35 = (void *)((uint8_t *)&F.l11);
            void * t36 = gm_l0;
            void * t37 = gm_l1;
            gm_t88rcucg1b7uk t38 = gm_l4;
            uint32_t t39 = gm_l9;
            gm_t88rcucg1b7uk t40 = gm_l4;
            gm_closure t41 = gm_l10;
            uint32_t t42 = gm_l8;
            gm_t88rcucg1b7uk t43 = gm_l4;
            gm_closure t44 = t41;
            gm_node *ccx45 = t44.cx;
            gm_node cn46; { gm_node *f = gm_find(cx, 587034433); if (f) { cn46 = *f; cn46.next = ccx45; ccx45 = &cn46; } }
            void * t47 = t44.code;
            void * t48;
            if (GM_PTR_TAGGED(t47)) {
                t48 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t47))(t42, GM_CONV(gm_csguankko4lmhd, t43));
            } else {
                t48 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t47)(t44.env, ccx45, NULL, t42, t43);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_closure t49 = gm_l10;
            uint32_t t50 = gm_l9;
            gm_t88rcucg1b7uk t51 = gm_l4;
            gm_closure t52 = t49;
            gm_node *ccx53 = t52.cx;
            gm_node cn54; { gm_node *f = gm_find(cx, 587034433); if (f) { cn54 = *f; cn54.next = ccx53; ccx53 = &cn54; } }
            void * t55 = t52.code;
            void * t56;
            if (GM_PTR_TAGGED(t55)) {
                t56 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t55))(t50, GM_CONV(gm_csguankko4lmhd, t51));
            } else {
                t56 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t55)(t52.env, ccx53, NULL, t50, t51);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t57 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t36, t37, t38, t39, t40, t48, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t58 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t58 + 8), t57);
            gm_tftb1odc739t5 t59 = gm_f_push_d6c76ud_2(t35, &t58);
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srclfnv05vuf0b8, 233, 9);
            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
        }
        {
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, (((uint8_t *)t61) + 0));
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            gm_tl874t2atnlh3 t64 = F.l6;
            void * t65 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t62, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t67 = gm_l0;
        gm_t88rcucg1b7uk t68 = gm_l7;
        gm_tl874t2atnlh3 t69 = F.l6;
        uint32_t t70 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t67, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t70;
    }
    {
        void * t71 = (void *)((uint8_t *)&F.l11);
        uint32_t t72 = gm_l12;
        gm_t88rcucg1b7uk t73 = gm_l7;
        gm_trl9d7m1k5lbd t74;
        memset((uint8_t *)&t74 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)11ULL));
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t74 + 0, t75);
        gm_closure t76 = gm_l10;
        uint32_t t77 = gm_l9;
        gm_t88rcucg1b7uk t78 = gm_l4;
        gm_closure t79 = t76;
        gm_node *ccx80 = t79.cx;
        gm_node cn81; { gm_node *f = gm_find(cx, 587034433); if (f) { cn81 = *f; cn81.next = ccx80; ccx80 = &cn81; } }
        void * t82 = t79.code;
        void * t83;
        if (GM_PTR_TAGGED(t82)) {
            t83 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t82))(t77, GM_CONV(gm_csguankko4lmhd, t78));
        } else {
            t83 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t82)(t79.env, ccx80, NULL, t77, t78);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t74 + 8, t83);
        gm_closure t84 = gm_l10;
        uint32_t t85 = gm_l8;
        gm_t88rcucg1b7uk t86 = gm_l4;
        gm_closure t87 = t84;
        gm_node *ccx88 = t87.cx;
        gm_node cn89; { gm_node *f = gm_find(cx, 587034433); if (f) { cn89 = *f; cn89.next = ccx88; ccx88 = &cn89; } }
        void * t90 = t87.code;
        void * t91;
        if (GM_PTR_TAGGED(t90)) {
            t91 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t90))(t85, GM_CONV(gm_csguankko4lmhd, t86));
        } else {
            t91 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t90)(t87.env, ccx88, NULL, t85, t86);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t74 + 16, t91);
        gm_teti411eujvf4 t92;
        memset(&t92, 0, sizeof t92);
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)30ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t92 + 8), t74);
        gm_tl874t2atnlh3 t93 = F.l6;
        void * t94 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t73, &t92, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t95;
        GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t95 + 0), t72);
        GM_ST(void *, ((uint8_t *)&t95 + 8), t94);
        gm_trcmmd12cefb1 t96;
        GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t96 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t96 + 8), t95);
        gm_tftb1odc739t5 t97 = gm_f_push_d6c76ud_2(t71, &t96);
        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srclfnv05vuf0b8, 237, 5);
        gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
    }
    {
        gm_t88rcucg1b7uk t99 = gm_l7;
        gm_tlcl4u558l6p5 t100;
        memset((uint8_t *)&t100 + 17, 0, 7);
        gm_closure t101 = gm_l10;
        uint32_t t102 = gm_l9;
        gm_t88rcucg1b7uk t103 = gm_l4;
        gm_closure t104 = t101;
        gm_node *ccx105 = t104.cx;
        gm_node cn106; { gm_node *f = gm_find(cx, 587034433); if (f) { cn106 = *f; cn106.next = ccx105; ccx105 = &cn106; } }
        void * t107 = t104.code;
        void * t108;
        if (GM_PTR_TAGGED(t107)) {
            t108 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t107))(t102, GM_CONV(gm_csguankko4lmhd, t103));
        } else {
            t108 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t107)(t104.env, ccx105, NULL, t102, t103);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t100 + 0, t108);
        gm_closure t109 = gm_l10;
        uint32_t t110 = gm_l8;
        gm_t88rcucg1b7uk t111 = gm_l4;
        gm_closure t112 = t109;
        gm_node *ccx113 = t112.cx;
        gm_node cn114; { gm_node *f = gm_find(cx, 587034433); if (f) { cn114 = *f; cn114.next = ccx113; ccx113 = &cn114; } }
        void * t115 = t112.code;
        void * t116;
        if (GM_PTR_TAGGED(t115)) {
            t116 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t115))(t110, GM_CONV(gm_csguankko4lmhd, t111));
        } else {
            t116 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t115)(t112.env, ccx113, NULL, t110, t111);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t100 + 8, t116);
        GM_ST(uint8_t, (uint8_t *)&t100 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t117;
        memset(&t117, 0, sizeof t117);
        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)43ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t117 + 8), t100);
        gm_tl874t2atnlh3 t118 = F.l6;
        void * t119 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t99, &t117, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t119;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t120 = (void *)((uint8_t *)&F.l14);
        gm_closure t121 = gm_l10;
        uint32_t t122 = gm_l12;
        gm_t88rcucg1b7uk t123 = gm_l7;
        gm_closure t124 = t121;
        gm_node *ccx125 = t124.cx;
        gm_node cn126; { gm_node *f = gm_find(cx, 587034433); if (f) { cn126 = *f; cn126.next = ccx125; ccx125 = &cn126; } }
        void * t127 = t124.code;
        void * t128;
        if (GM_PTR_TAGGED(t127)) {
            t128 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t127))(t122, GM_CONV(gm_csguankko4lmhd, t123));
        } else {
            t128 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t127)(t124.env, ccx125, NULL, t122, t123);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t129 = gm_l7;
        gm_teti411eujvf4 t130;
        memset(&t130, 0, sizeof t130);
        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t130 + 8), ((uint8_t)0));
        gm_tl874t2atnlh3 t131 = F.l6;
        void * t132 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t129, &t130, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t133;
        GM_ST(void *, ((uint8_t *)&t133 + 0), t128);
        GM_ST(void *, ((uint8_t *)&t133 + 8), t132);
        gm_trcmmd12cefb1 t134;
        GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t134 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t134 + 8), t133);
        gm_tftb1odc739t5 t135 = gm_f_push_d6c76ud_2(t120, &t134);
        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srclfnv05vuf0b8, 240, 5);
        gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t137 = (void *)((uint8_t *)&F.l15);
        uint32_t t138 = gm_l9;
        gm_t88rcucg1b7uk t139 = gm_l4;
        gm_tl874t2atnlh3 t140 = F.l6;
        gm_trcmmd12cefb1 t141;
        gm_f_increment_12n73t5((void *)&t141, (void *)NULL, cx, NULL, t138, t139, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t142 = gm_f_push_d6c76ud_2(t137, &t141);
        if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srclfnv05vuf0b8, 242, 5);
        gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
    }
    {
        gm_f_list_v5d1o9b_2((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t144 = (void *)((uint8_t *)&F.l16);
        void * t145 = gm_l13;
        gm_tlr9dp305htm7 t146;
        GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t146 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t146 + 8), t145);
        gm_tftb1odc739t5 t147 = gm_f_push_eb7kbhp_2(t144, &t146);
        if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_srclfnv05vuf0b8, 244, 5);
        gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
    }
    {
        void * t149 = gm_l0;
        void * t150 = GM_LD(void *, (((uint8_t *)t149) + 0));
        void * t151 = (void *)(((uint8_t *)t150) + 8);
        gm_t88rcucg1b7uk t152 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trl2khmuphncd t153;
        gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t153 + 0, t154);
        void * t155 = gm_l0;
        void * t156 = GM_LD(void *, (((uint8_t *)t155) + 0));
        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_tl874t2atnlh3 t158 = F.l6;
        void * t159 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t156, t157, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t153 + 16, t159);
        void * t160 = gm_l0;
        void * t161 = GM_LD(void *, (((uint8_t *)t160) + 0));
        gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        gm_tl874t2atnlh3 t163 = F.l6;
        void * t164 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t161, t162, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t153 + 24, t164);
        gm_teti411eujvf4 t165;
        GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t165 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t165 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t165 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)47ULL));
        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t165 + 8), t153);
        gm_tl874t2atnlh3 t166 = F.l6;
        void * t167 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t152, &t165, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t167;
    }
    {
        gm_f_list_v5d1o9b_2((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t168 = (void *)((uint8_t *)&F.l18);
        gm_closure t169 = gm_l10;
        uint32_t t170 = gm_l12;
        gm_t88rcucg1b7uk t171 = gm_l7;
        gm_closure t172 = t169;
        gm_node *ccx173 = t172.cx;
        gm_node cn174; { gm_node *f = gm_find(cx, 587034433); if (f) { cn174 = *f; cn174.next = ccx173; ccx173 = &cn174; } }
        void * t175 = t172.code;
        void * t176;
        if (GM_PTR_TAGGED(t175)) {
            t176 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t175))(t170, GM_CONV(gm_csguankko4lmhd, t171));
        } else {
            t176 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t175)(t172.env, ccx173, NULL, t170, t171);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlr9dp305htm7 t177;
        GM_ST(uint8_t, (uint8_t *)&t177 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t177 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t177 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t177 + 8), t176);
        gm_tftb1odc739t5 t178 = gm_f_push_eb7kbhp_2(t168, &t177);
        if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 1, gm_srclfnv05vuf0b8, 247, 5);
        gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 1));
    }
    {
        void * t180 = gm_l0;
        void * t181 = gm_l1;
        gm_t88rcucg1b7uk t182 = gm_l4;
        gm_closure t183 = gm_l10;
        uint32_t t184 = gm_l9;
        gm_t88rcucg1b7uk t185 = gm_l4;
        gm_closure t186 = t183;
        gm_node *ccx187 = t186.cx;
        gm_node cn188; { gm_node *f = gm_find(cx, 587034433); if (f) { cn188 = *f; cn188.next = ccx187; ccx187 = &cn188; } }
        void * t189 = t186.code;
        void * t190;
        if (GM_PTR_TAGGED(t189)) {
            t190 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t189))(t184, GM_CONV(gm_csguankko4lmhd, t185));
        } else {
            t190 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t189)(t186.env, ccx187, NULL, t184, t185);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        void * t192 = gm_l17;
        void * t193 = gm_f_loop_with_pattern_d8vl0mj((void *)NULL, cx, NULL, t180, t181, t182, t190, t191, t192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t193;
    }
    {
        void * t194 = (void *)((uint8_t *)&F.l11);
        void * t195 = gm_l19;
        gm_trcmmd12cefb1 t196;
        GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t196 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t196 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t196 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t196 + 8), t195);
        gm_tftb1odc739t5 t197 = gm_f_push_d6c76ud_2(t194, &t196);
        if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srclfnv05vuf0b8, 249, 5);
        gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
    }
    {
        void * t199 = gm_l0;
        void * t200 = GM_LD(void *, (((uint8_t *)t199) + 0));
        gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_tl874t2atnlh3 t202 = F.l6;
        void * t203 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t200, t201, t202);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t204 = 0;
        F.ret = t203;
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
__attribute__((aligned(4))) void * gm_f_range_test_tqcppmu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3, void * a4, gm_t88rcucg1b7uk a5, uint8_t a6, uint8_t a7, gm_tl874t2atnlh3 a8) {
    struct gm_fr_range_test_tqcppmu F;
    void * gm_l0; /* cx */
    void * gm_l1; /* elem */
    gm_b16_8 gm_l2; /* lets */
    void * gm_l3; /* lo */
    void * gm_l4; /* hi */
    uint8_t gm_l6; /* inclusive */
    uint8_t gm_l7; /* negated */
    gm_t88rcucg1b7uk gm_l9; /* b */
    gm_closure gm_l11; /* x */
    gm_ts6oqppjh0ba3 gm_l12; /* upper */
    void * gm_l13; /* test */
    GM_STACK_CHECK(&F, 938u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    F.l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    F.l8 = a8;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = F.l5;
        gm_tl874t2atnlh3 t8 = F.l8;
        uint32_t t9 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t9;
    }
    {
        gm_closure t10 = ((gm_closure){(void *)gm_f_x_dmu97o4, (void *)&F.h, cx});
        gm_l11 = t10;
    }
    {
        gm_ts6oqppjh0ba3 t11;
        uint8_t t13 = gm_l6;
        if (!t13) goto ie12;
        gm_ts6oqppjh0ba3 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)11ULL));
        gm_ts6oqppjh0ba3 t16 = t15;
        t11 = t16;
        goto ix12;
ie12:;
        gm_ts6oqppjh0ba3 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)10ULL));
        gm_ts6oqppjh0ba3 t19 = t18;
        t11 = t19;
ix12:;
        gm_l12 = t11;
    }
    {
        gm_t88rcucg1b7uk t20 = gm_l9;
        gm_ta612fil812pm t21;
        memset((uint8_t *)&t21 + 1, 0, 7);
        GM_ST(uint8_t, (uint8_t *)&t21 + 0, ((uint8_t)1));
        gm_t88rcucg1b7uk t22 = gm_l9;
        gm_trl9d7m1k5lbd t23;
        memset((uint8_t *)&t23 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)11ULL));
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t23 + 0, t24);
        void * t25 = gm_l3;
        GM_ST(void *, (uint8_t *)&t23 + 8, t25);
        gm_closure t26 = gm_l11;
        gm_closure t27 = t26;
        gm_node *ccx28 = t27.cx;
        gm_node cn29; { gm_node *f = gm_find(cx, 587034433); if (f) { cn29 = *f; cn29.next = ccx28; ccx28 = &cn29; } }
        void * t30 = t27.code;
        void * t31;
        if (GM_PTR_TAGGED(t30)) {
            t31 = ((void * (*)(void))GM_PTR_UNTAG(t30))();
        } else {
            t31 = ((void * (*)(void *, gm_node *, gm_node *))t30)(t27.env, ccx28, NULL);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t23 + 16, t31);
        gm_teti411eujvf4 t32;
        memset(&t32, 0, sizeof t32);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)30ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t32 + 8), t23);
        gm_tl874t2atnlh3 t33 = F.l8;
        void * t34 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t22, &t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t21 + 8, t34);
        gm_t88rcucg1b7uk t35 = gm_l9;
        gm_trl9d7m1k5lbd t36;
        memset((uint8_t *)&t36 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t37 = gm_l12;
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t36 + 0, t37);
        gm_closure t38 = gm_l11;
        gm_closure t39 = t38;
        gm_node *ccx40 = t39.cx;
        gm_node cn41; { gm_node *f = gm_find(cx, 587034433); if (f) { cn41 = *f; cn41.next = ccx40; ccx40 = &cn41; } }
        void * t42 = t39.code;
        void * t43;
        if (GM_PTR_TAGGED(t42)) {
            t43 = ((void * (*)(void))GM_PTR_UNTAG(t42))();
        } else {
            t43 = ((void * (*)(void *, gm_node *, gm_node *))t42)(t39.env, ccx40, NULL);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t36 + 8, t43);
        void * t44 = gm_l4;
        GM_ST(void *, (uint8_t *)&t36 + 16, t44);
        gm_teti411eujvf4 t45;
        memset(&t45, 0, sizeof t45);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)30ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t45 + 8), t36);
        gm_tl874t2atnlh3 t46 = F.l8;
        void * t47 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t35, &t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t21 + 16, t47);
        gm_teti411eujvf4 t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)31ULL));
        GM_ST(gm_ta612fil812pm, ((uint8_t *)&t48 + 8), t21);
        gm_tl874t2atnlh3 t49 = F.l8;
        void * t50 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t20, &t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t50;
    }
    {
        uint8_t t52 = gm_l7;
        if (!t52) goto ie51;
        {
            gm_t88rcucg1b7uk t54 = gm_l9;
            gm_t9qkh0rsooc8d t55;
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)2ULL));
            void * t56 = gm_l13;
            gm_t8ghgl2jj0qha t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t57 + 0), t55);
            GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
            gm_teti411eujvf4 t58;
            memset(&t58, 0, sizeof t58);
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t58 + 8), t57);
            gm_tl874t2atnlh3 t59 = F.l8;
            void * t60 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t60;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t61 = (void *)((uint8_t *)&F.l14);
        uint32_t t62 = F.l10;
        void * t63 = gm_l1;
        gm_ttggq7ng4kp3p t64;
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t64 + 0), t62);
        GM_ST(void *, ((uint8_t *)&t64 + 8), t63);
        gm_trcmmd12cefb1 t65;
        GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t65 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t65 + 8), t64);
        gm_tftb1odc739t5 t66 = gm_f_push_d6c76ud_2(t61, &t65);
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srclfnv05vuf0b8, 183, 5);
        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
    }
    {
        void * t68 = (void *)((uint8_t *)&F.l14);
        gm_b16_8 t69 = gm_l2;
        gm_tftb1odc739t5 t70 = gm_f_extend_nkql463(t68, t69);
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srclfnv05vuf0b8, 184, 5);
        gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
    }
    {
        gm_t88rcucg1b7uk t72 = gm_l9;
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t74 = gm_l13;
        gm_tl874t2atnlh3 t75 = F.l8;
        void * t76 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t72, t73, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_tuple_elem_join_om8llaj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_tuple_elem_join_om8llaj F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* elems */
    void * gm_l2; /* tys */
    gm_t88rcucg1b7uk gm_l3; /* first */
    uint8_t gm_l4; /* differ */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* e */
    gm_t88rcucg1b7uk gm_l8; /* r */
    gm_t88rcucg1b7uk gm_l9; /* joined */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_t88rcucg1b7uk gm_l13; /* r */
    GM_STACK_CHECK(&F, 712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint64_t t9 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srclfnv05vuf0b8, 158, 37);
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t9 * 4));
        gm_t88rcucg1b7uk t12 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t12;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t14 = gm_l1;
            gm_l5 = t14;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l6;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l6;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t22 * 4));
                    gm_l7 = t26;
                    {
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 96);
                        void * t30 = gm_l0;
                        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                        gm_t88rcucg1b7uk t32 = gm_l7;
                        gm_t88rcucg1b7uk t33 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t29, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                        gm_l8 = t33;
                    }
                    {
                        uint8_t t35 = 0;
                        void * t36 = gm_l2;
                        gm_t88rcucg1b7uk t37 = gm_l8;
                        uint8_t t38 = gm_f_has_infer_3o82a0b(t36, GM_CONV(gm_csguankko4lmhd, t37));
                        if (!t38) {
                            void * t39 = gm_l2;
                            gm_t88rcucg1b7uk t40 = gm_l8;
                            uint8_t t41 = gm_f_is_error_8k27gd1(t39, GM_CONV(gm_csguankko4lmhd, t40));
                            t35 = t41;
                        } else t35 = 1;
                        if (!t35) goto ie34;
                        {
                            gm_tkn42u7na3t81 t43;
                            memset(&t43, 0, sizeof t43);
                            uint8_t t44 = 0;
                            F.ret = t43;
                            goto gm_fret;
                        }
                        goto ix34;
ie34:;
ix34:;
                    }
                    {
                        gm_t88rcucg1b7uk t46 = gm_l8;
                        gm_t88rcucg1b7uk t47 = gm_l3;
                        uint8_t t48 = (!gm_eq9m6mvtmgde(((uint8_t *)&t46), ((uint8_t *)&t47)));
                        if (!t48) goto ie45;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 160, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t51 = gm_l6;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l6 = t54;
                }
            }
gm_ll19:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
            }
            goto gm_lad;
lb15:;
        }
    }
    {
        uint8_t t56 = gm_l4;
        uint8_t t57 = (uint8_t)!t56;
        if (!t57) goto ie55;
        {
            gm_tkn42u7na3t81 t59;
            memset(&t59, 0, sizeof t59);
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        void * t61 = gm_l0;
        void * t62 = (void *)(((uint8_t *)t61) + 96);
        void * t63 = gm_l0;
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
        void * t65 = gm_l0;
        gm_b16_8 t66 = gm_l1;
        gm_t88rcucg1b7uk t67 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t68 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t62, t64, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t68;
    }
    {
        {
            gm_b16_8 t70 = gm_l1;
            gm_l10 = t70;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t72 = gm_l11;
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t74 = (t72 < t73);
                if (!t74) goto lb71;
                {
                    uint64_t t77 = gm_l11;
                    uint64_t t78 = (uintptr_t)t77;
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t78 * 4));
                    gm_l12 = t82;
                    {
                        void * t84 = gm_l0;
                        void * t85 = (void *)(((uint8_t *)t84) + 96);
                        void * t86 = gm_l0;
                        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
                        gm_t88rcucg1b7uk t88 = gm_l12;
                        gm_t88rcucg1b7uk t89 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t85, t87, t88);
                        if (gm_unw.kind) {
                            goto gm_ll75;
                        }
                        gm_l13 = t89;
                    }
                    {
                        gm_t88rcucg1b7uk t91 = gm_l13;
                        gm_t88rcucg1b7uk t92 = gm_l9;
                        uint8_t t93 = (!gm_eq9m6mvtmgde(((uint8_t *)&t91), ((uint8_t *)&t92)));
                        if (!t93) goto ie90;
                        void * t94 = gm_l0;
                        gm_t88rcucg1b7uk t95 = gm_l13;
                        gm_t88rcucg1b7uk t96 = gm_l9;
                        gm_th2lqbo77pvsc t97;
                        gm_f_coercion_steps_ibqvhta((void *)&t97, (void *)NULL, cx, NULL, t94, t95, t96);
                        if (gm_unw.kind) {
                            goto gm_ll75;
                        }
                        uint8_t t98;
                        if (((((uint8_t *)&t97))[16] != 0)) goto mn100;
                        t98 = ((uint8_t)1);
                        goto me99;
mn100:;
                        t98 = ((uint8_t)0);
                        goto me99;
mn101:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 169, 32);
me99:;
                        if (!t98) goto ie90;
                        uint8_t t102 = 0;
                        void * t103 = gm_l2;
                        gm_t88rcucg1b7uk t104 = gm_l13;
                        uint8_t t105 = gm_f_is_positional_array_6sufsk8(t103, GM_CONV(gm_csguankko4lmhd, t104));
                        if (t105) {
                            void * t106 = gm_l2;
                            gm_t88rcucg1b7uk t107 = gm_l9;
                            gm_t53at80pfdus3 t108;
                            gm_f_kind_jcnt4hm((void *)&t108, t106, GM_CONV(gm_csguankko4lmhd, t107));
                            uint8_t t109;
                            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&t108) + 0));
                            if (t112 != ((uint8_t)10ULL)) goto mn111;
                            t109 = ((uint8_t)1);
                            goto me110;
mn111:;
                            t109 = ((uint8_t)0);
                            goto me110;
mn113:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 169, 109);
me110:;
                            t102 = t109;
                        } else t102 = 0;
                        uint8_t t114 = (uint8_t)!t102;
                        if (!t114) goto ie90;
                        {
                            gm_tkn42u7na3t81 t116;
                            memset(&t116, 0, sizeof t116);
                            uint8_t t117 = 0;
                            F.ret = t116;
                            goto gm_fret;
                        }
                        goto ix90;
ie90:;
ix90:;
                    }
                    goto me80;
mn81:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 167, 5);
me80:;
                }
lc71:;
                {
                    uint64_t t119 = gm_l11;
                    uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                    uint64_t t122 = r120;
                    gm_l11 = t122;
                }
            }
gm_ll75:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb71; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc71; }
            }
            goto gm_lad;
lb71:;
        }
    }
    {
        gm_t88rcucg1b7uk t123 = gm_l9;
        gm_tkn42u7na3t81 t124;
        GM_ST(uint8_t, (uint8_t *)&t124 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t124 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t124), t123);
        ((uint8_t *)&t124)[4] = 1;
        uint8_t t125 = 0;
        F.ret = t124;
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
extern __attribute__((aligned(4))) void * gm_f_x_dmu97o4(void *env, gm_node *cx, gm_node *inst);

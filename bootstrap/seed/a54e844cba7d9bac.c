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
struct gm_fr_empty_slice_c3qnk0a {
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
struct gm_fr_push_1ncolkq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_am7bo0v {
    gm_frame h;
    gm_tsoie1ppgdvcg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bu0mlkm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s5vr2a2 {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4mbcm9u {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8l7j2k6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8r12kcb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o08qncv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_7s68jhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_compile_error_qlau792(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handlers_c3ac89r(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_33kejrs(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_7s68jhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_calls_7s68jhs F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 72u);
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
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src5eu5l4a7gv6s, 381, 37);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        gm_closure t13 = ((gm_closure){(void *)gm_f_fn_2d811oi, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t12, t13);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3qnk0a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_crpekm1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
    GM_STACK_CHECK_SHALLOW(&F, 126u);
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
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
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
    GM_STACK_CHECK(&F, 80u);
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
            if (!((((uint8_t *)&t5))[40] != 0)) goto lb2;
            F.l1 = (*(gm_t0ut88vjdgrt1 *)(((uint8_t *)&t5) + 0));
            {
                void * t8 = gm_l0;
                gm_b0_1 t9 = gm_f_produce_instance_kcqgj3e((void *)NULL, cx, NULL, t8, &(*(gm_t0ut88vjdgrt1 *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_ll6;
                }
            }
lc2:;
        }
gm_ll6:;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
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
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_t4fb05ktf8oll *)gm_out) + 0, t29, 40);
        goto gm_fdone;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1ncolkq F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_crpekm1();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_o08qncv(t10, ((uint64_t)1ULL), t12);
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
        gm_tcc1rq4n1avb2 t21 = gm_l1;
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)t20 + t18 * 8), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_am7bo0v F;
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
            gm_b16_8 t11 = gm_f_empty_slice_6trg8r7();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8r12kcb(t10, ((uint64_t)1ULL), t12);
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
        gm_tsoie1ppgdvcg t21 = F.l1;
        GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)t20 + t18 * 40), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bu0mlkm F;
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
            gm_b16_8 t11 = gm_f_empty_slice_c3qnk0a();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8l7j2k6(t10, ((uint64_t)1ULL), t12);
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
        gm_t1cs2fjnrqap1 t21 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)a1));
        GM_ST(gm_t1cs2fjnrqap1, ((uint8_t *)t20 + t18 * 40), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h69n5fi F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_8fnsr5h();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_1sqb3ir(t10, ((uint64_t)1ULL), t12);
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
        gm_tplpbqf3duvn3 t21 = gm_l1;
        GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)t20 + t18 * 8), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_d914i5u();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_2onqfca(t10, ((uint64_t)1ULL), t12);
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
        uint32_t t21 = gm_l1;
        GM_ST(uint32_t, ((uint8_t *)t20 + t18 * 4), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
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
            gm_b16_8 t11 = gm_f_empty_slice_he774nl();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_7sj41hq(t10, ((uint64_t)1ULL), t12);
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
        gm_t5v0c8ia6a5mo t21 = F.l1;
        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)t20 + t18 * 56), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s5vr2a2 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_4mbcm9u(t10, ((uint64_t)1ULL), t12);
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
        gm_toa2vhe3l4hfr t21 = F.l1;
        GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)t20 + t18 * 40), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1sqb3ir F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4mbcm9u F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8l7j2k6 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8r12kcb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o08qncv F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 34u);
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
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srclv0dcbbfhves, 607, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf1lpoo3h6o)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t1))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1) + 8, gm_srcod98slk3vnra, 561, 80);
    void * t2 = GM_LD(void *, (((uint8_t *)&t1) + 8));
    memset(&(*(gm_teti411eujvf4 *)gm_out), 0, sizeof (*(gm_teti411eujvf4 *)gm_out));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 0, ((uint8_t)2ULL));
    GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 8, t2);
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
    GM_STACK_CHECK(&F, 612u);
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
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_src5eu5l4a7gv6s, 359, 9);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    {
                        gm_l6 = ((uint8_t)0);
                    }
                    {
                        for (;;) {
                            void * t23 = (void *)((uint8_t *)&F.l5);
                            gm_t9805qtonbsag t24 = gm_f_pop_570g79n(t23);
                            if (!((((uint8_t *)&t24))[4] != 0)) goto lb22;
                            uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&t24));
                            gm_l7 = t25;
                            {
                                uint32_t t28 = gm_l7;
                                uint64_t t29 = (uint64_t)t28;
                                uint64_t t30 = (uintptr_t)t29;
                                void * t31 = gm_l0;
                                uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 384) + 0) + 8);
                                if (t30 >= (uintptr_t)(t32)) gm_trap_index(t30, t32, gm_src5eu5l4a7gv6s, 362, 21);
                                void * t33 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 384) + 0)) + t30 * 64) + 40));
                                void * t34;
                                if ((GM_LD(void *, ((uint8_t *)&t33)) != 0)) {
                                    void * t35 = GM_LD(void *, (((uint8_t *)&t33) + 0));
                                    t34 = t35;
                                } else {
                                    goto lc22;
                                }
                                gm_l8 = t34;
                            }
                            {
                                gm_f_list_go2qg0b((void *)&F.l9, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll26;
                                }
                            }
                            {
                                void * t36 = gm_l8;
                                void * t37 = (void *)((uint8_t *)&F.l9);
                                gm_b0_1 t38 = gm_f_collect_calls_7s68jhs((void *)NULL, cx, NULL, t36, t37);
                                if (gm_unw.kind) {
                                    goto gm_ll26;
                                }
                            }
                            {
                                {
                                    gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                    gm_l10 = t40;
                                }
                                {
                                    gm_l11 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t42 = gm_l11;
                                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint8_t t44 = (t42 < t43);
                                        if (!t44) goto lb41;
                                        {
                                            uint64_t t47 = gm_l11;
                                            uint64_t t48 = (uintptr_t)t47;
                                            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            uint32_t t50 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t48 * 4));
                                            uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)&t50));
                                            gm_l12 = t53;
                                            {
                                                uint32_t t56 = gm_l12;
                                                uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                                                uint8_t t58 = ((t56 == t57));
                                                if (!t58) goto ie55;
                                                {
                                                    gm_l6 = ((uint8_t)1);
                                                }
                                                goto ix55;
ie55:;
                                                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                                gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                                                uint32_t t63 = gm_l12;
                                                gm_tf5srh4mp4b3i t64 = gm_f_index_of_41b084m(t62, t63);
                                                uint8_t t65;
                                                if (((((uint8_t *)&t64))[8] != 0)) goto mn67;
                                                t65 = ((uint8_t)1);
                                                goto me66;
mn67:;
                                                t65 = ((uint8_t)0);
                                                goto me66;
mn68:;
                                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 368, 27);
me66:;
                                                if (!t65) goto ie60;
                                                {
                                                    void * t70 = (void *)((uint8_t *)&F.l4);
                                                    uint32_t t71 = gm_l12;
                                                    gm_tftb1odc739t5 t72 = gm_f_push_im9d4ro(t70, t71);
                                                    if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_src5eu5l4a7gv6s, 369, 21);
                                                    gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                                                }
                                                {
                                                    void * t74 = (void *)((uint8_t *)&F.l5);
                                                    uint32_t t75 = gm_l12;
                                                    gm_tftb1odc739t5 t76 = gm_f_push_im9d4ro(t74, t75);
                                                    if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_src5eu5l4a7gv6s, 370, 21);
                                                    gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                                                }
                                                goto ix60;
ie60:;
ix60:;
ix55:;
                                            }
                                            goto me51;
mn52:;
                                            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 365, 13);
me51:;
                                        }
lc41:;
                                        {
                                            uint64_t t79 = gm_l11;
                                            uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                                            uint64_t t82 = r80;
                                            gm_l11 = t82;
                                        }
                                    }
lb41:;
                                }
                            }
lc22:;
                        }
gm_ll26:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
                        }
                        goto gm_ll9;
lb22:;
                    }
                    {
                        uint8_t t84 = gm_l6;
                        if (!t84) goto ie83;
                        {
                            void * t86 = gm_l0;
                            gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 4));
                            uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                            uint64_t t89 = (uint64_t)t88;
                            uint64_t t90 = (uintptr_t)t89;
                            void * t91 = gm_l0;
                            uint64_t t92 = GM_LD(uintptr_t, ((((uint8_t *)t91) + 344) + 0) + 8);
                            if (t90 >= (uintptr_t)(t92)) gm_trap_index(t90, t92, gm_src5eu5l4a7gv6s, 375, 116);
                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t91) + 344) + 0)) + t90 * 136) + 0));
                            gm_tnpifv20irnph t94;
                            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t93);
                            gm_tabb710d9mq2e t95;
                            gm_f_format_ra91g4n((void *)&t95, (void *)NULL, cx, NULL, t94);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_src5eu5l4a7gv6s, 375, 31);
                            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
                            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                            gm_b0_1 t98 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t86, t87, t97);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix83;
ie83:;
ix83:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 356, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t100 = gm_l2;
                    uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                    uint64_t t103 = r101;
                    gm_l2 = t103;
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
    GM_STACK_CHECK(&F, 1354u);
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
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l5 = t15;
                    {
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 280) + 0) + 8);
                        if (t18 >= (uintptr_t)(t21)) gm_trap_index(t18, t21, gm_src5eu5l4a7gv6s, 8, 14);
                        gm_tp8h583g7s5hd t22 = GM_LD(gm_tp8h583g7s5hd, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 280) + 0)) + t18 * 168));
                        F.l6 = t22;
                    }
                    {
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 0));
                        uint64_t t25 = (uintptr_t)t24;
                        void * t26 = gm_l0;
                        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
                        uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 200) + 0) + 8);
                        if (t25 >= (uintptr_t)(t28)) gm_trap_index(t25, t28, gm_src5eu5l4a7gv6s, 9, 12);
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 200) + 0)) + t25 * 184) + 112));
                        if (!t29) goto ie23;
                        {
                            goto lc6;
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    {
                        uint8_t t36 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma32; case ((uint8_t)3ULL): goto ma32; case ((uint8_t)4ULL): goto ma33; default: goto ma34; }
ma32:;
                        uint8_t t40 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        if (t40 != ((uint8_t)3ULL)) goto pa39;
                        goto po38;
pa39:;
                        uint8_t t42 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        if (t42 != ((uint8_t)2ULL)) goto pa41;
                        goto po38;
pa41:;
                        goto mn37;
po38:;
                        goto me31;
mn37:;
ma33:;
                        uint8_t t45 = GM_LD(uint8_t, (((((uint8_t *)&F.l6) + 8) + 64) + 0));
                        if (t45 != ((uint8_t)4ULL)) goto mn44;
                        void * t46 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 64) + 8));
                        gm_l7 = t46;
                        {
                            gm_teqc715vjig79 t48;
                            memset(&t48, 0, sizeof t48);
                            F.l8 = t48;
                        }
                        {
                            void * t50 = gm_l7;
                            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 0));
                            if (t51 != ((uint8_t)49ULL)) goto ie49;
                            gm_tet9guhev5r4u t52 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t50) + 8) + 8));
                            F.l9 = t52;
                            {
                                const uint8_t *t54 = ((uint8_t *)&F.l9);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_teqc715vjig79 *)((uint8_t *)&F.l8)) + 57, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_teqc715vjig79 *)((uint8_t *)&F.l8)) + 58, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_teqc715vjig79 *)((uint8_t *)&F.l8)) + 60, 0);
                                ((uint8_t *)&(*(gm_teqc715vjig79 *)((uint8_t *)&F.l8)))[56] = 1;
                                memmove((uint8_t *)&(*(gm_teqc715vjig79 *)((uint8_t *)&F.l8)) + 0, t54, 56);
                            }
                            goto ix49;
ie49:;
ix49:;
                        }
                        {
                            gm_teqc715vjig79 t55 = F.l8;
                            gm_tet9guhev5r4u t56;
                            if (((((uint8_t *)&t55))[56] != 0)) {
                                t56 = (*(gm_tet9guhev5r4u *)(((uint8_t *)&t55) + 0));
                            } else {
                                goto lc6;
                            }
                            F.l10 = t56;
                        }
                        {
                            gm_th2lqbo77pvsc t57 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l10) + 32));
                            gm_b16_8 t58;
                            if (((((uint8_t *)&t57))[16] != 0)) {
                                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 0));
                                t58 = t59;
                            } else {
                                gm_b16_8 t60 = gm_f_empty_slice_fa4sjvq();
                                t58 = t60;
                            }
                            gm_l11 = t58;
                        }
                        {
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 13) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_snikjq3vkoue5, 13) == 0)) goto mn62;
                            {
                                {
                                    void * t65 = gm_l0;
                                    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 0));
                                    gm_b16_8 t67 = gm_l11;
                                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                                    void * t69 = gm_l7;
                                    gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72));
                                    void * t71 = gm_f_static_assert_k8dt551((void *)NULL, cx, NULL, t65, t66, t68, t70);
                                    if (gm_unw.kind) {
                                        goto gm_ll10;
                                    }
                                }
                            }
                            goto me61;
mn62:;
                            if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 13) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_sisvmc7d4huip, 13) == 0)) goto mn72;
                            void * t73 = gm_l0;
                            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 0));
                            gm_b16_8 t75 = gm_l11;
                            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                            void * t77 = gm_l7;
                            gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                            gm_b0_1 t79 = gm_f_compile_error_qlau792((void *)NULL, cx, NULL, t73, t74, t76, t78);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me61;
mn72:;
                            void * t81 = gm_l0;
                            gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 8) + 4));
                            gm_b16_8 t83;
                            GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sfrcaft0lo33k));
                            GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(59));
                            gm_b0_1 t84 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t81, t82, t83);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            goto me61;
mn80:;
                            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 17, 17);
me61:;
                        }
                        goto me31;
mn44:;
ma34:;
                        void * t86 = gm_l0;
                        gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l6) + 8) + 4));
                        gm_b16_8 t88;
                        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_smhs805pum0dn));
                        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(25));
                        gm_b0_1 t89 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t86, t87, t88);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        goto me31;
mn85:;
ma35:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 10, 9);
me31:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 7, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t91 = gm_l4;
                    uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                    uint64_t t94 = r92;
                    gm_l4 = t94;
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
        void * t95 = gm_l0;
        gm_b0_1 t96 = gm_f_declare_handlers_c3ac89r((void *)NULL, cx, NULL, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t98 = gm_l2;
        if (!t98) goto ie97;
        {
            void * t100 = gm_l0;
            uint64_t t101 = gm_l1;
            gm_b0_1 t102 = gm_f_check_tests_ogqv9b0((void *)NULL, cx, NULL, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix97;
ie97:;
        void * t104 = gm_l0;
        void * t105 = GM_LD(void *, (((uint8_t *)t104) + 0));
        uint8_t t106 = (((GM_LD(uintptr_t, ((((uint8_t *)t105) + 88) + 24) + 8) == 12) && memcmp(GM_LD(void *, ((((uint8_t *)t105) + 88) + 24)), gm_soo4rtckloi4d, 12) == 0));
        if (!t106) goto ie103;
        {
            void * t109 = gm_l0;
            void * t110 = GM_LD(void *, (((uint8_t *)t109) + 0));
            uint64_t t111 = gm_l1;
            gm_b16_8 t112;
            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sog4rksrmsqd2));
            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(4));
            gm_tf5srh4mp4b3i t113 = gm_f_lookup_59juqoo(t110, t111, t112);
            if (!((((uint8_t *)&t113))[8] != 0)) goto ie108;
            uint64_t t114 = GM_LD(uint64_t, ((uint8_t *)&t113));
            gm_l12 = t114;
            {
                void * t116 = gm_l0;
                uint64_t t117 = gm_l12;
                uint64_t t118 = (uintptr_t)t117;
                void * t119 = gm_l0;
                void * t120 = GM_LD(void *, (((uint8_t *)t119) + 0));
                uint64_t t121 = GM_LD(uintptr_t, ((((uint8_t *)t120) + 240) + 0) + 8);
                if (t118 >= (uintptr_t)(t121)) gm_trap_index(t118, t121, gm_src5eu5l4a7gv6s, 33, 21);
                gm_tl874t2atnlh3 t122 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t120) + 240) + 0)) + t118 * 216) + 0) + 16));
                gm_b16_8 t123;
                GM_ST(void *, (uint8_t *)&t123, (void *)(gm_sq7c24l9r5rti));
                GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(117));
                gm_b0_1 t124 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t116, t122, t123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix108;
ie108:;
ix108:;
        }
        goto ix103;
ie103:;
        void * t126 = gm_l0;
        void * t127 = GM_LD(void *, (((uint8_t *)t126) + 0));
        uint64_t t128 = gm_l1;
        gm_b16_8 t129;
        GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(4));
        gm_tf5srh4mp4b3i t130 = gm_f_lookup_59juqoo(t127, t128, t129);
        if (!((((uint8_t *)&t130))[8] != 0)) goto ie125;
        uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)&t130));
        gm_l13 = t131;
        {
            void * t133 = gm_l0;
            uint64_t t134 = gm_l13;
            gm_b0_1 t135 = gm_f_find_main_ob90gra((void *)NULL, cx, NULL, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix125;
ie125:;
        {
            void * t137 = gm_l0;
            gm_tl874t2atnlh3 t138 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t139;
            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_srar03v1ldkqi));
            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(48));
            gm_b0_1 t140 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t137, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix125:;
ix103:;
ix97:;
    }
    {
        void * t141 = gm_l0;
        gm_b0_1 t142 = gm_f_check_consts_fsj0q4a((void *)NULL, cx, NULL, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t143 = gm_l0;
        gm_b0_1 t144 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t145 = gm_l0;
        gm_b0_1 t146 = gm_f_check_handler_clauses_n3lq2s1((void *)NULL, cx, NULL, t145);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t147 = gm_l0;
        gm_b0_1 t148 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t147);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t149 = gm_l0;
        gm_b0_1 t150 = gm_f_check_declared_defaults_m12p6ao((void *)NULL, cx, NULL, t149);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t151 = gm_l0;
        gm_b0_1 t152 = gm_f_check_bodies_qrmq5ll((void *)NULL, cx, NULL, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t153 = gm_l0;
        gm_b0_1 t154 = gm_f_check_decl_breaks_pk9cnmn((void *)NULL, cx, NULL, t153);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t155 = gm_l0;
        gm_b0_1 t156 = gm_f_monomorphize_21k6pku((void *)NULL, cx, NULL, t155);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t157 = gm_l0;
        gm_b0_1 t158 = gm_f_check_inline_n7kskmb((void *)NULL, cx, NULL, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t159 = gm_l0;
        gm_b0_1 t160 = gm_f_check_escapes_brb2os4((void *)NULL, cx, NULL, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t161 = gm_l0;
        gm_b0_1 t162 = gm_f_check_dangling_addresses_9hre1eh((void *)NULL, cx, NULL, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t163 = gm_l0;
        gm_b0_1 t164 = gm_f_check_continuations_ukkdj9c((void *)NULL, cx, NULL, t163);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_collect_calls_7s68jhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
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
    GM_STACK_CHECK(&F, 1131u);
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
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma13; default: goto ma14; }
ma12:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t18 != ((uint8_t)5ULL)) goto mn17;
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&t10) + 8));
        gm_l3 = t19;
        {
            uint32_t t21 = gm_l3;
            uint64_t t22 = (uint64_t)t21;
            uint64_t t23 = (uintptr_t)t22;
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 344) + 0) + 8);
            if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_src5eu5l4a7gv6s, 390, 20);
            gm_tqqhoivahirr0 t26 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 344) + 0)) + t23 * 136));
            F.l4 = t26;
        }
        {
            uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 40) + 8);
            uint8_t t29 = (t28 > ((uint64_t)0ULL));
            if (!t29) goto ie27;
            {
                void * t31 = gm_l0;
                gm_tl874t2atnlh3 t32 = gm_l2;
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sl85qrve5cdud));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(30));
                gm_b0_1 t34 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t31, t32, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            uint8_t t35 = 0;
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 8);
            gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            uint8_t t39 = gm_f_is_unit_n83ttnb(t37, GM_CONV(gm_csguankko4lmhd, t38));
            if (!t39) {
                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
                void * t41 = gm_l0;
                void * t42 = (void *)(((uint8_t *)t41) + 8);
                gm_t88rcucg1b7uk t43 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t44 = (gm_eq9m6mvtmgde(((uint8_t *)&t40), ((uint8_t *)&t43)));
                t35 = t44;
            } else t35 = 1;
            gm_l5 = t35;
        }
        {
            void * t46 = gm_l0;
            void * t47 = (void *)(((uint8_t *)t46) + 8);
            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
            gm_t53at80pfdus3 t49;
            gm_f_kind_jcnt4hm((void *)&t49, t47, GM_CONV(gm_csguankko4lmhd, t48));
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
            if (t50 != ((uint8_t)14ULL)) goto ie45;
            gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t49) + 8) + 4));
            gm_l6 = t51;
            void * t52 = gm_l0;
            void * t53 = (void *)(((uint8_t *)t52) + 8);
            gm_t88rcucg1b7uk t54 = gm_l6;
            uint8_t t55 = gm_f_is_unit_n83ttnb(t53, GM_CONV(gm_csguankko4lmhd, t54));
            if (!t55) goto ie45;
            {
                gm_l5 = ((uint8_t)1);
            }
            goto ix45;
ie45:;
ix45:;
        }
        {
            uint8_t t58 = gm_l5;
            uint8_t t59 = (uint8_t)!t58;
            if (!t59) goto ie57;
            {
                void * t61 = gm_l0;
                gm_tl874t2atnlh3 t62 = gm_l2;
                void * t63 = gm_l0;
                gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
                gm_b16_8 t65 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t66;
                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                gm_tabb710d9mq2e t67;
                gm_f_format_l251doo((void *)&t67, (void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_src5eu5l4a7gv6s, 394, 43);
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_b0_1 t70 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t61, t62, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            {
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 64) + 0));
                gm_l7 = t72;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t74 = gm_l8;
                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t76 = (t74 < t75);
                    if (!t76) goto lb73;
                    {
                        uint64_t t79 = gm_l8;
                        uint64_t t80 = (uintptr_t)t79;
                        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_tiilmfd16v3fc t84 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t80 * 24));
                        gm_l9 = t84;
                        {
                            void * t87 = gm_l0;
                            uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
                            uint8_t t89 = gm_f_is_ambient_m41pl1e(t87, t88);
                            uint8_t t90 = (uint8_t)!t89;
                            if (!t90) goto ie86;
                            {
                                void * t92 = gm_l0;
                                gm_tl874t2atnlh3 t93 = gm_l2;
                                void * t94 = gm_l0;
                                void * t95 = (void *)(((uint8_t *)t94) + 8);
                                uint32_t t96 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
                                gm_b16_8 t97 = gm_f_effect_name_prpp0ip(t95, t96);
                                gm_tnpifv20irnph t98;
                                GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
                                gm_tabb710d9mq2e t99;
                                gm_f_format_jhm2032((void *)&t99, (void *)NULL, cx, NULL, t98);
                                if (gm_unw.kind) {
                                    goto gm_ll77;
                                }
                                if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_src5eu5l4a7gv6s, 397, 35);
                                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
                                gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                                gm_b0_1 t102 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t92, t93, t101);
                                if (gm_unw.kind) {
                                    goto gm_ll77;
                                }
                            }
                            goto ix86;
ie86:;
ix86:;
                        }
                        goto me82;
mn83:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 395, 13);
me82:;
                    }
lc73:;
                    {
                        uint64_t t104 = gm_l8;
                        uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                        uint64_t t107 = r105;
                        gm_l8 = t107;
                    }
                }
gm_ll77:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb73; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc73; }
                }
                goto gm_lad;
lb73:;
            }
        }
        goto me11;
mn17:;
ma13:;
        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t109 != ((uint8_t)11ULL)) goto mn108;
        goto me11;
mn108:;
ma14:;
        void * t112 = gm_l0;
        gm_tl874t2atnlh3 t113 = gm_l2;
        gm_b16_8 t114;
        GM_ST(void *, (uint8_t *)&t114, (void *)(gm_s6d960pgtv3l9));
        GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(25));
        gm_b0_1 t115 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t112, t113, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn111:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 388, 5);
me11:;
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
    GM_STACK_CHECK(&F, 6236u);
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
                            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                            if (t49 != ((uint8_t)21ULL)) goto ie44;
                            gm_l10 = (*(gm_t428ue2u3hf5g *)(((uint8_t *)&t48) + 8));
                            {
                                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                switch (t57) { case ((uint8_t)0ULL): goto ma52; case ((uint8_t)1ULL): goto ma53; case ((uint8_t)2ULL): goto ma54; case ((uint8_t)3ULL): goto ma55; default: goto ma56; }
ma52:;
                                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t59 != ((uint8_t)0ULL)) goto mn58;
                                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l10) + 8) + 0));
                                gm_l11 = t60;
                                gm_b16_8 t61 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l10) + 8) + 8));
                                gm_l12 = t61;
                                {
                                    void * t64 = F.l0;
                                    gm_thrdpodrs8pgt t65;
                                    gm_f_key_int_pqscuvn((void *)&t65, (void *)NULL, cx, NULL, t64, &(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l10)));
                                    if (gm_unw.kind) {
                                        goto gm_ll30;
                                    }
                                    if (!((((uint8_t *)&t65))[48] != 0)) goto ie63;
                                    F.l13 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t65) + 0));
                                    {
                                        void * t67 = gm_l2;
                                        const uint8_t *t68 = ((uint8_t *)&F.l13);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t67) + 8)) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t67) + 8)) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t67) + 8)) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t67) + 8)) + 56, 0);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t67) + 8)) + 0, ((uint8_t)0ULL));
                                        memmove((uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t67) + 8)) + 8, t68, 48);
                                    }
                                    goto ix63;
ie63:;
                                    void * t70 = F.l0;
                                    void * t71 = (void *)(((uint8_t *)t70) + 8);
                                    gm_t88rcucg1b7uk t72 = gm_l11;
                                    gm_t53at80pfdus3 t73;
                                    gm_f_kind_jcnt4hm((void *)&t73, t71, GM_CONV(gm_csguankko4lmhd, t72));
                                    uint8_t t74 = ((GM_LD(uint8_t, ((uint8_t *)&t73) + 0) == ((uint8_t)4ULL)));
                                    if (!t74) goto ie69;
                                    {
                                        void * t76 = gm_l2;
                                        uint8_t t77 = 0;
                                        uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        uint8_t t79 = (t78 > ((uint64_t)0ULL));
                                        if (t79) {
                                            uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                            if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_src5eu5l4a7gv6s, 136, 58);
                                            uint8_t t82 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t80 * 1));
                                            uint8_t t83 = ((t82 == ((uint8_t)1ULL)));
                                            t77 = t83;
                                        } else t77 = 0;
                                        memset(&(*(gm_teti411eujvf4 *)(((uint8_t *)t76) + 8)), 0, sizeof (*(gm_teti411eujvf4 *)(((uint8_t *)t76) + 8)));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t76) + 8)) + 0, ((uint8_t)4ULL));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t76) + 8)) + 8, t77);
                                    }
                                    goto ix69;
ie69:;
                                    void * t85 = F.l1;
                                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t85) + 108));
                                    if (!t86) goto ie84;
                                    {
                                        void * t88 = F.l0;
                                        void * t89 = gm_l2;
                                        gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t89) + 72));
                                        gm_b16_8 t91;
                                        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sjg57h040l08d));
                                        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(55));
                                        gm_b0_1 t92 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t88, t90, t91);
                                        if (gm_unw.kind) {
                                            goto gm_ll30;
                                        }
                                    }
                                    goto ix84;
ie84:;
ix84:;
ix69:;
ix63:;
                                }
                                goto me51;
mn58:;
ma53:;
                                uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t94 != ((uint8_t)1ULL)) goto mn93;
                                uint32_t t95 = GM_LD(uint32_t, ((((uint8_t *)&gm_l10) + 8) + 0));
                                gm_l14 = t95;
                                {
                                    void * t97 = gm_l2;
                                    uint32_t t98 = gm_l14;
                                    memset(&(*(gm_teti411eujvf4 *)(((uint8_t *)t97) + 8)), 0, sizeof (*(gm_teti411eujvf4 *)(((uint8_t *)t97) + 8)));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t97) + 8)) + 0, ((uint8_t)26ULL));
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t97) + 8)) + 8, t98);
                                }
                                goto me51;
mn93:;
ma54:;
                                uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t100 != ((uint8_t)2ULL)) goto mn99;
                                {
                                    void * t102 = gm_l2;
                                    memset(&(*(gm_teti411eujvf4 *)(((uint8_t *)t102) + 8)), 0, sizeof (*(gm_teti411eujvf4 *)(((uint8_t *)t102) + 8)));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t102) + 8)) + 0, ((uint8_t)7ULL));
                                }
                                goto me51;
mn99:;
ma55:;
                                uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                                if (t104 != ((uint8_t)3ULL)) goto mn103;
                                gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                                gm_l15 = t105;
                                {
                                    void * t107 = gm_l2;
                                    gm_b16_8 t108 = gm_l15;
                                    memset(&(*(gm_teti411eujvf4 *)(((uint8_t *)t107) + 8)), 0, sizeof (*(gm_teti411eujvf4 *)(((uint8_t *)t107) + 8)));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t107) + 8)) + 0, ((uint8_t)5ULL));
                                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t107) + 8)) + 8, t108);
                                }
                                goto me51;
mn103:;
ma56:;
                                gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 131, 13);
me51:;
                            }
                            {
                                gm_b0_1 t109;
                                uint8_t t110 = 0;
                                F.ret = t109;
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
                        uint64_t t113 = gm_l8;
                        uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                        uint64_t t116 = r114;
                        gm_l8 = t116;
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
            gm_b0_1 t117;
            uint8_t t118 = 0;
            F.ret = t117;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t120 = gm_l2;
        uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t120) + 8) + 0));
        if (t121 != ((uint8_t)0ULL)) goto ie119;
        gm_tbci5ospk5nnn t122 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t120) + 8) + 8));
        F.l16 = t122;
        uint8_t t123 = gm_l3;
        if (!t123) goto ie119;
        {
            void * t126 = F.l0;
            void * t127 = (void *)(((uint8_t *)t126) + 8);
            gm_t88rcucg1b7uk t128 = gm_l4;
            gm_t53at80pfdus3 t129;
            gm_f_kind_jcnt4hm((void *)&t129, t127, GM_CONV(gm_csguankko4lmhd, t128));
            uint8_t t130;
            uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&t129) + 0));
            if (t133 != ((uint8_t)3ULL)) goto mn132;
            t130 = ((uint8_t)1);
            goto me131;
mn132:;
            t130 = ((uint8_t)0);
            goto me131;
mn134:;
            gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 151, 12);
me131:;
            if (!t130) goto ie125;
            {
                void * t136 = gm_l2;
                uint8_t *t137 = (((uint8_t *)t136) + 8);
                gm_tngtuiuskn938 t138;
                gm_f_from_int_ofjuoqh((void *)&t138, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_untyped_float_m6lbr0g((void *)&(*(gm_teti411eujvf4 *)t137), (void *)NULL, cx, NULL, &t138);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix125;
ie125:;
            void * t140 = F.l0;
            gm_t88rcucg1b7uk t141 = gm_l4;
            uint8_t t142 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t140, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)), t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t143 = (uint8_t)!t142;
            if (!t143) goto ie139;
            {
                void * t145 = F.l0;
                gm_tl874t2atnlh3 t146 = gm_l5;
                gm_t6j6smj7qoctu t147;
                gm_b16_8 t148 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t147 + 0, t148);
                void * t149 = F.l0;
                gm_t88rcucg1b7uk t150 = gm_l4;
                gm_b16_8 t151 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t149, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t147 + 16, t151);
                gm_tabb710d9mq2e t152;
                gm_f_format_afagrs5((void *)&t152, (void *)NULL, cx, NULL, &t147);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 8, gm_src5eu5l4a7gv6s, 154, 27);
                gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t152) + 8));
                gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
                gm_b0_1 t155 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t145, t146, t154);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix139;
ie139:;
ix139:;
ix125:;
        }
        goto ix119;
ie119:;
ix119:;
    }
    {
        void * t156 = gm_l2;
        uint8_t t164 = GM_LD(uint8_t, ((((uint8_t *)t156) + 8) + 0));
        switch (t164) { case ((uint8_t)0ULL): goto ma162; case ((uint8_t)1ULL): goto ma162; case ((uint8_t)2ULL): goto ma162; case ((uint8_t)3ULL): goto ma162; case ((uint8_t)4ULL): goto ma162; case ((uint8_t)5ULL): goto ma162; case ((uint8_t)6ULL): goto ma162; case ((uint8_t)7ULL): goto ma162; case ((uint8_t)8ULL): goto ma162; case ((uint8_t)9ULL): goto ma162; case ((uint8_t)10ULL): goto ma158; case ((uint8_t)11ULL): goto ma159; case ((uint8_t)12ULL): goto ma160; case ((uint8_t)13ULL): goto ma161; case ((uint8_t)14ULL): goto ma162; case ((uint8_t)15ULL): goto ma162; case ((uint8_t)16ULL): goto ma162; case ((uint8_t)17ULL): goto ma162; case ((uint8_t)18ULL): goto ma162; case ((uint8_t)19ULL): goto ma162; case ((uint8_t)20ULL): goto ma162; case ((uint8_t)21ULL): goto ma162; case ((uint8_t)22ULL): goto ma162; case ((uint8_t)23ULL): goto ma162; case ((uint8_t)24ULL): goto ma162; case ((uint8_t)25ULL): goto ma162; case ((uint8_t)26ULL): goto ma162; case ((uint8_t)27ULL): goto ma162; case ((uint8_t)28ULL): goto ma162; case ((uint8_t)29ULL): goto ma162; case ((uint8_t)30ULL): goto ma162; case ((uint8_t)31ULL): goto ma162; case ((uint8_t)32ULL): goto ma162; case ((uint8_t)33ULL): goto ma162; case ((uint8_t)34ULL): goto ma162; case ((uint8_t)35ULL): goto ma162; case ((uint8_t)36ULL): goto ma162; case ((uint8_t)37ULL): goto ma162; case ((uint8_t)38ULL): goto ma162; case ((uint8_t)39ULL): goto ma162; case ((uint8_t)40ULL): goto ma162; case ((uint8_t)41ULL): goto ma162; case ((uint8_t)42ULL): goto ma162; case ((uint8_t)43ULL): goto ma162; case ((uint8_t)44ULL): goto ma162; case ((uint8_t)45ULL): goto ma162; case ((uint8_t)46ULL): goto ma162; case ((uint8_t)47ULL): goto ma162; case ((uint8_t)48ULL): goto ma162; case ((uint8_t)49ULL): goto ma162; case ((uint8_t)50ULL): goto ma162; case ((uint8_t)51ULL): goto ma162; case ((uint8_t)52ULL): goto ma162; case ((uint8_t)53ULL): goto ma162; case ((uint8_t)54ULL): goto ma162; case ((uint8_t)55ULL): goto ma162; case ((uint8_t)56ULL): goto ma162; case ((uint8_t)57ULL): goto ma162; case ((uint8_t)58ULL): goto ma162; default: goto ma162; }
ma158:;
        uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)t156) + 8) + 0));
        if (t166 != ((uint8_t)10ULL)) goto mn165;
        gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t156) + 8) + 8) + 0));
        gm_l17 = t167;
        uint8_t t168 = GM_LD(uint8_t, (((((uint8_t *)t156) + 8) + 8) + 4));
        gm_l18 = t168;
        {
            void * t170 = F.l0;
            gm_t88rcucg1b7uk t171 = gm_l17;
            void * t172 = F.l1;
            gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)t172) + 0));
            gm_t88rcucg1b7uk t174 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t170, t171, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t174;
        }
        {
            void * t176 = F.l0;
            void * t177 = (void *)(((uint8_t *)t176) + 8);
            gm_t88rcucg1b7uk t178 = gm_l19;
            uint8_t t179 = gm_f_has_param_u9t0efs(t177, GM_CONV(gm_csguankko4lmhd, t178));
            if (!t179) goto ie175;
            {
                void * t181 = gm_l2;
                gm_t88rcucg1b7uk t182 = gm_l19;
                uint8_t t183 = gm_l18;
                gm_tkn42u7na3t81 t184;
                GM_ST(uint8_t, (uint8_t *)&t184 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t184 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t184 + 0), t182);
                GM_ST(uint8_t, ((uint8_t *)&t184 + 4), t183);
                memset(&(*(gm_teti411eujvf4 *)(((uint8_t *)t181) + 8)), 0, sizeof (*(gm_teti411eujvf4 *)(((uint8_t *)t181) + 8)));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t181) + 8)) + 0, ((uint8_t)10ULL));
                GM_ST(gm_tkn42u7na3t81, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t181) + 8)) + 8, t184);
            }
            goto ix175;
ie175:;
            {
                void * t186 = F.l0;
                gm_t88rcucg1b7uk t187 = gm_l19;
                gm_f_layout_a5ovnrm((void *)&gm_l20, (void *)NULL, cx, NULL, t186, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t188 = gm_l2;
                uint8_t *t189 = (((uint8_t *)t188) + 8);
                uint64_t t190;
                uint8_t t192 = gm_l18;
                if (!t192) goto ie191;
                uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint64_t t195 = t194;
                t190 = t195;
                goto ix191;
ie191:;
                uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 0));
                uint64_t t198 = t197;
                t190 = t198;
ix191:;
                gm_tbci5ospk5nnn t199;
                gm_f_from_u64_ud13q5v((void *)&t199, (void *)NULL, cx, NULL, t190);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t189) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t189) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t189) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t189) + 56, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t189) + 0, ((uint8_t)0ULL));
                GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_teti411eujvf4 *)t189) + 8, t199);
            }
ix175:;
        }
        goto me157;
mn165:;
ma159:;
        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)t156) + 8) + 0));
        if (t201 != ((uint8_t)11ULL)) goto mn200;
        gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t156) + 8) + 8) + 0));
        gm_l21 = t202;
        uint8_t t203 = GM_LD(uint8_t, (((((uint8_t *)t156) + 8) + 8) + 4));
        gm_l22 = t203;
        {
            void * t205 = F.l0;
            gm_t88rcucg1b7uk t206 = gm_l21;
            void * t207 = F.l1;
            gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)t207) + 0));
            gm_t88rcucg1b7uk t209 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t205, t206, t208);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t209;
        }
        {
            void * t210 = gm_l2;
            uint8_t *t211 = (((uint8_t *)t210) + 8);
            gm_teti411eujvf4 t212;
            void * t214 = F.l0;
            gm_t88rcucg1b7uk t215 = gm_l23;
            uint8_t t216 = gm_l22;
            gm_thrdpodrs8pgt t217;
            gm_f_int_limit_nncqn56((void *)&t217, (void *)NULL, cx, NULL, t214, t215, t216);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t217))[48] != 0)) goto ie213;
            F.l24 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t217) + 0));
            gm_tbci5ospk5nnn t219 = F.l24;
            gm_teti411eujvf4 t220;
            GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t220 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t220 + 8), t219);
            gm_teti411eujvf4 t221 = t220;
            t212 = t221;
            goto ix213;
ie213:;
            gm_t88rcucg1b7uk t223 = gm_l23;
            uint8_t t224 = gm_l22;
            gm_tkn42u7na3t81 t225;
            GM_ST(uint8_t, (uint8_t *)&t225 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t225 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t225 + 0), t223);
            GM_ST(uint8_t, ((uint8_t *)&t225 + 4), t224);
            gm_teti411eujvf4 t226;
            memset(&t226, 0, sizeof t226);
            GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)11ULL));
            GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t226 + 8), t225);
            gm_teti411eujvf4 t227 = t226;
            t212 = t227;
ix213:;
            GM_ST(gm_teti411eujvf4, t211, t212);
        }
        goto me157;
mn200:;
ma160:;
        uint8_t t229 = GM_LD(uint8_t, ((((uint8_t *)t156) + 8) + 0));
        if (t229 != ((uint8_t)12ULL)) goto mn228;
        gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t156) + 8) + 8));
        gm_l25 = t230;
        {
            void * t232 = F.l0;
            gm_t88rcucg1b7uk t233 = gm_l25;
            void * t234 = F.l1;
            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)t234) + 0));
            gm_t88rcucg1b7uk t236 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t232, t233, t235);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t236;
        }
        {
            void * t237 = gm_l2;
            uint8_t *t238 = (((uint8_t *)t237) + 8);
            gm_teti411eujvf4 t239;
            void * t241 = F.l0;
            void * t242 = (void *)(((uint8_t *)t241) + 8);
            gm_t88rcucg1b7uk t243 = gm_l26;
            uint8_t t244 = gm_f_has_param_u9t0efs(t242, GM_CONV(gm_csguankko4lmhd, t243));
            if (!t244) goto ie240;
            gm_t88rcucg1b7uk t246 = gm_l26;
            gm_teti411eujvf4 t247;
            memset(&t247, 0, sizeof t247);
            GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t247 + 8), t246);
            gm_teti411eujvf4 t248 = t247;
            t239 = t248;
            goto ix240;
ie240:;
            void * t250 = F.l0;
            gm_t88rcucg1b7uk t251 = gm_l26;
            gm_b16_8 t252 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t250, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t253;
            memset(&t253, 0, sizeof t253);
            GM_ST(uint8_t, ((uint8_t *)&t253 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t253 + 8), t252);
            gm_teti411eujvf4 t254 = t253;
            t239 = t254;
ix240:;
            GM_ST(gm_teti411eujvf4, t238, t239);
        }
        goto me157;
mn228:;
ma161:;
        uint8_t t256 = GM_LD(uint8_t, ((((uint8_t *)t156) + 8) + 0));
        if (t256 != ((uint8_t)13ULL)) goto mn255;
        gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t156) + 8) + 8) + 0));
        gm_l27 = t257;
        gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t156) + 8) + 8) + 4));
        gm_l28 = t258;
        uint8_t t259 = GM_LD(uint8_t, (((((uint8_t *)t156) + 8) + 8) + 8));
        gm_l29 = t259;
        {
            void * t261 = F.l0;
            gm_t88rcucg1b7uk t262 = gm_l27;
            void * t263 = F.l1;
            gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)t263) + 0));
            gm_t88rcucg1b7uk t265 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t261, t262, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t265;
        }
        {
            void * t266 = F.l0;
            gm_t88rcucg1b7uk t267 = gm_l28;
            void * t268 = F.l1;
            gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)t268) + 0));
            gm_t88rcucg1b7uk t270 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t266, t267, t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l31 = t270;
        }
        {
            void * t271 = gm_l2;
            uint8_t *t272 = (((uint8_t *)t271) + 8);
            gm_teti411eujvf4 t273;
            uint8_t t275 = 0;
            void * t276 = F.l0;
            void * t277 = (void *)(((uint8_t *)t276) + 8);
            gm_t88rcucg1b7uk t278 = gm_l30;
            uint8_t t279 = gm_f_has_param_u9t0efs(t277, GM_CONV(gm_csguankko4lmhd, t278));
            if (!t279) {
                void * t280 = F.l0;
                void * t281 = (void *)(((uint8_t *)t280) + 8);
                gm_t88rcucg1b7uk t282 = gm_l31;
                uint8_t t283 = gm_f_has_param_u9t0efs(t281, GM_CONV(gm_csguankko4lmhd, t282));
                t275 = t283;
            } else t275 = 1;
            if (!t275) goto ie274;
            gm_t88rcucg1b7uk t285 = gm_l30;
            gm_t88rcucg1b7uk t286 = gm_l31;
            uint8_t t287 = gm_l29;
            gm_t3rcjibl8kkg4 t288;
            GM_ST(uint8_t, (uint8_t *)&t288 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t288 + 10, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t288 + 0), t285);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t288 + 4), t286);
            GM_ST(uint8_t, ((uint8_t *)&t288 + 8), t287);
            gm_teti411eujvf4 t289;
            memset(&t289, 0, sizeof t289);
            GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)13ULL));
            GM_ST(gm_t3rcjibl8kkg4, ((uint8_t *)&t289 + 8), t288);
            gm_teti411eujvf4 t290 = t289;
            t273 = t290;
            goto ix274;
ie274:;
            gm_t88rcucg1b7uk t292 = gm_l30;
            gm_t88rcucg1b7uk t293 = gm_l31;
            uint8_t t294 = (gm_eq9m6mvtmgde(((uint8_t *)&t292), ((uint8_t *)&t293)));
            uint8_t t295 = gm_l29;
            uint8_t t296 = (!(t294 == t295));
            gm_teti411eujvf4 t297;
            memset(&t297, 0, sizeof t297);
            GM_ST(uint8_t, ((uint8_t *)&t297 + 0), ((uint8_t)4ULL));
            GM_ST(uint8_t, ((uint8_t *)&t297 + 8), t296);
            gm_teti411eujvf4 t298 = t297;
            t273 = t298;
ix274:;
            GM_ST(gm_teti411eujvf4, t272, t273);
        }
        goto me157;
mn255:;
ma162:;
        goto me157;
mn299:;
ma163:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 157, 5);
me157:;
    }
    {
        void * t302 = gm_l2;
        uint8_t t303 = GM_LD(uint8_t, ((((uint8_t *)t302) + 8) + 0));
        if (t303 != ((uint8_t)2ULL)) goto ie301;
        void * t304 = GM_LD(void *, ((((uint8_t *)t302) + 8) + 8));
        gm_l32 = t304;
        {
            void * t307 = F.l0;
            void * t308 = (void *)(((uint8_t *)t307) + 8);
            gm_t88rcucg1b7uk t309 = gm_l4;
            gm_t53at80pfdus3 t310;
            gm_f_kind_jcnt4hm((void *)&t310, t308, GM_CONV(gm_csguankko4lmhd, t309));
            uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&t310) + 0));
            if (t311 != ((uint8_t)3ULL)) goto ie306;
            uint16_t t312 = GM_LD(uint16_t, (((uint8_t *)&t310) + 8));
            gm_l33 = t312;
            {
                void * t314 = gm_l32;
                uint16_t t315 = gm_l33;
                double t316 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t314)), t315);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t316;
            }
            {
                double t318 = gm_l34;
                double t319 = gm_l34;
                double t320 = (t318 - t319);
                uint8_t t321 = (!(t320 == ((double)0e0)));
                if (!t321) goto ie317;
                {
                    void * t323 = F.l0;
                    gm_tl874t2atnlh3 t324 = gm_l5;
                    void * t325 = F.l0;
                    gm_t88rcucg1b7uk t326 = gm_l4;
                    gm_b16_8 t327 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t325, t326);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t328;
                    GM_ST(gm_b16_8, ((uint8_t *)&t328 + 0), t327);
                    gm_tabb710d9mq2e t329;
                    gm_f_format_r4smojn((void *)&t329, (void *)NULL, cx, NULL, t328);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t329))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t329) + 8, gm_src5eu5l4a7gv6s, 186, 31);
                    gm_b16_8 t330 = GM_LD(gm_b16_8, (((uint8_t *)&t329) + 8));
                    gm_b16_8 t331 = GM_LD(gm_b16_8, ((uint8_t *)&t330));
                    gm_b0_1 t332 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t323, t324, t331);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix317;
ie317:;
                {
                    void * t334 = gm_l2;
                    double t335 = gm_l34;
                    memset(&(*(gm_teti411eujvf4 *)(((uint8_t *)t334) + 8)), 0, sizeof (*(gm_teti411eujvf4 *)(((uint8_t *)t334) + 8)));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t334) + 8)) + 0, ((uint8_t)1ULL));
                    GM_ST(double, (uint8_t *)&(*(gm_teti411eujvf4 *)(((uint8_t *)t334) + 8)) + 8, t335);
                }
ix317:;
            }
            goto ix306;
ie306:;
            {
                void * t337 = F.l0;
                gm_tl874t2atnlh3 t338 = gm_l5;
                void * t339 = F.l0;
                gm_t88rcucg1b7uk t340 = gm_l4;
                gm_b16_8 t341 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t339, t340);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t342;
                GM_ST(gm_b16_8, ((uint8_t *)&t342 + 0), t341);
                gm_tabb710d9mq2e t343;
                gm_f_format_ht23jk8((void *)&t343, (void *)NULL, cx, NULL, t342);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 8, gm_src5eu5l4a7gv6s, 191, 27);
                gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&t343) + 8));
                gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                gm_b0_1 t346 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t337, t338, t345);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix306:;
        }
        goto ix301;
ie301:;
ix301:;
    }
    {
        void * t348 = gm_l2;
        uint8_t t349 = GM_LD(uint8_t, ((((uint8_t *)t348) + 8) + 0));
        if (t349 != ((uint8_t)58ULL)) goto ie347;
        gm_t710lsrq1m8gk t350 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t348) + 8) + 8));
        F.l35 = t350;
        {
            {
                gm_l37 = ((uint64_t)0ULL);
            }
            {
                uint64_t t353 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 8) + 8);
                gm_l36 = t353;
            }
            {
                for (;;) {
                    uint64_t t355 = gm_l37;
                    uint64_t t356 = gm_l36;
                    uint8_t t357 = (t355 < t356);
                    if (!t357) goto lb354;
                    {
                        uint64_t t360 = gm_l37;
                        uint64_t t363 = GM_LD(uint64_t, ((uint8_t *)&t360));
                        gm_l38 = t363;
                        {
                            uint64_t t366 = gm_l38;
                            uint64_t t367 = (uintptr_t)t366;
                            uint64_t t368 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 8) + 8);
                            if (t367 >= (uintptr_t)(t368)) gm_trap_index(t367, t368, gm_src5eu5l4a7gv6s, 197, 35);
                            uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l35) + 8)) + t367 * 32) + 0));
                            if (t369 != ((uint8_t)1ULL)) goto ie365;
                            void * t370 = GM_LD(void *, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l35) + 8)) + t367 * 32) + 8) + 16));
                            gm_l39 = t370;
                            {
                                uint64_t t372 = gm_l38;
                                uint64_t t373 = (uintptr_t)t372;
                                uint64_t t374 = GM_LD(uintptr_t, (((uint8_t *)&F.l35) + 8) + 8);
                                if (t373 >= (uintptr_t)(t374)) gm_trap_index(t373, t374, gm_src5eu5l4a7gv6s, 198, 17);
                                uint8_t *t375 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l35) + 8)) + t373 * 32);
                                gm_tpe1hv3fekn05 t376;
                                void * t377 = F.l0;
                                void * t378 = F.l0;
                                void * t379 = gm_l39;
                                gm_t88rcucg1b7uk t380 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t379) + 0));
                                void * t381 = F.l1;
                                gm_b16_8 t382 = GM_LD(gm_b16_8, (((uint8_t *)t381) + 0));
                                gm_t88rcucg1b7uk t383 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t378, t380, t382);
                                if (gm_unw.kind) {
                                    goto gm_ll358;
                                }
                                gm_b16_8 t384 = gm_f_handlers_of_type_5gareb4((void *)NULL, cx, NULL, t377, t383);
                                if (gm_unw.kind) {
                                    goto gm_ll358;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t376 + 0, t384);
                                void * t385 = gm_l39;
                                GM_ST(void *, (uint8_t *)&t376 + 16, t385);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4lnf71mruqpt *)t375) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t4lnf71mruqpt *)t375) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t4lnf71mruqpt *)t375) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4lnf71mruqpt *)t375) + 0, ((uint8_t)1ULL));
                                GM_ST(gm_tpe1hv3fekn05, (uint8_t *)&(*(gm_t4lnf71mruqpt *)t375) + 8, t376);
                            }
                            goto ix365;
ie365:;
ix365:;
                        }
                        goto me361;
mn362:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 196, 9);
me361:;
                    }
lc354:;
                    {
                        uint64_t t387 = gm_l37;
                        uint64_t r388; bool o389 = __builtin_add_overflow(t387, ((uint64_t)1ULL), &r388);
                        uint64_t t390 = r388;
                        gm_l37 = t390;
                    }
                }
gm_ll358:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb354; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc354; }
                }
                goto gm_lad;
lb354:;
            }
        }
        goto ix347;
ie347:;
ix347:;
    }
    {
        void * t391 = gm_l2;
        uint8_t t401 = GM_LD(uint8_t, ((((uint8_t *)t391) + 8) + 0));
        switch (t401) { case ((uint8_t)0ULL): goto ma399; case ((uint8_t)1ULL): goto ma399; case ((uint8_t)2ULL): goto ma399; case ((uint8_t)3ULL): goto ma399; case ((uint8_t)4ULL): goto ma399; case ((uint8_t)5ULL): goto ma399; case ((uint8_t)6ULL): goto ma399; case ((uint8_t)7ULL): goto ma399; case ((uint8_t)8ULL): goto ma399; case ((uint8_t)9ULL): goto ma399; case ((uint8_t)10ULL): goto ma399; case ((uint8_t)11ULL): goto ma399; case ((uint8_t)12ULL): goto ma399; case ((uint8_t)13ULL): goto ma399; case ((uint8_t)14ULL): goto ma399; case ((uint8_t)15ULL): goto ma399; case ((uint8_t)16ULL): goto ma399; case ((uint8_t)17ULL): goto ma399; case ((uint8_t)18ULL): goto ma399; case ((uint8_t)19ULL): goto ma399; case ((uint8_t)20ULL): goto ma399; case ((uint8_t)21ULL): goto ma399; case ((uint8_t)22ULL): goto ma399; case ((uint8_t)23ULL): goto ma399; case ((uint8_t)24ULL): goto ma399; case ((uint8_t)25ULL): goto ma399; case ((uint8_t)26ULL): goto ma395; case ((uint8_t)27ULL): goto ma396; case ((uint8_t)28ULL): goto ma397; case ((uint8_t)29ULL): goto ma399; case ((uint8_t)30ULL): goto ma399; case ((uint8_t)31ULL): goto ma399; case ((uint8_t)32ULL): goto ma399; case ((uint8_t)33ULL): goto ma399; case ((uint8_t)34ULL): goto ma393; case ((uint8_t)35ULL): goto ma399; case ((uint8_t)36ULL): goto ma399; case ((uint8_t)37ULL): goto ma399; case ((uint8_t)38ULL): goto ma399; case ((uint8_t)39ULL): goto ma399; case ((uint8_t)40ULL): goto ma399; case ((uint8_t)41ULL): goto ma399; case ((uint8_t)42ULL): goto ma399; case ((uint8_t)43ULL): goto ma399; case ((uint8_t)44ULL): goto ma399; case ((uint8_t)45ULL): goto ma399; case ((uint8_t)46ULL): goto ma399; case ((uint8_t)47ULL): goto ma399; case ((uint8_t)48ULL): goto ma399; case ((uint8_t)49ULL): goto ma399; case ((uint8_t)50ULL): goto ma399; case ((uint8_t)51ULL): goto ma399; case ((uint8_t)52ULL): goto ma399; case ((uint8_t)53ULL): goto ma399; case ((uint8_t)54ULL): goto ma399; case ((uint8_t)55ULL): goto ma398; case ((uint8_t)56ULL): goto ma399; case ((uint8_t)57ULL): goto ma399; case ((uint8_t)58ULL): goto ma399; default: goto ma399; }
ma393:;
        uint8_t t403 = GM_LD(uint8_t, ((((uint8_t *)t391) + 8) + 0));
        if (t403 != ((uint8_t)34ULL)) goto mn402;
        uint8_t t404 = GM_LD(uint8_t, ((((((uint8_t *)t391) + 8) + 8) + 0) + 0));
        if (t404 != ((uint8_t)0ULL)) goto mn402;
        uint32_t t405 = GM_LD(uint32_t, ((((((uint8_t *)t391) + 8) + 8) + 0) + 8));
        gm_l40 = t405;
        gm_b16_8 t406 = GM_LD(gm_b16_8, (((((uint8_t *)t391) + 8) + 8) + 40));
        gm_l41 = t406;
        {
            void * t408 = gm_l2;
            uint8_t *t409 = (((uint8_t *)t408) + 8);
            gm_to76jli3bqddq t410;
            void * t411 = F.l0;
            void * t412 = F.l1;
            uint32_t t413 = gm_l40;
            uint32_t t414 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t411, t412, t413);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tsth9qu1dv0g3 t415;
            memset(&t415, 0, sizeof t415);
            GM_ST(uint8_t, ((uint8_t *)&t415 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t415 + 8), t414);
            GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t410 + 0, t415);
            gm_b16_8 t416 = gm_l41;
            GM_ST(gm_b16_8, (uint8_t *)&t410 + 40, t416);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t409) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t409) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t409) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t409) + 0, ((uint8_t)34ULL));
            GM_ST(gm_to76jli3bqddq, (uint8_t *)&(*(gm_teti411eujvf4 *)t409) + 8, t410);
        }
        goto me392;
mn402:;
ma394:;
        uint8_t t418 = GM_LD(uint8_t, ((((uint8_t *)t391) + 8) + 0));
        if (t418 != ((uint8_t)34ULL)) goto mn417;
        uint8_t t419 = GM_LD(uint8_t, ((((((uint8_t *)t391) + 8) + 8) + 0) + 0));
        if (t419 != ((uint8_t)1ULL)) goto mn417;
        gm_tiilmfd16v3fc t420 = GM_LD(gm_tiilmfd16v3fc, (((((((uint8_t *)t391) + 8) + 8) + 0) + 8) + 0));
        gm_l42 = t420;
        uint32_t t421 = GM_LD(uint32_t, (((((((uint8_t *)t391) + 8) + 8) + 0) + 8) + 24));
        gm_l43 = t421;
        gm_b16_8 t422 = GM_LD(gm_b16_8, (((((uint8_t *)t391) + 8) + 8) + 40));
        gm_l44 = t422;
        {
            void * t424 = gm_l2;
            uint8_t *t425 = (((uint8_t *)t424) + 8);
            gm_to76jli3bqddq t426;
            gm_t419mo45g7j0a t427;
            memset((uint8_t *)&t427 + 28, 0, 4);
            void * t428 = F.l0;
            void * t429 = (void *)((uint8_t *)&gm_l42);
            void * t430 = F.l1;
            gm_b16_8 t431 = GM_LD(gm_b16_8, (((uint8_t *)t430) + 0));
            gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t427 + 0)), (void *)NULL, cx, NULL, t428, t429, t431);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t432 = gm_l43;
            GM_ST(uint32_t, (uint8_t *)&t427 + 24, t432);
            gm_tsth9qu1dv0g3 t433;
            GM_ST(uint8_t, (uint8_t *)&t433 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t433 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t433 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t433 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t433 + 8), t427);
            GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t426 + 0, t433);
            gm_b16_8 t434 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t426 + 40, t434);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t425) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t425) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t425) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t425) + 0, ((uint8_t)34ULL));
            GM_ST(gm_to76jli3bqddq, (uint8_t *)&(*(gm_teti411eujvf4 *)t425) + 8, t426);
        }
        goto me392;
mn417:;
ma395:;
        uint8_t t436 = GM_LD(uint8_t, ((((uint8_t *)t391) + 8) + 0));
        if (t436 != ((uint8_t)26ULL)) goto mn435;
        uint32_t t437 = GM_LD(uint32_t, ((((uint8_t *)t391) + 8) + 8));
        gm_l45 = t437;
        {
            void * t439 = gm_l2;
            uint8_t *t440 = (((uint8_t *)t439) + 8);
            void * t441 = F.l0;
            void * t442 = F.l1;
            uint32_t t443 = gm_l45;
            uint32_t t444 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t441, t442, t443);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_teti411eujvf4 *)t440), 0, sizeof (*(gm_teti411eujvf4 *)t440));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t440) + 0, ((uint8_t)26ULL));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t440) + 8, t444);
        }
        goto me392;
mn435:;
ma396:;
        uint8_t t446 = GM_LD(uint8_t, ((((uint8_t *)t391) + 8) + 0));
        if (t446 != ((uint8_t)27ULL)) goto mn445;
        uint32_t t447 = GM_LD(uint32_t, ((((uint8_t *)t391) + 8) + 8));
        gm_l46 = t447;
        {
            void * t449 = gm_l2;
            uint8_t *t450 = (((uint8_t *)t449) + 8);
            void * t451 = F.l0;
            void * t452 = F.l1;
            uint32_t t453 = gm_l46;
            uint32_t t454 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t451, t452, t453);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_teti411eujvf4 *)t450), 0, sizeof (*(gm_teti411eujvf4 *)t450));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t450) + 0, ((uint8_t)27ULL));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t450) + 8, t454);
        }
        goto me392;
mn445:;
ma397:;
        uint8_t t456 = GM_LD(uint8_t, ((((uint8_t *)t391) + 8) + 0));
        if (t456 != ((uint8_t)28ULL)) goto mn455;
        uint32_t t457 = GM_LD(uint32_t, ((((uint8_t *)t391) + 8) + 8));
        gm_l47 = t457;
        {
            void * t459 = gm_l2;
            uint8_t *t460 = (((uint8_t *)t459) + 8);
            void * t461 = F.l0;
            void * t462 = F.l1;
            uint32_t t463 = gm_l47;
            uint32_t t464 = gm_f_mono_handler_38b78qh((void *)NULL, cx, NULL, t461, t462, t463);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_teti411eujvf4 *)t460), 0, sizeof (*(gm_teti411eujvf4 *)t460));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t460) + 0, ((uint8_t)28ULL));
            GM_ST(uint32_t, (uint8_t *)&(*(gm_teti411eujvf4 *)t460) + 8, t464);
        }
        goto me392;
mn455:;
ma398:;
        uint8_t t466 = GM_LD(uint8_t, ((((uint8_t *)t391) + 8) + 0));
        if (t466 != ((uint8_t)55ULL)) goto mn465;
        void * t467 = GM_LD(void *, ((((uint8_t *)t391) + 8) + 8));
        gm_l48 = t467;
        void * t469 = gm_l48;
        uint8_t t470 = GM_LD(uint8_t, ((((uint8_t *)t469) + 0) + 0));
        if (t470 != ((uint8_t)0ULL)) goto ie468;
        gm_tiilmfd16v3fc t471 = GM_LD(gm_tiilmfd16v3fc, (((((uint8_t *)t469) + 0) + 8) + 0));
        gm_l49 = t471;
        uint32_t t472 = GM_LD(uint32_t, (((((uint8_t *)t469) + 0) + 8) + 24));
        gm_l50 = t472;
        {
            void * t474 = gm_l48;
            uint8_t *t475 = (((uint8_t *)t474) + 0);
            gm_t419mo45g7j0a t476;
            memset((uint8_t *)&t476 + 28, 0, 4);
            void * t477 = F.l0;
            void * t478 = (void *)((uint8_t *)&gm_l49);
            void * t479 = F.l1;
            gm_b16_8 t480 = GM_LD(gm_b16_8, (((uint8_t *)t479) + 0));
            gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t476 + 0)), (void *)NULL, cx, NULL, t477, t478, t480);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t481 = gm_l50;
            GM_ST(uint32_t, (uint8_t *)&t476 + 24, t481);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th1g69d7vpipg *)t475) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th1g69d7vpipg *)t475) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th1g69d7vpipg *)t475) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th1g69d7vpipg *)t475) + 0, ((uint8_t)0ULL));
            GM_ST(gm_t419mo45g7j0a, (uint8_t *)&(*(gm_th1g69d7vpipg *)t475) + 8, t476);
        }
        goto ix468;
ie468:;
ix468:;
        goto me392;
mn465:;
ma399:;
        goto me392;
mn482:;
ma400:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 202, 5);
me392:;
    }
    {
        void * t484 = gm_l2;
        gm_closure t485 = ((gm_closure){(void *)gm_f_fn_ah95er7, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t484, t485);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t486 = gm_l2;
        uint8_t t493 = GM_LD(uint8_t, ((((uint8_t *)t486) + 8) + 0));
        switch (t493) { case ((uint8_t)0ULL): goto ma491; case ((uint8_t)1ULL): goto ma491; case ((uint8_t)2ULL): goto ma491; case ((uint8_t)3ULL): goto ma491; case ((uint8_t)4ULL): goto ma491; case ((uint8_t)5ULL): goto ma491; case ((uint8_t)6ULL): goto ma491; case ((uint8_t)7ULL): goto ma491; case ((uint8_t)8ULL): goto ma491; case ((uint8_t)9ULL): goto ma491; case ((uint8_t)10ULL): goto ma491; case ((uint8_t)11ULL): goto ma491; case ((uint8_t)12ULL): goto ma491; case ((uint8_t)13ULL): goto ma491; case ((uint8_t)14ULL): goto ma491; case ((uint8_t)15ULL): goto ma491; case ((uint8_t)16ULL): goto ma491; case ((uint8_t)17ULL): goto ma491; case ((uint8_t)18ULL): goto ma491; case ((uint8_t)19ULL): goto ma491; case ((uint8_t)20ULL): goto ma491; case ((uint8_t)21ULL): goto ma491; case ((uint8_t)22ULL): goto ma491; case ((uint8_t)23ULL): goto ma491; case ((uint8_t)24ULL): goto ma491; case ((uint8_t)25ULL): goto ma491; case ((uint8_t)26ULL): goto ma491; case ((uint8_t)27ULL): goto ma491; case ((uint8_t)28ULL): goto ma491; case ((uint8_t)29ULL): goto ma491; case ((uint8_t)30ULL): goto ma491; case ((uint8_t)31ULL): goto ma491; case ((uint8_t)32ULL): goto ma491; case ((uint8_t)33ULL): goto ma491; case ((uint8_t)34ULL): goto ma491; case ((uint8_t)35ULL): goto ma491; case ((uint8_t)36ULL): goto ma491; case ((uint8_t)37ULL): goto ma491; case ((uint8_t)38ULL): goto ma491; case ((uint8_t)39ULL): goto ma491; case ((uint8_t)40ULL): goto ma491; case ((uint8_t)41ULL): goto ma491; case ((uint8_t)42ULL): goto ma491; case ((uint8_t)43ULL): goto ma491; case ((uint8_t)44ULL): goto ma488; case ((uint8_t)45ULL): goto ma491; case ((uint8_t)46ULL): goto ma491; case ((uint8_t)47ULL): goto ma489; case ((uint8_t)48ULL): goto ma490; case ((uint8_t)49ULL): goto ma491; case ((uint8_t)50ULL): goto ma491; case ((uint8_t)51ULL): goto ma491; case ((uint8_t)52ULL): goto ma491; case ((uint8_t)53ULL): goto ma491; case ((uint8_t)54ULL): goto ma491; case ((uint8_t)55ULL): goto ma491; case ((uint8_t)56ULL): goto ma491; case ((uint8_t)57ULL): goto ma491; case ((uint8_t)58ULL): goto ma491; default: goto ma491; }
ma488:;
        uint8_t t495 = GM_LD(uint8_t, ((((uint8_t *)t486) + 8) + 0));
        if (t495 != ((uint8_t)44ULL)) goto mn494;
        gm_b16_8 t496 = GM_LD(gm_b16_8, (((((uint8_t *)t486) + 8) + 8) + 8));
        gm_l51 = t496;
        {
            gm_b16_8 t498 = gm_l51;
            gm_l52 = t498;
        }
        {
            gm_l53 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t500 = gm_l53;
                uint64_t t501 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                uint8_t t502 = (t500 < t501);
                if (!t502) goto lb499;
                {
                    uint64_t t505 = gm_l53;
                    uint64_t t506 = (uintptr_t)t505;
                    uint64_t t507 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                    gm_tbbqefuhv2r8h t510 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t506 * 24));
                    gm_l54 = t510;
                    {
                        void * t512 = F.l0;
                        void * t513 = F.l1;
                        void * t514 = GM_LD(void *, (((uint8_t *)&gm_l54) + 0));
                        gm_b0_1 t515 = gm_f_mono_pat_lipeb0h((void *)NULL, cx, NULL, t512, t513, t514);
                        if (gm_unw.kind) {
                            goto gm_ll503;
                        }
                    }
                    goto me508;
mn509:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 213, 28);
me508:;
                }
lc499:;
                {
                    uint64_t t517 = gm_l53;
                    uint64_t r518; bool o519 = __builtin_add_overflow(t517, ((uint64_t)1ULL), &r518);
                    uint64_t t520 = r518;
                    gm_l53 = t520;
                }
            }
gm_ll503:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb499; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc499; }
            }
            goto gm_lad;
lb499:;
        }
        goto me487;
mn494:;
ma489:;
        uint8_t t522 = GM_LD(uint8_t, ((((uint8_t *)t486) + 8) + 0));
        if (t522 != ((uint8_t)47ULL)) goto mn521;
        gm_trl2khmuphncd t523 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t486) + 8) + 8));
        gm_l55 = t523;
        void * t524 = F.l0;
        void * t525 = F.l1;
        gm_b16_8 t526 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 0));
        gm_b0_1 t527 = gm_f_mono_cond_pats_armprh7((void *)NULL, cx, NULL, t524, t525, t526);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me487;
mn521:;
ma490:;
        uint8_t t529 = GM_LD(uint8_t, ((((uint8_t *)t486) + 8) + 0));
        if (t529 != ((uint8_t)48ULL)) goto mn528;
        gm_t710lsrq1m8gk t530 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t486) + 8) + 8));
        F.l56 = t530;
        void * t531 = F.l0;
        void * t532 = F.l1;
        gm_b16_8 t533 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 8));
        gm_b0_1 t534 = gm_f_mono_cond_pats_armprh7((void *)NULL, cx, NULL, t531, t532, t533);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me487;
mn528:;
ma491:;
        goto me487;
mn535:;
ma492:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 212, 5);
me487:;
    }
    {
        void * t538 = gm_l2;
        uint8_t t539 = GM_LD(uint8_t, ((((uint8_t *)t538) + 8) + 0));
        if (t539 != ((uint8_t)55ULL)) goto ie537;
        void * t540 = GM_LD(void *, ((((uint8_t *)t538) + 8) + 8));
        gm_l57 = t540;
        void * t541 = F.l1;
        uint64_t t542 = GM_LD(uintptr_t, (((uint8_t *)t541) + 0) + 8);
        uint8_t t543 = (t542 > ((uint64_t)0ULL));
        if (!t543) goto ie537;
        {
            {
                gm_l59 = ((uint64_t)0ULL);
            }
            {
                void * t546 = gm_l57;
                uint64_t t547 = GM_LD(uintptr_t, (((uint8_t *)t546) + 56) + 8);
                void * t548 = gm_l57;
                uint64_t t549 = GM_LD(uintptr_t, (((uint8_t *)t548) + 72) + 8);
                uint64_t t550 = (t547 < t549 ? t547 : t549);
                gm_l58 = t550;
            }
            {
                for (;;) {
                    uint64_t t552 = gm_l59;
                    uint64_t t553 = gm_l58;
                    uint8_t t554 = (t552 < t553);
                    if (!t554) goto lb551;
                    {
                        uint64_t t557 = gm_l59;
                        uint64_t t560 = GM_LD(uint64_t, ((uint8_t *)&t557));
                        gm_l60 = t560;
                        {
                            void * t563 = F.l0;
                            uint64_t t564 = gm_l60;
                            uint64_t t565 = (uintptr_t)t564;
                            void * t566 = gm_l57;
                            uint64_t t567 = GM_LD(uintptr_t, (((uint8_t *)t566) + 56) + 8);
                            if (t565 >= (uintptr_t)(t567)) gm_trap_index(t565, t567, gm_src5eu5l4a7gv6s, 222, 38);
                            void * t568 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t566) + 56)) + t565 * 24);
                            uint64_t t569 = gm_l60;
                            uint64_t t570 = (uintptr_t)t569;
                            void * t571 = gm_l57;
                            uint64_t t572 = GM_LD(uintptr_t, (((uint8_t *)t571) + 72) + 8);
                            if (t570 >= (uintptr_t)(t572)) gm_trap_index(t570, t572, gm_src5eu5l4a7gv6s, 222, 50);
                            gm_t88rcucg1b7uk t573 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t571) + 72)) + t570 * 88) + 0));
                            gm_th2lqbo77pvsc t574;
                            gm_f_check_spec_4vp7jhg((void *)&t574, (void *)NULL, cx, NULL, t563, t568, t573);
                            if (gm_unw.kind) {
                                goto gm_ll555;
                            }
                            if (!((((uint8_t *)&t574))[16] != 0)) goto ie562;
                            gm_b16_8 t575 = GM_LD(gm_b16_8, ((uint8_t *)&t574));
                            gm_l61 = t575;
                            {
                                void * t577 = F.l0;
                                gm_tl874t2atnlh3 t578 = gm_l5;
                                gm_b16_8 t579 = gm_l61;
                                gm_tnpifv20irnph t580;
                                GM_ST(gm_b16_8, ((uint8_t *)&t580 + 0), t579);
                                gm_tabb710d9mq2e t581;
                                gm_f_format_mlqiori((void *)&t581, (void *)NULL, cx, NULL, t580);
                                if (gm_unw.kind) {
                                    goto gm_ll555;
                                }
                                if (((((uint8_t *)&t581))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t581) + 8, gm_src5eu5l4a7gv6s, 223, 31);
                                gm_b16_8 t582 = GM_LD(gm_b16_8, (((uint8_t *)&t581) + 8));
                                gm_b16_8 t583 = GM_LD(gm_b16_8, ((uint8_t *)&t582));
                                gm_b0_1 t584 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t577, t578, t583);
                                if (gm_unw.kind) {
                                    goto gm_ll555;
                                }
                            }
                            goto ix562;
ie562:;
ix562:;
                        }
                        goto me558;
mn559:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 221, 9);
me558:;
                    }
lc551:;
                    {
                        uint64_t t586 = gm_l59;
                        uint64_t r587; bool o588 = __builtin_add_overflow(t586, ((uint64_t)1ULL), &r587);
                        uint64_t t589 = r587;
                        gm_l59 = t589;
                    }
                }
gm_ll555:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb551; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc551; }
                }
                goto gm_lad;
lb551:;
            }
        }
        goto ix537;
ie537:;
ix537:;
    }
    {
        void * t591 = gm_l2;
        uint8_t t592;
        uint8_t t595 = GM_LD(uint8_t, ((((uint8_t *)t591) + 8) + 0));
        if (t595 != ((uint8_t)56ULL)) goto mn594;
        t592 = ((uint8_t)1);
        goto me593;
mn594:;
        t592 = ((uint8_t)0);
        goto me593;
mn596:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 227, 8);
me593:;
        if (!t592) goto ie590;
        {
            void * t598 = F.l0;
            void * t599 = F.l1;
            void * t600 = gm_l2;
            gm_b0_1 t601 = gm_f_resolve_write_e97fjom((void *)NULL, cx, NULL, t598, t599, t600);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t602;
            uint8_t t603 = 0;
            F.ret = t602;
            goto gm_fret;
        }
        goto ix590;
ie590:;
ix590:;
    }
    {
        void * t605 = gm_l2;
        uint8_t t606 = GM_LD(uint8_t, ((((uint8_t *)t605) + 8) + 0));
        if (t606 != ((uint8_t)33ULL)) goto ie604;
        uint8_t t607 = GM_LD(uint8_t, ((((((uint8_t *)t605) + 8) + 8) + 0) + 0));
        if (t607 != ((uint8_t)80ULL)) goto ie604;
        uint32_t t608 = GM_LD(uint32_t, ((((((uint8_t *)t605) + 8) + 8) + 0) + 4));
        gm_l62 = t608;
        gm_b16_8 t609 = GM_LD(gm_b16_8, (((((uint8_t *)t605) + 8) + 8) + 16));
        gm_l63 = t609;
        {
            void * t611 = F.l0;
            uint64_t t612 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t613 = GM_LD(uintptr_t, ((uint8_t *)&gm_l63) + 8);
            if (t612 >= (uintptr_t)(t613)) gm_trap_index(t612, t613, gm_src5eu5l4a7gv6s, 233, 33);
            void * t614 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l63)) + t612 * 88);
            uint32_t t615 = gm_l62;
            uint64_t t616 = (uint64_t)t615;
            uint64_t t617 = (uintptr_t)t616;
            void * t618 = F.l0;
            uint64_t t619 = GM_LD(uintptr_t, ((((uint8_t *)t618) + 2232) + 0) + 8);
            if (t617 >= (uintptr_t)(t619)) gm_trap_index(t617, t619, gm_src5eu5l4a7gv6s, 233, 42);
            void * t620 = gm_l2;
            gm_tl874t2atnlh3 t621 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t620) + 72));
            void * t622 = F.l1;
            gm_tl874t2atnlh3 t623 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t622) + 96));
            gm_toceffb697p0u t624;
            GM_ST(uint8_t, (uint8_t *)&t624 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t624 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t624), t623);
            ((uint8_t *)&t624)[12] = 1;
            gm_b0_1 t625 = gm_f_check_static_assert_s4f7i6i((void *)NULL, cx, NULL, t611, t614, &(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t618) + 2232) + 0)) + t617 * 24)), t621, t624);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t626 = gm_l2;
            uint8_t *t627 = ((uint8_t *)t626);
            void * t628 = F.l0;
            void * t629 = gm_l2;
            gm_tl874t2atnlh3 t630 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t629) + 72));
            void * t631 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t628, t630);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t69m56meqefs8 t632 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t631));
            GM_ST(gm_t69m56meqefs8, t627, t632);
        }
        {
            gm_b0_1 t633;
            uint8_t t634 = 0;
            F.ret = t633;
            goto gm_fret;
        }
        goto ix604;
ie604:;
ix604:;
    }
    {
        void * t636 = gm_l2;
        uint8_t t637;
        uint8_t t640 = GM_LD(uint8_t, ((((uint8_t *)t636) + 8) + 0));
        if (t640 != ((uint8_t)57ULL)) goto mn639;
        t637 = ((uint8_t)1);
        goto me638;
mn639:;
        t637 = ((uint8_t)0);
        goto me638;
mn641:;
        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 237, 8);
me638:;
        if (!t637) goto ie635;
        {
            void * t643 = gm_l2;
            uint8_t *t644 = ((uint8_t *)t643);
            void * t645 = F.l0;
            void * t646 = F.l1;
            void * t647 = GM_LD(void *, (((uint8_t *)t646) + 112));
            void * t648 = gm_l2;
            void * t649 = gm_f_soa_expand_2e8rmqf((void *)NULL, cx, NULL, t645, t647, t648);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t69m56meqefs8 t650 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t649));
            GM_ST(gm_t69m56meqefs8, t644, t650);
        }
        {
            gm_b0_1 t651;
            uint8_t t652 = 0;
            F.ret = t651;
            goto gm_fret;
        }
        goto ix635;
ie635:;
ix635:;
    }
    {
        void * t654 = gm_l2;
        uint8_t t655 = GM_LD(uint8_t, ((((uint8_t *)t654) + 8) + 0));
        if (t655 != ((uint8_t)33ULL)) goto ie653;
        uint8_t t656 = GM_LD(uint8_t, ((((((uint8_t *)t654) + 8) + 8) + 0) + 0));
        if (t656 != ((uint8_t)7ULL)) goto ie653;
        gm_b16_8 t657 = GM_LD(gm_b16_8, (((((uint8_t *)t654) + 8) + 8) + 16));
        gm_l64 = t657;
        void * t658 = F.l1;
        uint64_t t659 = GM_LD(uintptr_t, (((uint8_t *)t658) + 0) + 8);
        uint8_t t660 = (t659 > ((uint64_t)0ULL));
        if (!t660) goto ie653;
        void * t661 = F.l0;
        void * t662 = (void *)(((uint8_t *)t661) + 8);
        gm_t88rcucg1b7uk t663 = gm_l4;
        uint8_t t664 = gm_f_has_param_u9t0efs(t662, GM_CONV(gm_csguankko4lmhd, t663));
        uint8_t t665 = (uint8_t)!t664;
        if (!t665) goto ie653;
        {
            uint64_t t667 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t668 = GM_LD(uintptr_t, ((uint8_t *)&gm_l64) + 8);
            if (t667 >= (uintptr_t)(t668)) gm_trap_index(t667, t668, gm_src5eu5l4a7gv6s, 243, 16);
            gm_t88rcucg1b7uk t669 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l64)) + t667 * 88) + 0));
            gm_l65 = t669;
        }
        {
            void * t671 = F.l0;
            void * t672 = (void *)(((uint8_t *)t671) + 8);
            gm_t88rcucg1b7uk t673 = gm_l65;
            uint8_t t674 = gm_f_has_param_u9t0efs(t672, GM_CONV(gm_csguankko4lmhd, t673));
            uint8_t t675 = (uint8_t)!t674;
            if (!t675) goto ie670;
            {
                void * t677 = F.l0;
                gm_t88rcucg1b7uk t678 = gm_l65;
                gm_tbccvikg6b96e t679;
                gm_f_layout_a5ovnrm((void *)&t679, (void *)NULL, cx, NULL, t677, t678);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t680 = GM_LD(uint64_t, (((uint8_t *)&t679) + 0));
                gm_l66 = t680;
            }
            {
                void * t681 = F.l0;
                gm_t88rcucg1b7uk t682 = gm_l4;
                gm_tbccvikg6b96e t683;
                gm_f_layout_a5ovnrm((void *)&t683, (void *)NULL, cx, NULL, t681, t682);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t684 = GM_LD(uint64_t, (((uint8_t *)&t683) + 0));
                gm_l67 = t684;
            }
            {
                uint64_t t686 = gm_l66;
                uint64_t t687 = gm_l67;
                uint8_t t688 = (!(t686 == t687));
                if (!t688) goto ie685;
                {
                    void * t690 = F.l0;
                    gm_tl874t2atnlh3 t691 = gm_l5;
                    gm_thddbqb6snabm t692;
                    void * t693 = F.l0;
                    gm_t88rcucg1b7uk t694 = gm_l65;
                    gm_b16_8 t695 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t693, t694);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t692 + 0, t695);
                    uint64_t t696 = gm_l66;
                    GM_ST(uint64_t, (uint8_t *)&t692 + 16, t696);
                    void * t697 = F.l0;
                    gm_t88rcucg1b7uk t698 = gm_l4;
                    gm_b16_8 t699 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t697, t698);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t692 + 24, t699);
                    uint64_t t700 = gm_l67;
                    GM_ST(uint64_t, (uint8_t *)&t692 + 40, t700);
                    gm_tabb710d9mq2e t701;
                    gm_f_format_le00do2((void *)&t701, (void *)NULL, cx, NULL, &t692);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t701))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t701) + 8, gm_src5eu5l4a7gv6s, 248, 31);
                    gm_b16_8 t702 = GM_LD(gm_b16_8, (((uint8_t *)&t701) + 8));
                    gm_b16_8 t703 = GM_LD(gm_b16_8, ((uint8_t *)&t702));
                    gm_b0_1 t704 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t690, t691, t703);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix685;
ie685:;
ix685:;
            }
            goto ix670;
ie670:;
ix670:;
        }
        goto ix653;
ie653:;
ix653:;
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
    GM_STACK_CHECK(&F, 1893u);
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
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint32_t t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
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
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 344) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src5eu5l4a7gv6s, 288, 16);
        if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 344) + 0)) + t14 * 136) + 96))[24] != 0)) goto ie11;
        gm_tiilmfd16v3fc t17 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 344) + 0)) + t14 * 136) + 96));
        gm_l4 = t17;
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_l6 = t20;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t22 = gm_l7;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t24 = (t22 < t23);
                    if (!t24) goto lb21;
                    {
                        uint64_t t27 = gm_l7;
                        uint64_t t28 = (uintptr_t)t27;
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t28 * 4));
                        gm_l8 = t32;
                        {
                            void * t35 = gm_l0;
                            void * t36 = (void *)(((uint8_t *)t35) + 8);
                            gm_t88rcucg1b7uk t37 = gm_l8;
                            uint8_t t38 = gm_f_has_param_u9t0efs(t36, GM_CONV(gm_csguankko4lmhd, t37));
                            if (!t38) goto ie34;
                            {
                                gm_l5 = ((uint8_t)1);
                            }
                            goto ix34;
ie34:;
ix34:;
                        }
                        goto me30;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 290, 9);
me30:;
                    }
lc21:;
                    {
                        uint64_t t41 = gm_l7;
                        uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                        uint64_t t44 = r42;
                        gm_l7 = t44;
                    }
                }
lb21:;
            }
        }
        {
            uint8_t t46 = gm_l5;
            uint8_t t47 = (uint8_t)!t46;
            if (!t47) goto ie45;
            {
                uint32_t t49 = gm_l2;
                uint8_t t50 = 0;
                F.ret = t49;
                goto gm_fret;
            }
            goto ix45;
ie45:;
ix45:;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_l10 = t52;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l11;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l11;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t60 * 4));
                        gm_l12 = t64;
                        {
                            void * t66 = (void *)((uint8_t *)&F.l9);
                            void * t67 = gm_l0;
                            gm_t88rcucg1b7uk t68 = gm_l12;
                            void * t69 = gm_l1;
                            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)t69) + 0));
                            gm_t88rcucg1b7uk t71 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t67, t68, t70);
                            if (gm_unw.kind) {
                                goto gm_ll57;
                            }
                            gm_tftb1odc739t5 t72 = gm_f_push_k3l9nic(t66, GM_CONV(gm_csguankko4lmhd, t71));
                            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_src5eu5l4a7gv6s, 295, 30);
                            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                        }
                        goto me62;
mn63:;
                        gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 295, 9);
me62:;
                    }
lc53:;
                    {
                        uint64_t t75 = gm_l11;
                        uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                        uint64_t t78 = r76;
                        gm_l11 = t78;
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
        {
            void * t79 = gm_l0;
            uint32_t t80 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            void * t83 = gm_l1;
            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 96));
            uint32_t t85 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t79, t80, t82, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        uint32_t t88 = gm_l2;
        uint64_t t89 = (uint64_t)t88;
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = gm_l0;
        uint64_t t92 = GM_LD(uintptr_t, ((((uint8_t *)t91) + 384) + 0) + 8);
        if (t90 >= (uintptr_t)(t92)) gm_trap_index(t90, t92, gm_src5eu5l4a7gv6s, 298, 12);
        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t91) + 384) + 0)) + t90 * 64) + 56));
        uint8_t t94 = (uint8_t)!t93;
        if (!t94) goto ie87;
        {
            uint32_t t96 = gm_l2;
            uint8_t t97 = 0;
            F.ret = t96;
            goto gm_fret;
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        uint32_t t98 = gm_l2;
        uint64_t t99 = (uint64_t)t98;
        uint64_t t100 = (uintptr_t)t99;
        void * t101 = gm_l0;
        uint64_t t102 = GM_LD(uintptr_t, ((((uint8_t *)t101) + 344) + 0) + 8);
        if (t100 >= (uintptr_t)(t102)) gm_trap_index(t100, t102, gm_src5eu5l4a7gv6s, 300, 12);
        gm_tqqhoivahirr0 t103 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t101) + 344) + 0)) + t100 * 136));
        F.l13 = t103;
    }
    {
        gm_f_list_af74s0q((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 40));
            gm_l15 = t105;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t107 = gm_l16;
                uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t109 = (t107 < t108);
                if (!t109) goto lb106;
                {
                    uint64_t t112 = gm_l16;
                    uint64_t t113 = (uintptr_t)t112;
                    uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_t5v0c8ia6a5mo t117 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t113 * 56));
                    F.l17 = t117;
                    {
                        gm_t5v0c8ia6a5mo t119 = F.l17;
                        F.l18 = t119;
                    }
                    {
                        void * t120 = gm_l0;
                        gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                        void * t122 = gm_l1;
                        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)t122) + 0));
                        gm_t88rcucg1b7uk t124 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t120, t121, t123);
                        if (gm_unw.kind) {
                            goto gm_ll110;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16), t124);
                    }
                    {
                        void * t125 = (void *)((uint8_t *)&F.l14);
                        gm_tftb1odc739t5 t126 = gm_f_push_o8u01sh(t125, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l18)));
                        if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_src5eu5l4a7gv6s, 305, 9);
                        gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                    }
                    goto me115;
mn116:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 302, 5);
me115:;
                }
lc106:;
                {
                    uint64_t t129 = gm_l16;
                    uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                    uint64_t t132 = r130;
                    gm_l16 = t132;
                }
            }
gm_ll110:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb106; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc106; }
            }
            goto gm_lad;
lb106:;
        }
    }
    {
        uint32_t t133 = gm_l2;
        uint64_t t134 = (uint64_t)t133;
        uint64_t t135 = (uintptr_t)t134;
        void * t136 = gm_l0;
        uint64_t t137 = GM_LD(uintptr_t, ((((uint8_t *)t136) + 384) + 0) + 8);
        if (t135 >= (uintptr_t)(t137)) gm_trap_index(t135, t137, gm_src5eu5l4a7gv6s, 307, 12);
        gm_tdgfduqjheqdm t138 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t136) + 384) + 0)) + t135 * 64));
        F.l19 = t138;
    }
    {
        void * t139 = gm_l0;
        gm_t9qkh0rsooc8d t140 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l19) + 0));
        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_tl874t2atnlh3 t142 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 16));
        uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 32));
        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t145 = gm_l0;
        gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 56));
        void * t147 = gm_l1;
        gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)t147) + 0));
        gm_t88rcucg1b7uk t149 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t145, t146, t148);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t150 = gm_l0;
        void * t151 = (void *)(((uint8_t *)&F.l13) + 64);
        void * t152 = gm_l1;
        gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)t152) + 0));
        gm_tnpifv20irnph t154 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t150, t151, t153);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t155 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t139, t140, t141, t142, t143, t144, t149, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t155;
    }
    {
        void * t156 = gm_l1;
        void * t157 = (void *)(((uint8_t *)t156) + 16);
        uint32_t t158 = gm_l2;
        uint32_t t159 = gm_l20;
        gm_tcc1rq4n1avb2 t160;
        GM_ST(uint32_t, ((uint8_t *)&t160 + 0), t158);
        GM_ST(uint32_t, ((uint8_t *)&t160 + 4), t159);
        gm_tftb1odc739t5 t161 = gm_f_push_1ncolkq(t157, GM_CONV(gm_cs9ufftihtbpo2, t160));
        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_src5eu5l4a7gv6s, 309, 5);
        gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
    }
    {
        void * t163 = gm_l0;
        void * t164 = (void *)(((uint8_t *)t163) + 2736);
        uint32_t t165 = gm_l20;
        uint32_t t166 = gm_l20;
        gm_tftb1odc739t5 t167 = gm_f_put_33kejrs(t164, t165, t166);
        if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_src5eu5l4a7gv6s, 310, 5);
        gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
    }
    {
        gm_f_list_p9s2t99((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 24));
            gm_l22 = t170;
        }
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t172 = gm_l23;
                uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                uint8_t t174 = (t172 < t173);
                if (!t174) goto lb171;
                {
                    uint64_t t177 = gm_l23;
                    uint64_t t178 = (uintptr_t)t177;
                    uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    gm_tsoie1ppgdvcg t182 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t178 * 40));
                    F.l24 = t182;
                    {
                        gm_tsoie1ppgdvcg t184 = F.l24;
                        F.l25 = t184;
                    }
                    {
                        void * t185 = gm_l0;
                        void * t186 = gm_l1;
                        gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l24) + 16));
                        gm_t88rcucg1b7uk t188 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t185, t186, t187);
                        if (gm_unw.kind) {
                            goto gm_ll175;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l25) + 16), t188);
                    }
                    {
                        void * t189 = (void *)((uint8_t *)&F.l21);
                        gm_tftb1odc739t5 t190 = gm_f_push_am7bo0v(t189, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l25)));
                        if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 1, gm_src5eu5l4a7gv6s, 315, 9);
                        gm_b0_1 t191 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 1));
                    }
                    goto me180;
mn181:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 312, 5);
me180:;
                }
lc171:;
                {
                    uint64_t t193 = gm_l23;
                    uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                    uint64_t t196 = r194;
                    gm_l23 = t196;
                }
            }
gm_ll175:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb171; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc171; }
            }
            goto gm_lad;
lb171:;
        }
    }
    {
        gm_t9805qtonbsag t197;
        memset(&t197, 0, sizeof t197);
        gm_l26 = t197;
    }
    {
        if (!(((((uint8_t *)&F.l19) + 48))[4] != 0)) goto ie198;
        uint32_t t199 = GM_LD(uint32_t, (((uint8_t *)&F.l19) + 48));
        gm_l27 = t199;
        {
            void * t201 = gm_l0;
            void * t202 = gm_l1;
            uint32_t t203 = gm_l27;
            uint32_t t204 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t201, t202, t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t205;
            GM_ST(uint8_t, (uint8_t *)&t205 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t205 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t205), t204);
            ((uint8_t *)&t205)[4] = 1;
            gm_l26 = t205;
        }
        goto ix198;
ie198:;
ix198:;
    }
    {
        gm_l28 = ((void *)0);
    }
    {
        void * t207 = GM_LD(void *, (((uint8_t *)&F.l19) + 40));
        if (!(GM_LD(void *, ((uint8_t *)&t207)) != 0)) goto ie206;
        void * t208 = GM_LD(void *, ((uint8_t *)&t207));
        gm_l29 = t208;
        {
            void * t210 = gm_l29;
            void * t211 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t211;
        }
        {
            void * t212 = gm_l1;
            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)t212) + 108));
            gm_l31 = t213;
        }
        {
            void * t214 = gm_l1;
            void * t215 = GM_LD(void *, (((uint8_t *)t214) + 112));
            gm_l32 = t215;
        }
        {
            void * t216 = gm_l1;
            GM_ST(uint8_t, (((uint8_t *)t216) + 108), ((uint8_t)1));
        }
        {
            void * t217 = gm_l1;
            void * t218 = (void *)((uint8_t *)&F.l21);
            GM_ST(void *, (((uint8_t *)t217) + 112), t218);
        }
        {
            void * t219 = gm_l0;
            void * t220 = gm_l1;
            void * t221 = gm_l30;
            gm_b0_1 t222 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t219, t220, t221);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t223 = gm_l1;
            uint8_t t224 = gm_l31;
            GM_ST(uint8_t, (((uint8_t *)t223) + 108), t224);
        }
        {
            void * t225 = gm_l1;
            void * t226 = gm_l32;
            GM_ST(void *, (((uint8_t *)t225) + 112), t226);
        }
        {
            void * t227 = gm_l30;
            gm_l28 = t227;
        }
        goto ix206;
ie206:;
ix206:;
    }
    {
        uint32_t t228 = gm_l20;
        uint64_t t229 = (uint64_t)t228;
        uint64_t t230 = (uintptr_t)t229;
        void * t231 = gm_l0;
        uint64_t t232 = GM_LD(uintptr_t, ((((uint8_t *)t231) + 384) + 0) + 8);
        if (t230 >= (uintptr_t)(t232)) gm_trap_index(t230, t232, gm_src5eu5l4a7gv6s, 332, 12);
        void * t233 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t231) + 384) + 0)) + t230 * 64);
        gm_l33 = t233;
    }
    {
        void * t234 = gm_l33;
        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 8));
        GM_ST(gm_b16_8, (((uint8_t *)t234) + 8), t235);
    }
    {
        void * t236 = gm_l33;
        gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)t236) + 24), t237);
    }
    {
        void * t238 = gm_l33;
        void * t239 = gm_l28;
        GM_ST(void *, (((uint8_t *)t238) + 40), t239);
    }
    {
        void * t240 = gm_l33;
        gm_t9805qtonbsag t241 = gm_l26;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)t240) + 48), t241);
    }
    {
        void * t242 = gm_l33;
        GM_ST(uint8_t, (((uint8_t *)t242) + 56), ((uint8_t)1));
    }
    {
        uint32_t t243 = gm_l20;
        uint8_t t244 = 0;
        F.ret = t243;
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
    GM_STACK_CHECK(&F, 517u);
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
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint32_t t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t11 = gm_l2;
        uint64_t t12 = (uint64_t)t11;
        uint64_t t13 = (uintptr_t)t12;
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 504) + 0) + 8);
        if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src5eu5l4a7gv6s, 344, 12);
        gm_t1cs2fjnrqap1 t16 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 504) + 0)) + t13 * 40));
        F.l4 = t16;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 504) + 0) + 8);
        uint32_t t19 = (uint32_t)t18;
        gm_l5 = t19;
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)(((uint8_t *)t20) + 504);
        gm_t1cs2fjnrqap1 t22;
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)&F.l4) + 0);
        void * t25 = gm_l1;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 0));
        gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t22 + 0)), (void *)NULL, cx, NULL, t23, t24, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t27 = gm_f_empty_slice_d914i5u();
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 24, t27);
        gm_tftb1odc739t5 t28 = gm_f_push_bu0mlkm(t21, &t22);
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_src5eu5l4a7gv6s, 346, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        void * t30 = gm_l1;
        void * t31 = (void *)(((uint8_t *)t30) + 56);
        uint32_t t32 = gm_l2;
        uint32_t t33 = gm_l5;
        gm_tcc1rq4n1avb2 t34;
        GM_ST(uint32_t, ((uint8_t *)&t34 + 0), t32);
        GM_ST(uint32_t, ((uint8_t *)&t34 + 4), t33);
        gm_tftb1odc739t5 t35 = gm_f_push_1ncolkq(t31, GM_CONV(gm_cs9ufftihtbpo2, t34));
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_src5eu5l4a7gv6s, 347, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    {
        gm_f_list_go2qg0b((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 24));
            gm_l7 = t38;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t40 = gm_l8;
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l8;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint32_t t48 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t46 * 4));
                    uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)&t48));
                    gm_l9 = t51;
                    {
                        void * t53 = (void *)((uint8_t *)&F.l6);
                        void * t54 = gm_l0;
                        void * t55 = gm_l1;
                        uint32_t t56 = gm_l9;
                        uint32_t t57 = gm_f_mono_fn_aj1vgri((void *)NULL, cx, NULL, t54, t55, t56);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_tftb1odc739t5 t58 = gm_f_push_im9d4ro(t53, t57);
                        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_src5eu5l4a7gv6s, 349, 28);
                        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
                    }
                    goto me49;
mn50:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 349, 5);
me49:;
                }
lc39:;
                {
                    uint64_t t61 = gm_l8;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l8 = t64;
                }
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
            }
            goto gm_lad;
lb39:;
        }
    }
    {
        uint32_t t65 = gm_l5;
        uint64_t t66 = (uint64_t)t65;
        uint64_t t67 = (uintptr_t)t66;
        void * t68 = gm_l0;
        uint64_t t69 = GM_LD(uintptr_t, ((((uint8_t *)t68) + 504) + 0) + 8);
        if (t67 >= (uintptr_t)(t69)) gm_trap_index(t67, t69, gm_src5eu5l4a7gv6s, 350, 5);
        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t68) + 504) + 0)) + t67 * 40) + 24), t70);
    }
    {
        uint32_t t71 = gm_l5;
        uint8_t t72 = 0;
        F.ret = t71;
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
    GM_STACK_CHECK(&F, 255u);
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
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)25ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l3 = t8;
        {
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            void * t12 = gm_l0;
            void * t13 = gm_l1;
            uint32_t t14 = gm_l3;
            uint32_t t15 = gm_f_mono_handler_38b78qh((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t16;
            memset(&t16, 0, sizeof t16);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)25ULL));
            GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t15);
            gm_t88rcucg1b7uk t17 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t11, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l2;
        void * t21 = gm_l1;
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
        gm_t88rcucg1b7uk t23 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t19, t20, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
    GM_STACK_CHECK(&F, 1190u);
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
        void * t9;
        if ((GM_LD(void *, ((uint8_t *)&t8)) != 0)) {
            void * t10 = GM_LD(void *, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b0_1 t11;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l3 = t9;
    }
    {
        gm_f_list_oalq03d((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)a1) + 0));
            uint64_t t15 = (uint64_t)t14;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 344) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_src5eu5l4a7gv6s, 69, 27);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 344) + 0)) + t16 * 136) + 80));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_l5 = t20;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t22 = gm_l6;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l6;
                    uint64_t t28 = gm_l6;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t29 * 4));
                    gm_tn68el8d14vku t32;
                    GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t32 + 0), t27);
                    GM_ST(uint32_t, ((uint8_t *)&t32 + 8), t31);
                    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&t32) + 0));
                    gm_l7 = t35;
                    uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&t32) + 8));
                    gm_l8 = t36;
                    {
                        void * t38 = (void *)((uint8_t *)&F.l4);
                        uint32_t t39 = gm_l8;
                        uint64_t t40 = gm_l7;
                        uint64_t t41 = (uintptr_t)t40;
                        uint64_t t42 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
                        if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src5eu5l4a7gv6s, 69, 105);
                        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)a1) + 8)) + t41 * 4));
                        gm_tplpbqf3duvn3 t44;
                        GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t39);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44 + 4), t43);
                        gm_tftb1odc739t5 t45 = gm_f_push_h69n5fi(t38, GM_CONV(gm_cs7e9ppsd384bj, t44));
                        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_src5eu5l4a7gv6s, 69, 71);
                        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 69, 5);
me33:;
                }
lc21:;
                {
                    uint64_t t48 = gm_l6;
                    uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                    uint64_t t51 = r49;
                    gm_l6 = t51;
                }
            }
lb21:;
        }
    }
    {
        void * t52 = gm_l0;
        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)t52) + 1360));
        gm_l9 = t53;
    }
    {
        void * t54 = gm_l0;
        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)a1) + 36));
        GM_ST(uint32_t, (((uint8_t *)t54) + 1360), t55);
    }
    {
        void * t56 = gm_l0;
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_toceffb697p0u t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t60), t59);
        ((uint8_t *)&t60)[12] = 1;
        gm_b0_1 t61 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t56, t58, t60);
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
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        GM_ST(gm_b16_8, (uint8_t *)&F.l11 + 0, t63);
        gm_f_list_7l73vjb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11 + 16)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_7l73vjb((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11 + 56)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l11 + 96, t64);
        GM_ST(uint8_t, (uint8_t *)&F.l11 + 108, ((uint8_t)0));
        void * t65 = (void *)((uint8_t *)&F.l10);
        GM_ST(void *, (uint8_t *)&F.l11 + 112, t65);
    }
    {
        void * t66 = (void *)(((uint8_t *)&F.l11) + 16);
        uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)a1) + 0));
        uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        gm_tcc1rq4n1avb2 t69;
        GM_ST(uint32_t, ((uint8_t *)&t69 + 0), t67);
        GM_ST(uint32_t, ((uint8_t *)&t69 + 4), t68);
        gm_tftb1odc739t5 t70 = gm_f_push_1ncolkq(t66, GM_CONV(gm_cs9ufftihtbpo2, t69));
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_src5eu5l4a7gv6s, 75, 5);
        gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
    }
    {
        {
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
            gm_l12 = t73;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t75 = gm_l13;
                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t77 = (t75 < t76);
                if (!t77) goto lb74;
                {
                    uint64_t t80 = gm_l13;
                    uint64_t t81 = (uintptr_t)t80;
                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_tsoie1ppgdvcg t85 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t81 * 40));
                    F.l14 = t85;
                    {
                        gm_tsoie1ppgdvcg t87 = F.l14;
                        F.l15 = t87;
                    }
                    {
                        void * t88 = gm_l0;
                        void * t89 = (void *)((uint8_t *)&F.l11);
                        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                        gm_t88rcucg1b7uk t91 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t88, t89, t90);
                        if (gm_unw.kind) {
                            goto gm_ll78;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16), t91);
                    }
                    {
                        void * t92 = (void *)((uint8_t *)&F.l10);
                        gm_tftb1odc739t5 t93 = gm_f_push_am7bo0v(t92, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l15)));
                        if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_src5eu5l4a7gv6s, 79, 9);
                        gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                    }
                    goto me83;
mn84:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 76, 5);
me83:;
                }
lc74:;
                {
                    uint64_t t96 = gm_l13;
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                    uint64_t t99 = r97;
                    gm_l13 = t99;
                }
            }
gm_ll78:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
            }
            goto gm_lad;
lb74:;
        }
    }
    {
        void * t100 = gm_l3;
        void * t101 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t101;
    }
    {
        void * t102 = gm_l0;
        void * t103 = (void *)((uint8_t *)&F.l11);
        void * t104 = gm_l16;
        gm_b0_1 t105 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t102, t103, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t106 = gm_l0;
        gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_b0_1 t108 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t109 = gm_l0;
        uint32_t t110 = gm_l9;
        GM_ST(uint32_t, (((uint8_t *)t109) + 1360), t110);
    }
    {
        void * t111 = gm_l0;
        uint32_t t112 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        gm_tdgfduqjheqdm t113;
        memset((uint8_t *)&t113 + 1, 0, 7);
        memset((uint8_t *)&t113 + 57, 0, 7);
        gm_t9qkh0rsooc8d t114 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t113 + 0, t114);
        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 8, t115);
        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 24, t116);
        void * t117 = gm_l16;
        GM_ST(void *, (uint8_t *)&t113 + 40, t117);
        gm_t9805qtonbsag t118 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l2) + 48));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t113 + 48, t118);
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 56));
        GM_ST(uint8_t, (uint8_t *)&t113 + 56, t119);
        gm_b0_1 t120 = gm_f_set_inst_def_tcpabg0((void *)NULL, cx, NULL, t111, t112, &t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
            gm_l17 = t122;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t124 = gm_l18;
                uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t126 = (t124 < t125);
                if (!t126) goto lb123;
                {
                    uint64_t t129 = gm_l18;
                    uint64_t t130 = (uintptr_t)t129;
                    uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_tsoie1ppgdvcg t134 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t130 * 40));
                    F.l19 = t134;
                    {
                        void * t137 = gm_l0;
                        void * t138 = gm_l0;
                        gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                        gm_t88rcucg1b7uk t142 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t138, t139, t141);
                        if (gm_unw.kind) {
                            goto gm_ll127;
                        }
                        gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                        uint8_t t144 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t137, t142, t143);
                        if (gm_unw.kind) {
                            goto gm_ll127;
                        }
                        uint8_t t145 = (uint8_t)!t144;
                        if (!t145) goto ie136;
                        {
                            goto lb123;
                        }
                        goto ix136;
ie136:;
ix136:;
                    }
                    goto me132;
mn133:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 87, 5);
me132:;
                }
lc123:;
                {
                    uint64_t t148 = gm_l18;
                    uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                    uint64_t t151 = r149;
                    gm_l18 = t151;
                }
            }
gm_ll127:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb123; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc123; }
            }
            goto gm_lad;
lb123:;
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
    GM_STACK_CHECK(&F, 432u);
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
                        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_src5eu5l4a7gv6s, 276, 9);
                        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src5eu5l4a7gv6s, 275, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t35 = gm_l5;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l5 = t38;
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
        void * t39 = gm_l1;
        uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)t39) + 0));
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 0, t40);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)gm_out) + 8, t42);
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

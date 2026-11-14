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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { gm_b16_8 f0; int32_t f1; uint8_t pe[4]; } gm_t0bf4lga76ktk;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; double f1; } gm_t2mvr8fpgjkg6;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t0bf4lga76ktk) == 24 && _Alignof(gm_t0bf4lga76ktk) == 8 && offsetof(gm_t0bf4lga76ktk, f0) == 0 && offsetof(gm_t0bf4lga76ktk, f1) == 16, "layout of `(string, i32)`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2mvr8fpgjkg6) == 24 && _Alignof(gm_t2mvr8fpgjkg6) == 8 && offsetof(gm_t2mvr8fpgjkg6, f0) == 0 && offsetof(gm_t2mvr8fpgjkg6, f1) == 16, "layout of `(string, f64)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_00qg1g3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_069unrn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0jg0fc8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_10eqf8a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1ikrr1s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2agisls {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2o85j6u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2ttu2pe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3764lq1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3bmo9nu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3g158bi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3sa8h99 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3tj64sm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_40qkrph {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_41e0srt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_47etgnk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4hbbbf0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4kktfdj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4mcpeld {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4rkhbtp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5hkvedq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_67s9e97 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_68m5rrl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6k0kemf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6k11rh8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8fom6sk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8g8nrr6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_930s2re {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_955f6re {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9m9i854 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a57v4pe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a7a2haq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a7bba9q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aojb2c8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b9ef3bb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b9r4ekr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bnpbmcc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cghu5ur {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cm7eh31 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cru4lds {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cu8cqgt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dm2l0vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_efkt607 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ej5sdtn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fd380sp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fou71i7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_g1gumfr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_g2qn3vh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_g4itb9o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gee2nac {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_geu2ipt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gsl696h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_h28k9hu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hq2sdog {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i4on1pl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i9m1qbm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ir0df61 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ith45r1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j6u63hu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jtn6932 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kngq99i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_knls90g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l6e4eki {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ld8hf76 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m1id2sb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mi6s2qr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nudto06 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o55qls2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pbi3fui {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_psqe8d9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q1c0du7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q8121ul {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r6cjqr1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_s4ja923 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_srgiqer {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_t06ahi8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tk1c2p3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tppsct0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_u4u1c45 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uci28p7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_udcpr90 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_va5jnqp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vnbkf1p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vosr5ur {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vq1f9r0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vqiq209 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_f1281l5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f2fgi8v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f2pdud5 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f36nqla {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f6fn313 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f6rro2t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f89fec3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fambdq4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fb5qv68 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fbigqms {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fd6oegj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fdvg246 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fea89f6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fego630 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fh9to9u {
    gm_frame h;
    gm_tk6rvhh54g86p l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tk6rvhh54g86p l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fi775k0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fiq5e0p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fj9bgml {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fjhi9ui {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fk1icrl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fl4q4sd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fn68qf0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fnh5q7b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fp763nd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fpa95lg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fpoafkq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fqec6j2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fqmocq1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_frab70n {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_frbkb04 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_frnomph {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fsfutor {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fskpepf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ftgbk9j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fu8jpss {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fuvjr35 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fv5tfj6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fvh5va2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v0s3sc7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v2s58vm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v2sfrt0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v301q4v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v3lvmh8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v40sfri {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v5s6luj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v6efj7n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v71csbq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v7qttjn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v9dnkq5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vakr6l2 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vau3iu8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vc10pth {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vc9g56o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vcvtacd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vd1blml {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vdba58s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vddm9q0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ven7kt5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vfce7nu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vfnel93 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vfrm49j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vft2h8j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vgl5p2e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vilpuor {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vj1olp7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vjen3ng {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vlf90rk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vlsb354 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmb8nb9 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmi8kfe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmlgaaq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmsom9v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vno937p {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vns89mr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_voj344o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vom8db6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_voqg5cl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vp0clr1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vprps7i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vq3bur7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vqmfrgb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vrlgsjr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vrv37j5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vsbf5l9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vtr1b5e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vucln8v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_00qg1g3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_069unrn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0jg0fc8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_10eqf8a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1ikrr1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2agisls(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2o85j6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2ttu2pe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3764lq1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3bmo9nu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3g158bi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sa8h99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3tj64sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_40qkrph(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_41e0srt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47etgnk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4hbbbf0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4kktfdj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4mcpeld(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4rkhbtp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5hkvedq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_67s9e97(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_68m5rrl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6k0kemf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6k11rh8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8fom6sk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8g8nrr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_930s2re(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_955f6re(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9m9i854(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a57v4pe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7a2haq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7bba9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aojb2c8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b9ef3bb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b9r4ekr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bnpbmcc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cghu5ur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cm7eh31(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cru4lds(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cu8cqgt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dm2l0vv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_efkt607(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ej5sdtn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fd380sp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fou71i7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g1gumfr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g2qn3vh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4itb9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gee2nac(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_geu2ipt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gsl696h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h28k9hu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hq2sdog(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i4on1pl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i9m1qbm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ir0df61(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ith45r1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j6u63hu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jtn6932(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kngq99i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_knls90g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l6e4eki(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ld8hf76(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1id2sb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mi6s2qr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nudto06(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o55qls2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbi3fui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_psqe8d9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q1c0du7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q8121ul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r6cjqr1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s4ja923(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_srgiqer(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t06ahi8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tk1c2p3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tppsct0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_u4u1c45(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uci28p7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_udcpr90(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_va5jnqp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vnbkf1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vosr5ur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vq1f9r0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqiq209(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_f1281l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f2fgi8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f2pdud5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f36nqla(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f6fn313(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f89fec3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fambdq4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fb5qv68(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fbigqms(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fd6oegj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdvg246(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fea89f6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fego630(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_fh9to9u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fi775k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fiq5e0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fj9bgml(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fjhi9ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fk1icrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_fl4q4sd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fn68qf0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fnh5q7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fp763nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fpa95lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_fpoafkq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fqec6j2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fqmocq1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_frab70n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_frbkb04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fskpepf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ftgbk9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fu8jpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fuvjr35(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fvh5va2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v0s3sc7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_v2s58vm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_v301q4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_v3lvmh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_v40sfri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v5s6luj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v6efj7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_v71csbq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v7qttjn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_v9dnkq5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_vakr6l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vau3iu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vc10pth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vc9g56o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vcvtacd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_vd1blml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vdba58s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vddm9q0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_vfce7nu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vfnel93(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vfrm49j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vft2h8j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vgl5p2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vilpuor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vj1olp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vjen3ng(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vlf90rk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vlsb354(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vmb8nb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vmi8kfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vmlgaaq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vmsom9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vno937p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vns89mr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_voj344o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_voqg5cl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vp0clr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vprps7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vq3bur7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vqmfrgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vrlgsjr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vrv37j5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vsbf5l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vtr1b5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vucln8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s0147vgtvilnq[19];
extern const uint8_t gm_s078u1763sqk3[11];
extern const uint8_t gm_s0886ibkvne07[52];
extern const uint8_t gm_s091o161s6kba[5];
extern const uint8_t gm_s0bpji6inee0r[19];
extern const uint8_t gm_s0kajv5j53ub7[38];
extern const uint8_t gm_s11aakm4d1c2s[19];
extern const uint8_t gm_s137iv03ua7e7[30];
extern const uint8_t gm_s16mf4cvajh59[13];
extern const uint8_t gm_s18e8dmribdb3[7];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1cn5ab5opg9f[131];
extern const uint8_t gm_s1o778dp7d59d[43];
extern const uint8_t gm_s1s57v200fem0[6];
extern const uint8_t gm_s23n3pek53qm1[24];
extern const uint8_t gm_s2aj4fqmgmn1n[67];
extern const uint8_t gm_s2ha7brjf2gmp[7];
extern const uint8_t gm_s2jlhlq33qoal[12];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s3avlcjhdh976[10];
extern const uint8_t gm_s3d91jdjen8gc[8];
extern const uint8_t gm_s3duthvmnjluh[148];
extern const uint8_t gm_s3hg5go4uihat[52];
extern const uint8_t gm_s3kumt3nslbnp[9];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3pj34lce6rqp[10];
extern const uint8_t gm_s3q858hngo0ho[39];
extern const uint8_t gm_s3sb6rrf98gpi[20];
extern const uint8_t gm_s3t3r41giacns[14];
extern const uint8_t gm_s41bap0m04tt9[12];
extern const uint8_t gm_s495moo6tjeh0[20];
extern const uint8_t gm_s4av1h975k9ok[47];
extern const uint8_t gm_s4g35ls4ma80g[13];
extern const uint8_t gm_s4gdu7v0gur88[11];
extern const uint8_t gm_s4h6hphsrrhvo[14];
extern const uint8_t gm_s4lj2bemc87jp[3];
extern const uint8_t gm_s4m9nol66dh0a[25];
extern const uint8_t gm_s4mqk7h9f98g3[30];
extern const uint8_t gm_s516pv14t5t6i[73];
extern const uint8_t gm_s5958kaiuh54g[84];
extern const uint8_t gm_s5974q1kmorlc[52];
extern const uint8_t gm_s5d58k7uql7sf[72];
extern const uint8_t gm_s5dmjeoiprfiv[72];
extern const uint8_t gm_s5gn6cmh4thug[38];
extern const uint8_t gm_s5irlrsnb30cc[24];
extern const uint8_t gm_s5kue8bfcvqfr[27];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5s1m7053mh3n[8];
extern const uint8_t gm_s5v9muv1jh89m[75];
extern const uint8_t gm_s60n1k0cqjuaq[144];
extern const uint8_t gm_s65ftmvtj458k[11];
extern const uint8_t gm_s6n2mkqhnsjpk[10];
extern const uint8_t gm_s6q7rsv30u3ma[34];
extern const uint8_t gm_s6tn8kl12f5ru[27];
extern const uint8_t gm_s6uch3l6osunv[50];
extern const uint8_t gm_s72l6okdj2d3g[95];
extern const uint8_t gm_s7ddabri39fvk[9];
extern const uint8_t gm_s7fdjhp9ga5oo[8];
extern const uint8_t gm_s7qbqthgde8lk[29];
extern const uint8_t gm_s7uepsfl7trr3[70];
extern const uint8_t gm_s7umjdfqes00m[52];
extern const uint8_t gm_s82d2namrcvs3[17];
extern const uint8_t gm_s87p55h1isc6m[6];
extern const uint8_t gm_s8gaig0cs2uce[19];
extern const uint8_t gm_s8gf7bnv27fi9[7];
extern const uint8_t gm_s8pt0g4bme9og[30];
extern const uint8_t gm_s8s91tiu5r15q[45];
extern const uint8_t gm_s8us8sihs6eh7[74];
extern const uint8_t gm_sa56bd0p8nuib[143];
extern const uint8_t gm_sa8q5t8i6ht30[6];
extern const uint8_t gm_sagfhrncf3ga2[11];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_sb6brpqpm5qjh[21];
extern const uint8_t gm_sb77prkml0t3e[15];
extern const uint8_t gm_sbcjfjm1sdn3u[42];
extern const uint8_t gm_sbf8eactntf1g[21];
extern const uint8_t gm_sc6celu0e642t[6];
extern const uint8_t gm_scaoagismk27j[18];
extern const uint8_t gm_sccm0heds95r6[60];
extern const uint8_t gm_scd25kom6sdt8[34];
extern const uint8_t gm_scgm14ju6d0p8[15];
extern const uint8_t gm_sciu02cqahje3[52];
extern const uint8_t gm_scjfd9b9vg99c[12];
extern const uint8_t gm_scnmhrk4ghd85[17];
extern const uint8_t gm_scrnhm4ha22vj[18];
extern const uint8_t gm_scs1evojsik8q[4];
extern const uint8_t gm_sct07pl02n02h[27];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_sdt2alvv2h6jh[48];
extern const uint8_t gm_sdugj3qbhl84q[5];
extern const uint8_t gm_se08n5d1gngu7[5];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_seglpsc899j08[11];
extern const uint8_t gm_segrf6njnsvht[69];
extern const uint8_t gm_sf42bd5sk6816[26];
extern const uint8_t gm_sf51sbqtper4f[74];
extern const uint8_t gm_sf6ug774thos9[15];
extern const uint8_t gm_sf7ldnukv79cp[24];
extern const uint8_t gm_sf8e06i5juo6a[29];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfg1v6bqcbbav[10];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfn5aos9cps1h[13];
extern const uint8_t gm_sfn640udk6pjn[19];
extern const uint8_t gm_sfn9k9vgl9ged[28];
extern const uint8_t gm_sft2smqlktfvo[19];
extern const uint8_t gm_sfu1s4a4bo4he[25];
extern const uint8_t gm_sg027ftm3ja87[33];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg8f7kt1n4uc5[48];
extern const uint8_t gm_sg93rj30beob9[29];
extern const uint8_t gm_sga7ge5dc8qdr[50];
extern const uint8_t gm_sgb4vh2ukunor[3];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sgqep6dpse2vs[64];
extern const uint8_t gm_sgsd94uuisj8t[73];
extern const uint8_t gm_sgur19lpnttci[7];
extern const uint8_t gm_sha819aic5lkk[22];
extern const uint8_t gm_shol80fqem8c6[48];
extern const uint8_t gm_shq89iblo3j6k[51];
extern const uint8_t gm_shrlc4g1vtjsu[47];
extern const uint8_t gm_sht5iivcbi64u[12];
extern const uint8_t gm_shv2qb39iqelf[31];
extern const uint8_t gm_si7qrooamq5ei[26];
extern const uint8_t gm_sicq1bvp13t5d[23];
extern const uint8_t gm_sictcqjuumcng[63];
extern const uint8_t gm_sif59l16kubo3[17];
extern const uint8_t gm_siot2vlveunn8[18];
extern const uint8_t gm_sisn94tbdoeof[65];
extern const uint8_t gm_sj1gcl0qo4i9p[7];
extern const uint8_t gm_sjh4l64ncn038[52];
extern const uint8_t gm_sjk9nricijji8[45];
extern const uint8_t gm_sjm2j40p6489d[9];
extern const uint8_t gm_sjndugdkuh68b[58];
extern const uint8_t gm_sk2oomiuvp44g[6];
extern const uint8_t gm_sk5d5fbcb3pir[10];
extern const uint8_t gm_sk8it0s509dbd[38];
extern const uint8_t gm_skbd3tdpfkbk5[24];
extern const uint8_t gm_sktjfeu2a0ava[15];
extern const uint8_t gm_sl9h8e795pkr4[29];
extern const uint8_t gm_sl9mgk5ho4kmr[29];
extern const uint8_t gm_sl9pujidpnv0r[4];
extern const uint8_t gm_slc0d2s6km3s5[5];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slhm5kfb42dip[11];
extern const uint8_t gm_slj3uno8bsqk4[27];
extern const uint8_t gm_sljj5lb6g47i1[38];
extern const uint8_t gm_slogre7vmagad[35];
extern const uint8_t gm_slqq08j0fro21[31];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smc2kdmn5qjql[113];
extern const uint8_t gm_smcnje2cdb03e[3];
extern const uint8_t gm_smf5jnr5ps1bl[5];
extern const uint8_t gm_smggdkb5phvp0[56];
extern const uint8_t gm_smn3oh253jlna[10];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_sn0ac8j2fho01[22];
extern const uint8_t gm_sn1r7qad8j2p2[5];
extern const uint8_t gm_sn5pv7k63ni4c[11];
extern const uint8_t gm_sn88c2jg5b9gv[4];
extern const uint8_t gm_sn8d077r4fgpq[26];
extern const uint8_t gm_snbrjpgiqf592[6];
extern const uint8_t gm_snfsrvfr6vnkt[8];
extern const uint8_t gm_sngn7trhgdnc2[43];
extern const uint8_t gm_snja4gioias3h[19];
extern const uint8_t gm_snkdp6opmsb8i[42];
extern const uint8_t gm_snl38trih7qf5[19];
extern const uint8_t gm_snrrubbi4s05g[16];
extern const uint8_t gm_snsq190kog2e4[60];
extern const uint8_t gm_snsqui8o89u28[15];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so5l2ibq9ia3i[65];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_soajcqnnsnidh[57];
extern const uint8_t gm_sod9r7r024h9q[6];
extern const uint8_t gm_soeesub7ccc96[31];
extern const uint8_t gm_sogev0d3re2th[11];
extern const uint8_t gm_soq66vtlbhee2[56];
extern const uint8_t gm_soqhrfmr1feii[7];
extern const uint8_t gm_sp02l2e4viomq[26];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_spb7dvkqr71v2[77];
extern const uint8_t gm_spkrf9e7cr6h4[7];
extern const uint8_t gm_splvrmi68lqi3[111];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sq2qrq8ir5ihu[8];
extern const uint8_t gm_sq32stff79ol1[22];
extern const uint8_t gm_sq43o0crfp05h[18];
extern const uint8_t gm_sq4jcg2uujqmj[16];
extern const uint8_t gm_sq9f472jpmh59[64];
extern const uint8_t gm_sqldj8dr0j7rf[12];
extern const uint8_t gm_sqmifqg6egoj0[41];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srl8r23tls393[15];
extern const uint8_t gm_ssbb728kngg91[79];
extern const uint8_t gm_ssk8mqd1u6nn7[52];
extern const uint8_t gm_ssq1no648r8mj[63];
extern const uint8_t gm_ssr91slt3ah1c[11];
extern const uint8_t gm_sst2khrvr4nus[10];
extern const uint8_t gm_sstqe1228ic6k[51];
extern const uint8_t gm_st4gqcsm35nph[90];
extern const uint8_t gm_st52k2850n5r5[5];
extern const uint8_t gm_st7fm661apctp[17];
extern const uint8_t gm_st9djk4oc14um[67];
extern const uint8_t gm_stgav1sk1eq6u[59];
extern const uint8_t gm_stkqau5o4elnq[15];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_stqiimqtsnd8d[9];
extern const uint8_t gm_su2ima1nbr01d[52];
extern const uint8_t gm_su2vudghakb3u[55];
extern const uint8_t gm_su5bsj1nioctr[22];
extern const uint8_t gm_sufif7dqc1pm3[17];
extern const uint8_t gm_sujipgkhb2fih[8];
extern const uint8_t gm_suv79odpsscph[8];
extern const uint8_t gm_sv89nb1qmpoaj[8];
extern const uint8_t gm_svaaloo7n72id[9];
extern const uint8_t gm_svbftsm94r0bv[7];
extern const uint8_t gm_svdd37ikql4fk[45];
extern const uint8_t gm_svdl0vjc2ha7u[50];
extern const uint8_t gm_svgk3n4sk1uat[9];
extern const uint8_t gm_svl0gta90bsdr[49];
extern const uint8_t gm_svml0iv6h5a82[48];
extern const uint8_t gm_svu4pa4qq20q7[6];
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyqa6gfa8omk;
extern const gm_type gm_tyqqajgod2bb;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_00qg1g3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_00qg1g3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fnh5q7b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fnh5q7b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fnh5q7b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_069unrn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_069unrn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_f6fn313 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_f6fn313 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_f6fn313 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0jg0fc8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0jg0fc8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fpa95lg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fpa95lg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fpa95lg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_10eqf8a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_10eqf8a F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmi8kfe *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vmi8kfe *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmi8kfe *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1ikrr1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1ikrr1s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fl4q4sd *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fl4q4sd *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fl4q4sd *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2agisls(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2agisls F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fea89f6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fea89f6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fea89f6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2o85j6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2o85j6u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_f36nqla *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_f36nqla *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_f36nqla *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2ttu2pe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2ttu2pe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_f6rro2t *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_f6rro2t *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_f6rro2t *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3764lq1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3764lq1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_frnomph *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_frnomph *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_frnomph *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3bmo9nu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3bmo9nu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vgl5p2e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vgl5p2e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vgl5p2e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3g158bi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3g158bi F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vucln8v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vucln8v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vucln8v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sa8h99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3sa8h99 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vfnel93 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vfnel93 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vfnel93 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3tj64sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3tj64sm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v0s3sc7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v0s3sc7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v0s3sc7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_40qkrph(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_40qkrph F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v2s58vm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v2s58vm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v2s58vm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_41e0srt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_41e0srt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v9dnkq5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v9dnkq5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v9dnkq5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47etgnk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_47etgnk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vns89mr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vns89mr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vns89mr *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4hbbbf0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4hbbbf0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fpoafkq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fpoafkq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fpoafkq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4kktfdj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4kktfdj F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vd1blml *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vd1blml *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vd1blml *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4mcpeld(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4mcpeld F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vfrm49j *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vfrm49j *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vfrm49j *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4rkhbtp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4rkhbtp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v2sfrt0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v2sfrt0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v2sfrt0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5hkvedq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5hkvedq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vrv37j5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vrv37j5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vrv37j5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_67s9e97(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_67s9e97 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_frab70n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_frab70n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_frab70n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_68m5rrl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_68m5rrl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vom8db6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vom8db6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vom8db6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6k0kemf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6k0kemf F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fskpepf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fskpepf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fskpepf *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6k11rh8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6k11rh8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vno937p *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vno937p *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vno937p *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8fom6sk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8fom6sk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_voj344o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_voj344o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_voj344o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8g8nrr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8g8nrr6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v301q4v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v301q4v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v301q4v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_930s2re(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_930s2re F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vcvtacd *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vcvtacd *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vcvtacd *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_955f6re(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_955f6re F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fuvjr35 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fuvjr35 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fuvjr35 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9m9i854(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9m9i854 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vft2h8j *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vft2h8j *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vft2h8j *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a57v4pe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a57v4pe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v6efj7n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v6efj7n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v6efj7n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7a2haq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a7a2haq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fv5tfj6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fv5tfj6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fv5tfj6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7bba9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a7bba9q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fqmocq1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fqmocq1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fqmocq1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aojb2c8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_aojb2c8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vtr1b5e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vtr1b5e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vtr1b5e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b9ef3bb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b9ef3bb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_voqg5cl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_voqg5cl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_voqg5cl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b9r4ekr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b9r4ekr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fk1icrl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fk1icrl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fk1icrl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bnpbmcc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bnpbmcc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vdba58s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vdba58s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vdba58s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cghu5ur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cghu5ur F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vsbf5l9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vsbf5l9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vsbf5l9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cm7eh31(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cm7eh31 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ftgbk9j *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ftgbk9j *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ftgbk9j *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cru4lds(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cru4lds F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vq3bur7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vq3bur7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vq3bur7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cu8cqgt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cu8cqgt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v3lvmh8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v3lvmh8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v3lvmh8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dm2l0vv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dm2l0vv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v40sfri *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v40sfri *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v40sfri *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_efkt607(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_efkt607 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fu8jpss *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fu8jpss *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fu8jpss *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ej5sdtn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ej5sdtn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fj9bgml *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fj9bgml *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fj9bgml *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fd380sp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fd380sp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fi775k0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fi775k0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fi775k0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fou71i7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fou71i7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ven7kt5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ven7kt5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ven7kt5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g1gumfr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_g1gumfr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v5s6luj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v5s6luj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v5s6luj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g2qn3vh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_g2qn3vh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fvh5va2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fvh5va2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fvh5va2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4itb9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_g4itb9o F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vj1olp7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vj1olp7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vj1olp7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gee2nac(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gee2nac F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fd6oegj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fd6oegj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fd6oegj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_geu2ipt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_geu2ipt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmsom9v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vmsom9v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmsom9v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gsl696h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gsl696h F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fego630 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fego630 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fego630 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h28k9hu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_h28k9hu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmb8nb9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vmb8nb9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmb8nb9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hq2sdog(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_hq2sdog F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fdvg246 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fdvg246 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fdvg246 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i4on1pl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i4on1pl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fjhi9ui *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fjhi9ui *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fjhi9ui *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i9m1qbm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i9m1qbm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_f89fec3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_f89fec3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_f89fec3 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ir0df61(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ir0df61 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vilpuor *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vilpuor *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vilpuor *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ith45r1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ith45r1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fp763nd *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fp763nd *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fp763nd *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j6u63hu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_j6u63hu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vc9g56o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vc9g56o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vc9g56o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jtn6932(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jtn6932 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_f2pdud5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_f2pdud5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_f2pdud5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kngq99i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kngq99i F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vakr6l2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vakr6l2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vakr6l2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_knls90g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_knls90g F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vfce7nu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vfce7nu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vfce7nu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l6e4eki(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l6e4eki F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vqmfrgb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vqmfrgb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vqmfrgb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ld8hf76(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ld8hf76 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vlsb354 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vlsb354 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vlsb354 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1id2sb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_m1id2sb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_f2fgi8v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_f2fgi8v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_f2fgi8v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mi6s2qr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mi6s2qr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fb5qv68 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fb5qv68 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fb5qv68 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nudto06(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nudto06 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vc10pth *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vc10pth *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vc10pth *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o55qls2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o55qls2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fqec6j2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fqec6j2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fqec6j2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbi3fui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pbi3fui F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vp0clr1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vp0clr1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vp0clr1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_psqe8d9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_psqe8d9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_frbkb04 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_frbkb04 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_frbkb04 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q1c0du7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q1c0du7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v71csbq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v71csbq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v71csbq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q8121ul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q8121ul F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fn68qf0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fn68qf0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fn68qf0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r6cjqr1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_r6cjqr1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vrlgsjr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vrlgsjr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vrlgsjr *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s4ja923(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_s4ja923 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fiq5e0p *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fiq5e0p *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fiq5e0p *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_srgiqer(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_srgiqer F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vprps7i *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vprps7i *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vprps7i *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t06ahi8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_t06ahi8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vjen3ng *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vjen3ng *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vjen3ng *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tk1c2p3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tk1c2p3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_v7qttjn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_v7qttjn *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_v7qttjn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tppsct0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tppsct0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fsfutor *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fsfutor *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fsfutor *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_u4u1c45(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_u4u1c45 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vau3iu8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vau3iu8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vau3iu8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uci28p7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_uci28p7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vddm9q0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vddm9q0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vddm9q0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_udcpr90(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_udcpr90 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vlf90rk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vlf90rk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vlf90rk *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_va5jnqp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_va5jnqp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmlgaaq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_vmlgaaq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_vmlgaaq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vnbkf1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vnbkf1p F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fh9to9u *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fh9to9u *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fh9to9u *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vosr5ur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vosr5ur F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fambdq4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fambdq4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fambdq4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vq1f9r0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vq1f9r0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_fbigqms *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_fbigqms *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_fbigqms *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqiq209(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vqiq209 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_f1281l5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_f1281l5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_f1281l5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_00qg1g3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_069unrn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0jg0fc8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_10eqf8a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1ikrr1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2agisls(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2o85j6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2ttu2pe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3764lq1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3bmo9nu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3g158bi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3sa8h99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3tj64sm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_40qkrph(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_41e0srt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_47etgnk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4hbbbf0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4kktfdj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4mcpeld(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4rkhbtp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5hkvedq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_67s9e97(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_68m5rrl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6k0kemf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6k11rh8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8fom6sk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8g8nrr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_930s2re(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_955f6re(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9m9i854(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a57v4pe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7a2haq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7bba9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aojb2c8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b9ef3bb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b9r4ekr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bnpbmcc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cghu5ur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cm7eh31(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cru4lds(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cu8cqgt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dm2l0vv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_efkt607(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ej5sdtn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fd380sp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fou71i7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g1gumfr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g2qn3vh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4itb9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gee2nac(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_geu2ipt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gsl696h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h28k9hu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hq2sdog(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i4on1pl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i9m1qbm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ir0df61(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ith45r1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j6u63hu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jtn6932(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kngq99i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_knls90g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l6e4eki(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ld8hf76(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1id2sb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mi6s2qr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nudto06(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o55qls2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pbi3fui(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_psqe8d9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q1c0du7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q8121ul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r6cjqr1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s4ja923(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_srgiqer(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t06ahi8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tk1c2p3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tppsct0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_u4u1c45(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uci28p7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_udcpr90(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_va5jnqp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vnbkf1p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vosr5ur(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vq1f9r0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqiq209(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_f1281l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_f1281l5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srl8r23tls393));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vqiq209, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s4g35ls4ma80g, 12)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_f2fgi8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_f2fgi8v F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st4gqcsm35nph));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(89));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_m1id2sb, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_skbd3tdpfkbk5, 23)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_ssq1no648r8mj, 62)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_f2pdud5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f2pdud5 F;
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s65ftmvtj458k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)64ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t13 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_jtn6932, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_closure t19 = t14;
            gm_node *ccx20 = t19.cx;
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, ((uint8_t *)&t17)), GM_LD(uintptr_t, ((uint8_t *)&t17) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, ((uint8_t *)&t18)), GM_LD(uintptr_t, ((uint8_t *)&t18) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_f36nqla(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f36nqla F;
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smc2kdmn5qjql));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(112));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_2o85j6u, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_s5kue8bfcvqfr, 26)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_sujipgkhb2fih, 7)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_tybihgc69jv3, ((uint8_t *)&t17), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_sgsd94uuisj8t, 72)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_f6fn313(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_f6fn313 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3pj34lce6rqp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_069unrn, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_smf5jnr5ps1bl, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfdbfrv46lihd, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f6rro2t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8gf7bnv27fi9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_2ttu2pe, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_slgmqgkioa4u6, 2)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_f89fec3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f89fec3 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk8it0s509dbd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_i9m1qbm, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sf42bd5sk6816, 25)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s2ha7brjf2gmp, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fambdq4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fambdq4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s516pv14t5t6i));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(72));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vosr5ur, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_su5bsj1nioctr, 21)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_svdl0vjc2ha7u, 49)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fb5qv68(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fb5qv68 F;
    gm_t0bf4lga76ktk gm_l0; /* args */
    gm_t0bf4lga76ktk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scnmhrk4ghd85));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0bf4lga76ktk t13 = GM_LD(gm_t0bf4lga76ktk, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_mi6s2qr, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            int32_t t16 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_scjfd9b9vg99c, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyqa6gfa8omk, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fbigqms(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fbigqms F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snl38trih7qf5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vq1f9r0, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sn88c2jg5b9gv, 3)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s4h6hphsrrhvo, 13)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fd6oegj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fd6oegj F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slc0d2s6km3s5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gee2nac, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s4lj2bemc87jp, 2)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fdvg246(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fdvg246 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjndugdkuh68b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t13 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_hq2sdog, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_seglpsc899j08, 10)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s5gn6cmh4thug, 37)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sj1gcl0qo4i9p, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fea89f6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fea89f6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sstqe1228ic6k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_2agisls, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_shol80fqem8c6, 47)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fego630(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_fego630 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq2qrq8ir5ihu));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gsl696h, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_snbrjpgiqf592, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fh9to9u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fh9to9u F;
    GM_STACK_CHECK(&F, 429u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5dmjeoiprfiv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)160ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tk6rvhh54g86p t13 = GM_LD(gm_tk6rvhh54g86p, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vnbkf1p, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 80));
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 96));
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 112));
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 128));
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 144));
            gm_closure t25 = t14;
            gm_node *ccx26 = t25.cx;
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_s82d2namrcvs3, 16)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_sk2oomiuvp44g, 5)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_s18e8dmribdb3, 6)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t17)), GM_LD(uintptr_t, ((uint8_t *)&t17) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_sgfqei77bsia8, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t25.code, t25.env, ccx26, &gm_tybihgc69jv3, ((uint8_t *)&t18), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_scs1evojsik8q, 3)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t19) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t19)), GM_LD(uintptr_t, ((uint8_t *)&t19) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_sdugj3qbhl84q, 4)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t20) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t20)), GM_LD(uintptr_t, ((uint8_t *)&t20) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_st52k2850n5r5, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t25.code, t25.env, ccx26, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_scs1evojsik8q, 3)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t22) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t22)), GM_LD(uintptr_t, ((uint8_t *)&t22) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_sdugj3qbhl84q, 4)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t23) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t23)), GM_LD(uintptr_t, ((uint8_t *)&t23) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t24) + 8) != 0 && gm_pf0aqhubr4up(t25.code, t25.env, ccx26, GM_LD(void *, ((uint8_t *)&t24)), GM_LD(uintptr_t, ((uint8_t *)&t24) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t25.code, t25.env, ccx26, gm_sqvepl1893o5g, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t28 = F.l2;
        if (!t28) goto ie27;
        {
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t30);
        }
        {
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t31);
            goto gm_fdone;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_fi775k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fi775k0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3avlcjhdh976));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_fd380sp, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s5s1m7053mh3n, 7)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fiq5e0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fiq5e0p F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6uch3l6osunv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_s4ja923, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s4av1h975k9ok, 46)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fj9bgml(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fj9bgml F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sif59l16kubo3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ej5sdtn, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sht5iivcbi64u, 11)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fjhi9ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fjhi9ui F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sct07pl02n02h));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_i4on1pl, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfu1s4a4bo4he, 24)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fk1icrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_fk1icrl F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 125u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn1r7qad8j2p2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_b9r4ekr, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sgb4vh2ukunor, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fl4q4sd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fl4q4sd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_segrf6njnsvht));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(68));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_1ikrr1s, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s6q7rsv30u3ma, 33)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_shv2qb39iqelf, 30)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fn68qf0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fn68qf0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbcjfjm1sdn3u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_q8121ul, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s8pt0g4bme9og, 29)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sn5pv7k63ni4c, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fnh5q7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fnh5q7b F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb6brpqpm5qjh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_00qg1g3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s8gaig0cs2uce, 18)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fp763nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fp763nd F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1s57v200fem0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tiilmfd16v3fc t13 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ith45r1, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fpa95lg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_fpa95lg F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7fdjhp9ga5oo));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_0jg0fc8, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s87p55h1isc6m, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fpoafkq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fpoafkq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so5l2ibq9ia3i));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4hbbbf0, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sccm0heds95r6, 59)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fqec6j2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fqec6j2 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svaaloo7n72id));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_o55qls2, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sao4mumvadoct, 3)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_so514oj5nbhcu, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fqmocq1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fqmocq1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqmifqg6egoj0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_a7bba9q, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_scaoagismk27j, 17)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfn640udk6pjn, 18)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_frab70n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_frab70n F;
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sngn7trhgdnc2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)64ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t13 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_67s9e97, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_closure t19 = t14;
            gm_node *ccx20 = t19.cx;
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_scrnhm4ha22vj, 17)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_sq4jcg2uujqmj, 15)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, ((uint8_t *)&t17)), GM_LD(uintptr_t, ((uint8_t *)&t17) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t19.code, t19.env, ccx20, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0 && gm_pf0aqhubr4up(t19.code, t19.env, ccx20, GM_LD(void *, ((uint8_t *)&t18)), GM_LD(uintptr_t, ((uint8_t *)&t18) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_frbkb04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_frbkb04 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sv89nb1qmpoaj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_psqe8d9, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sc6celu0e642t, 5)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_frnomph F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7uepsfl7trr3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_3764lq1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s2aj4fqmgmn1n, 66)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fsfutor F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdsb9longvn22));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_tppsct0, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s0kajv5j53ub7, 37)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fskpepf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fskpepf F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snsq190kog2e4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_6k0kemf, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_scd25kom6sdt8, 33)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sq32stff79ol1, 21)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_ftgbk9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ftgbk9j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5958kaiuh54g));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(83));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_cm7eh31, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s5d58k7uql7sf, 71)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sp7cdd0n91vli, 7)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fu8jpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fu8jpss F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sljj5lb6g47i1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_efkt607, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s0147vgtvilnq, 18)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_snsqui8o89u28, 14)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_fuvjr35(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fuvjr35 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp02l2e4viomq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_955f6re, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sicq1bvp13t5d, 22)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fv5tfj6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl9mgk5ho4kmr));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_a7a2haq, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_slj3uno8bsqk4, 26)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_fvh5va2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fvh5va2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3duthvmnjluh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(147));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_g2qn3vh, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sdt2alvv2h6jh, 47)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s72l6okdj2d3g, 94)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_v0s3sc7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v0s3sc7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssbb728kngg91));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(78));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_3tj64sm, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_snrrubbi4s05g, 15)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_stgav1sk1eq6u, 58)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_v2s58vm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v2s58vm F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5974q1kmorlc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_40qkrph, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_stmsadvf54k73, 2)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sg8f7kt1n4uc5, 47)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_v2sfrt0 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 125u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl9pujidpnv0r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4rkhbtp, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_se0f2jka437t4, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_v301q4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v301q4v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s137iv03ua7e7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8g8nrr6, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sn8d077r4fgpq, 25)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_v3lvmh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_v3lvmh8 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siot2vlveunn8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_cu8cqgt, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_soqhrfmr1feii, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s3d91jdjen8gc, 7)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_v40sfri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v40sfri F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sha819aic5lkk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_dm2l0vv, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s0bpji6inee0r, 18)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_v5s6luj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v5s6luj F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8us8sihs6eh7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_g1gumfr, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_st7fm661apctp, 16)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_soq66vtlbhee2, 55)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_v6efj7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v6efj7n F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqldj8dr0j7rf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tiilmfd16v3fc t13 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_a57v4pe, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_spkrf9e7cr6h4, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_v71csbq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v71csbq F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spb7dvkqr71v2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_q1c0du7, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s5irlrsnb30cc, 23)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_su2ima1nbr01d, 51)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_v7qttjn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v7qttjn F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7umjdfqes00m));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_tk1c2p3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_shrlc4g1vtjsu, 46)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tybihgc69jv3, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_v9dnkq5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_v9dnkq5 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 125u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s091o161s6kba));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_41e0srt, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_smcnje2cdb03e, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vakr6l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vakr6l2 F;
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soajcqnnsnidh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_kngq99i, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_slqq08j0fro21, 30)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_s41bap0m04tt9, 11)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_svgk3n4sk1uat, 8)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t17)), GM_LD(uintptr_t, ((uint8_t *)&t17) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_vau3iu8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vau3iu8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7qbqthgde8lk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_u4u1c45, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_si7qrooamq5ei, 25)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vc10pth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vc10pth F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5v9muv1jh89m));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(74));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_nudto06, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sg93rj30beob9, 28)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sjk9nricijji8, 44)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vc9g56o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vc9g56o F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssk8mqd1u6nn7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_j6u63hu, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_svl0gta90bsdr, 48)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vcvtacd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_vcvtacd F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shq89iblo3j6k));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_930s2re, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sq43o0crfp05h, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s4mqk7h9f98g3, 29)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vd1blml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vd1blml F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjh4l64ncn038));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4kktfdj, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_svml0iv6h5a82, 47)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sqvepl1893o5g, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vdba58s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vdba58s F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sictcqjuumcng));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_bnpbmcc, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s3hg5go4uihat, 51)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_svbftsm94r0bv, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vddm9q0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vddm9q0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su2vudghakb3u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_uci28p7, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s0886ibkvne07, 51)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ven7kt5 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 125u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sciu02cqahje3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_fou71i7, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_svdd37ikql4fk, 44)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sod9r7r024h9q, 5)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vfce7nu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vfce7nu F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st9djk4oc14um));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_knls90g, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sq9f472jpmh59, 63)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vfnel93(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vfnel93 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjm2j40p6489d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_3sa8h99, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s2urait735ofq, 4)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vfrm49j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vfrm49j F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf8e06i5juo6a));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4mcpeld, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s16mf4cvajh59, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_stkqau5o4elnq, 14)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vft2h8j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vft2h8j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sisn94tbdoeof));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_9m9i854, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sf6ug774thos9, 14)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sb77prkml0t3e, 14)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sg027ftm3ja87, 32)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vgl5p2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vgl5p2e F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sktjfeu2a0ava));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_3bmo9nu, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_stqiimqtsnd8d, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s5m9qimbv7e8k, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vilpuor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vilpuor F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn0ac8j2fho01));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ir0df61, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_snja4gioias3h, 18)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vj1olp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vj1olp7 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa56bd0p8nuib));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(142));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_g4itb9o, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s1cn5ab5opg9f, 130)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s078u1763sqk3, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vjen3ng(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vjen3ng F;
    gm_t2mvr8fpgjkg6 gm_l0; /* args */
    gm_t2mvr8fpgjkg6 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sagfhrncf3ga2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2mvr8fpgjkg6 t13 = GM_LD(gm_t2mvr8fpgjkg6, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_t06ahi8, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            double t16 = GM_LD(double, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyqqajgod2bb, ((uint8_t *)&t16), &(gm_spec){32, 0, 0, 0, 0, 5, 0u, -1})) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vlf90rk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vlf90rk F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sga7ge5dc8qdr));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_udcpr90, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s8s91tiu5r15q, 44)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vlsb354(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vlsb354 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1o778dp7d59d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ld8hf76, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sl9h8e795pkr4, 28)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfg1v6bqcbbav, 9)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vmb8nb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vmb8nb9 F;
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s60n1k0cqjuaq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(143));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_h28k9hu, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_scaoagismk27j, 17)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_s6n2mkqhnsjpk, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0 && gm_pf0aqhubr4up(t18.code, t18.env, ccx19, GM_LD(void *, ((uint8_t *)&t17)), GM_LD(uintptr_t, ((uint8_t *)&t17) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t18.code, t18.env, ccx19, gm_splvrmi68lqi3, 110)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_vmi8kfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vmi8kfe F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sogev0d3re2th));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_10eqf8a, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s7ddabri39fvk, 8)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vmlgaaq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vmlgaaq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa8q5t8i6ht30));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_va5jnqp, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tybihgc69jv3, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vmsom9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vmsom9v F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sft2smqlktfvo));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_geu2ipt, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_ssr91slt3ah1c, 10)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sgur19lpnttci, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vno937p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vno937p F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sufif7dqc1pm3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_6k11rh8, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_scgm14ju6d0p8, 14)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vns89mr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vns89mr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf7ldnukv79cp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_47etgnk, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s2jlhlq33qoal, 11)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_suv79odpsscph, 7)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_voj344o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_voj344o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3t3r41giacns));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8fom6sk, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s1bpuqd44lur7, 6)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vom8db6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vom8db6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk5d5fbcb3pir));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_68m5rrl, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_se08n5d1gngu7, 4)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_voqg5cl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_voqg5cl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf51sbqtper4f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_b9ef3bb, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfn9k9vgl9ged, 27)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_snkdp6opmsb8i, 41)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vp0clr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vp0clr1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfn5aos9cps1h));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_pbi3fui, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_smbbaf3n1gml2, 7)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t16)), GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vprps7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vprps7i F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4gdu7v0gur88));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_srgiqer, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s3kumt3nslbnp, 8)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vq3bur7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vq3bur7 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6tn8kl12f5ru));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_cru4lds, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s4m9nol66dh0a, 24)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vqmfrgb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vqmfrgb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbf8eactntf1g));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_l6e4eki, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s11aakm4d1c2s, 18)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vrlgsjr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vrlgsjr F;
    gm_tommf4j9upomc gm_l0; /* args */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slhm5kfb42dip));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tommf4j9upomc t13 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_r6cjqr1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_svu4pa4qq20q7, 5)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t17.code, t17.env, ccx18, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t17.code, t17.env, ccx18, gm_sfdbfrv46lihd, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty467d5cm1d5, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_vrv37j5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vrv37j5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgqep6dpse2vs));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_5hkvedq, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s23n3pek53qm1, 23)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s3q858hngo0ho, 38)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vsbf5l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vsbf5l9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smn3oh253jlna));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_cghu5ur, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_snfsrvfr6vnkt, 7)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vtr1b5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vtr1b5e F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 173u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smggdkb5phvp0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_aojb2c8, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s495moo6tjeh0, 19)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0 && gm_pf0aqhubr4up(t16.code, t16.env, ccx17, GM_LD(void *, ((uint8_t *)&t15)), GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)))) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_slogre7vmagad, 34)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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
inline __attribute__((aligned(4))) void gm_f_format_vucln8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_vucln8v F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soeesub7ccc96));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_3g158bi, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_sst2khrvr4nus, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t16.code, t16.env, ccx17, gm_s3sb6rrf98gpi, 19)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t19 = F.l2;
        if (!t19) goto ie18;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t21);
        }
        {
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t23 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t23);
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

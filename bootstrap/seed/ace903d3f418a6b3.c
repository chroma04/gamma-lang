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
typedef struct gm_cs2lqoac0a5ng4 { _Bool f0; uint32_t f1; uint64_t f2; } gm_cs2lqoac0a5ng4;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs2lqoac0a5ng4) == 16 && _Alignof(gm_cs2lqoac0a5ng4) == 8 && offsetof(gm_cs2lqoac0a5ng4, f0) == 0 && offsetof(gm_cs2lqoac0a5ng4, f1) == 4 && offsetof(gm_cs2lqoac0a5ng4, f2) == 8, "layout of `IntInfo`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; } gm_t0j6181meiu4s;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint32_t f4; } gm_t133qiba7tvhc;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; uint64_t f6; gm_b16_8 f7; } gm_t6q08imqe2768;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t f3; uint8_t p4[3]; gm_b16_8 f4; gm_b16_8 f5; } gm_t8fvq98mjik04;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_tg1t0c5hkovn5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_tpm6m3o0hprmc;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; gm_b16_8 f6; uint64_t f7; } gm_tsroeipraj6j8;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0j6181meiu4s) == 72 && _Alignof(gm_t0j6181meiu4s) == 8 && offsetof(gm_t0j6181meiu4s, f0) == 0 && offsetof(gm_t0j6181meiu4s, f1) == 16 && offsetof(gm_t0j6181meiu4s, f2) == 32 && offsetof(gm_t0j6181meiu4s, f3) == 48 && offsetof(gm_t0j6181meiu4s, f4) == 64, "layout of `(string, string, string, string, u64)`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t133qiba7tvhc) == 48 && _Alignof(gm_t133qiba7tvhc) == 8 && offsetof(gm_t133qiba7tvhc, f0) == 0 && offsetof(gm_t133qiba7tvhc, f1) == 8 && offsetof(gm_t133qiba7tvhc, f2) == 24 && offsetof(gm_t133qiba7tvhc, f3) == 40 && offsetof(gm_t133qiba7tvhc, f4) == 44, "layout of `(u32, string, string, u32, u32)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_th2lqbo77pvsc v1; } f1; } gm_t6j4d6ei14c2t;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6q08imqe2768) == 104 && _Alignof(gm_t6q08imqe2768) == 8 && offsetof(gm_t6q08imqe2768, f0) == 0 && offsetof(gm_t6q08imqe2768, f1) == 16 && offsetof(gm_t6q08imqe2768, f2) == 32 && offsetof(gm_t6q08imqe2768, f3) == 40 && offsetof(gm_t6q08imqe2768, f4) == 56 && offsetof(gm_t6q08imqe2768, f5) == 64 && offsetof(gm_t6q08imqe2768, f6) == 80 && offsetof(gm_t6q08imqe2768, f7) == 88, "layout of `(string, string, usize, string, usize, string, usize, string)`");
_Static_assert(sizeof(gm_t8fvq98mjik04) == 72 && _Alignof(gm_t8fvq98mjik04) == 8 && offsetof(gm_t8fvq98mjik04, f0) == 0 && offsetof(gm_t8fvq98mjik04, f1) == 16 && offsetof(gm_t8fvq98mjik04, f2) == 32 && offsetof(gm_t8fvq98mjik04, f3) == 36 && offsetof(gm_t8fvq98mjik04, f4) == 40 && offsetof(gm_t8fvq98mjik04, f5) == 56, "layout of `([]u8, string, u32, bool, string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg1t0c5hkovn5) == 128 && _Alignof(gm_tg1t0c5hkovn5) == 8 && offsetof(gm_tg1t0c5hkovn5, f0) == 0 && offsetof(gm_tg1t0c5hkovn5, f1) == 16 && offsetof(gm_tg1t0c5hkovn5, f2) == 32 && offsetof(gm_tg1t0c5hkovn5, f3) == 48 && offsetof(gm_tg1t0c5hkovn5, f4) == 64 && offsetof(gm_tg1t0c5hkovn5, f5) == 80 && offsetof(gm_tg1t0c5hkovn5, f6) == 96 && offsetof(gm_tg1t0c5hkovn5, f7) == 112, "layout of `([]u8, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpm6m3o0hprmc) == 64 && _Alignof(gm_tpm6m3o0hprmc) == 8 && offsetof(gm_tpm6m3o0hprmc, f0) == 0 && offsetof(gm_tpm6m3o0hprmc, f1) == 16 && offsetof(gm_tpm6m3o0hprmc, f2) == 24 && offsetof(gm_tpm6m3o0hprmc, f3) == 40 && offsetof(gm_tpm6m3o0hprmc, f4) == 48, "layout of `(string, u64, string, u64, string)`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_tvdg94bk7aa22 f1; } gm_tq9t4giin3pm0;
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsroeipraj6j8) == 112 && _Alignof(gm_tsroeipraj6j8) == 8 && offsetof(gm_tsroeipraj6j8, f0) == 0 && offsetof(gm_tsroeipraj6j8, f1) == 16 && offsetof(gm_tsroeipraj6j8, f2) == 32 && offsetof(gm_tsroeipraj6j8, f3) == 48 && offsetof(gm_tsroeipraj6j8, f4) == 64 && offsetof(gm_tsroeipraj6j8, f5) == 80 && offsetof(gm_tsroeipraj6j8, f6) == 88 && offsetof(gm_tsroeipraj6j8, f7) == 104, "layout of `(string, string, string, string, string, u64, string, u64)`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t6j4d6ei14c2t) == 40 && _Alignof(gm_t6j4d6ei14c2t) == 8 && offsetof(gm_t6j4d6ei14c2t, f0) == 0 && offsetof(gm_t6j4d6ei14c2t, f1) == 8, "layout of `Unsup!?string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq9t4giin3pm0) == 24 && _Alignof(gm_tq9t4giin3pm0) == 4 && offsetof(gm_tq9t4giin3pm0, f0) == 0 && offsetof(gm_tq9t4giin3pm0, f1) == 12, "layout of `(Builtin, Builtin)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
struct gm_fr_alloc_tag_a71p35s {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* v */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_arith_asm_pklrmbh {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* outs */
    gm_trqfvmjtt4sij l13; /* ins */
    gm_trqfvmjtt4sij l20; /* clobbers */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_arith_get_jao7f8p {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_asm_text_fmvci6l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_assert_m8ussus {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_atomic_plj0t12 {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* ords */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bit_count_77uni44 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_black_box_r6deojc {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_builtin_s7ark4p {
    gm_frame h;
    gm_trqfvmjtt4sij l21; /* vals */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_convert_lqrk2n0 {
    gm_frame h;
    gm_tpautr4br7h3l l13; /* sd */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_from_tag_2m7nfln {
    gm_frame h;
    gm_tckdobunaulee l12; /* vd */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_pow_s0lbtt4 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_rotate_tpbpt1c {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_sys_builtin_svdoho3 {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* cargs */
    gm_t5o0tpv5jka2l ret;
};
bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_alloc_tag_a71p35s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_arith_asm_pklrmbh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_arith_fits_7degt3e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_arith_get_jao7f8p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_arith_pointee_align_vqoa1mk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_asm_text_fmvci6l(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_assert_m8ussus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_atomic_plj0t12(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_bit_count_77uni44(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_black_box_r6deojc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_bound_n4p0vpb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_builtin_s7ark4p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_convert_lqrk2n0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_fiber_builtin_bgfcp56(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_finish_int_cr0p55i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tuglhjjmj2g58 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8, gm_b16_8 a9);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_00b54tv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_15kpcel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_21vnq4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_25p33s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_261kv0l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2g08abe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2qdci62(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3itad0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3j59e2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3s3fqnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3vgp4uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4ok6i29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_53mv29j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5rrmec0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_68bbqsa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6cn78ik(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6df13cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7g5nhjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7gtji42(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_83msomh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_87dn9rn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_898275p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8d9jpvv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8m4h4ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8sn6h96(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_a1ch927(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a1ec0ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_a7bth9i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aculdom(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_aghnr00(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b05vb48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bm6js5o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c57v5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c9b4vl3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cjdn0sp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ckgfhm4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_da6unq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dv5f37i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eqjcikj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eucvbnh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_fh9to9u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gf4f6rt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gp2h96m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h95c7u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_he769rg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_iqjnutf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_jj2aipm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jpvl2js(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kbs7h9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ljsk7ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_m0j4lkh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_m7gv7a8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ms7m2d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nkjq1if(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nm3fl8o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o73frof(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ohk11b5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p1ca3i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_patlqq0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q296tqt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qofaffd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s2ml2tu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tvr4di5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ucng4n7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_uhjjs86(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uj77khu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_from_tag_2m7nfln(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_ii_max_gtp5h90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_098d9va(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_0kkt9cm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_10398ks(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_156hint(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_21h063v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3utfuce(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_5q2h82g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6h7r936(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6sel63v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6t3ttmd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7e19lva(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7pb5lg6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_81ikvle(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_a98fois(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ahjvru7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_biu9mas(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_bq6igl9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_calnga5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_di9ea86(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_evpuk0q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl8o1sr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jq6ksk1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_kafc4c5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ki2bckn(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_linqala(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_lm0b3ih(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_n685kk5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_o1esdvv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_oi4f7u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qno93os(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_vgjqckp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_vh7uebp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_pow_s0lbtt4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_rotate_tpbpt1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_sys_builtin_svdoho3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_vector_builtin_a1f1ati(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s0cpkv7s4ef85[24];
extern const uint8_t gm_s0fg7tcl0p4ij[13];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s12ng38ve3205[8];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s42h828lb45uf[19];
extern const uint8_t gm_s565fgm0tsngr[15];
extern const uint8_t gm_s63o5blu3j46d[16];
extern const uint8_t gm_s66fk044n1n0k[9];
extern const uint8_t gm_s9gk1b0ve64mt[10];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sd2ht7fkpnftp[17];
extern const uint8_t gm_sdeu8nuc597e8[19];
extern const uint8_t gm_sg3ba4vpic0jb[19];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgjc0ebf56hf4[8];
extern const uint8_t gm_shhrbleiv8o3j[14];
extern const uint8_t gm_si0mnqg6j1eu8[19];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm0f8bs4gd2ma[18];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sn8alsh9na9ne[12];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2j6ljeja7ilp[];
extern const uint8_t gm_sv7lsha1e0i8r[13];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const uint8_t gm_svpr02j6er8vd[15];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) void gm_f_alloc_tag_a71p35s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_alloc_tag_a71p35s F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* loc */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* k */
    gm_b16_8 gm_l9; /* f */
    gm_b16_8 gm_l10; /* fa */
    gm_b16_8 gm_l11; /* fp */
    gm_b16_8 gm_l12; /* fl */
    gm_b16_8 gm_l13; /* line */
    gm_b16_8 gm_l14; /* col */
    GM_STACK_CHECK(&F, 2042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_b16_8 t4 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 1136) + 1));
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie5;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t13 + 32, 0);
            ((uint8_t *)&t13)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t12);
            gm_t5o0tpv5jka2l t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            gm_l6 = ((uint64_t)3ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l7;
                uint64_t t19 = gm_l6;
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l7;
                    uint64_t t24 = t23;
                    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&t24));
                    gm_l8 = t27;
                    {
                        void * t29 = (void *)((uint8_t *)&F.l5);
                        void * t30 = gm_l0;
                        uint64_t t31 = gm_l8;
                        uint64_t t32 = (uintptr_t)t31;
                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_src2j6ljeja7ilp, 259, 41);
                        void * t34 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t32 * 224);
                        gm_t5o0tpv5jka2l t35;
                        gm_f_expr_porbc9t((void *)&t35, (void *)NULL, cx, NULL, t30, t34);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_t5o0tpv5jka2l t36 = t35;
                        if (((((uint8_t *)&t36))[0] != 0)) {
                            gm_t08olq97rscn3 t37 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t36) + 8));
                            gm_t5o0tpv5jka2l t38;
                            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                            ((uint8_t *)&t38)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t38 + 8), t37);
                            F.ret = t38;
                            goto gm_fret;
                        }
                        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                        gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t29, t39);
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src2j6ljeja7ilp, 259, 21);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 259, 5);
me25:;
                }
lc17:;
                {
                    uint64_t t44 = gm_l7;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l7 = t47;
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
        void * t48 = gm_l0;
        uint64_t t49 = (uintptr_t)((uint64_t)3ULL);
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_src2j6ljeja7ilp, 260, 23);
        void * t51 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t49 * 224);
        gm_t5o0tpv5jka2l t52;
        gm_f_expr_porbc9t((void *)&t52, (void *)NULL, cx, NULL, t48, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t53 = t52;
        if (((((uint8_t *)&t53))[0] != 0)) {
            gm_t08olq97rscn3 t54 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t53) + 8));
            gm_t5o0tpv5jka2l t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
            ((uint8_t *)&t55)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t55 + 8), t54);
            F.ret = t55;
            goto gm_fret;
        }
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
        gm_l9 = t56;
    }
    {
        void * t57 = gm_l0;
        uint64_t t58 = (uintptr_t)((uint64_t)3ULL);
        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_src2j6ljeja7ilp, 261, 22);
        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t58 * 224) + 0));
        gm_b16_8 t61 = gm_l9;
        gm_b16_8 t62 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t57, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t62;
    }
    {
        gm_b16_8 t63 = gm_l10;
        gm_b16_8 t64 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t64;
    }
    {
        void * t65 = gm_l0;
        gm_b16_8 t66 = gm_l10;
        gm_b16_8 t67 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t67;
    }
    {
        void * t68 = gm_l0;
        uint64_t t69 = (uintptr_t)((uint64_t)4ULL);
        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_src2j6ljeja7ilp, 264, 26);
        void * t71 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t69 * 224);
        gm_t5o0tpv5jka2l t72;
        gm_f_expr_porbc9t((void *)&t72, (void *)NULL, cx, NULL, t68, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t73 = t72;
        if (((((uint8_t *)&t73))[0] != 0)) {
            gm_t08olq97rscn3 t74 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t73) + 8));
            gm_t5o0tpv5jka2l t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            ((uint8_t *)&t75)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t75 + 8), t74);
            F.ret = t75;
            goto gm_fret;
        }
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
        gm_l13 = t76;
    }
    {
        void * t77 = gm_l0;
        uint64_t t78 = (uintptr_t)((uint64_t)5ULL);
        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_src2j6ljeja7ilp, 265, 25);
        void * t80 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t78 * 224);
        gm_t5o0tpv5jka2l t81;
        gm_f_expr_porbc9t((void *)&t81, (void *)NULL, cx, NULL, t77, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t82 = t81;
        if (((((uint8_t *)&t82))[0] != 0)) {
            gm_t08olq97rscn3 t83 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t82) + 8));
            gm_t5o0tpv5jka2l t84;
            GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
            ((uint8_t *)&t84)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t84 + 8), t83);
            F.ret = t84;
            goto gm_fret;
        }
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
        gm_l14 = t85;
    }
    {
        uint8_t t87 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)76ULL)));
        if (!t87) goto ie86;
        {
            void * t89 = gm_l0;
            gm_th2d9c7aju8hp t90;
            uint64_t t91 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t92 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_src2j6ljeja7ilp, 267, 63);
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t91 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 0, t93);
            uint64_t t94 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t95 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t94 >= (uintptr_t)(t95)) gm_trap_index(t94, t95, gm_src2j6ljeja7ilp, 267, 75);
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t94 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 16, t96);
            uint64_t t97 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_src2j6ljeja7ilp, 267, 87);
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t97 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 32, t99);
            gm_b16_8 t100 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 48, t100);
            gm_b16_8 t101 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 64, t101);
            gm_b16_8 t102 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 80, t102);
            gm_b16_8 t103 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 96, t103);
            gm_b0_1 t104 = gm_f_linef_ki2bckn((void *)NULL, cx, NULL, t89, &t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix86;
ie86:;
        {
            void * t106 = gm_l0;
            gm_tg1t0c5hkovn5 t107;
            uint64_t t108 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t109 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_src2j6ljeja7ilp, 270, 69);
            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t108 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t110);
            uint64_t t111 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t112 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_src2j6ljeja7ilp, 270, 81);
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t111 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t113);
            uint64_t t114 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t115 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_src2j6ljeja7ilp, 270, 93);
            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t114 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 32, t116);
            gm_b16_8 t117 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 48, t117);
            gm_b16_8 t118 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 64, t118);
            gm_b16_8 t119 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 80, t119);
            gm_b16_8 t120 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 96, t120);
            gm_b16_8 t121 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t107 + 112, t121);
            gm_b0_1 t122 = gm_f_linef_evpuk0q((void *)NULL, cx, NULL, t106, &t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix86:;
    }
    {
        void * t123 = gm_l0;
        gm_b16_8 t124 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t125;
        GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t125 + 32, 0);
        ((uint8_t *)&t125)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 8), t124);
        gm_t5o0tpv5jka2l t126 = t125;
        uint8_t t127 = ((((uint8_t *)&t126))[0] != 0);
        F.ret = t125;
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
__attribute__((aligned(4))) void gm_f_arith_asm_pklrmbh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_arith_asm_pklrmbh F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* outputs */
    uint64_t gm_l3; /* inputs */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* c */
    void * gm_l9; /* p */
    gm_t88rcucg1b7uk gm_l10; /* pointee */
    gm_b16_8 gm_l11; /* ct */
    gm_b16_8 gm_l12; /* pv */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    void * gm_l17; /* c */
    void * gm_l18; /* v */
    gm_b16_8 gm_l19; /* vv */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* k */
    GM_STACK_CHECK(&F, 2273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l2;
            gm_l5 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = gm_l5;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l7 = t14;
                    {
                        uint64_t t16 = gm_l7;
                        uint64_t r17; bool o18 = __builtin_mul_overflow(((uint64_t)2ULL), t16, &r17);
                        if (o18) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 14, 24);
                        uint64_t t19 = r17;
                        uint64_t r20; bool o21 = __builtin_add_overflow(((uint64_t)1ULL), t19, &r20);
                        if (o21) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 14, 20);
                        uint64_t t22 = r20;
                        uint64_t t23 = (uintptr_t)t22;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_src2j6ljeja7ilp, 14, 15);
                        void * t25 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 224);
                        gm_l8 = t25;
                    }
                    {
                        uint64_t t26 = gm_l7;
                        uint64_t r27; bool o28 = __builtin_mul_overflow(((uint64_t)2ULL), t26, &r27);
                        if (o28) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 15, 24);
                        uint64_t t29 = r27;
                        uint64_t r30; bool o31 = __builtin_add_overflow(((uint64_t)2ULL), t29, &r30);
                        if (o31) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 15, 20);
                        uint64_t t32 = r30;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src2j6ljeja7ilp, 15, 15);
                        void * t35 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t33 * 224);
                        gm_l9 = t35;
                    }
                    {
                        void * t37 = gm_l0;
                        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
                        void * t39 = (void *)(((uint8_t *)t38) + 8);
                        void * t40 = gm_l9;
                        gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t40) + 0));
                        gm_t53at80pfdus3 t42;
                        gm_f_kind_ghvav22((void *)&t42, t39, GM_CONV(gm_csguankko4lmhd, t41));
                        gm_t53at80pfdus3 t43 = t42;
                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
                        if (t44 != ((uint8_t)8ULL)) goto ie36;
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t43) + 8) + 4));
                        gm_l10 = t45;
                        {
                            void * t47 = gm_l0;
                            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                            void * t49 = (void *)(((uint8_t *)t48) + 8);
                            gm_t88rcucg1b7uk t50 = gm_l10;
                            gm_b16_8 t51 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t49, t50);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_l11 = t51;
                        }
                        {
                            void * t52 = gm_l0;
                            void * t53 = gm_l9;
                            gm_t5o0tpv5jka2l t54;
                            gm_f_expr_porbc9t((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_t5o0tpv5jka2l t55 = t54;
                            if (((((uint8_t *)&t55))[0] != 0)) {
                                gm_t08olq97rscn3 t56 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t55) + 8));
                                gm_t5o0tpv5jka2l t57;
                                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                                ((uint8_t *)&t57)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t57 + 8), t56);
                                F.ret = t57;
                                goto gm_fret;
                            }
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                            gm_l12 = t58;
                        }
                        {
                            void * t59 = (void *)((uint8_t *)&F.l4);
                            gm_tfeab279c16rr t60;
                            void * t61 = gm_l8;
                            gm_b16_8 t62 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t61);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t62);
                            gm_b16_8 t63 = gm_l11;
                            GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t63);
                            gm_b16_8 t64 = gm_l12;
                            GM_ST(gm_b16_8, (uint8_t *)&t60 + 32, t64);
                            gm_tabb710d9mq2e t65;
                            gm_f_format_bm6js5o((void *)&t65, (void *)NULL, cx, NULL, &t60);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tabb710d9mq2e t66 = t65;
                            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_src2j6ljeja7ilp, 19, 19);
                            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                            gm_b16_8 t68 = t67;
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                            gm_tftb1odc739t5 t70 = gm_f_push_3ef746b(t59, t69);
                            gm_tftb1odc739t5 t71 = t70;
                            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_src2j6ljeja7ilp, 19, 9);
                            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                        }
                        goto ix36;
ie36:;
                        {
                            gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 16, 53);
                        }
ix36:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 13, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t75 = gm_l6;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l6 = t78;
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
    {
        gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            uint64_t t80 = gm_l3;
            gm_l14 = t80;
        }
        {
            for (;;) {
                uint64_t t82 = gm_l15;
                uint64_t t83 = gm_l14;
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l15;
                    uint64_t t88 = t87;
                    uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&t88));
                    gm_l16 = t91;
                    {
                        uint64_t t93 = gm_l2;
                        uint64_t t94 = gm_l16;
                        uint64_t r95; bool o96 = __builtin_add_overflow(t93, t94, &r95);
                        if (o96) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 23, 29);
                        uint64_t t97 = r95;
                        uint64_t r98; bool o99 = __builtin_mul_overflow(((uint64_t)2ULL), t97, &r98);
                        if (o99) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 23, 24);
                        uint64_t t100 = r98;
                        uint64_t r101; bool o102 = __builtin_add_overflow(((uint64_t)1ULL), t100, &r101);
                        if (o102) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 23, 20);
                        uint64_t t103 = r101;
                        uint64_t t104 = (uintptr_t)t103;
                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_src2j6ljeja7ilp, 23, 15);
                        void * t106 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 224);
                        gm_l17 = t106;
                    }
                    {
                        uint64_t t107 = gm_l2;
                        uint64_t t108 = gm_l16;
                        uint64_t r109; bool o110 = __builtin_add_overflow(t107, t108, &r109);
                        if (o110) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 24, 29);
                        uint64_t t111 = r109;
                        uint64_t r112; bool o113 = __builtin_mul_overflow(((uint64_t)2ULL), t111, &r112);
                        if (o113) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 24, 24);
                        uint64_t t114 = r112;
                        uint64_t r115; bool o116 = __builtin_add_overflow(((uint64_t)2ULL), t114, &r115);
                        if (o116) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 24, 20);
                        uint64_t t117 = r115;
                        uint64_t t118 = (uintptr_t)t117;
                        uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_src2j6ljeja7ilp, 24, 15);
                        void * t120 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 224);
                        gm_l18 = t120;
                    }
                    {
                        void * t121 = gm_l0;
                        void * t122 = gm_l18;
                        gm_t5o0tpv5jka2l t123;
                        gm_f_expr_porbc9t((void *)&t123, (void *)NULL, cx, NULL, t121, t122);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                        gm_t5o0tpv5jka2l t124 = t123;
                        if (((((uint8_t *)&t124))[0] != 0)) {
                            gm_t08olq97rscn3 t125 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t124) + 8));
                            gm_t5o0tpv5jka2l t126;
                            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                            ((uint8_t *)&t126)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t126 + 8), t125);
                            F.ret = t126;
                            goto gm_fret;
                        }
                        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                        gm_l19 = t127;
                    }
                    {
                        void * t128 = (void *)((uint8_t *)&F.l13);
                        gm_t6j6smj7qoctu t129;
                        void * t130 = gm_l17;
                        gm_b16_8 t131 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t130);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t129 + 0, t131);
                        gm_b16_8 t132 = gm_l19;
                        GM_ST(gm_b16_8, (uint8_t *)&t129 + 16, t132);
                        gm_tabb710d9mq2e t133;
                        gm_f_format_nm3fl8o((void *)&t133, (void *)NULL, cx, NULL, &t129);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                        gm_tabb710d9mq2e t134 = t133;
                        if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_src2j6ljeja7ilp, 26, 18);
                        gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                        gm_b16_8 t136 = t135;
                        gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                        gm_tftb1odc739t5 t138 = gm_f_push_3ef746b(t128, t137);
                        gm_tftb1odc739t5 t139 = t138;
                        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_src2j6ljeja7ilp, 26, 9);
                        gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
                    }
                    goto me89;
mn90:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 22, 5);
me89:;
                }
lc81:;
                {
                    uint64_t t142 = gm_l15;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l15 = t145;
                }
            }
gm_ll85:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
            }
            goto gm_lad;
lb81:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t147 = gm_l2;
            uint64_t t148 = gm_l3;
            uint64_t r149; bool o150 = __builtin_add_overflow(t147, t148, &r149);
            if (o150) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 29, 24);
            uint64_t t151 = r149;
            uint64_t r152; bool o153 = __builtin_mul_overflow(((uint64_t)2ULL), t151, &r152);
            if (o153) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 29, 19);
            uint64_t t154 = r152;
            uint64_t r155; bool o156 = __builtin_add_overflow(((uint64_t)1ULL), t154, &r155);
            if (o156) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 29, 15);
            uint64_t t157 = r155;
            gm_l22 = t157;
        }
        {
            uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l21 = t158;
        }
        {
            for (;;) {
                uint64_t t160 = gm_l22;
                uint64_t t161 = gm_l21;
                uint8_t t162 = (t160 < t161);
                if (!t162) goto lb159;
                {
                    uint64_t t165 = gm_l22;
                    uint64_t t166 = t165;
                    uint64_t t169 = GM_LD(uint64_t, ((uint8_t *)&t166));
                    gm_l23 = t169;
                    {
                        void * t171 = (void *)((uint8_t *)&F.l20);
                        uint64_t t172 = gm_l23;
                        uint64_t t173 = (uintptr_t)t172;
                        uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t173 >= (uintptr_t)(t174)) gm_trap_index(t173, t174, gm_src2j6ljeja7ilp, 29, 79);
                        void * t175 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t173 * 224);
                        gm_b16_8 t176 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t175);
                        if (gm_unw.kind) {
                            goto gm_ll163;
                        }
                        gm_tftb1odc739t5 t177 = gm_f_push_3ef746b(t171, t176);
                        gm_tftb1odc739t5 t178 = t177;
                        if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 1, gm_src2j6ljeja7ilp, 29, 55);
                        gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 1));
                    }
                    goto me167;
mn168:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 29, 5);
me167:;
                }
lc159:;
                {
                    uint64_t t181 = gm_l22;
                    uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                    uint64_t t184 = r182;
                    gm_l22 = t184;
                }
            }
gm_ll163:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb159; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc159; }
            }
            goto gm_lad;
lb159:;
        }
    }
    {
        void * t185 = gm_l0;
        gm_t0uuhaljsf410 t186;
        uint64_t t187 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t188 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t187 >= (uintptr_t)(t188)) gm_trap_index(t187, t188, gm_src2j6ljeja7ilp, 30, 65);
        void * t189 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t187 * 224);
        gm_b16_8 t190 = gm_f_asm_text_fmvci6l((void *)NULL, cx, NULL, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t186 + 0, t190);
        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t192 = t191;
        gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
        gm_b16_8 t194;
        GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t195;
        gm_f_join_sfknljb((void *)&t195, (void *)NULL, cx, NULL, t193, t194);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t196 = t195;
        if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 8, gm_src2j6ljeja7ilp, 30, 75);
        gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&t196) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t186 + 16, t197);
        gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t199 = t198;
        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
        gm_b16_8 t201;
        GM_ST(void *, (uint8_t *)&t201, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t202;
        gm_f_join_sfknljb((void *)&t202, (void *)NULL, cx, NULL, t200, t201);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t203 = t202;
        if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 8, gm_src2j6ljeja7ilp, 30, 104);
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&t203) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t186 + 32, t204);
        gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        gm_b16_8 t206 = t205;
        gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
        gm_b16_8 t208;
        GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t209;
        gm_f_join_sfknljb((void *)&t209, (void *)NULL, cx, NULL, t207, t208);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t210 = t209;
        if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 8, gm_src2j6ljeja7ilp, 30, 132);
        gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&t210) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t186 + 48, t211);
        gm_b0_1 t212 = gm_f_linef_81ikvle((void *)NULL, cx, NULL, t185, &t186);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t213 = gm_l0;
        gm_b16_8 t214 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t213);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t215;
        GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t215 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t215 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t215 + 32, 0);
        ((uint8_t *)&t215)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t215 + 8), t214);
        gm_t5o0tpv5jka2l t216 = t215;
        uint8_t t217 = ((((uint8_t *)&t216))[0] != 0);
        F.ret = t215;
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
__attribute__((aligned(4))) void gm_f_arith_get_jao7f8p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_arith_get_jao7f8p F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l4; /* t */
    void * gm_l5; /* base */
    gm_b16_8 gm_l6; /* ptr */
    gm_b16_8 gm_l7; /* len */
    gm_b16_8 gm_l8; /* v */
    gm_b16_8 gm_l9; /* a */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* pointee */
    uint64_t gm_l12; /* n */
    gm_b16_8 gm_l13; /* v */
    void * gm_l14; /* tys */
    gm_b16_8 gm_l15; /* v */
    gm_b16_8 gm_l16; /* i */
    gm_t88rcucg1b7uk gm_l17; /* inner */
    gm_t88rcucg1b7uk gm_l18; /* elem */
    gm_t88rcucg1b7uk gm_l19; /* pointee */
    uint64_t gm_l20; /* esz */
    gm_b16_8 gm_l21; /* res */
    gm_b16_8 gm_l22; /* addr */
    gm_b16_8 gm_l23; /* v */
    gm_b16_8 gm_l24; /* some */
    gm_b16_8 gm_l25; /* none */
    GM_STACK_CHECK(&F, 4388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 449, 14);
        void * t6 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t4 * 224);
        gm_l5 = t6;
    }
    {
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
        gm_l6 = t7;
    }
    {
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        gm_l7 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        void * t12 = gm_l5;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
        gm_t53at80pfdus3 t14;
        gm_f_kind_ghvav22((void *)&t14, t11, GM_CONV(gm_csguankko4lmhd, t13));
        gm_t53at80pfdus3 t15 = t14;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma20; case ((uint8_t)7ULL): goto ma20; case ((uint8_t)8ULL): goto ma19; case ((uint8_t)9ULL): goto ma20; case ((uint8_t)10ULL): goto ma17; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma20; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma20; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma20; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma20; case ((uint8_t)21ULL): goto ma20; case ((uint8_t)22ULL): goto ma20; case ((uint8_t)23ULL): goto ma20; case ((uint8_t)24ULL): goto ma20; case ((uint8_t)25ULL): goto ma20; case ((uint8_t)26ULL): goto ma20; default: goto ma20; }
ma17:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t24 != ((uint8_t)10ULL)) goto mn23;
        {
            void * t26 = gm_l0;
            void * t27 = gm_l5;
            gm_t5o0tpv5jka2l t28;
            gm_f_expr_porbc9t((void *)&t28, (void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) {
                gm_t08olq97rscn3 t30 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t29) + 8));
                gm_t5o0tpv5jka2l t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                ((uint8_t *)&t31)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t31 + 8), t30);
                F.ret = t31;
                goto gm_fret;
            }
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
            gm_l8 = t32;
        }
        {
            void * t33 = gm_l0;
            void * t34 = gm_l5;
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
            gm_b16_8 t36 = gm_l8;
            gm_b16_8 t37 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t33, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t37;
        }
        {
            gm_b16_8 t38 = gm_l9;
            gm_b16_8 t39 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t40;
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
            gm_tabb710d9mq2e t41;
            gm_f_format_da6unq6((void *)&t41, (void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t42 = t41;
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_src2j6ljeja7ilp, 456, 19);
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
            gm_b16_8 t44 = t43;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_l6 = t45;
        }
        {
            void * t46 = gm_l0;
            gm_b16_8 t47 = gm_l9;
            gm_b16_8 t48 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t48;
        }
        goto me16;
mn23:;
ma18:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t50 != ((uint8_t)11ULL)) goto mn49;
        uint64_t t51 = GM_LD(uint64_t, ((((uint8_t *)&t15) + 8) + 0));
        gm_l10 = t51;
        {
            void * t53 = gm_l0;
            void * t54 = gm_l5;
            gm_t5o0tpv5jka2l t55;
            gm_f_place_addr_7ji15ot((void *)&t55, (void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t56 = t55;
            if (((((uint8_t *)&t56))[0] != 0)) {
                gm_t08olq97rscn3 t57 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t56) + 8));
                gm_t5o0tpv5jka2l t58;
                GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                ((uint8_t *)&t58)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t58 + 8), t57);
                F.ret = t58;
                goto gm_fret;
            }
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
            gm_l6 = t59;
        }
        {
            uint64_t t60 = gm_l10;
            gm_t4ce0jd3tdro4 t61;
            GM_ST(uint64_t, ((uint8_t *)&t61 + 0), t60);
            gm_tabb710d9mq2e t62;
            gm_f_format_1amnh72((void *)&t62, (void *)NULL, cx, NULL, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t63 = t62;
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_src2j6ljeja7ilp, 461, 19);
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
            gm_b16_8 t65 = t64;
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
            gm_l7 = t66;
        }
        goto me16;
mn49:;
ma19:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t68 != ((uint8_t)8ULL)) goto mn67;
        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t15) + 8) + 4));
        gm_l11 = t69;
        void * t70 = gm_l0;
        void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
        void * t72 = (void *)(((uint8_t *)t71) + 8);
        gm_t88rcucg1b7uk t73 = gm_l11;
        gm_t53at80pfdus3 t74;
        gm_f_kind_ghvav22((void *)&t74, t72, GM_CONV(gm_csguankko4lmhd, t73));
        gm_t53at80pfdus3 t75 = t74;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
        if (t78 != ((uint8_t)11ULL)) goto mn77;
        uint64_t t79 = GM_LD(uint64_t, ((((uint8_t *)&t75) + 8) + 0));
        gm_l12 = t79;
        {
            void * t81 = gm_l0;
            void * t82 = gm_l5;
            gm_t5o0tpv5jka2l t83;
            gm_f_expr_porbc9t((void *)&t83, (void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t84 = t83;
            if (((((uint8_t *)&t84))[0] != 0)) {
                gm_t08olq97rscn3 t85 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t84) + 8));
                gm_t5o0tpv5jka2l t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                ((uint8_t *)&t86)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t86 + 8), t85);
                F.ret = t86;
                goto gm_fret;
            }
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t84) + 8));
            gm_l13 = t87;
        }
        {
            gm_b16_8 t88 = gm_l13;
            gm_tnpifv20irnph t89;
            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
            gm_tabb710d9mq2e t90;
            gm_f_format_da6unq6((void *)&t90, (void *)NULL, cx, NULL, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t91 = t90;
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_src2j6ljeja7ilp, 466, 23);
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
            gm_b16_8 t93 = t92;
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_l6 = t94;
        }
        {
            uint64_t t95 = gm_l12;
            gm_t4ce0jd3tdro4 t96;
            GM_ST(uint64_t, ((uint8_t *)&t96 + 0), t95);
            gm_tabb710d9mq2e t97;
            gm_f_format_1amnh72((void *)&t97, (void *)NULL, cx, NULL, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t98 = t97;
            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_src2j6ljeja7ilp, 467, 23);
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
            gm_b16_8 t100 = t99;
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            gm_l7 = t101;
        }
        goto me76;
mn77:;
        void * t103 = gm_l1;
        gm_tl874t2atnlh3 t104 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t103) + 208));
        gm_b16_8 t105;
        GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sd2ht7fkpnftp));
        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(16));
        gm_t08olq97rscn3 t106;
        gm_f_unsupported_531q51r((void *)&t106, (void *)NULL, cx, NULL, t104, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t107;
        GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
        ((uint8_t *)&t107)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t107 + 8), t106);
        gm_t5o0tpv5jka2l t108 = t107;
        uint8_t t109 = ((((uint8_t *)&t108))[0] != 0);
        F.ret = t107;
        goto gm_fret;
        goto me76;
mn102:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 463, 29);
me76:;
        goto me16;
mn67:;
ma20:;
        void * t111 = gm_l1;
        gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t111) + 208));
        void * t113 = gm_l0;
        void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
        void * t115 = (void *)(((uint8_t *)t114) + 8);
        void * t116 = gm_l5;
        gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t116) + 0));
        gm_b16_8 t118 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t115, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t119;
        GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t118);
        gm_tabb710d9mq2e t120;
        gm_f_format_4ok6i29((void *)&t120, (void *)NULL, cx, NULL, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t121 = t120;
        if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 8, gm_src2j6ljeja7ilp, 471, 42);
        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t121) + 8));
        gm_b16_8 t123 = t122;
        gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
        gm_t08olq97rscn3 t125;
        gm_f_unsupported_531q51r((void *)&t125, (void *)NULL, cx, NULL, t112, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t126;
        GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
        ((uint8_t *)&t126)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t126 + 8), t125);
        gm_t5o0tpv5jka2l t127 = t126;
        uint8_t t128 = ((((uint8_t *)&t127))[0] != 0);
        F.ret = t126;
        goto gm_fret;
        goto me16;
mn110:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 452, 5);
me16:;
    }
    {
        void * t129 = gm_l0;
        void * t130 = GM_LD(void *, (((uint8_t *)t129) + 0));
        void * t131 = GM_LD(void *, ((((uint8_t *)t130) + 8) + 0));
        void * t132 = (void *)(((uint8_t *)t131) + 8);
        gm_l14 = t132;
    }
    {
        void * t133 = gm_l0;
        void * t134 = gm_l14;
        void * t135 = gm_l14;
        gm_t88rcucg1b7uk t136 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t137 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t134, ((uint8_t)0), t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t138 = gm_l6;
        gm_b16_8 t139 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t133, t137, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t139;
    }
    {
        void * t140 = gm_l0;
        void * t141 = gm_l0;
        void * t142 = GM_LD(void *, (((uint8_t *)t141) + 0));
        void * t143 = (void *)(((uint8_t *)t142) + 8);
        gm_t88rcucg1b7uk t144 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t145 = gm_l7;
        gm_tnpifv20irnph t146;
        GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t145);
        gm_tabb710d9mq2e t147;
        gm_f_format_6df13cd((void *)&t147, (void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t148 = t147;
        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 8, gm_src2j6ljeja7ilp, 475, 38);
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 8));
        gm_b16_8 t150 = t149;
        gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
        gm_b16_8 t152 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t140, t144, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t152;
    }
    {
        gm_b16_8 t153;
        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t159) { case ((uint8_t)0ULL): goto ma157; case ((uint8_t)1ULL): goto ma157; case ((uint8_t)2ULL): goto ma157; case ((uint8_t)3ULL): goto ma157; case ((uint8_t)4ULL): goto ma157; case ((uint8_t)5ULL): goto ma157; case ((uint8_t)6ULL): goto ma157; case ((uint8_t)7ULL): goto ma157; case ((uint8_t)8ULL): goto ma157; case ((uint8_t)9ULL): goto ma157; case ((uint8_t)10ULL): goto ma157; case ((uint8_t)11ULL): goto ma157; case ((uint8_t)12ULL): goto ma157; case ((uint8_t)13ULL): goto ma157; case ((uint8_t)14ULL): goto ma157; case ((uint8_t)15ULL): goto ma157; case ((uint8_t)16ULL): goto ma157; case ((uint8_t)17ULL): goto ma157; case ((uint8_t)18ULL): goto ma157; case ((uint8_t)19ULL): goto ma157; case ((uint8_t)20ULL): goto ma157; case ((uint8_t)21ULL): goto ma157; case ((uint8_t)22ULL): goto ma157; case ((uint8_t)23ULL): goto ma157; case ((uint8_t)24ULL): goto ma157; case ((uint8_t)25ULL): goto ma157; case ((uint8_t)26ULL): goto ma157; case ((uint8_t)27ULL): goto ma157; case ((uint8_t)28ULL): goto ma157; case ((uint8_t)29ULL): goto ma157; case ((uint8_t)30ULL): goto ma157; case ((uint8_t)31ULL): goto ma157; case ((uint8_t)32ULL): goto ma157; case ((uint8_t)33ULL): goto ma157; case ((uint8_t)34ULL): goto ma157; case ((uint8_t)35ULL): goto ma157; case ((uint8_t)36ULL): goto ma157; case ((uint8_t)37ULL): goto ma157; case ((uint8_t)38ULL): goto ma155; case ((uint8_t)39ULL): goto ma156; case ((uint8_t)40ULL): goto ma157; case ((uint8_t)41ULL): goto ma157; case ((uint8_t)42ULL): goto ma157; case ((uint8_t)43ULL): goto ma157; case ((uint8_t)44ULL): goto ma157; case ((uint8_t)45ULL): goto ma157; case ((uint8_t)46ULL): goto ma157; case ((uint8_t)47ULL): goto ma157; case ((uint8_t)48ULL): goto ma157; case ((uint8_t)49ULL): goto ma157; case ((uint8_t)50ULL): goto ma157; case ((uint8_t)51ULL): goto ma157; case ((uint8_t)52ULL): goto ma157; case ((uint8_t)53ULL): goto ma157; case ((uint8_t)54ULL): goto ma157; case ((uint8_t)55ULL): goto ma157; case ((uint8_t)56ULL): goto ma157; case ((uint8_t)57ULL): goto ma157; case ((uint8_t)58ULL): goto ma157; case ((uint8_t)59ULL): goto ma157; case ((uint8_t)60ULL): goto ma157; case ((uint8_t)61ULL): goto ma157; case ((uint8_t)62ULL): goto ma157; case ((uint8_t)63ULL): goto ma157; case ((uint8_t)64ULL): goto ma157; case ((uint8_t)65ULL): goto ma157; case ((uint8_t)66ULL): goto ma157; case ((uint8_t)67ULL): goto ma157; case ((uint8_t)68ULL): goto ma157; case ((uint8_t)69ULL): goto ma157; case ((uint8_t)70ULL): goto ma157; case ((uint8_t)71ULL): goto ma157; case ((uint8_t)72ULL): goto ma157; case ((uint8_t)73ULL): goto ma157; case ((uint8_t)74ULL): goto ma157; case ((uint8_t)75ULL): goto ma157; case ((uint8_t)76ULL): goto ma157; case ((uint8_t)77ULL): goto ma157; case ((uint8_t)78ULL): goto ma157; case ((uint8_t)79ULL): goto ma157; case ((uint8_t)80ULL): goto ma157; case ((uint8_t)81ULL): goto ma157; default: goto ma157; }
ma155:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t161 != ((uint8_t)38ULL)) goto mn160;
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_s0fg7tcl0p4ij));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(12));
        t153 = t162;
        goto me154;
mn160:;
ma156:;
        uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t164 != ((uint8_t)39ULL)) goto mn163;
        gm_b16_8 t165 = gm_l7;
        gm_tnpifv20irnph t166;
        GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t165);
        gm_tabb710d9mq2e t167;
        gm_f_format_uj77khu((void *)&t167, (void *)NULL, cx, NULL, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t168 = t167;
        if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 8, gm_src2j6ljeja7ilp, 478, 18);
        gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
        gm_b16_8 t170 = t169;
        gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
        t153 = t171;
        goto me154;
mn163:;
ma157:;
        {
            void * t174 = gm_l0;
            uint64_t t175 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_src2j6ljeja7ilp, 480, 31);
            void * t177 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t175 * 224);
            gm_t5o0tpv5jka2l t178;
            gm_f_expr_porbc9t((void *)&t178, (void *)NULL, cx, NULL, t174, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t179 = t178;
            if (((((uint8_t *)&t179))[0] != 0)) {
                gm_t08olq97rscn3 t180 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t179) + 8));
                gm_t5o0tpv5jka2l t181;
                GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                ((uint8_t *)&t181)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t181 + 8), t180);
                F.ret = t181;
                goto gm_fret;
            }
            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t179) + 8));
            gm_l15 = t182;
        }
        void * t183 = gm_l0;
        uint64_t t184 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t184 >= (uintptr_t)(t185)) gm_trap_index(t184, t185, gm_src2j6ljeja7ilp, 481, 22);
        gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t184 * 224) + 0));
        gm_b16_8 t187 = gm_l15;
        gm_b16_8 t188 = gm_f_bound_n4p0vpb((void *)NULL, cx, NULL, t183, t186, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t189 = t188;
        t153 = t189;
        goto me154;
mn172:;
ma158:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 476, 17);
me154:;
        gm_l16 = t153;
    }
    {
        void * t190 = gm_l0;
        void * t191 = gm_l0;
        void * t192 = GM_LD(void *, (((uint8_t *)t191) + 0));
        void * t193 = (void *)(((uint8_t *)t192) + 8);
        gm_t88rcucg1b7uk t194 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t193);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t195 = gm_l16;
        gm_b16_8 t196 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t190, t194, t195);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t196;
    }
    {
        void * t198 = gm_l0;
        void * t199 = GM_LD(void *, (((uint8_t *)t198) + 0));
        void * t200 = (void *)(((uint8_t *)t199) + 8);
        gm_t88rcucg1b7uk t201 = gm_l4;
        gm_t53at80pfdus3 t202;
        gm_f_kind_ghvav22((void *)&t202, t200, GM_CONV(gm_csguankko4lmhd, t201));
        gm_t53at80pfdus3 t203 = t202;
        uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&t203) + 0));
        if (t204 != ((uint8_t)13ULL)) goto ie197;
        gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t203) + 8));
        gm_l17 = t205;
        {
            gm_t88rcucg1b7uk t207 = gm_l17;
            gm_l18 = t207;
        }
        {
            uint8_t t209 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
            if (!t209) goto ie208;
            {
                void * t212 = gm_l0;
                void * t213 = GM_LD(void *, (((uint8_t *)t212) + 0));
                void * t214 = (void *)(((uint8_t *)t213) + 8);
                gm_t88rcucg1b7uk t215 = gm_l17;
                gm_t53at80pfdus3 t216;
                gm_f_kind_ghvav22((void *)&t216, t214, GM_CONV(gm_csguankko4lmhd, t215));
                gm_t53at80pfdus3 t217 = t216;
                uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&t217) + 0));
                if (t218 != ((uint8_t)8ULL)) goto ie211;
                gm_t88rcucg1b7uk t219 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t217) + 8) + 4));
                gm_l19 = t219;
                {
                    gm_t88rcucg1b7uk t221 = gm_l19;
                    gm_l18 = t221;
                }
                goto ix211;
ie211:;
ix211:;
            }
            goto ix208;
ie208:;
ix208:;
        }
        {
            void * t222 = gm_l0;
            void * t223 = GM_LD(void *, (((uint8_t *)t222) + 0));
            void * t224 = (void *)(((uint8_t *)t223) + 8);
            gm_t88rcucg1b7uk t225 = gm_l18;
            uint64_t t226 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t224, t225);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t226;
        }
        {
            void * t227 = gm_l0;
            gm_t88rcucg1b7uk t228 = gm_l4;
            gm_b16_8 t229 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t227, t228);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t229;
        }
        {
            void * t230 = gm_l0;
            gm_t6j6smj7qoctu t231;
            gm_b16_8 t232 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t231 + 0, t232);
            gm_b16_8 t233 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t231 + 16, t233);
            gm_b0_1 t234 = gm_f_linef_lm0b3ih((void *)NULL, cx, NULL, t230, &t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tftksr4op114e t235;
            gm_b16_8 t236 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t235 + 0, t236);
            gm_b16_8 t237 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t235 + 16, t237);
            uint64_t t238 = gm_l20;
            GM_ST(uint64_t, (uint8_t *)&t235 + 32, t238);
            gm_tabb710d9mq2e t239;
            gm_f_format_8sn6h96((void *)&t239, (void *)NULL, cx, NULL, &t235);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t240 = t239;
            if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 8, gm_src2j6ljeja7ilp, 493, 13);
            gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)&t240) + 8));
            gm_l22 = t241;
        }
        {
            gm_b16_8 t242;
            uint8_t t244 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
            if (!t244) goto ie243;
            gm_b16_8 t246 = gm_l22;
            gm_tnpifv20irnph t247;
            GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t246);
            gm_tabb710d9mq2e t248;
            gm_f_format_2g08abe((void *)&t248, (void *)NULL, cx, NULL, t247);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t249 = t248;
            if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 8, gm_src2j6ljeja7ilp, 494, 36);
            gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&t249) + 8));
            gm_b16_8 t251 = t250;
            gm_b16_8 t252 = t251;
            gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
            t242 = t253;
            goto ix243;
ie243:;
            void * t255 = gm_l0;
            gm_t88rcucg1b7uk t256 = gm_l17;
            gm_b16_8 t257 = gm_l22;
            gm_b16_8 t258 = t257;
            gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
            gm_b16_8 t260 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t255, t256, t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t261 = t260;
            t242 = t261;
ix243:;
            gm_l23 = t242;
        }
        {
            void * t262 = gm_l0;
            gm_t88rcucg1b7uk t263 = gm_l4;
            gm_b16_8 t264 = gm_l23;
            gm_b16_8 t265 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t262, t263, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t265;
        }
        {
            void * t266 = gm_l0;
            gm_t6j6smj7qoctu t267;
            gm_b16_8 t268 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t267 + 0, t268);
            gm_b16_8 t269 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t267 + 16, t269);
            gm_b0_1 t270 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t266, &t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t271 = gm_l0;
            gm_b16_8 t272;
            GM_ST(void *, (uint8_t *)&t272, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(8));
            gm_b0_1 t273 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t271, t272);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t274 = gm_l0;
            gm_t88rcucg1b7uk t275 = gm_l4;
            gm_b16_8 t276 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t274, t275);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t276;
        }
        {
            void * t277 = gm_l0;
            gm_t6j6smj7qoctu t278;
            gm_b16_8 t279 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t278 + 0, t279);
            gm_b16_8 t280 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t278 + 16, t280);
            gm_b0_1 t281 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t277, &t278);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t282 = gm_l0;
            gm_b16_8 t283;
            GM_ST(void *, (uint8_t *)&t283, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(1));
            gm_b0_1 t284 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t282, t283);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t285 = gm_l21;
            gm_t5o0tpv5jka2l t286;
            GM_ST(uint8_t, (uint8_t *)&t286 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t286 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t286 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t286 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t286 + 32, 0);
            ((uint8_t *)&t286)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t286 + 8), t285);
            gm_t5o0tpv5jka2l t287 = t286;
            uint8_t t288 = ((((uint8_t *)&t287))[0] != 0);
            F.ret = t286;
            goto gm_fret;
        }
        goto ix197;
ie197:;
        {
            void * t290 = gm_l1;
            gm_tl874t2atnlh3 t291 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t290) + 208));
            gm_b16_8 t292;
            GM_ST(void *, (uint8_t *)&t292, (void *)(gm_sbb1ri5er9djq));
            GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(3));
            gm_t08olq97rscn3 t293;
            gm_f_unsupported_531q51r((void *)&t293, (void *)NULL, cx, NULL, t291, t292);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t294;
            GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
            ((uint8_t *)&t294)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t294 + 8), t293);
            gm_t5o0tpv5jka2l t295 = t294;
            uint8_t t296 = ((((uint8_t *)&t295))[0] != 0);
            F.ret = t294;
            goto gm_fret;
        }
ix197:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_asm_text_fmvci6l(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_asm_text_fmvci6l F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)5ULL)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            gm_b16_8 t7 = gm_l1;
            gm_b16_8 t8 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 5, 28);
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
__attribute__((aligned(4))) void gm_f_assert_m8ussus(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_assert_m8ussus F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* loc */
    gm_b16_8 gm_l4; /* c */
    gm_b16_8 gm_l5; /* m */
    gm_b16_8 gm_l6; /* a */
    gm_b16_8 gm_l7; /* p */
    gm_b16_8 gm_l8; /* l */
    gm_b16_8 gm_l9; /* prefix */
    GM_STACK_CHECK(&F, 1304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_b16_8 t4 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 1136) + 0));
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie5;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t13 + 32, 0);
            ((uint8_t *)&t13)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t12);
            gm_t5o0tpv5jka2l t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t16 = gm_l0;
        uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_src2j6ljeja7ilp, 201, 23);
        void * t19 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t17 * 224);
        gm_t5o0tpv5jka2l t20;
        gm_f_expr_porbc9t((void *)&t20, (void *)NULL, cx, NULL, t16, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) {
            gm_t08olq97rscn3 t22 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t21) + 8));
            gm_t5o0tpv5jka2l t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t23 + 8), t22);
            F.ret = t23;
            goto gm_fret;
        }
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_l4 = t24;
    }
    {
        void * t25 = gm_l0;
        gm_b16_8 t26 = gm_l4;
        gm_tnpifv20irnph t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
        gm_b0_1 t28 = gm_f_linef_o1esdvv((void *)NULL, cx, NULL, t25, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l0;
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 224));
        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
        if (o33) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 203, 5);
        uint64_t t34 = r32;
        GM_ST(uint64_t, (((uint8_t *)t29) + 224), t34);
    }
    {
        void * t35 = gm_l0;
        uint64_t t36 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t36 >= (uintptr_t)(t37)) gm_trap_index(t36, t37, gm_src2j6ljeja7ilp, 204, 23);
        void * t38 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t36 * 224);
        gm_t5o0tpv5jka2l t39;
        gm_f_expr_porbc9t((void *)&t39, (void *)NULL, cx, NULL, t35, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t40 = t39;
        if (((((uint8_t *)&t40))[0] != 0)) {
            gm_t08olq97rscn3 t41 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t40) + 8));
            gm_t5o0tpv5jka2l t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
            ((uint8_t *)&t42)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t42 + 8), t41);
            F.ret = t42;
            goto gm_fret;
        }
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
        gm_l5 = t43;
    }
    {
        void * t44 = gm_l0;
        uint64_t t45 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_src2j6ljeja7ilp, 205, 21);
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t45 * 224) + 0));
        gm_b16_8 t48 = gm_l5;
        gm_b16_8 t49 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t44, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t49;
    }
    {
        gm_b16_8 t50 = gm_l6;
        gm_b16_8 t51 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t51;
    }
    {
        void * t52 = gm_l0;
        gm_b16_8 t53 = gm_l6;
        gm_b16_8 t54 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t54;
    }
    {
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_spi8n2atkg603));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(16));
        gm_l9 = t55;
    }
    {
        void * t56 = gm_l0;
        gm_t6q08imqe2768 t57;
        gm_b16_8 t58 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t58);
        gm_b16_8 t59 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t59);
        uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        GM_ST(uint64_t, (uint8_t *)&t57 + 32, t60);
        gm_b16_8 t61 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 40, t61);
        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        GM_ST(uint64_t, (uint8_t *)&t57 + 56, t62);
        gm_b16_8 t63 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 64, t63);
        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)2ULL), &r65);
        if (o66) gm_trap("integer overflow in `+`", gm_src2j6ljeja7ilp, 209, 298);
        uint64_t t67 = r65;
        GM_ST(uint64_t, (uint8_t *)&t57 + 80, t67);
        gm_b16_8 t68 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 88, t68);
        gm_b0_1 t69 = gm_f_linef_098d9va((void *)NULL, cx, NULL, t56, &t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t70 = gm_l0;
        void * t71 = gm_l0;
        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 224));
        uint64_t r73; bool o74 = __builtin_sub_overflow(t72, ((uint64_t)1ULL), &r73);
        if (o74) gm_trap("integer overflow in `-`", gm_src2j6ljeja7ilp, 210, 5);
        uint64_t t75 = r73;
        GM_ST(uint64_t, (((uint8_t *)t70) + 224), t75);
    }
    {
        void * t76 = gm_l0;
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
        gm_b0_1 t78 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t79 = gm_l0;
        gm_b16_8 t80 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t81 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t81 + 32, 0);
        ((uint8_t *)&t81)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t81 + 8), t80);
        gm_t5o0tpv5jka2l t82 = t81;
        uint8_t t83 = ((((uint8_t *)&t82))[0] != 0);
        F.ret = t81;
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
__attribute__((aligned(4))) void gm_f_atomic_plj0t12(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_atomic_plj0t12 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* v0 */
    gm_t88rcucg1b7uk gm_l7; /* at */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* p */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    void * gm_l15; /* x */
    gm_b16_8 gm_l16; /* a */
    gm_b16_8 gm_l17; /* exp */
    gm_b16_8 gm_l18; /* ok */
    gm_b16_8 gm_l19; /* res */
    gm_b16_8 gm_l20; /* none */
    gm_b16_8 gm_l21; /* some */
    gm_t9qkh0rsooc8d gm_l22; /* rmw */
    gm_th2lqbo77pvsc gm_l23; /* f */
    gm_b16_8 gm_l24; /* fname */
    gm_b16_8 gm_l25; /* cmp */
    gm_b16_8 gm_l26; /* old */
    gm_b16_8 gm_l27; /* v */
    GM_STACK_CHECK(&F, 4724u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 366, 11);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t4 * 16));
        gm_l6 = t6;
    }
    {
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_src2j6ljeja7ilp, 367, 11);
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t7 * 224) + 0));
        gm_l7 = t9;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_t88rcucg1b7uk t14 = gm_l7;
        gm_t53at80pfdus3 t15;
        gm_f_kind_ghvav22((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
        gm_t53at80pfdus3 t16 = t15;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
        if (t17 != ((uint8_t)8ULL)) goto ie10;
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t16) + 8) + 4));
        gm_l8 = t18;
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            void * t22 = (void *)(((uint8_t *)t21) + 8);
            gm_t88rcucg1b7uk t23 = gm_l8;
            gm_b16_8 t24 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t24;
        }
        {
            gm_t6j6smj7qoctu t25;
            gm_b16_8 t26 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
            gm_b16_8 t27 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
            gm_tabb710d9mq2e t28;
            gm_f_format_8m4h4ip((void *)&t28, (void *)NULL, cx, NULL, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src2j6ljeja7ilp, 370, 10);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
            gm_l10 = t30;
        }
        {
            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l13 = ((uint64_t)1ULL);
            }
            {
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l12 = t32;
            }
            {
                for (;;) {
                    uint64_t t34 = gm_l13;
                    uint64_t t35 = gm_l12;
                    uint8_t t36 = (t34 < t35);
                    if (!t36) goto lb33;
                    {
                        uint64_t t39 = gm_l13;
                        uint64_t t40 = t39;
                        uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)&t40));
                        gm_l14 = t43;
                        {
                            uint64_t t45 = gm_l14;
                            uint64_t t46 = (uintptr_t)t45;
                            uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_src2j6ljeja7ilp, 373, 15);
                            void * t48 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t46 * 224);
                            gm_l15 = t48;
                        }
                        {
                            void * t50 = gm_l0;
                            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
                            void * t52 = (void *)(((uint8_t *)t51) + 8);
                            void * t53 = gm_l15;
                            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t53) + 0));
                            gm_t53at80pfdus3 t55;
                            gm_f_kind_ghvav22((void *)&t55, t52, GM_CONV(gm_csguankko4lmhd, t54));
                            gm_t53at80pfdus3 t56 = t55;
                            uint8_t t57;
                            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
                            if (t60 != ((uint8_t)17ULL)) goto mn59;
                            t57 = ((uint8_t)0);
                            goto me58;
mn59:;
                            t57 = ((uint8_t)1);
                            goto me58;
mn61:;
                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 374, 12);
me58:;
                            if (!t57) goto ie49;
                            {
                                goto lc33;
                            }
                            goto ix49;
ie49:;
ix49:;
                        }
                        {
                            void * t63 = gm_l0;
                            void * t64 = gm_l15;
                            gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 0));
                            uint64_t t66 = gm_l14;
                            uint64_t t67 = (uintptr_t)t66;
                            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_src2j6ljeja7ilp, 375, 31);
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t67 * 16));
                            gm_b16_8 t70 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t63, t65, t69);
                            if (gm_unw.kind) {
                                goto gm_ll37;
                            }
                            gm_l16 = t70;
                        }
                        {
                            void * t71 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t72 = gm_l16;
                            gm_tnpifv20irnph t73;
                            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                            gm_tabb710d9mq2e t74;
                            gm_f_format_8d9jpvv((void *)&t74, (void *)NULL, cx, NULL, t73);
                            if (gm_unw.kind) {
                                goto gm_ll37;
                            }
                            gm_tabb710d9mq2e t75 = t74;
                            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_src2j6ljeja7ilp, 376, 19);
                            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
                            gm_b16_8 t77 = t76;
                            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                            gm_tftb1odc739t5 t79 = gm_f_push_3ef746b(t71, t78);
                            gm_tftb1odc739t5 t80 = t79;
                            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_src2j6ljeja7ilp, 376, 9);
                            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                        }
                        goto me41;
mn42:;
                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 372, 5);
me41:;
                    }
lc33:;
                    {
                        uint64_t t83 = gm_l13;
                        uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                        uint64_t t86 = r84;
                        gm_l13 = t86;
                    }
                }
gm_ll37:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb33; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc33; }
                }
                goto gm_lad;
lb33:;
            }
        }
        {
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t97) { case ((uint8_t)0ULL): goto ma95; case ((uint8_t)1ULL): goto ma95; case ((uint8_t)2ULL): goto ma95; case ((uint8_t)3ULL): goto ma95; case ((uint8_t)4ULL): goto ma95; case ((uint8_t)5ULL): goto ma95; case ((uint8_t)6ULL): goto ma95; case ((uint8_t)7ULL): goto ma95; case ((uint8_t)8ULL): goto ma95; case ((uint8_t)9ULL): goto ma95; case ((uint8_t)10ULL): goto ma95; case ((uint8_t)11ULL): goto ma95; case ((uint8_t)12ULL): goto ma95; case ((uint8_t)13ULL): goto ma95; case ((uint8_t)14ULL): goto ma95; case ((uint8_t)15ULL): goto ma95; case ((uint8_t)16ULL): goto ma95; case ((uint8_t)17ULL): goto ma95; case ((uint8_t)18ULL): goto ma95; case ((uint8_t)19ULL): goto ma95; case ((uint8_t)20ULL): goto ma95; case ((uint8_t)21ULL): goto ma95; case ((uint8_t)22ULL): goto ma95; case ((uint8_t)23ULL): goto ma95; case ((uint8_t)24ULL): goto ma95; case ((uint8_t)25ULL): goto ma95; case ((uint8_t)26ULL): goto ma95; case ((uint8_t)27ULL): goto ma95; case ((uint8_t)28ULL): goto ma95; case ((uint8_t)29ULL): goto ma95; case ((uint8_t)30ULL): goto ma95; case ((uint8_t)31ULL): goto ma95; case ((uint8_t)32ULL): goto ma95; case ((uint8_t)33ULL): goto ma95; case ((uint8_t)34ULL): goto ma95; case ((uint8_t)35ULL): goto ma95; case ((uint8_t)36ULL): goto ma95; case ((uint8_t)37ULL): goto ma95; case ((uint8_t)38ULL): goto ma95; case ((uint8_t)39ULL): goto ma95; case ((uint8_t)40ULL): goto ma95; case ((uint8_t)41ULL): goto ma95; case ((uint8_t)42ULL): goto ma95; case ((uint8_t)43ULL): goto ma95; case ((uint8_t)44ULL): goto ma95; case ((uint8_t)45ULL): goto ma95; case ((uint8_t)46ULL): goto ma95; case ((uint8_t)47ULL): goto ma95; case ((uint8_t)48ULL): goto ma95; case ((uint8_t)49ULL): goto ma95; case ((uint8_t)50ULL): goto ma95; case ((uint8_t)51ULL): goto ma95; case ((uint8_t)52ULL): goto ma95; case ((uint8_t)53ULL): goto ma95; case ((uint8_t)54ULL): goto ma95; case ((uint8_t)55ULL): goto ma95; case ((uint8_t)56ULL): goto ma95; case ((uint8_t)57ULL): goto ma95; case ((uint8_t)58ULL): goto ma95; case ((uint8_t)59ULL): goto ma95; case ((uint8_t)60ULL): goto ma95; case ((uint8_t)61ULL): goto ma95; case ((uint8_t)62ULL): goto ma95; case ((uint8_t)63ULL): goto ma95; case ((uint8_t)64ULL): goto ma90; case ((uint8_t)65ULL): goto ma91; case ((uint8_t)66ULL): goto ma92; case ((uint8_t)67ULL): goto ma93; case ((uint8_t)68ULL): goto ma94; case ((uint8_t)69ULL): goto ma95; case ((uint8_t)70ULL): goto ma88; case ((uint8_t)71ULL): goto ma89; case ((uint8_t)72ULL): goto ma95; case ((uint8_t)73ULL): goto ma95; case ((uint8_t)74ULL): goto ma95; case ((uint8_t)75ULL): goto ma95; case ((uint8_t)76ULL): goto ma95; case ((uint8_t)77ULL): goto ma95; case ((uint8_t)78ULL): goto ma95; case ((uint8_t)79ULL): goto ma95; case ((uint8_t)80ULL): goto ma95; case ((uint8_t)81ULL): goto ma95; default: goto ma95; }
ma88:;
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t99 != ((uint8_t)70ULL)) goto mn98;
            void * t100 = gm_l0;
            gm_t88rcucg1b7uk t101 = gm_l5;
            gm_t6j6smj7qoctu t102;
            gm_b16_8 t103 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t103);
            gm_b16_8 t104 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t102 + 16, t104);
            gm_tabb710d9mq2e t105;
            gm_f_format_dv5f37i((void *)&t105, (void *)NULL, cx, NULL, &t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t106 = t105;
            if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_src2j6ljeja7ilp, 379, 43);
            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
            gm_b16_8 t108 = t107;
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            gm_b16_8 t110 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t100, t101, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t111;
            GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 32, 0);
            ((uint8_t *)&t111)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
            gm_t5o0tpv5jka2l t112 = t111;
            uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
            F.ret = t111;
            goto gm_fret;
            goto me87;
mn98:;
ma89:;
            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t115 != ((uint8_t)71ULL)) goto mn114;
            {
                void * t117 = gm_l0;
                gm_tfeab279c16rr t118;
                gm_b16_8 t119 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t118 + 0, t119);
                gm_b16_8 t120 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t118 + 16, t120);
                uint64_t t121 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_src2j6ljeja7ilp, 381, 57);
                gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t121 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t118 + 32, t123);
                gm_b0_1 t124 = gm_f_linef_linqala((void *)NULL, cx, NULL, t117, &t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t125 = gm_l0;
                gm_b16_8 t126 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t125);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t127;
                GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t127 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t127 + 32, 0);
                ((uint8_t *)&t127)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t127 + 8), t126);
                gm_t5o0tpv5jka2l t128 = t127;
                uint8_t t129 = ((((uint8_t *)&t128))[0] != 0);
                F.ret = t127;
                goto gm_fret;
            }
            goto me87;
mn114:;
ma90:;
            uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t131 != ((uint8_t)64ULL)) goto mn130;
            void * t132 = gm_l0;
            gm_t88rcucg1b7uk t133 = gm_l5;
            gm_t6j6smj7qoctu t134;
            gm_b16_8 t135 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t134 + 0, t135);
            uint64_t t136 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t137 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t136 >= (uintptr_t)(t137)) gm_trap_index(t136, t137, gm_src2j6ljeja7ilp, 384, 82);
            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t136 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t134 + 16, t138);
            gm_tabb710d9mq2e t139;
            gm_f_format_6cn78ik((void *)&t139, (void *)NULL, cx, NULL, &t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t140 = t139;
            if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 8, gm_src2j6ljeja7ilp, 384, 41);
            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
            gm_b16_8 t142 = t141;
            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
            gm_b16_8 t144 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t132, t133, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t145;
            GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t145 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t145 + 32, 0);
            ((uint8_t *)&t145)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t145 + 8), t144);
            gm_t5o0tpv5jka2l t146 = t145;
            uint8_t t147 = ((((uint8_t *)&t146))[0] != 0);
            F.ret = t145;
            goto gm_fret;
            goto me87;
mn130:;
ma91:;
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t149 != ((uint8_t)65ULL)) goto mn148;
            {
                void * t151 = gm_l0;
                gm_tfeab279c16rr t152;
                gm_b16_8 t153 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t152 + 0, t153);
                uint64_t t154 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t154 >= (uintptr_t)(t155)) gm_trap_index(t154, t155, gm_src2j6ljeja7ilp, 386, 58);
                gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t154 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t152 + 16, t156);
                uint64_t t157 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t158 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t157 >= (uintptr_t)(t158)) gm_trap_index(t157, t158, gm_src2j6ljeja7ilp, 386, 67);
                gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t157 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t152 + 32, t159);
                gm_b0_1 t160 = gm_f_linef_6t3ttmd((void *)NULL, cx, NULL, t151, &t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t161 = gm_l0;
                gm_b16_8 t162 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t163;
                GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t163 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t163 + 32, 0);
                ((uint8_t *)&t163)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t163 + 8), t162);
                gm_t5o0tpv5jka2l t164 = t163;
                uint8_t t165 = ((((uint8_t *)&t164))[0] != 0);
                F.ret = t163;
                goto gm_fret;
            }
            goto me87;
mn148:;
ma92:;
            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t167 != ((uint8_t)66ULL)) goto mn166;
            void * t168 = gm_l0;
            gm_t88rcucg1b7uk t169 = gm_l5;
            gm_tfeab279c16rr t170;
            gm_b16_8 t171 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t170 + 0, t171);
            uint64_t t172 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_src2j6ljeja7ilp, 389, 90);
            gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t172 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t170 + 16, t174);
            uint64_t t175 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t176 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_src2j6ljeja7ilp, 389, 99);
            gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t175 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t170 + 32, t177);
            gm_tabb710d9mq2e t178;
            gm_f_format_25p33s1((void *)&t178, (void *)NULL, cx, NULL, &t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t179 = t178;
            if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 8, gm_src2j6ljeja7ilp, 389, 41);
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&t179) + 8));
            gm_b16_8 t181 = t180;
            gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
            gm_b16_8 t183 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t168, t169, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t184;
            GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t184 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t184 + 32, 0);
            ((uint8_t *)&t184)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t184 + 8), t183);
            gm_t5o0tpv5jka2l t185 = t184;
            uint8_t t186 = ((((uint8_t *)&t185))[0] != 0);
            F.ret = t184;
            goto gm_fret;
            goto me87;
mn166:;
ma93:;
            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t188 != ((uint8_t)67ULL)) goto mn187;
            {
                void * t190 = gm_l0;
                gm_t88rcucg1b7uk t191 = gm_l8;
                uint64_t t192 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_src2j6ljeja7ilp, 391, 33);
                gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t192 * 16));
                gm_b16_8 t195 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t190, t191, t194);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t195;
            }
            {
                void * t196 = gm_l0;
                void * t197 = gm_l0;
                void * t198 = GM_LD(void *, (((uint8_t *)t197) + 0));
                void * t199 = (void *)(((uint8_t *)t198) + 8);
                gm_t88rcucg1b7uk t200 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t199);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tud7oovfvnf43 t201;
                gm_b16_8 t202 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t201 + 0, t202);
                gm_b16_8 t203 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t201 + 16, t203);
                uint64_t t204 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t204 >= (uintptr_t)(t205)) gm_trap_index(t204, t205, gm_src2j6ljeja7ilp, 392, 119);
                gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t204 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t201 + 32, t206);
                uint64_t t207 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t208 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t207 >= (uintptr_t)(t208)) gm_trap_index(t207, t208, gm_src2j6ljeja7ilp, 392, 128);
                gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t207 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t201 + 48, t209);
                uint64_t t210 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t211 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t210 >= (uintptr_t)(t211)) gm_trap_index(t210, t211, gm_src2j6ljeja7ilp, 392, 143);
                gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t210 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t201 + 64, t212);
                gm_tabb710d9mq2e t213;
                gm_f_format_5rrmec0((void *)&t213, (void *)NULL, cx, NULL, &t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t214 = t213;
                if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 8, gm_src2j6ljeja7ilp, 392, 45);
                gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 8));
                gm_b16_8 t216 = t215;
                gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
                gm_b16_8 t218 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t196, t200, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t218;
            }
            {
                void * t219 = gm_l0;
                gm_t88rcucg1b7uk t220 = gm_l5;
                gm_b16_8 t221 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t219, t220);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t221;
            }
            {
                void * t222 = gm_l0;
                gm_t88rcucg1b7uk t223 = gm_l5;
                gm_b16_8 t224 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t222, t223);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t224;
            }
            {
                void * t225 = gm_l0;
                gm_t88rcucg1b7uk t226 = gm_l5;
                gm_b16_8 t227 = gm_l17;
                gm_b16_8 t228 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t225, t226, t227);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t228;
            }
            {
                void * t229 = gm_l0;
                gm_t0uuhaljsf410 t230;
                gm_b16_8 t231 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t230 + 0, t231);
                gm_b16_8 t232 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t230 + 16, t232);
                gm_b16_8 t233 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t230 + 32, t233);
                gm_b16_8 t234 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t230 + 48, t234);
                gm_b0_1 t235 = gm_f_linef_6h7r936((void *)NULL, cx, NULL, t229, &t230);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t236 = gm_l19;
                gm_t5o0tpv5jka2l t237;
                GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t237 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t237 + 32, 0);
                ((uint8_t *)&t237)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t237 + 8), t236);
                gm_t5o0tpv5jka2l t238 = t237;
                uint8_t t239 = ((((uint8_t *)&t238))[0] != 0);
                F.ret = t237;
                goto gm_fret;
            }
            goto me87;
mn187:;
ma94:;
            uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t241 != ((uint8_t)68ULL)) goto mn240;
            gm_t9qkh0rsooc8d t242 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
            gm_l22 = t242;
            {
                gm_th2lqbo77pvsc t244;
                uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                switch (t253) { case ((uint8_t)0ULL): goto ma246; case ((uint8_t)1ULL): goto ma247; case ((uint8_t)2ULL): goto ma248; case ((uint8_t)3ULL): goto ma249; case ((uint8_t)4ULL): goto ma250; case ((uint8_t)5ULL): goto ma251; case ((uint8_t)6ULL): goto ma251; default: goto ma252; }
ma246:;
                uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t255 != ((uint8_t)0ULL)) goto mn254;
                gm_b16_8 t256;
                GM_ST(void *, (uint8_t *)&t256, (void *)(gm_si0mnqg6j1eu8));
                GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t257;
                GM_ST(uint8_t, (uint8_t *)&t257 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t257 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t257 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t257), t256);
                ((uint8_t *)&t257)[16] = 1;
                t244 = t257;
                goto me245;
mn254:;
ma247:;
                uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t259 != ((uint8_t)1ULL)) goto mn258;
                gm_b16_8 t260;
                GM_ST(void *, (uint8_t *)&t260, (void *)(gm_sdeu8nuc597e8));
                GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t261;
                GM_ST(uint8_t, (uint8_t *)&t261 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t261 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t261 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t261), t260);
                ((uint8_t *)&t261)[16] = 1;
                t244 = t261;
                goto me245;
mn258:;
ma248:;
                uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t263 != ((uint8_t)2ULL)) goto mn262;
                gm_b16_8 t264;
                GM_ST(void *, (uint8_t *)&t264, (void *)(gm_s42h828lb45uf));
                GM_ST(uintptr_t, (uint8_t *)&t264 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t265;
                GM_ST(uint8_t, (uint8_t *)&t265 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t265 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t265 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t265), t264);
                ((uint8_t *)&t265)[16] = 1;
                t244 = t265;
                goto me245;
mn262:;
ma249:;
                uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t267 != ((uint8_t)3ULL)) goto mn266;
                gm_b16_8 t268;
                GM_ST(void *, (uint8_t *)&t268, (void *)(gm_sm0f8bs4gd2ma));
                GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(17));
                gm_th2lqbo77pvsc t269;
                GM_ST(uint8_t, (uint8_t *)&t269 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t269 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t269 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t269), t268);
                ((uint8_t *)&t269)[16] = 1;
                t244 = t269;
                goto me245;
mn266:;
ma250:;
                uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t271 != ((uint8_t)4ULL)) goto mn270;
                gm_b16_8 t272;
                GM_ST(void *, (uint8_t *)&t272, (void *)(gm_sg3ba4vpic0jb));
                GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(18));
                gm_th2lqbo77pvsc t273;
                GM_ST(uint8_t, (uint8_t *)&t273 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t273 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t273 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t273), t272);
                ((uint8_t *)&t273)[16] = 1;
                t244 = t273;
                goto me245;
mn270:;
ma251:;
                uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t277 != ((uint8_t)5ULL)) goto pa276;
                goto po275;
pa276:;
                uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                if (t279 != ((uint8_t)6ULL)) goto pa278;
                goto po275;
pa278:;
                goto mn274;
po275:;
                gm_th2lqbo77pvsc t280;
                memset(&t280, 0, sizeof t280);
                t244 = t280;
                goto me245;
mn274:;
ma252:;
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 400, 26);
me245:;
                gm_l23 = t244;
            }
            {
                if (!((((uint8_t *)&gm_l23))[16] != 0)) goto ie281;
                gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                gm_l24 = t282;
                {
                    void * t284 = gm_l0;
                    gm_t88rcucg1b7uk t285 = gm_l5;
                    gm_t0uuhaljsf410 t286;
                    gm_b16_8 t287 = gm_l24;
                    GM_ST(gm_b16_8, (uint8_t *)&t286 + 0, t287);
                    gm_b16_8 t288 = gm_l10;
                    GM_ST(gm_b16_8, (uint8_t *)&t286 + 16, t288);
                    uint64_t t289 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t290 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t289 >= (uintptr_t)(t290)) gm_trap_index(t289, t290, gm_src2j6ljeja7ilp, 409, 73);
                    gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t289 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t286 + 32, t291);
                    uint64_t t292 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t293 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                    if (t292 >= (uintptr_t)(t293)) gm_trap_index(t292, t293, gm_src2j6ljeja7ilp, 409, 82);
                    gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t292 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t286 + 48, t294);
                    gm_tabb710d9mq2e t295;
                    gm_f_format_patlqq0((void *)&t295, (void *)NULL, cx, NULL, &t286);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t296 = t295;
                    if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 8, gm_src2j6ljeja7ilp, 409, 34);
                    gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&t296) + 8));
                    gm_b16_8 t298 = t297;
                    gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
                    gm_b16_8 t300 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t284, t285, t299);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t301;
                    GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t301 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t301 + 32, 0);
                    ((uint8_t *)&t301)[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&t301 + 8), t300);
                    gm_t5o0tpv5jka2l t302 = t301;
                    uint8_t t303 = ((((uint8_t *)&t302))[0] != 0);
                    F.ret = t301;
                    goto gm_fret;
                }
                goto ix281;
ie281:;
ix281:;
            }
            {
                gm_b16_8 t304;
                uint8_t t306 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l22) + 0) == ((uint8_t)5ULL)));
                if (!t306) goto ie305;
                gm_b16_8 t308;
                GM_ST(void *, (uint8_t *)&t308, (void *)(gm_sm5be4659ulci));
                GM_ST(uintptr_t, (uint8_t *)&t308 + 8, (uintptr_t)(1));
                gm_b16_8 t309 = t308;
                t304 = t309;
                goto ix305;
ie305:;
                gm_b16_8 t311;
                GM_ST(void *, (uint8_t *)&t311, (void *)(gm_s1f440st4mnkp));
                GM_ST(uintptr_t, (uint8_t *)&t311 + 8, (uintptr_t)(1));
                gm_b16_8 t312 = t311;
                t304 = t312;
ix305:;
                gm_l25 = t304;
            }
            {
                void * t313 = gm_l0;
                gm_t88rcucg1b7uk t314 = gm_l8;
                gm_b16_8 t315 = gm_l10;
                gm_tnpifv20irnph t316;
                GM_ST(gm_b16_8, ((uint8_t *)&t316 + 0), t315);
                gm_tabb710d9mq2e t317;
                gm_f_format_a1ch927((void *)&t317, (void *)NULL, cx, NULL, t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t318 = t317;
                if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 8, gm_src2j6ljeja7ilp, 412, 33);
                gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&t318) + 8));
                gm_b16_8 t320 = t319;
                gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&t320));
                gm_b16_8 t322 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t313, t314, t321);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t322;
            }
            {
                uint64_t t323 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t324 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t323 >= (uintptr_t)(t324)) gm_trap_index(t323, t324, gm_src2j6ljeja7ilp, 413, 18);
                gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t323 * 16));
                gm_l27 = t325;
            }
            {
                void * t326 = gm_l0;
                gm_tg1t0c5hkovn5 t327;
                gm_b16_8 t328 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 0, t328);
                gm_b16_8 t329 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 16, t329);
                gm_b16_8 t330 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 32, t330);
                gm_b16_8 t331 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 48, t331);
                gm_b16_8 t332 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 64, t332);
                gm_b16_8 t333 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 80, t333);
                gm_b16_8 t334 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 96, t334);
                uint64_t t335 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t336 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                if (t335 >= (uintptr_t)(t336)) gm_trap_index(t335, t336, gm_src2j6ljeja7ilp, 414, 150);
                gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t335 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 112, t337);
                gm_b0_1 t338 = gm_f_linef_n685kk5((void *)NULL, cx, NULL, t326, &t327);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t339 = gm_l26;
                gm_t5o0tpv5jka2l t340;
                GM_ST(uint8_t, (uint8_t *)&t340 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t340 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t340 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t340 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t340 + 32, 0);
                ((uint8_t *)&t340)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t340 + 8), t339);
                gm_t5o0tpv5jka2l t341 = t340;
                uint8_t t342 = ((((uint8_t *)&t341))[0] != 0);
                F.ret = t340;
                goto gm_fret;
            }
            goto me87;
mn240:;
ma95:;
            gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 417, 14);
            goto me87;
mn343:;
ma96:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 378, 5);
me87:;
        }
        goto ix10;
ie10:;
        {
            void * t345 = gm_l1;
            gm_tl874t2atnlh3 t346 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t345) + 208));
            gm_b16_8 t347;
            GM_ST(void *, (uint8_t *)&t347, (void *)(gm_sn8alsh9na9ne));
            GM_ST(uintptr_t, (uint8_t *)&t347 + 8, (uintptr_t)(11));
            gm_t08olq97rscn3 t348;
            gm_f_unsupported_531q51r((void *)&t348, (void *)NULL, cx, NULL, t346, t347);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t349;
            GM_ST(uint8_t, (uint8_t *)&t349 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t349 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t349 + 4, 0);
            ((uint8_t *)&t349)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t349 + 8), t348);
            gm_t5o0tpv5jka2l t350 = t349;
            uint8_t t351 = ((((uint8_t *)&t350))[0] != 0);
            F.ret = t349;
            goto gm_fret;
        }
ix10:;
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
__attribute__((aligned(4))) void gm_f_bit_count_77uni44(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_bit_count_77uni44 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* v0 */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* x0 */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* call */
    gm_b16_8 gm_l11; /* r */
    gm_tuglhjjmj2g58 gm_l12; /* ri */
    gm_b16_8 gm_l13; /* n */
    GM_STACK_CHECK(&F, 2212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = gm_l3;
        gm_tqcc6dmlne11b t6;
        gm_f_int_info_2tcdf1n((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqcc6dmlne11b t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 313, 11);
        gm_tuglhjjmj2g58 t8 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t7));
        gm_l6 = t8;
    }
    {
        gm_t6j6smj7qoctu t9;
        gm_tuglhjjmj2g58 t10 = gm_l6;
        gm_b16_8 t11 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t10));
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t11);
        gm_b16_8 t12 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 16, t12);
        gm_tabb710d9mq2e t13;
        gm_f_format_21vnq4a((void *)&t13, (void *)NULL, cx, NULL, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t14 = t13;
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_src2j6ljeja7ilp, 314, 11);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
        gm_l7 = t15;
    }
    {
        gm_b16_8 t16;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint8_t t19 = (t18 >= ((uint32_t)128ULL));
        if (!t19) goto ie17;
        gm_b16_8 t21 = gm_l7;
        gm_b16_8 t22 = t21;
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        t16 = t24;
        goto ix17;
ie17:;
        gm_tommf4j9upomc t26;
        memset((uint8_t *)&t26 + 20, 0, 4);
        gm_b16_8 t27 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
        uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t26 + 16, t28);
        gm_tabb710d9mq2e t29;
        gm_f_format_s2ml2tu((void *)&t29, (void *)NULL, cx, NULL, &t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src2j6ljeja7ilp, 316, 49);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
        gm_b16_8 t32 = t31;
        gm_b16_8 t33 = t32;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        t16 = t34;
ix17:;
        gm_l8 = t16;
    }
    {
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
        void * t37 = (void *)(((uint8_t *)t36) + 8);
        gm_t88rcucg1b7uk t38 = gm_l5;
        gm_b16_8 t39 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t39;
    }
    {
        gm_b16_8 t40;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t48) { case ((uint8_t)0ULL): goto ma46; case ((uint8_t)1ULL): goto ma46; case ((uint8_t)2ULL): goto ma46; case ((uint8_t)3ULL): goto ma46; case ((uint8_t)4ULL): goto ma46; case ((uint8_t)5ULL): goto ma46; case ((uint8_t)6ULL): goto ma46; case ((uint8_t)7ULL): goto ma46; case ((uint8_t)8ULL): goto ma46; case ((uint8_t)9ULL): goto ma46; case ((uint8_t)10ULL): goto ma46; case ((uint8_t)11ULL): goto ma46; case ((uint8_t)12ULL): goto ma46; case ((uint8_t)13ULL): goto ma46; case ((uint8_t)14ULL): goto ma46; case ((uint8_t)15ULL): goto ma46; case ((uint8_t)16ULL): goto ma46; case ((uint8_t)17ULL): goto ma46; case ((uint8_t)18ULL): goto ma46; case ((uint8_t)19ULL): goto ma46; case ((uint8_t)20ULL): goto ma46; case ((uint8_t)21ULL): goto ma46; case ((uint8_t)22ULL): goto ma46; case ((uint8_t)23ULL): goto ma46; case ((uint8_t)24ULL): goto ma46; case ((uint8_t)25ULL): goto ma46; case ((uint8_t)26ULL): goto ma46; case ((uint8_t)27ULL): goto ma46; case ((uint8_t)28ULL): goto ma42; case ((uint8_t)29ULL): goto ma43; case ((uint8_t)30ULL): goto ma44; case ((uint8_t)31ULL): goto ma45; case ((uint8_t)32ULL): goto ma46; case ((uint8_t)33ULL): goto ma46; case ((uint8_t)34ULL): goto ma46; case ((uint8_t)35ULL): goto ma46; case ((uint8_t)36ULL): goto ma46; case ((uint8_t)37ULL): goto ma46; case ((uint8_t)38ULL): goto ma46; case ((uint8_t)39ULL): goto ma46; case ((uint8_t)40ULL): goto ma46; case ((uint8_t)41ULL): goto ma46; case ((uint8_t)42ULL): goto ma46; case ((uint8_t)43ULL): goto ma46; case ((uint8_t)44ULL): goto ma46; case ((uint8_t)45ULL): goto ma46; case ((uint8_t)46ULL): goto ma46; case ((uint8_t)47ULL): goto ma46; case ((uint8_t)48ULL): goto ma46; case ((uint8_t)49ULL): goto ma46; case ((uint8_t)50ULL): goto ma46; case ((uint8_t)51ULL): goto ma46; case ((uint8_t)52ULL): goto ma46; case ((uint8_t)53ULL): goto ma46; case ((uint8_t)54ULL): goto ma46; case ((uint8_t)55ULL): goto ma46; case ((uint8_t)56ULL): goto ma46; case ((uint8_t)57ULL): goto ma46; case ((uint8_t)58ULL): goto ma46; case ((uint8_t)59ULL): goto ma46; case ((uint8_t)60ULL): goto ma46; case ((uint8_t)61ULL): goto ma46; case ((uint8_t)62ULL): goto ma46; case ((uint8_t)63ULL): goto ma46; case ((uint8_t)64ULL): goto ma46; case ((uint8_t)65ULL): goto ma46; case ((uint8_t)66ULL): goto ma46; case ((uint8_t)67ULL): goto ma46; case ((uint8_t)68ULL): goto ma46; case ((uint8_t)69ULL): goto ma46; case ((uint8_t)70ULL): goto ma46; case ((uint8_t)71ULL): goto ma46; case ((uint8_t)72ULL): goto ma46; case ((uint8_t)73ULL): goto ma46; case ((uint8_t)74ULL): goto ma46; case ((uint8_t)75ULL): goto ma46; case ((uint8_t)76ULL): goto ma46; case ((uint8_t)77ULL): goto ma46; case ((uint8_t)78ULL): goto ma46; case ((uint8_t)79ULL): goto ma46; case ((uint8_t)80ULL): goto ma46; case ((uint8_t)81ULL): goto ma46; default: goto ma46; }
ma42:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t50 != ((uint8_t)28ULL)) goto mn49;
        gm_tommf4j9upomc t51;
        memset((uint8_t *)&t51 + 20, 0, 4);
        gm_b16_8 t52 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t51 + 16, t53);
        gm_tabb710d9mq2e t54;
        gm_f_format_c9b4vl3((void *)&t54, (void *)NULL, cx, NULL, &t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t55 = t54;
        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_src2j6ljeja7ilp, 319, 17);
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
        gm_b16_8 t57 = t56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        t40 = t58;
        goto me41;
mn49:;
ma43:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t60 != ((uint8_t)29ULL)) goto mn59;
        gm_tommf4j9upomc t61;
        memset((uint8_t *)&t61 + 20, 0, 4);
        gm_b16_8 t62 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t61 + 0, t62);
        uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t61 + 16, t63);
        gm_tabb710d9mq2e t64;
        gm_f_format_a1ec0ji((void *)&t64, (void *)NULL, cx, NULL, &t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t65 = t64;
        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_src2j6ljeja7ilp, 320, 17);
        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
        gm_b16_8 t67 = t66;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        t40 = t68;
        goto me41;
mn59:;
ma44:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t70 != ((uint8_t)30ULL)) goto mn69;
        gm_b16_8 t71 = gm_l8;
        gm_tnpifv20irnph t72;
        GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
        gm_tabb710d9mq2e t73;
        gm_f_format_q296tqt((void *)&t73, (void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t74 = t73;
        if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_src2j6ljeja7ilp, 321, 22);
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
        gm_b16_8 t76 = t75;
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
        t40 = t77;
        goto me41;
mn69:;
ma45:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t79 != ((uint8_t)31ULL)) goto mn78;
        {
            uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_src2j6ljeja7ilp, 323, 16);
            uint32_t t83 = (uint32_t)(t82 % ((uint32_t)8ULL));
            uint8_t t84 = (!(t83 == ((uint32_t)0ULL)));
            if (!t84) goto ie81;
            {
                void * t86 = gm_l1;
                gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t86) + 208));
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s0cpkv7s4ef85));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(23));
                gm_t08olq97rscn3 t89;
                gm_f_unsupported_531q51r((void *)&t89, (void *)NULL, cx, NULL, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t90;
                GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
                ((uint8_t *)&t90)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t90 + 8), t89);
                gm_t5o0tpv5jka2l t91 = t90;
                uint8_t t92 = ((((uint8_t *)&t91))[0] != 0);
                F.ret = t90;
                goto gm_fret;
            }
            goto ix81;
ie81:;
ix81:;
        }
        gm_tommf4j9upomc t93;
        memset((uint8_t *)&t93 + 20, 0, 4);
        gm_b16_8 t94 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 0, t94);
        uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t93 + 16, t95);
        gm_tabb710d9mq2e t96;
        gm_f_format_87dn9rn((void *)&t96, (void *)NULL, cx, NULL, &t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t97 = t96;
        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_src2j6ljeja7ilp, 324, 13);
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
        gm_b16_8 t99 = t98;
        gm_b16_8 t100 = t99;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
        t40 = t101;
        goto me41;
mn78:;
ma46:;
        gm_tommf4j9upomc t103;
        memset((uint8_t *)&t103 + 20, 0, 4);
        gm_b16_8 t104 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t103 + 0, t104);
        uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t103 + 16, t105);
        gm_tabb710d9mq2e t106;
        gm_f_format_kbs7h9q((void *)&t106, (void *)NULL, cx, NULL, &t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t107 = t106;
        if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_src2j6ljeja7ilp, 326, 14);
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
        gm_b16_8 t109 = t108;
        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
        t40 = t110;
        goto me41;
mn102:;
ma47:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 318, 20);
me41:;
        gm_l10 = t40;
    }
    {
        void * t111 = gm_l0;
        gm_t88rcucg1b7uk t112 = gm_l5;
        gm_t6j6smj7qoctu t113;
        gm_b16_8 t114 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t114);
        gm_b16_8 t115 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t115);
        gm_tabb710d9mq2e t116;
        gm_f_format_3vtl3te((void *)&t116, (void *)NULL, cx, NULL, &t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t117 = t116;
        if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src2j6ljeja7ilp, 328, 20);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
        gm_b16_8 t119 = t118;
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        gm_b16_8 t121 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t111, t112, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t121;
    }
    {
        gm_tvdg94bk7aa22 t123 = gm_l2;
        gm_tq9t4giin3pm0 t124;
        gm_tvdg94bk7aa22 t125;
        GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t125 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)31ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t124 + 0, t125);
        gm_tvdg94bk7aa22 t126;
        GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t126 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)34ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t124 + 12, t126);
        gm_tq9t4giin3pm0 t127 = t124;
        uint8_t t128 = 0;
        gm_tvdg94bk7aa22 t129 = t123;
        if (gm_eqg63o53ar52(((uint8_t *)&t127) + 0, ((uint8_t *)&t129))) t128 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t127) + 12, ((uint8_t *)&t129))) t128 = 1;
        uint8_t t130 = (uint8_t)(t128);
        if (!t130) goto ie122;
        {
            void * t133 = gm_l0;
            gm_t88rcucg1b7uk t134 = gm_l5;
            gm_tqcc6dmlne11b t135;
            gm_f_int_info_2tcdf1n((void *)&t135, (void *)NULL, cx, NULL, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t136 = t135;
            if (!((((uint8_t *)&t136))[16] != 0)) goto ie132;
            gm_tuglhjjmj2g58 t137 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t136));
            gm_l12 = t137;
            {
                gm_tuglhjjmj2g58 t139 = gm_l12;
                gm_b16_8 t140 = gm_l11;
                gm_b16_8 t141 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t139, t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t141;
            }
            {
                void * t142 = gm_l0;
                gm_t88rcucg1b7uk t143 = gm_l5;
                gm_b16_8 t144 = gm_l13;
                gm_b16_8 t145 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t142, t143, t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t146;
                GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t146 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t146 + 32, 0);
                ((uint8_t *)&t146)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t146 + 8), t145);
                gm_t5o0tpv5jka2l t147 = t146;
                uint8_t t148 = ((((uint8_t *)&t147))[0] != 0);
                F.ret = t146;
                goto gm_fret;
            }
            goto ix132;
ie132:;
ix132:;
        }
        goto ix122;
ie122:;
ix122:;
    }
    {
        gm_b16_8 t149 = gm_l11;
        gm_t5o0tpv5jka2l t150;
        GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t150 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t150 + 32, 0);
        ((uint8_t *)&t150)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t150 + 8), t149);
        gm_t5o0tpv5jka2l t151 = t150;
        uint8_t t152 = ((((uint8_t *)&t151))[0] != 0);
        F.ret = t150;
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
__attribute__((aligned(4))) void gm_f_black_box_r6deojc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_black_box_r6deojc F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* v0 */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* c */
    uint64_t gm_l5; /* storage */
    uint8_t gm_l6; /* in_register */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* slot */
    gm_b16_8 gm_l9; /* p */
    GM_STACK_CHECK(&F, 998u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l3;
        uint8_t t9 = gm_f_is_unit_17uennd(t7, GM_CONV(gm_csguankko4lmhd, t8));
        if (!t9) goto ie4;
        {
            gm_b16_8 t11 = gm_l2;
            gm_t5o0tpv5jka2l t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 32, 0);
            ((uint8_t *)&t12)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 8), t11);
            gm_t5o0tpv5jka2l t13 = t12;
            uint8_t t14 = ((((uint8_t *)&t13))[0] != 0);
            F.ret = t12;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t15 = gm_l0;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
        void * t17 = (void *)(((uint8_t *)t16) + 8);
        gm_t88rcucg1b7uk t18 = gm_l3;
        gm_b16_8 t19 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        uint8_t t20 = 0;
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        uint8_t t23 = GM_LD(uint8_t, ((((((uint8_t *)t22) + 8) + 8) + 0) + 56));
        uint8_t t24 = (uint8_t)!t23;
        if (t24) {
            void * t25 = gm_l0;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            void * t27 = (void *)(((uint8_t *)t26) + 8);
            gm_t88rcucg1b7uk t28 = gm_l3;
            gm_t5v8n6qrn4rh4 t29;
            gm_f_repr_53quqki((void *)&t29, (void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t30 = t29;
            uint8_t t31;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
            switch (t37) { case ((uint8_t)0ULL): goto ma33; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma35; default: goto ma35; }
ma33:;
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
            if (t39 != ((uint8_t)0ULL)) goto mn38;
            uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)&t30) + 8) + 8));
            gm_l5 = t40;
            uint64_t t41 = gm_l5;
            uint8_t t42 = (t41 <= ((uint64_t)8ULL));
            t31 = t42;
            goto me32;
mn38:;
ma34:;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
            if (t44 != ((uint8_t)2ULL)) goto mn43;
            t31 = ((uint8_t)1);
            goto me32;
mn43:;
ma35:;
            t31 = ((uint8_t)0);
            goto me32;
mn45:;
ma36:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 430, 57);
me32:;
            t20 = t31;
        } else t20 = 0;
        gm_l6 = t20;
    }
    {
        uint8_t t47 = gm_l6;
        if (!t47) goto ie46;
        {
            void * t49 = gm_l0;
            gm_t88rcucg1b7uk t50 = gm_l3;
            gm_b16_8 t51 = gm_l2;
            gm_b16_8 t52 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t49, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t52;
        }
        {
            void * t53 = gm_l0;
            gm_b16_8 t54 = gm_l7;
            gm_tnpifv20irnph t55;
            GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
            gm_b0_1 t56 = gm_f_linef_7e19lva((void *)NULL, cx, NULL, t53, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t57 = gm_l7;
            gm_t5o0tpv5jka2l t58;
            GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t58 + 32, 0);
            ((uint8_t *)&t58)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
            gm_t5o0tpv5jka2l t59 = t58;
            uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
            F.ret = t58;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t61 = gm_l0;
        gm_t88rcucg1b7uk t62 = gm_l3;
        gm_b16_8 t63 = gm_l2;
        gm_b16_8 t64 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t61, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t64;
    }
    {
        void * t65 = gm_l0;
        gm_b16_8 t66 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t66;
    }
    {
        void * t67 = gm_l0;
        gm_tfeab279c16rr t68;
        gm_b16_8 t69 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t69);
        gm_b16_8 t70 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t70);
        gm_b16_8 t71 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t68 + 32, t71);
        gm_b0_1 t72 = gm_f_linef_0kkt9cm((void *)NULL, cx, NULL, t67, &t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t73 = gm_l0;
        gm_t88rcucg1b7uk t74 = gm_l3;
        gm_b16_8 t75 = gm_l9;
        gm_tnpifv20irnph t76;
        GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
        gm_tabb710d9mq2e t77;
        gm_f_format_ohk11b5((void *)&t77, (void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t78 = t77;
        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_src2j6ljeja7ilp, 443, 22);
        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
        gm_b16_8 t80 = t79;
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        gm_b16_8 t82 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t73, t74, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 32, 0);
        ((uint8_t *)&t83)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t83 + 8), t82);
        gm_t5o0tpv5jka2l t84 = t83;
        uint8_t t85 = ((((uint8_t *)&t84))[0] != 0);
        F.ret = t83;
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
__attribute__((aligned(4))) void gm_f_builtin_s7ark4p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_builtin_s7ark4p F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* r */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* loc */
    void * gm_l7; /* tys */
    gm_tcc1rq4n1avb2 gm_l8; /* counts */
    gm_t9qkh0rsooc8d gm_l9; /* sop */
    uint32_t gm_l10; /* id */
    gm_b16_8 gm_l11; /* s */
    gm_b16_8 gm_l12; /* a */
    gm_b16_8 gm_l13; /* p */
    gm_b16_8 gm_l14; /* p */
    gm_b16_8 gm_l15; /* s */
    gm_b16_8 gm_l16; /* a */
    gm_t88rcucg1b7uk gm_l17; /* elem */
    gm_b16_8 gm_l18; /* d */
    gm_b16_8 gm_l19; /* s */
    gm_b16_8 gm_l20; /* n */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l25; /* v0 */
    gm_t88rcucg1b7uk gm_l26; /* at */
    gm_b16_8 gm_l27; /* a */
    gm_b16_8 gm_l28; /* r */
    uint64_t gm_l29; /* sz */
    gm_tuglhjjmj2g58 gm_l30; /* ii */
    gm_b16_8 gm_l31; /* nv */
    uint64_t gm_l32; /* al */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    uint64_t gm_l34; /* stride */
    gm_b16_8 gm_l35; /* p */
    gm_b16_8 gm_l36; /* x */
    gm_b16_8 gm_l37; /* sign */
    gm_b16_8 gm_l38; /* a */
    gm_b16_8 gm_l39; /* gt */
    gm_b16_8 gm_l40; /* f */
    gm_b16_8 gm_l41; /* r */
    gm_b16_8 gm_l42; /* x */
    gm_b16_8 gm_l43; /* lo */
    gm_b16_8 gm_l44; /* hi */
    gm_b16_8 gm_l45; /* r */
    gm_tuglhjjmj2g58 gm_l46; /* ii */
    gm_b16_8 gm_l47; /* min */
    gm_tuglhjjmj2g58 gm_l48; /* ii */
    gm_b16_8 gm_l49; /* u */
    gm_b16_8 gm_l50; /* a */
    gm_b16_8 gm_l51; /* b */
    gm_b16_8 gm_l52; /* f */
    gm_b16_8 gm_l53; /* r */
    gm_b16_8 gm_l54; /* f */
    gm_b16_8 gm_l55; /* r */
    gm_t9qkh0rsooc8d gm_l56; /* fl */
    gm_b16_8 gm_l57; /* a */
    gm_b16_8 gm_l58; /* f */
    gm_b16_8 gm_l59; /* a */
    GM_STACK_CHECK(&F, 15683u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        gm_tvdg94bk7aa22 t5 = gm_l2;
        gm_b16_8 t6 = gm_l3;
        gm_t6j4d6ei14c2t t7;
        gm_f_vector_builtin_a1f1ati((void *)&t7, (void *)NULL, cx, NULL, t3, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j4d6ei14c2t t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_t08olq97rscn3 t9 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t8) + 8));
            gm_t5o0tpv5jka2l t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t10 + 8), t9);
            F.ret = t10;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t11 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t8) + 8));
        gm_th2lqbo77pvsc t12 = t11;
        if (!((((uint8_t *)&t12))[16] != 0)) goto ie2;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_l4 = t13;
        {
            gm_b16_8 t15 = gm_l4;
            gm_t5o0tpv5jka2l t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t16 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t16 + 32, 0);
            ((uint8_t *)&t16)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
            gm_t5o0tpv5jka2l t17 = t16;
            uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l1;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 0));
        gm_l5 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l1;
        gm_b16_8 t23 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t23;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = GM_LD(void *, ((((uint8_t *)t25) + 8) + 0));
        void * t27 = (void *)(((uint8_t *)t26) + 8);
        gm_l7 = t27;
    }
    {
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t42) { case ((uint8_t)0ULL): goto ma40; case ((uint8_t)1ULL): goto ma40; case ((uint8_t)2ULL): goto ma40; case ((uint8_t)3ULL): goto ma40; case ((uint8_t)4ULL): goto ma40; case ((uint8_t)5ULL): goto ma40; case ((uint8_t)6ULL): goto ma40; case ((uint8_t)7ULL): goto ma40; case ((uint8_t)8ULL): goto ma40; case ((uint8_t)9ULL): goto ma40; case ((uint8_t)10ULL): goto ma40; case ((uint8_t)11ULL): goto ma40; case ((uint8_t)12ULL): goto ma40; case ((uint8_t)13ULL): goto ma40; case ((uint8_t)14ULL): goto ma40; case ((uint8_t)15ULL): goto ma40; case ((uint8_t)16ULL): goto ma40; case ((uint8_t)17ULL): goto ma40; case ((uint8_t)18ULL): goto ma40; case ((uint8_t)19ULL): goto ma40; case ((uint8_t)20ULL): goto ma40; case ((uint8_t)21ULL): goto ma40; case ((uint8_t)22ULL): goto ma40; case ((uint8_t)23ULL): goto ma40; case ((uint8_t)24ULL): goto ma40; case ((uint8_t)25ULL): goto ma40; case ((uint8_t)26ULL): goto ma40; case ((uint8_t)27ULL): goto ma40; case ((uint8_t)28ULL): goto ma40; case ((uint8_t)29ULL): goto ma40; case ((uint8_t)30ULL): goto ma40; case ((uint8_t)31ULL): goto ma40; case ((uint8_t)32ULL): goto ma40; case ((uint8_t)33ULL): goto ma40; case ((uint8_t)34ULL): goto ma40; case ((uint8_t)35ULL): goto ma40; case ((uint8_t)36ULL): goto ma35; case ((uint8_t)37ULL): goto ma35; case ((uint8_t)38ULL): goto ma35; case ((uint8_t)39ULL): goto ma35; case ((uint8_t)40ULL): goto ma40; case ((uint8_t)41ULL): goto ma40; case ((uint8_t)42ULL): goto ma40; case ((uint8_t)43ULL): goto ma40; case ((uint8_t)44ULL): goto ma40; case ((uint8_t)45ULL): goto ma40; case ((uint8_t)46ULL): goto ma40; case ((uint8_t)47ULL): goto ma40; case ((uint8_t)48ULL): goto ma40; case ((uint8_t)49ULL): goto ma40; case ((uint8_t)50ULL): goto ma40; case ((uint8_t)51ULL): goto ma40; case ((uint8_t)52ULL): goto ma40; case ((uint8_t)53ULL): goto ma40; case ((uint8_t)54ULL): goto ma40; case ((uint8_t)55ULL): goto ma40; case ((uint8_t)56ULL): goto ma40; case ((uint8_t)57ULL): goto ma40; case ((uint8_t)58ULL): goto ma40; case ((uint8_t)59ULL): goto ma40; case ((uint8_t)60ULL): goto ma40; case ((uint8_t)61ULL): goto ma29; case ((uint8_t)62ULL): goto ma40; case ((uint8_t)63ULL): goto ma34; case ((uint8_t)64ULL): goto ma40; case ((uint8_t)65ULL): goto ma40; case ((uint8_t)66ULL): goto ma40; case ((uint8_t)67ULL): goto ma40; case ((uint8_t)68ULL): goto ma40; case ((uint8_t)69ULL): goto ma40; case ((uint8_t)70ULL): goto ma40; case ((uint8_t)71ULL): goto ma40; case ((uint8_t)72ULL): goto ma32; case ((uint8_t)73ULL): goto ma30; case ((uint8_t)74ULL): goto ma36; case ((uint8_t)75ULL): goto ma38; case ((uint8_t)76ULL): goto ma37; case ((uint8_t)77ULL): goto ma37; case ((uint8_t)78ULL): goto ma39; case ((uint8_t)79ULL): goto ma31; case ((uint8_t)80ULL): goto ma40; case ((uint8_t)81ULL): goto ma33; default: goto ma40; }
ma29:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t44 != ((uint8_t)61ULL)) goto mn43;
        void * t45 = gm_l0;
        void * t46 = gm_l1;
        gm_b16_8 t47 = gm_l3;
        gm_f_assert_m8ussus((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me28;
mn43:;
ma30:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t49 != ((uint8_t)73ULL)) goto mn48;
        void * t50 = gm_l0;
        void * t51 = gm_l1;
        gm_b16_8 t52 = gm_l3;
        gm_t5o0tpv5jka2l t53;
        gm_f_fiber_builtin_bgfcp56((void *)&t53, (void *)NULL, cx, NULL, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) {
            gm_t08olq97rscn3 t55 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t54) + 8));
            gm_t5o0tpv5jka2l t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
            ((uint8_t *)&t56)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t56 + 8), t55);
            F.ret = t56;
            goto gm_fret;
        }
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
        gm_t5o0tpv5jka2l t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t58 + 32, 0);
        ((uint8_t *)&t58)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
        gm_t5o0tpv5jka2l t59 = t58;
        uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
        F.ret = t58;
        goto gm_fret;
        goto me28;
mn48:;
ma31:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t62 != ((uint8_t)79ULL)) goto mn61;
        gm_tcc1rq4n1avb2 t63 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)&gm_l2) + 4));
        gm_l8 = t63;
        void * t64 = gm_l0;
        gm_b16_8 t65 = gm_l3;
        uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        uint64_t t67 = (uint64_t)t66;
        uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
        uint64_t t69 = (uint64_t)t68;
        gm_t5o0tpv5jka2l t70;
        gm_f_arith_asm_pklrmbh((void *)&t70, (void *)NULL, cx, NULL, t64, t65, t67, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t71 = t70;
        if (((((uint8_t *)&t71))[0] != 0)) {
            gm_t08olq97rscn3 t72 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t71) + 8));
            gm_t5o0tpv5jka2l t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
            ((uint8_t *)&t73)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t73 + 8), t72);
            F.ret = t73;
            goto gm_fret;
        }
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
        gm_t5o0tpv5jka2l t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 32, 0);
        ((uint8_t *)&t75)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 8), t74);
        gm_t5o0tpv5jka2l t76 = t75;
        uint8_t t77 = ((((uint8_t *)&t76))[0] != 0);
        F.ret = t75;
        goto gm_fret;
        goto me28;
mn61:;
ma32:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t79 != ((uint8_t)72ULL)) goto mn78;
        gm_t9qkh0rsooc8d t80 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
        gm_l9 = t80;
        void * t81 = gm_l0;
        void * t82 = gm_l1;
        gm_t9qkh0rsooc8d t83 = gm_l9;
        gm_b16_8 t84 = gm_l3;
        gm_f_sys_builtin_svdoho3((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t81, t82, t83, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me28;
mn78:;
ma33:;
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t86 != ((uint8_t)81ULL)) goto mn85;
        uint32_t t87 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_l10 = t87;
        {
            void * t89 = gm_l0;
            uint32_t t90 = gm_l10;
            gm_t88rcucg1b7uk t91;
            GM_ST(uint32_t, ((uint8_t *)&t91 + 0), t90);
            gm_b0_1 t92 = gm_f_linef_10398ks((void *)NULL, cx, NULL, t89, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t93 = gm_l0;
            gm_b16_8 t94 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t95 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t95 + 32, 0);
            ((uint8_t *)&t95)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 8), t94);
            gm_t5o0tpv5jka2l t96 = t95;
            uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
            F.ret = t95;
            goto gm_fret;
        }
        goto me28;
mn85:;
ma34:;
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t99 != ((uint8_t)63ULL)) goto mn98;
        {
            void * t101 = gm_l0;
            gm_b16_8 t102 = gm_l6;
            gm_tnpifv20irnph t103;
            GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
            gm_b0_1 t104 = gm_f_linef_oi4f7u8((void *)NULL, cx, NULL, t101, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t105 = gm_l0;
            gm_t88rcucg1b7uk t106 = gm_l5;
            gm_b16_8 t107 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t108;
            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t108 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t108 + 32, 0);
            ((uint8_t *)&t108)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t108 + 8), t107);
            gm_t5o0tpv5jka2l t109 = t108;
            uint8_t t110 = ((((uint8_t *)&t109))[0] != 0);
            F.ret = t108;
            goto gm_fret;
        }
        goto me28;
mn98:;
ma35:;
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t114 != ((uint8_t)36ULL)) goto pa113;
        goto po112;
pa113:;
        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t116 != ((uint8_t)37ULL)) goto pa115;
        goto po112;
pa115:;
        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t118 != ((uint8_t)38ULL)) goto pa117;
        goto po112;
pa117:;
        uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t120 != ((uint8_t)39ULL)) goto pa119;
        goto po112;
pa119:;
        goto mn111;
po112:;
        void * t121 = gm_l0;
        void * t122 = gm_l1;
        gm_tvdg94bk7aa22 t123 = gm_l2;
        gm_b16_8 t124 = gm_l3;
        gm_t5o0tpv5jka2l t125;
        gm_f_arith_get_jao7f8p((void *)&t125, (void *)NULL, cx, NULL, t121, t122, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t126 = t125;
        if (((((uint8_t *)&t126))[0] != 0)) {
            gm_t08olq97rscn3 t127 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t126) + 8));
            gm_t5o0tpv5jka2l t128;
            GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
            ((uint8_t *)&t128)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t128 + 8), t127);
            F.ret = t128;
            goto gm_fret;
        }
        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
        gm_t5o0tpv5jka2l t130;
        GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t130 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t130 + 32, 0);
        ((uint8_t *)&t130)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t130 + 8), t129);
        gm_t5o0tpv5jka2l t131 = t130;
        uint8_t t132 = ((((uint8_t *)&t131))[0] != 0);
        F.ret = t130;
        goto gm_fret;
        goto me28;
mn111:;
ma36:;
        uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t134 != ((uint8_t)74ULL)) goto mn133;
        {
            void * t136 = gm_l0;
            uint64_t t137 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_src2j6ljeja7ilp, 56, 31);
            void * t139 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t137 * 224);
            gm_t5o0tpv5jka2l t140;
            gm_f_expr_porbc9t((void *)&t140, (void *)NULL, cx, NULL, t136, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t141 = t140;
            if (((((uint8_t *)&t141))[0] != 0)) {
                gm_t08olq97rscn3 t142 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t141) + 8));
                gm_t5o0tpv5jka2l t143;
                GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                ((uint8_t *)&t143)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t143 + 8), t142);
                F.ret = t143;
                goto gm_fret;
            }
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
            gm_l11 = t144;
        }
        {
            void * t145 = gm_l0;
            uint64_t t146 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t146 >= (uintptr_t)(t147)) gm_trap_index(t146, t147, gm_src2j6ljeja7ilp, 57, 31);
            void * t148 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t146 * 224);
            gm_t5o0tpv5jka2l t149;
            gm_f_expr_porbc9t((void *)&t149, (void *)NULL, cx, NULL, t145, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t150 = t149;
            if (((((uint8_t *)&t150))[0] != 0)) {
                gm_t08olq97rscn3 t151 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t150) + 8));
                gm_t5o0tpv5jka2l t152;
                GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                ((uint8_t *)&t152)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t152 + 8), t151);
                F.ret = t152;
                goto gm_fret;
            }
            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&t150) + 8));
            gm_l12 = t153;
        }
        {
            void * t154 = gm_l0;
            gm_t0uuhaljsf410 t155;
            gm_b16_8 t156 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t155 + 0, t156);
            gm_b16_8 t157 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t155 + 16, t157);
            gm_b16_8 t158 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t155 + 32, t158);
            gm_b16_8 t159 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t155 + 48, t159);
            gm_b0_1 t160 = gm_f_linef_vh7uebp((void *)NULL, cx, NULL, t154, &t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t161 = gm_l0;
            void * t162 = gm_l7;
            gm_t53at80pfdus3 t163;
            memset(&t163, 0, sizeof t163);
            GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)7ULL));
            gm_t88rcucg1b7uk t164 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t162, &t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t165;
            gm_b16_8 t166 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t165 + 0, t166);
            gm_b16_8 t167 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t165 + 16, t167);
            gm_tabb710d9mq2e t168;
            gm_f_format_7g5nhjp((void *)&t168, (void *)NULL, cx, NULL, &t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t169 = t168;
            if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 8, gm_src2j6ljeja7ilp, 59, 46);
            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&t169) + 8));
            gm_b16_8 t171 = t170;
            gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
            gm_b16_8 t173 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t161, t164, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t173;
        }
        {
            void * t174 = gm_l0;
            gm_t88rcucg1b7uk t175 = gm_l5;
            gm_b16_8 t176 = gm_l13;
            gm_b16_8 t177 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t174, t175, t176);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t178;
            GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t178 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t178 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t178 + 32, 0);
            ((uint8_t *)&t178)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t178 + 8), t177);
            gm_t5o0tpv5jka2l t179 = t178;
            uint8_t t180 = ((((uint8_t *)&t179))[0] != 0);
            F.ret = t178;
            goto gm_fret;
        }
        goto me28;
mn133:;
ma37:;
        uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t184 != ((uint8_t)76ULL)) goto pa183;
        goto po182;
pa183:;
        uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t186 != ((uint8_t)77ULL)) goto pa185;
        goto po182;
pa185:;
        goto mn181;
po182:;
        void * t187 = gm_l0;
        void * t188 = gm_l1;
        gm_tvdg94bk7aa22 t189 = gm_l2;
        gm_b16_8 t190 = gm_l3;
        gm_f_alloc_tag_a71p35s((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t187, t188, t189, t190);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me28;
mn181:;
ma38:;
        uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t192 != ((uint8_t)75ULL)) goto mn191;
        {
            void * t194 = gm_l0;
            uint64_t t195 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t195 >= (uintptr_t)(t196)) gm_trap_index(t195, t196, gm_src2j6ljeja7ilp, 64, 31);
            void * t197 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t195 * 224);
            gm_t5o0tpv5jka2l t198;
            gm_f_expr_porbc9t((void *)&t198, (void *)NULL, cx, NULL, t194, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t199 = t198;
            if (((((uint8_t *)&t199))[0] != 0)) {
                gm_t08olq97rscn3 t200 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t199) + 8));
                gm_t5o0tpv5jka2l t201;
                GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
                ((uint8_t *)&t201)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t201 + 8), t200);
                F.ret = t201;
                goto gm_fret;
            }
            gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&t199) + 8));
            gm_l14 = t202;
        }
        {
            void * t203 = gm_l0;
            uint64_t t204 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t204 >= (uintptr_t)(t205)) gm_trap_index(t204, t205, gm_src2j6ljeja7ilp, 65, 31);
            void * t206 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t204 * 224);
            gm_t5o0tpv5jka2l t207;
            gm_f_expr_porbc9t((void *)&t207, (void *)NULL, cx, NULL, t203, t206);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t208 = t207;
            if (((((uint8_t *)&t208))[0] != 0)) {
                gm_t08olq97rscn3 t209 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t208) + 8));
                gm_t5o0tpv5jka2l t210;
                GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                ((uint8_t *)&t210)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t210 + 8), t209);
                F.ret = t210;
                goto gm_fret;
            }
            gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&t208) + 8));
            gm_l15 = t211;
        }
        {
            void * t212 = gm_l0;
            uint64_t t213 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t213 >= (uintptr_t)(t214)) gm_trap_index(t213, t214, gm_src2j6ljeja7ilp, 66, 31);
            void * t215 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t213 * 224);
            gm_t5o0tpv5jka2l t216;
            gm_f_expr_porbc9t((void *)&t216, (void *)NULL, cx, NULL, t212, t215);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t217 = t216;
            if (((((uint8_t *)&t217))[0] != 0)) {
                gm_t08olq97rscn3 t218 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t217) + 8));
                gm_t5o0tpv5jka2l t219;
                GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                ((uint8_t *)&t219)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t219 + 8), t218);
                F.ret = t219;
                goto gm_fret;
            }
            gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t217) + 8));
            gm_l16 = t220;
        }
        {
            void * t221 = gm_l0;
            gm_t0uuhaljsf410 t222;
            gm_b16_8 t223 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t222 + 0, t223);
            gm_b16_8 t224 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t222 + 16, t224);
            gm_b16_8 t225 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t222 + 32, t225);
            gm_b16_8 t226 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t222 + 48, t226);
            gm_b0_1 t227 = gm_f_linef_biu9mas((void *)NULL, cx, NULL, t221, &t222);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t228 = gm_l0;
            gm_b16_8 t229 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t228);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t230;
            GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t230 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t230 + 32, 0);
            ((uint8_t *)&t230)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t230 + 8), t229);
            gm_t5o0tpv5jka2l t231 = t230;
            uint8_t t232 = ((((uint8_t *)&t231))[0] != 0);
            F.ret = t230;
            goto gm_fret;
        }
        goto me28;
mn191:;
ma39:;
        uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t234 != ((uint8_t)78ULL)) goto mn233;
        {
            void * t237 = gm_l7;
            uint64_t t238 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t238 >= (uintptr_t)(t239)) gm_trap_index(t238, t239, gm_src2j6ljeja7ilp, 71, 41);
            gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t238 * 224) + 0));
            gm_t53at80pfdus3 t241;
            gm_f_kind_jcnt4hm((void *)&t241, t237, GM_CONV(gm_csguankko4lmhd, t240));
            gm_t53at80pfdus3 t242 = t241;
            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&t242) + 0));
            if (t243 != ((uint8_t)10ULL)) goto ie236;
            gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t242) + 8) + 4));
            gm_l17 = t244;
            {
                void * t246 = gm_l0;
                uint64_t t247 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t247 >= (uintptr_t)(t248)) gm_trap_index(t247, t248, gm_src2j6ljeja7ilp, 72, 29);
                gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t247 * 224) + 0));
                void * t250 = gm_l0;
                uint64_t t251 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t252 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t251 >= (uintptr_t)(t252)) gm_trap_index(t251, t252, gm_src2j6ljeja7ilp, 72, 54);
                void * t253 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t251 * 224);
                gm_t5o0tpv5jka2l t254;
                gm_f_expr_porbc9t((void *)&t254, (void *)NULL, cx, NULL, t250, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t255 = t254;
                if (((((uint8_t *)&t255))[0] != 0)) {
                    gm_t08olq97rscn3 t256 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t255) + 8));
                    gm_t5o0tpv5jka2l t257;
                    GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                    ((uint8_t *)&t257)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t257 + 8), t256);
                    F.ret = t257;
                    goto gm_fret;
                }
                gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
                gm_b16_8 t259 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t246, t249, t258);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t259;
            }
            {
                void * t260 = gm_l0;
                uint64_t t261 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t261 >= (uintptr_t)(t262)) gm_trap_index(t261, t262, gm_src2j6ljeja7ilp, 73, 29);
                gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t261 * 224) + 0));
                void * t264 = gm_l0;
                uint64_t t265 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t265 >= (uintptr_t)(t266)) gm_trap_index(t265, t266, gm_src2j6ljeja7ilp, 73, 54);
                void * t267 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t265 * 224);
                gm_t5o0tpv5jka2l t268;
                gm_f_expr_porbc9t((void *)&t268, (void *)NULL, cx, NULL, t264, t267);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t269 = t268;
                if (((((uint8_t *)&t269))[0] != 0)) {
                    gm_t08olq97rscn3 t270 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t269) + 8));
                    gm_t5o0tpv5jka2l t271;
                    GM_ST(uint8_t, (uint8_t *)&t271 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t271 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t271 + 4, 0);
                    ((uint8_t *)&t271)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t271 + 8), t270);
                    F.ret = t271;
                    goto gm_fret;
                }
                gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&t269) + 8));
                gm_b16_8 t273 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t260, t263, t272);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t273;
            }
            {
                void * t274 = gm_l0;
                gm_b16_8 t275 = gm_l18;
                gm_b16_8 t276 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t274, t275);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t276;
            }
            {
                void * t277 = gm_l0;
                gm_t0j6181meiu4s t278;
                gm_b16_8 t279 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t278 + 0, t279);
                gm_b16_8 t280 = gm_l18;
                gm_b16_8 t281 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t278 + 16, t281);
                gm_b16_8 t282 = gm_l19;
                gm_b16_8 t283 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t282);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t278 + 32, t283);
                gm_b16_8 t284 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t278 + 48, t284);
                void * t285 = gm_l0;
                void * t286 = GM_LD(void *, (((uint8_t *)t285) + 0));
                void * t287 = (void *)(((uint8_t *)t286) + 8);
                gm_t88rcucg1b7uk t288 = gm_l17;
                uint64_t t289 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t287, t288);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint64_t, (uint8_t *)&t278 + 64, t289);
                gm_b0_1 t290 = gm_f_linef_jq6ksk1((void *)NULL, cx, NULL, t277, &t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t291 = gm_l0;
                gm_b16_8 t292 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t291);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t293;
                GM_ST(uint8_t, (uint8_t *)&t293 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t293 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t293 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t293 + 32, 0);
                ((uint8_t *)&t293)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t293 + 8), t292);
                gm_t5o0tpv5jka2l t294 = t293;
                uint8_t t295 = ((((uint8_t *)&t294))[0] != 0);
                F.ret = t293;
                goto gm_fret;
            }
            goto ix236;
ie236:;
            {
                void * t297 = gm_l0;
                gm_b16_8 t298 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t297);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t299;
                GM_ST(uint8_t, (uint8_t *)&t299 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t299 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t299 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t299 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t299 + 32, 0);
                ((uint8_t *)&t299)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t299 + 8), t298);
                gm_t5o0tpv5jka2l t300 = t299;
                uint8_t t301 = ((((uint8_t *)&t300))[0] != 0);
                F.ret = t299;
                goto gm_fret;
            }
ix236:;
        }
        goto me28;
mn233:;
ma40:;
        goto me28;
mn302:;
ma41:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 39, 5);
me28:;
    }
    {
        gm_f_list_epvducg((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            uint64_t t305 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l22 = t305;
        }
        {
            for (;;) {
                uint64_t t307 = gm_l23;
                uint64_t t308 = gm_l22;
                uint8_t t309 = (t307 < t308);
                if (!t309) goto lb306;
                {
                    uint64_t t312 = gm_l23;
                    uint64_t t313 = t312;
                    uint64_t t316 = GM_LD(uint64_t, ((uint8_t *)&t313));
                    gm_l24 = t316;
                    {
                        void * t318 = (void *)((uint8_t *)&F.l21);
                        void * t319 = gm_l0;
                        uint64_t t320 = gm_l24;
                        uint64_t t321 = (uintptr_t)t320;
                        uint64_t t322 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t321 >= (uintptr_t)(t322)) gm_trap_index(t321, t322, gm_src2j6ljeja7ilp, 81, 51);
                        void * t323 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t321 * 224);
                        gm_t5o0tpv5jka2l t324;
                        gm_f_expr_porbc9t((void *)&t324, (void *)NULL, cx, NULL, t319, t323);
                        if (gm_unw.kind) {
                            goto gm_ll310;
                        }
                        gm_t5o0tpv5jka2l t325 = t324;
                        if (((((uint8_t *)&t325))[0] != 0)) {
                            gm_t08olq97rscn3 t326 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t325) + 8));
                            gm_t5o0tpv5jka2l t327;
                            GM_ST(uint8_t, (uint8_t *)&t327 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t327 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t327 + 4, 0);
                            ((uint8_t *)&t327)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t327 + 8), t326);
                            F.ret = t327;
                            goto gm_fret;
                        }
                        gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&t325) + 8));
                        gm_tftb1odc739t5 t329 = gm_f_push_3ef746b(t318, t328);
                        gm_tftb1odc739t5 t330 = t329;
                        if (((((uint8_t *)&t330))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t330) + 1, gm_src2j6ljeja7ilp, 81, 28);
                        gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t330) + 1));
                    }
                    goto me314;
mn315:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 81, 5);
me314:;
                }
lc306:;
                {
                    uint64_t t333 = gm_l23;
                    uint64_t r334; bool o335 = __builtin_add_overflow(t333, ((uint64_t)1ULL), &r334);
                    uint64_t t336 = r334;
                    gm_l23 = t336;
                }
            }
gm_ll310:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb306; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc306; }
            }
            goto gm_lad;
lb306:;
        }
    }
    {
        uint64_t t337 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t338 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        if (t337 >= (uintptr_t)(t338)) gm_trap_index(t337, t338, gm_src2j6ljeja7ilp, 82, 11);
        gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t337 * 16));
        gm_l25 = t339;
    }
    {
        uint64_t t340 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t341 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t340 >= (uintptr_t)(t341)) gm_trap_index(t340, t341, gm_src2j6ljeja7ilp, 83, 11);
        gm_t88rcucg1b7uk t342 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t340 * 224) + 0));
        gm_l26 = t342;
    }
    {
        uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t372) { case ((uint8_t)0ULL): goto ma344; case ((uint8_t)1ULL): goto ma344; case ((uint8_t)2ULL): goto ma344; case ((uint8_t)3ULL): goto ma344; case ((uint8_t)4ULL): goto ma344; case ((uint8_t)5ULL): goto ma344; case ((uint8_t)6ULL): goto ma344; case ((uint8_t)7ULL): goto ma345; case ((uint8_t)8ULL): goto ma346; case ((uint8_t)9ULL): goto ma347; case ((uint8_t)10ULL): goto ma349; case ((uint8_t)11ULL): goto ma349; case ((uint8_t)12ULL): goto ma349; case ((uint8_t)13ULL): goto ma348; case ((uint8_t)14ULL): goto ma350; case ((uint8_t)15ULL): goto ma351; case ((uint8_t)16ULL): goto ma352; case ((uint8_t)17ULL): goto ma352; case ((uint8_t)18ULL): goto ma353; case ((uint8_t)19ULL): goto ma354; case ((uint8_t)20ULL): goto ma355; case ((uint8_t)21ULL): goto ma364; case ((uint8_t)22ULL): goto ma356; case ((uint8_t)23ULL): goto ma357; case ((uint8_t)24ULL): goto ma358; case ((uint8_t)25ULL): goto ma359; case ((uint8_t)26ULL): goto ma360; case ((uint8_t)27ULL): goto ma361; case ((uint8_t)28ULL): goto ma363; case ((uint8_t)29ULL): goto ma363; case ((uint8_t)30ULL): goto ma363; case ((uint8_t)31ULL): goto ma363; case ((uint8_t)32ULL): goto ma362; case ((uint8_t)33ULL): goto ma362; case ((uint8_t)34ULL): goto ma363; case ((uint8_t)35ULL): goto ma368; case ((uint8_t)36ULL): goto ma370; case ((uint8_t)37ULL): goto ma370; case ((uint8_t)38ULL): goto ma370; case ((uint8_t)39ULL): goto ma370; case ((uint8_t)40ULL): goto ma370; case ((uint8_t)41ULL): goto ma370; case ((uint8_t)42ULL): goto ma370; case ((uint8_t)43ULL): goto ma370; case ((uint8_t)44ULL): goto ma370; case ((uint8_t)45ULL): goto ma370; case ((uint8_t)46ULL): goto ma370; case ((uint8_t)47ULL): goto ma370; case ((uint8_t)48ULL): goto ma370; case ((uint8_t)49ULL): goto ma370; case ((uint8_t)50ULL): goto ma370; case ((uint8_t)51ULL): goto ma370; case ((uint8_t)52ULL): goto ma370; case ((uint8_t)53ULL): goto ma370; case ((uint8_t)54ULL): goto ma370; case ((uint8_t)55ULL): goto ma370; case ((uint8_t)56ULL): goto ma370; case ((uint8_t)57ULL): goto ma370; case ((uint8_t)58ULL): goto ma370; case ((uint8_t)59ULL): goto ma370; case ((uint8_t)60ULL): goto ma365; case ((uint8_t)61ULL): goto ma370; case ((uint8_t)62ULL): goto ma369; case ((uint8_t)63ULL): goto ma370; case ((uint8_t)64ULL): goto ma366; case ((uint8_t)65ULL): goto ma366; case ((uint8_t)66ULL): goto ma366; case ((uint8_t)67ULL): goto ma366; case ((uint8_t)68ULL): goto ma366; case ((uint8_t)69ULL): goto ma367; case ((uint8_t)70ULL): goto ma366; case ((uint8_t)71ULL): goto ma366; case ((uint8_t)72ULL): goto ma370; case ((uint8_t)73ULL): goto ma370; case ((uint8_t)74ULL): goto ma370; case ((uint8_t)75ULL): goto ma370; case ((uint8_t)76ULL): goto ma370; case ((uint8_t)77ULL): goto ma370; case ((uint8_t)78ULL): goto ma370; case ((uint8_t)79ULL): goto ma370; case ((uint8_t)80ULL): goto ma370; case ((uint8_t)81ULL): goto ma370; default: goto ma370; }
ma344:;
        uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t376 != ((uint8_t)0ULL)) goto pa375;
        goto po374;
pa375:;
        uint8_t t378 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t378 != ((uint8_t)1ULL)) goto pa377;
        goto po374;
pa377:;
        uint8_t t380 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t380 != ((uint8_t)2ULL)) goto pa379;
        goto po374;
pa379:;
        uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t382 != ((uint8_t)3ULL)) goto pa381;
        goto po374;
pa381:;
        uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t384 != ((uint8_t)4ULL)) goto pa383;
        goto po374;
pa383:;
        uint8_t t386 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t386 != ((uint8_t)5ULL)) goto pa385;
        goto po374;
pa385:;
        uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t388 != ((uint8_t)6ULL)) goto pa387;
        goto po374;
pa387:;
        goto mn373;
po374:;
        void * t389 = gm_l0;
        void * t390 = gm_l1;
        gm_tvdg94bk7aa22 t391 = gm_l2;
        gm_t88rcucg1b7uk t392 = gm_l26;
        gm_b16_8 t393 = gm_l25;
        gm_t5o0tpv5jka2l t394;
        gm_f_convert_lqrk2n0((void *)&t394, (void *)NULL, cx, NULL, t389, t390, t391, t392, t393);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t395 = t394;
        if (((((uint8_t *)&t395))[0] != 0)) {
            gm_t08olq97rscn3 t396 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t395) + 8));
            gm_t5o0tpv5jka2l t397;
            GM_ST(uint8_t, (uint8_t *)&t397 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t397 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t397 + 4, 0);
            ((uint8_t *)&t397)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t397 + 8), t396);
            F.ret = t397;
            goto gm_fret;
        }
        gm_b16_8 t398 = GM_LD(gm_b16_8, (((uint8_t *)&t395) + 8));
        gm_t5o0tpv5jka2l t399;
        GM_ST(uint8_t, (uint8_t *)&t399 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t399 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t399 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t399 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t399 + 32, 0);
        ((uint8_t *)&t399)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t399 + 8), t398);
        gm_t5o0tpv5jka2l t400 = t399;
        uint8_t t401 = ((((uint8_t *)&t400))[0] != 0);
        F.ret = t399;
        goto gm_fret;
        goto me343;
mn373:;
ma345:;
        uint8_t t403 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t403 != ((uint8_t)7ULL)) goto mn402;
        {
            void * t405 = gm_l0;
            gm_t88rcucg1b7uk t406 = gm_l26;
            gm_b16_8 t407 = gm_l25;
            gm_b16_8 t408 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t405, t406, t407);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t408;
        }
        {
            void * t409 = gm_l0;
            gm_t88rcucg1b7uk t410 = gm_l5;
            gm_b16_8 t411 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t409, t410);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l28 = t411;
        }
        {
            void * t412 = gm_l0;
            void * t413 = GM_LD(void *, (((uint8_t *)t412) + 0));
            void * t414 = (void *)(((uint8_t *)t413) + 8);
            gm_t88rcucg1b7uk t415 = gm_l26;
            uint64_t t416 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t414, t415);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t416;
        }
        {
            void * t417 = gm_l0;
            gm_tsroeipraj6j8 t418;
            gm_b16_8 t419 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 0, t419);
            gm_b16_8 t420 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 16, t420);
            gm_b16_8 t421 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 32, t421);
            gm_b16_8 t422 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 48, t422);
            gm_b16_8 t423 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 64, t423);
            uint64_t t424 = gm_l29;
            GM_ST(uint64_t, (uint8_t *)&t418 + 80, t424);
            gm_b16_8 t425 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t418 + 88, t425);
            uint64_t t426 = gm_l29;
            GM_ST(uint64_t, (uint8_t *)&t418 + 104, t426);
            gm_b0_1 t427 = gm_f_linef_a98fois((void *)NULL, cx, NULL, t417, &t418);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t429 = gm_l0;
            gm_t88rcucg1b7uk t430 = gm_l5;
            gm_tqcc6dmlne11b t431;
            gm_f_int_info_2tcdf1n((void *)&t431, (void *)NULL, cx, NULL, t429, t430);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t432 = t431;
            if (!((((uint8_t *)&t432))[16] != 0)) goto ie428;
            gm_tuglhjjmj2g58 t433 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t432));
            gm_l30 = t433;
            {
                gm_tuglhjjmj2g58 t436 = gm_l30;
                uint8_t t437 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t436));
                uint8_t t438 = (uint8_t)!t437;
                if (!t438) goto ie435;
                {
                    gm_tuglhjjmj2g58 t440 = gm_l30;
                    gm_b16_8 t441 = gm_l28;
                    gm_b16_8 t442 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t440, t441);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l31 = t442;
                }
                {
                    void * t443 = gm_l0;
                    gm_t6j6smj7qoctu t444;
                    gm_b16_8 t445 = gm_l28;
                    GM_ST(gm_b16_8, (uint8_t *)&t444 + 0, t445);
                    gm_b16_8 t446 = gm_l31;
                    GM_ST(gm_b16_8, (uint8_t *)&t444 + 16, t446);
                    gm_b0_1 t447 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t443, &t444);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix435;
ie435:;
ix435:;
            }
            goto ix428;
ie428:;
ix428:;
        }
        {
            gm_b16_8 t448 = gm_l28;
            gm_t5o0tpv5jka2l t449;
            GM_ST(uint8_t, (uint8_t *)&t449 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t449 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t449 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t449 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t449 + 32, 0);
            ((uint8_t *)&t449)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t449 + 8), t448);
            gm_t5o0tpv5jka2l t450 = t449;
            uint8_t t451 = ((((uint8_t *)&t450))[0] != 0);
            F.ret = t449;
            goto gm_fret;
        }
        goto me343;
mn402:;
ma346:;
        uint8_t t453 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t453 != ((uint8_t)8ULL)) goto mn452;
        {
            void * t455 = gm_l0;
            gm_t88rcucg1b7uk t456 = gm_l5;
            uint64_t t457 = gm_f_arith_pointee_align_vqoa1mk((void *)NULL, cx, NULL, t455, t456);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l32 = t457;
        }
        {
            uint64_t t459 = gm_l32;
            uint8_t t460 = (t459 > ((uint64_t)1ULL));
            if (!t460) goto ie458;
            void * t461 = gm_l0;
            void * t462 = GM_LD(void *, (((uint8_t *)t461) + 0));
            uint8_t t463 = GM_LD(uint8_t, ((((uint8_t *)t462) + 1136) + 0));
            if (!t463) goto ie458;
            {
                void * t465 = gm_l0;
                gm_tpm6m3o0hprmc t466;
                gm_b16_8 t467 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t466 + 0, t467);
                uint64_t t468 = gm_l32;
                GM_ST(uint64_t, (uint8_t *)&t466 + 16, t468);
                gm_b16_8 t469 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t466 + 24, t469);
                uint64_t t470 = gm_l32;
                GM_ST(uint64_t, (uint8_t *)&t466 + 40, t470);
                gm_b16_8 t471 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t466 + 48, t471);
                gm_b0_1 t472 = gm_f_linef_kafc4c5((void *)NULL, cx, NULL, t465, &t466);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix458;
ie458:;
ix458:;
        }
        {
            void * t473 = gm_l0;
            gm_t88rcucg1b7uk t474 = gm_l5;
            gm_b16_8 t475 = gm_l25;
            gm_b16_8 t476 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t473, t474, t475);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t477;
            GM_ST(uint8_t, (uint8_t *)&t477 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t477 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t477 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t477 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t477 + 32, 0);
            ((uint8_t *)&t477)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t477 + 8), t476);
            gm_t5o0tpv5jka2l t478 = t477;
            uint8_t t479 = ((((uint8_t *)&t478))[0] != 0);
            F.ret = t477;
            goto gm_fret;
        }
        goto me343;
mn452:;
ma347:;
        uint8_t t481 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t481 != ((uint8_t)9ULL)) goto mn480;
        void * t482 = gm_l0;
        gm_t88rcucg1b7uk t483 = gm_l5;
        gm_b16_8 t484 = gm_l25;
        gm_tnpifv20irnph t485;
        GM_ST(gm_b16_8, ((uint8_t *)&t485 + 0), t484);
        gm_tabb710d9mq2e t486;
        gm_f_format_6df13cd((void *)&t486, (void *)NULL, cx, NULL, t485);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t487 = t486;
        if (((((uint8_t *)&t487))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t487) + 8, gm_src2j6ljeja7ilp, 106, 35);
        gm_b16_8 t488 = GM_LD(gm_b16_8, (((uint8_t *)&t487) + 8));
        gm_b16_8 t489 = t488;
        gm_b16_8 t490 = GM_LD(gm_b16_8, ((uint8_t *)&t489));
        gm_b16_8 t491 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t482, t483, t490);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t492;
        GM_ST(uint8_t, (uint8_t *)&t492 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t492 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t492 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t492 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t492 + 32, 0);
        ((uint8_t *)&t492)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t492 + 8), t491);
        gm_t5o0tpv5jka2l t493 = t492;
        uint8_t t494 = ((((uint8_t *)&t493))[0] != 0);
        F.ret = t492;
        goto gm_fret;
        goto me343;
mn480:;
ma348:;
        uint8_t t496 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t496 != ((uint8_t)13ULL)) goto mn495;
        void * t497 = gm_l0;
        gm_t88rcucg1b7uk t498 = gm_l5;
        gm_b16_8 t499 = gm_l25;
        gm_tnpifv20irnph t500;
        GM_ST(gm_b16_8, ((uint8_t *)&t500 + 0), t499);
        gm_tabb710d9mq2e t501;
        gm_f_format_a7bth9i((void *)&t501, (void *)NULL, cx, NULL, t500);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t502 = t501;
        if (((((uint8_t *)&t502))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t502) + 8, gm_src2j6ljeja7ilp, 107, 36);
        gm_b16_8 t503 = GM_LD(gm_b16_8, (((uint8_t *)&t502) + 8));
        gm_b16_8 t504 = t503;
        gm_b16_8 t505 = GM_LD(gm_b16_8, ((uint8_t *)&t504));
        gm_b16_8 t506 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t497, t498, t505);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t507;
        GM_ST(uint8_t, (uint8_t *)&t507 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t507 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t507 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t507 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t507 + 32, 0);
        ((uint8_t *)&t507)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t507 + 8), t506);
        gm_t5o0tpv5jka2l t508 = t507;
        uint8_t t509 = ((((uint8_t *)&t508))[0] != 0);
        F.ret = t507;
        goto gm_fret;
        goto me343;
mn495:;
ma349:;
        uint8_t t513 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t513 != ((uint8_t)10ULL)) goto pa512;
        goto po511;
pa512:;
        uint8_t t515 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t515 != ((uint8_t)11ULL)) goto pa514;
        goto po511;
pa514:;
        uint8_t t517 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t517 != ((uint8_t)12ULL)) goto pa516;
        goto po511;
pa516:;
        goto mn510;
po511:;
        {
            void * t520 = gm_l0;
            void * t521 = GM_LD(void *, (((uint8_t *)t520) + 0));
            void * t522 = (void *)(((uint8_t *)t521) + 8);
            gm_t88rcucg1b7uk t523 = gm_l26;
            gm_t53at80pfdus3 t524;
            gm_f_kind_ghvav22((void *)&t524, t522, GM_CONV(gm_csguankko4lmhd, t523));
            gm_t53at80pfdus3 t525 = t524;
            uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&t525) + 0));
            if (t526 != ((uint8_t)9ULL)) goto ie519;
            gm_t88rcucg1b7uk t527 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t525) + 8) + 4));
            gm_l33 = t527;
            {
                void * t529 = gm_l0;
                void * t530 = GM_LD(void *, (((uint8_t *)t529) + 0));
                void * t531 = (void *)(((uint8_t *)t530) + 8);
                gm_t88rcucg1b7uk t532 = gm_l33;
                uint64_t t533 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t531, t532);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t533;
            }
            {
                gm_b16_8 t534 = gm_l25;
                gm_l35 = t534;
            }
            {
                uint64_t t535 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t536 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
                if (t535 >= (uintptr_t)(t536)) gm_trap_index(t535, t536, gm_src2j6ljeja7ilp, 112, 18);
                gm_b16_8 t537 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t535 * 16));
                gm_l36 = t537;
            }
            {
                uint8_t t539 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)12ULL)));
                if (!t539) goto ie538;
                {
                    uint64_t t542 = gm_l34;
                    uint8_t t543 = ((t542 == ((uint64_t)0ULL)));
                    if (!t543) goto ie541;
                    {
                        void * t545 = gm_l0;
                        gm_t88rcucg1b7uk t546 = gm_l5;
                        gm_b16_8 t547;
                        GM_ST(void *, (uint8_t *)&t547, (void *)(gm_sicgmpca63jln));
                        GM_ST(uintptr_t, (uint8_t *)&t547 + 8, (uintptr_t)(1));
                        gm_b16_8 t548 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t545, t546, t547);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t5o0tpv5jka2l t549;
                        GM_ST(uint8_t, (uint8_t *)&t549 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t549 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t549 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t549 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t549 + 32, 0);
                        ((uint8_t *)&t549)[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&t549 + 8), t548);
                        gm_t5o0tpv5jka2l t550 = t549;
                        uint8_t t551 = ((((uint8_t *)&t550))[0] != 0);
                        F.ret = t549;
                        goto gm_fret;
                    }
                    goto ix541;
ie541:;
ix541:;
                }
                {
                    void * t552 = gm_l0;
                    gm_t88rcucg1b7uk t553 = gm_l5;
                    gm_tftksr4op114e t554;
                    gm_b16_8 t555 = gm_l35;
                    GM_ST(gm_b16_8, (uint8_t *)&t554 + 0, t555);
                    gm_b16_8 t556 = gm_l36;
                    GM_ST(gm_b16_8, (uint8_t *)&t554 + 16, t556);
                    uint64_t t557 = gm_l34;
                    GM_ST(uint64_t, (uint8_t *)&t554 + 32, t557);
                    gm_tabb710d9mq2e t558;
                    gm_f_format_ms7m2d2((void *)&t558, (void *)NULL, cx, NULL, &t554);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t559 = t558;
                    if (((((uint8_t *)&t559))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t559) + 8, gm_src2j6ljeja7ilp, 115, 34);
                    gm_b16_8 t560 = GM_LD(gm_b16_8, (((uint8_t *)&t559) + 8));
                    gm_b16_8 t561 = t560;
                    gm_b16_8 t562 = GM_LD(gm_b16_8, ((uint8_t *)&t561));
                    gm_b16_8 t563 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t552, t553, t562);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t564;
                    GM_ST(uint8_t, (uint8_t *)&t564 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t564 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t564 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t564 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t564 + 32, 0);
                    ((uint8_t *)&t564)[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&t564 + 8), t563);
                    gm_t5o0tpv5jka2l t565 = t564;
                    uint8_t t566 = ((((uint8_t *)&t565))[0] != 0);
                    F.ret = t564;
                    goto gm_fret;
                }
                goto ix538;
ie538:;
ix538:;
            }
            {
                gm_b16_8 t567;
                uint8_t t569 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)10ULL)));
                if (!t569) goto ie568;
                gm_b16_8 t571;
                GM_ST(void *, (uint8_t *)&t571, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t571 + 8, (uintptr_t)(0));
                gm_b16_8 t572 = t571;
                t567 = t572;
                goto ix568;
ie568:;
                gm_b16_8 t574;
                GM_ST(void *, (uint8_t *)&t574, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t574 + 8, (uintptr_t)(1));
                gm_b16_8 t575 = t574;
                t567 = t575;
ix568:;
                gm_l37 = t567;
            }
            {
                void * t576 = gm_l0;
                gm_t88rcucg1b7uk t577 = gm_l5;
                gm_tttl9nsdo1iu6 t578;
                gm_b16_8 t579 = gm_l35;
                GM_ST(gm_b16_8, (uint8_t *)&t578 + 0, t579);
                gm_b16_8 t580 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t578 + 16, t580);
                gm_b16_8 t581 = gm_l36;
                GM_ST(gm_b16_8, (uint8_t *)&t578 + 32, t581);
                uint64_t t582 = gm_l34;
                GM_ST(uint64_t, (uint8_t *)&t578 + 48, t582);
                gm_tabb710d9mq2e t583;
                gm_f_format_83msomh((void *)&t583, (void *)NULL, cx, NULL, &t578);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t584 = t583;
                if (((((uint8_t *)&t584))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t584) + 8, gm_src2j6ljeja7ilp, 118, 30);
                gm_b16_8 t585 = GM_LD(gm_b16_8, (((uint8_t *)&t584) + 8));
                gm_b16_8 t586 = t585;
                gm_b16_8 t587 = GM_LD(gm_b16_8, ((uint8_t *)&t586));
                gm_b16_8 t588 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t576, t577, t587);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t589;
                GM_ST(uint8_t, (uint8_t *)&t589 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t589 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t589 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t589 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t589 + 32, 0);
                ((uint8_t *)&t589)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t589 + 8), t588);
                gm_t5o0tpv5jka2l t590 = t589;
                uint8_t t591 = ((((uint8_t *)&t590))[0] != 0);
                F.ret = t589;
                goto gm_fret;
            }
            goto ix519;
ie519:;
            {
                gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 109, 56);
            }
ix519:;
        }
        goto me343;
mn510:;
ma350:;
        uint8_t t594 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t594 != ((uint8_t)14ULL)) goto mn593;
        {
            void * t596 = gm_l0;
            gm_t88rcucg1b7uk t597 = gm_l26;
            gm_b16_8 t598 = gm_l25;
            gm_b16_8 t599 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t596, t597, t598);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l38 = t599;
        }
        {
            void * t600 = gm_l0;
            gm_t88rcucg1b7uk t601 = gm_l5;
            gm_b16_8 t602 = gm_l38;
            gm_b16_8 t603 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t600, t601, t602);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t604;
            GM_ST(uint8_t, (uint8_t *)&t604 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t604 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t604 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t604 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t604 + 32, 0);
            ((uint8_t *)&t604)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t604 + 8), t603);
            gm_t5o0tpv5jka2l t605 = t604;
            uint8_t t606 = ((((uint8_t *)&t605))[0] != 0);
            F.ret = t604;
            goto gm_fret;
        }
        goto me343;
mn593:;
ma351:;
        uint8_t t608 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t608 != ((uint8_t)15ULL)) goto mn607;
        void * t609 = gm_l0;
        void * t610 = gm_l1;
        gm_t88rcucg1b7uk t611 = gm_l26;
        gm_b16_8 t612 = gm_l25;
        gm_f_from_tag_2m7nfln((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t609, t610, t611, t612);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me343;
mn607:;
ma352:;
        uint8_t t616 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t616 != ((uint8_t)16ULL)) goto pa615;
        goto po614;
pa615:;
        uint8_t t618 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t618 != ((uint8_t)17ULL)) goto pa617;
        goto po614;
pa617:;
        goto mn613;
po614:;
        {
            gm_b16_8 t620;
            uint8_t t622 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)16ULL)));
            if (!t622) goto ie621;
            gm_b16_8 t624;
            GM_ST(void *, (uint8_t *)&t624, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t624 + 8, (uintptr_t)(1));
            gm_b16_8 t625 = t624;
            t620 = t625;
            goto ix621;
ie621:;
            gm_b16_8 t627;
            GM_ST(void *, (uint8_t *)&t627, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t627 + 8, (uintptr_t)(1));
            gm_b16_8 t628 = t627;
            t620 = t628;
ix621:;
            gm_l39 = t620;
        }
        {
            void * t630 = gm_l0;
            void * t631 = GM_LD(void *, (((uint8_t *)t630) + 0));
            void * t632 = (void *)(((uint8_t *)t631) + 8);
            gm_t88rcucg1b7uk t633 = gm_l26;
            gm_t5v8n6qrn4rh4 t634;
            gm_f_repr_53quqki((void *)&t634, (void *)NULL, cx, NULL, t632, t633);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t635 = t634;
            uint8_t t636;
            uint8_t t639 = GM_LD(uint8_t, (((uint8_t *)&t635) + 0));
            if (t639 != ((uint8_t)1ULL)) goto mn638;
            t636 = ((uint8_t)1);
            goto me637;
mn638:;
            t636 = ((uint8_t)0);
            goto me637;
mn640:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 127, 16);
me637:;
            if (!t636) goto ie629;
            {
                gm_b16_8 t642;
                uint8_t t644 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)16ULL)));
                if (!t644) goto ie643;
                gm_b16_8 t646;
                GM_ST(void *, (uint8_t *)&t646, (void *)(gm_s12ng38ve3205));
                GM_ST(uintptr_t, (uint8_t *)&t646 + 8, (uintptr_t)(7));
                gm_b16_8 t647 = t646;
                t642 = t647;
                goto ix643;
ie643:;
                gm_b16_8 t649;
                GM_ST(void *, (uint8_t *)&t649, (void *)(gm_sgjc0ebf56hf4));
                GM_ST(uintptr_t, (uint8_t *)&t649 + 8, (uintptr_t)(7));
                gm_b16_8 t650 = t649;
                t642 = t650;
ix643:;
                gm_l40 = t642;
            }
            {
                void * t651 = gm_l0;
                gm_t88rcucg1b7uk t652 = gm_l5;
                gm_tfeab279c16rr t653;
                gm_b16_8 t654 = gm_l40;
                GM_ST(gm_b16_8, (uint8_t *)&t653 + 0, t654);
                gm_b16_8 t655 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t653 + 16, t655);
                uint64_t t656 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t657 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
                if (t656 >= (uintptr_t)(t657)) gm_trap_index(t656, t657, gm_src2j6ljeja7ilp, 129, 87);
                gm_b16_8 t658 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t656 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t653 + 32, t658);
                gm_tabb710d9mq2e t659;
                gm_f_format_lqnh3lt((void *)&t659, (void *)NULL, cx, NULL, &t653);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t660 = t659;
                if (((((uint8_t *)&t660))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t660) + 8, gm_src2j6ljeja7ilp, 129, 39);
                gm_b16_8 t661 = GM_LD(gm_b16_8, (((uint8_t *)&t660) + 8));
                gm_b16_8 t662 = t661;
                gm_b16_8 t663 = GM_LD(gm_b16_8, ((uint8_t *)&t662));
                gm_b16_8 t664 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t651, t652, t663);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t664;
            }
            {
                void * t665 = gm_l0;
                gm_t88rcucg1b7uk t666 = gm_l5;
                gm_b16_8 t667 = gm_l41;
                gm_b16_8 t668 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t665, t666, t667);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t669;
                GM_ST(uint8_t, (uint8_t *)&t669 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t669 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t669 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t669 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t669 + 32, 0);
                ((uint8_t *)&t669)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t669 + 8), t668);
                gm_t5o0tpv5jka2l t670 = t669;
                uint8_t t671 = ((((uint8_t *)&t670))[0] != 0);
                F.ret = t669;
                goto gm_fret;
            }
            goto ix629;
ie629:;
ix629:;
        }
        {
            void * t672 = gm_l0;
            gm_t88rcucg1b7uk t673 = gm_l5;
            gm_tud7oovfvnf43 t674;
            gm_b16_8 t675 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t674 + 0, t675);
            gm_b16_8 t676 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t674 + 16, t676);
            uint64_t t677 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t678 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t677 >= (uintptr_t)(t678)) gm_trap_index(t677, t678, gm_src2j6ljeja7ilp, 132, 73);
            gm_b16_8 t679 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t677 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t674 + 32, t679);
            gm_b16_8 t680 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t674 + 48, t680);
            uint64_t t681 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t682 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t681 >= (uintptr_t)(t682)) gm_trap_index(t681, t682, gm_src2j6ljeja7ilp, 132, 92);
            gm_b16_8 t683 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t681 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t674 + 64, t683);
            gm_tabb710d9mq2e t684;
            gm_f_format_eqjcikj((void *)&t684, (void *)NULL, cx, NULL, &t674);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t685 = t684;
            if (((((uint8_t *)&t685))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t685) + 8, gm_src2j6ljeja7ilp, 132, 30);
            gm_b16_8 t686 = GM_LD(gm_b16_8, (((uint8_t *)&t685) + 8));
            gm_b16_8 t687 = t686;
            gm_b16_8 t688 = GM_LD(gm_b16_8, ((uint8_t *)&t687));
            gm_b16_8 t689 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t672, t673, t688);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t690;
            GM_ST(uint8_t, (uint8_t *)&t690 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t690 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t690 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t690 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t690 + 32, 0);
            ((uint8_t *)&t690)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t690 + 8), t689);
            gm_t5o0tpv5jka2l t691 = t690;
            uint8_t t692 = ((((uint8_t *)&t691))[0] != 0);
            F.ret = t690;
            goto gm_fret;
        }
        goto me343;
mn613:;
ma353:;
        uint8_t t694 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t694 != ((uint8_t)18ULL)) goto mn693;
        {
            gm_b16_8 t696 = gm_l25;
            gm_l42 = t696;
        }
        {
            uint64_t t697 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t698 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t697 >= (uintptr_t)(t698)) gm_trap_index(t697, t698, gm_src2j6ljeja7ilp, 136, 19);
            gm_b16_8 t699 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t697 * 16));
            gm_l43 = t699;
        }
        {
            uint64_t t700 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t701 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t700 >= (uintptr_t)(t701)) gm_trap_index(t700, t701, gm_src2j6ljeja7ilp, 137, 19);
            gm_b16_8 t702 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t700 * 16));
            gm_l44 = t702;
        }
        {
            void * t704 = gm_l0;
            void * t705 = GM_LD(void *, (((uint8_t *)t704) + 0));
            void * t706 = (void *)(((uint8_t *)t705) + 8);
            gm_t88rcucg1b7uk t707 = gm_l26;
            gm_t5v8n6qrn4rh4 t708;
            gm_f_repr_53quqki((void *)&t708, (void *)NULL, cx, NULL, t706, t707);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t709 = t708;
            uint8_t t710;
            uint8_t t713 = GM_LD(uint8_t, (((uint8_t *)&t709) + 0));
            if (t713 != ((uint8_t)1ULL)) goto mn712;
            t710 = ((uint8_t)1);
            goto me711;
mn712:;
            t710 = ((uint8_t)0);
            goto me711;
mn714:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 138, 16);
me711:;
            if (!t710) goto ie703;
            {
                void * t716 = gm_l0;
                gm_t88rcucg1b7uk t717 = gm_l5;
                gm_tfeab279c16rr t718;
                gm_b16_8 t719 = gm_l42;
                GM_ST(gm_b16_8, (uint8_t *)&t718 + 0, t719);
                gm_b16_8 t720 = gm_l43;
                GM_ST(gm_b16_8, (uint8_t *)&t718 + 16, t720);
                gm_b16_8 t721 = gm_l44;
                GM_ST(gm_b16_8, (uint8_t *)&t718 + 32, t721);
                gm_tabb710d9mq2e t722;
                gm_f_format_c57v5iu((void *)&t722, (void *)NULL, cx, NULL, &t718);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t723 = t722;
                if (((((uint8_t *)&t723))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t723) + 8, gm_src2j6ljeja7ilp, 139, 39);
                gm_b16_8 t724 = GM_LD(gm_b16_8, (((uint8_t *)&t723) + 8));
                gm_b16_8 t725 = t724;
                gm_b16_8 t726 = GM_LD(gm_b16_8, ((uint8_t *)&t725));
                gm_b16_8 t727 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t716, t717, t726);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l45 = t727;
            }
            {
                void * t728 = gm_l0;
                gm_t88rcucg1b7uk t729 = gm_l5;
                gm_b16_8 t730 = gm_l45;
                gm_b16_8 t731 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t728, t729, t730);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t732;
                GM_ST(uint8_t, (uint8_t *)&t732 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t732 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t732 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t732 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t732 + 32, 0);
                ((uint8_t *)&t732)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t732 + 8), t731);
                gm_t5o0tpv5jka2l t733 = t732;
                uint8_t t734 = ((((uint8_t *)&t733))[0] != 0);
                F.ret = t732;
                goto gm_fret;
            }
            goto ix703;
ie703:;
ix703:;
        }
        {
            void * t735 = gm_l0;
            gm_t88rcucg1b7uk t736 = gm_l5;
            gm_th2d9c7aju8hp t737;
            gm_b16_8 t738 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t737 + 0, t738);
            gm_b16_8 t739 = gm_l43;
            GM_ST(gm_b16_8, (uint8_t *)&t737 + 16, t739);
            gm_b16_8 t740 = gm_l43;
            GM_ST(gm_b16_8, (uint8_t *)&t737 + 32, t740);
            gm_b16_8 t741 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t737 + 48, t741);
            gm_b16_8 t742 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t737 + 64, t742);
            gm_b16_8 t743 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t737 + 80, t743);
            gm_b16_8 t744 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t737 + 96, t744);
            gm_tabb710d9mq2e t745;
            gm_f_format_jpvl2js((void *)&t745, (void *)NULL, cx, NULL, &t737);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t746 = t745;
            if (((((uint8_t *)&t746))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t746) + 8, gm_src2j6ljeja7ilp, 142, 30);
            gm_b16_8 t747 = GM_LD(gm_b16_8, (((uint8_t *)&t746) + 8));
            gm_b16_8 t748 = t747;
            gm_b16_8 t749 = GM_LD(gm_b16_8, ((uint8_t *)&t748));
            gm_b16_8 t750 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t735, t736, t749);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t751;
            GM_ST(uint8_t, (uint8_t *)&t751 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t751 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t751 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t751 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t751 + 32, 0);
            ((uint8_t *)&t751)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t751 + 8), t750);
            gm_t5o0tpv5jka2l t752 = t751;
            uint8_t t753 = ((((uint8_t *)&t752))[0] != 0);
            F.ret = t751;
            goto gm_fret;
        }
        goto me343;
mn693:;
ma354:;
        uint8_t t755 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t755 != ((uint8_t)19ULL)) goto mn754;
        {
            void * t758 = gm_l0;
            void * t759 = GM_LD(void *, (((uint8_t *)t758) + 0));
            void * t760 = (void *)(((uint8_t *)t759) + 8);
            gm_t88rcucg1b7uk t761 = gm_l26;
            gm_t5v8n6qrn4rh4 t762;
            gm_f_repr_53quqki((void *)&t762, (void *)NULL, cx, NULL, t760, t761);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t763 = t762;
            uint8_t t764;
            uint8_t t767 = GM_LD(uint8_t, (((uint8_t *)&t763) + 0));
            if (t767 != ((uint8_t)1ULL)) goto mn766;
            t764 = ((uint8_t)1);
            goto me765;
mn766:;
            t764 = ((uint8_t)0);
            goto me765;
mn768:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 145, 16);
me765:;
            if (!t764) goto ie757;
            {
                void * t770 = gm_l0;
                gm_t88rcucg1b7uk t771 = gm_l5;
                gm_tud7oovfvnf43 t772;
                gm_b16_8 t773 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t772 + 0, t773);
                gm_b16_8 t774 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t772 + 16, t774);
                gm_b16_8 t775 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t772 + 32, t775);
                gm_b16_8 t776 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t772 + 48, t776);
                gm_b16_8 t777 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t772 + 64, t777);
                gm_tabb710d9mq2e t778;
                gm_f_format_3j59e2u((void *)&t778, (void *)NULL, cx, NULL, &t772);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t779 = t778;
                if (((((uint8_t *)&t779))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t779) + 8, gm_src2j6ljeja7ilp, 146, 34);
                gm_b16_8 t780 = GM_LD(gm_b16_8, (((uint8_t *)&t779) + 8));
                gm_b16_8 t781 = t780;
                gm_b16_8 t782 = GM_LD(gm_b16_8, ((uint8_t *)&t781));
                gm_b16_8 t783 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t770, t771, t782);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t784;
                GM_ST(uint8_t, (uint8_t *)&t784 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t784 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t784 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t784 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t784 + 32, 0);
                ((uint8_t *)&t784)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t784 + 8), t783);
                gm_t5o0tpv5jka2l t785 = t784;
                uint8_t t786 = ((((uint8_t *)&t785))[0] != 0);
                F.ret = t784;
                goto gm_fret;
            }
            goto ix757;
ie757:;
ix757:;
        }
        {
            void * t787 = gm_l0;
            gm_t88rcucg1b7uk t788 = gm_l26;
            gm_tqcc6dmlne11b t789;
            gm_f_int_info_2tcdf1n((void *)&t789, (void *)NULL, cx, NULL, t787, t788);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t790 = t789;
            if (!((((uint8_t *)&t790))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 148, 19);
            gm_tuglhjjmj2g58 t791 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t790));
            gm_l46 = t791;
        }
        {
            uint8_t t793 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
            if (!t793) goto ie792;
            void * t794 = gm_l0;
            void * t795 = GM_LD(void *, (((uint8_t *)t794) + 0));
            uint8_t t796 = GM_LD(uint8_t, ((((uint8_t *)t795) + 1136) + 0));
            if (!t796) goto ie792;
            {
                void * t798 = gm_l0;
                gm_t88rcucg1b7uk t799 = gm_l26;
                gm_tuglhjjmj2g58 t800 = gm_l46;
                gm_tbci5ospk5nnn t801;
                gm_f_ii_min_lku7pdi((void *)&t801, (void *)NULL, cx, NULL, t800);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t802 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t798, t799, &t801);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l47 = t802;
            }
            {
                void * t803 = gm_l0;
                gm_tfeab279c16rr t804;
                gm_b16_8 t805 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t804 + 0, t805);
                gm_b16_8 t806 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t804 + 16, t806);
                gm_b16_8 t807 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t804 + 32, t807);
                gm_b0_1 t808 = gm_f_linef_qno93os((void *)NULL, cx, NULL, t803, &t804);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix792;
ie792:;
ix792:;
        }
        {
            void * t809 = gm_l0;
            gm_t88rcucg1b7uk t810 = gm_l5;
            gm_tfeab279c16rr t811;
            gm_b16_8 t812 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t811 + 0, t812);
            gm_b16_8 t813 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t811 + 16, t813);
            gm_b16_8 t814 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t811 + 32, t814);
            gm_tabb710d9mq2e t815;
            gm_f_format_iqjnutf((void *)&t815, (void *)NULL, cx, NULL, &t811);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t816 = t815;
            if (((((uint8_t *)&t816))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t816) + 8, gm_src2j6ljeja7ilp, 153, 30);
            gm_b16_8 t817 = GM_LD(gm_b16_8, (((uint8_t *)&t816) + 8));
            gm_b16_8 t818 = t817;
            gm_b16_8 t819 = GM_LD(gm_b16_8, ((uint8_t *)&t818));
            gm_b16_8 t820 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t809, t810, t819);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t821;
            GM_ST(uint8_t, (uint8_t *)&t821 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t821 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t821 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t821 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t821 + 32, 0);
            ((uint8_t *)&t821)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t821 + 8), t820);
            gm_t5o0tpv5jka2l t822 = t821;
            uint8_t t823 = ((((uint8_t *)&t822))[0] != 0);
            F.ret = t821;
            goto gm_fret;
        }
        goto me343;
mn754:;
ma355:;
        uint8_t t825 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t825 != ((uint8_t)20ULL)) goto mn824;
        {
            void * t827 = gm_l0;
            gm_t88rcucg1b7uk t828 = gm_l26;
            gm_tqcc6dmlne11b t829;
            gm_f_int_info_2tcdf1n((void *)&t829, (void *)NULL, cx, NULL, t827, t828);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t830 = t829;
            if (!((((uint8_t *)&t830))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 156, 19);
            gm_tuglhjjmj2g58 t831 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t830));
            gm_l48 = t831;
        }
        {
            gm_tuglhjjmj2g58 t832 = gm_l48;
            gm_b16_8 t833 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t832));
            gm_l49 = t833;
        }
        {
            gm_b16_8 t834 = gm_l25;
            gm_l50 = t834;
        }
        {
            uint64_t t835 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t836 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t835 >= (uintptr_t)(t836)) gm_trap_index(t835, t836, gm_src2j6ljeja7ilp, 159, 18);
            gm_b16_8 t837 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t835 * 16));
            gm_l51 = t837;
        }
        {
            void * t838 = gm_l0;
            gm_t88rcucg1b7uk t839 = gm_l5;
            gm_tk6rvhh54g86p t840;
            gm_b16_8 t841 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 0, t841);
            gm_b16_8 t842 = gm_l51;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 16, t842);
            gm_b16_8 t843 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 32, t843);
            gm_b16_8 t844 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 48, t844);
            gm_b16_8 t845 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 64, t845);
            gm_b16_8 t846 = gm_l51;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 80, t846);
            gm_b16_8 t847 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 96, t847);
            gm_b16_8 t848 = gm_l51;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 112, t848);
            gm_b16_8 t849 = gm_l49;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 128, t849);
            gm_b16_8 t850 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t840 + 144, t850);
            gm_tabb710d9mq2e t851;
            gm_f_format_53mv29j((void *)&t851, (void *)NULL, cx, NULL, &t840);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t852 = t851;
            if (((((uint8_t *)&t852))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t852) + 8, gm_src2j6ljeja7ilp, 160, 30);
            gm_b16_8 t853 = GM_LD(gm_b16_8, (((uint8_t *)&t852) + 8));
            gm_b16_8 t854 = t853;
            gm_b16_8 t855 = GM_LD(gm_b16_8, ((uint8_t *)&t854));
            gm_b16_8 t856 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t838, t839, t855);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t857;
            GM_ST(uint8_t, (uint8_t *)&t857 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t857 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t857 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t857 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t857 + 32, 0);
            ((uint8_t *)&t857)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t857 + 8), t856);
            gm_t5o0tpv5jka2l t858 = t857;
            uint8_t t859 = ((((uint8_t *)&t858))[0] != 0);
            F.ret = t857;
            goto gm_fret;
        }
        goto me343;
mn824:;
ma356:;
        uint8_t t861 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t861 != ((uint8_t)22ULL)) goto mn860;
        void * t862 = gm_l0;
        gm_t88rcucg1b7uk t863 = gm_l5;
        gm_b16_8 t864 = gm_l25;
        gm_tnpifv20irnph t865;
        GM_ST(gm_b16_8, ((uint8_t *)&t865 + 0), t864);
        gm_tabb710d9mq2e t866;
        gm_f_format_o73frof((void *)&t866, (void *)NULL, cx, NULL, t865);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t867 = t866;
        if (((((uint8_t *)&t867))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t867) + 8, gm_src2j6ljeja7ilp, 162, 36);
        gm_b16_8 t868 = GM_LD(gm_b16_8, (((uint8_t *)&t867) + 8));
        gm_b16_8 t869 = t868;
        gm_b16_8 t870 = GM_LD(gm_b16_8, ((uint8_t *)&t869));
        gm_b16_8 t871 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t862, t863, t870);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t872;
        GM_ST(uint8_t, (uint8_t *)&t872 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t872 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t872 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t872 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t872 + 32, 0);
        ((uint8_t *)&t872)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t872 + 8), t871);
        gm_t5o0tpv5jka2l t873 = t872;
        uint8_t t874 = ((((uint8_t *)&t873))[0] != 0);
        F.ret = t872;
        goto gm_fret;
        goto me343;
mn860:;
ma357:;
        uint8_t t876 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t876 != ((uint8_t)23ULL)) goto mn875;
        void * t877 = gm_l0;
        gm_t88rcucg1b7uk t878 = gm_l5;
        gm_b16_8 t879 = gm_l25;
        gm_tnpifv20irnph t880;
        GM_ST(gm_b16_8, ((uint8_t *)&t880 + 0), t879);
        gm_tabb710d9mq2e t881;
        gm_f_format_ljsk7ji((void *)&t881, (void *)NULL, cx, NULL, t880);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t882 = t881;
        if (((((uint8_t *)&t882))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t882) + 8, gm_src2j6ljeja7ilp, 163, 36);
        gm_b16_8 t883 = GM_LD(gm_b16_8, (((uint8_t *)&t882) + 8));
        gm_b16_8 t884 = t883;
        gm_b16_8 t885 = GM_LD(gm_b16_8, ((uint8_t *)&t884));
        gm_b16_8 t886 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t877, t878, t885);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t887;
        GM_ST(uint8_t, (uint8_t *)&t887 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t887 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t887 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t887 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t887 + 32, 0);
        ((uint8_t *)&t887)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t887 + 8), t886);
        gm_t5o0tpv5jka2l t888 = t887;
        uint8_t t889 = ((((uint8_t *)&t888))[0] != 0);
        F.ret = t887;
        goto gm_fret;
        goto me343;
mn875:;
ma358:;
        uint8_t t891 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t891 != ((uint8_t)24ULL)) goto mn890;
        void * t892 = gm_l0;
        gm_t88rcucg1b7uk t893 = gm_l5;
        gm_b16_8 t894 = gm_l25;
        gm_tnpifv20irnph t895;
        GM_ST(gm_b16_8, ((uint8_t *)&t895 + 0), t894);
        gm_tabb710d9mq2e t896;
        gm_f_format_jj2aipm((void *)&t896, (void *)NULL, cx, NULL, t895);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t897 = t896;
        if (((((uint8_t *)&t897))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t897) + 8, gm_src2j6ljeja7ilp, 164, 39);
        gm_b16_8 t898 = GM_LD(gm_b16_8, (((uint8_t *)&t897) + 8));
        gm_b16_8 t899 = t898;
        gm_b16_8 t900 = GM_LD(gm_b16_8, ((uint8_t *)&t899));
        gm_b16_8 t901 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t892, t893, t900);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t902;
        GM_ST(uint8_t, (uint8_t *)&t902 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t902 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t902 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t902 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t902 + 32, 0);
        ((uint8_t *)&t902)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t902 + 8), t901);
        gm_t5o0tpv5jka2l t903 = t902;
        uint8_t t904 = ((((uint8_t *)&t903))[0] != 0);
        F.ret = t902;
        goto gm_fret;
        goto me343;
mn890:;
ma359:;
        uint8_t t906 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t906 != ((uint8_t)25ULL)) goto mn905;
        void * t907 = gm_l0;
        gm_t88rcucg1b7uk t908 = gm_l5;
        gm_b16_8 t909 = gm_l25;
        gm_tnpifv20irnph t910;
        GM_ST(gm_b16_8, ((uint8_t *)&t910 + 0), t909);
        gm_tabb710d9mq2e t911;
        gm_f_format_ckgfhm4((void *)&t911, (void *)NULL, cx, NULL, t910);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t912 = t911;
        if (((((uint8_t *)&t912))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t912) + 8, gm_src2j6ljeja7ilp, 165, 38);
        gm_b16_8 t913 = GM_LD(gm_b16_8, (((uint8_t *)&t912) + 8));
        gm_b16_8 t914 = t913;
        gm_b16_8 t915 = GM_LD(gm_b16_8, ((uint8_t *)&t914));
        gm_b16_8 t916 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t907, t908, t915);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t917;
        GM_ST(uint8_t, (uint8_t *)&t917 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t917 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t917 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t917 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t917 + 32, 0);
        ((uint8_t *)&t917)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t917 + 8), t916);
        gm_t5o0tpv5jka2l t918 = t917;
        uint8_t t919 = ((((uint8_t *)&t918))[0] != 0);
        F.ret = t917;
        goto gm_fret;
        goto me343;
mn905:;
ma360:;
        uint8_t t921 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t921 != ((uint8_t)26ULL)) goto mn920;
        {
            gm_b16_8 t923;
            void * t925 = gm_l0;
            void * t926 = GM_LD(void *, (((uint8_t *)t925) + 0));
            void * t927 = (void *)(((uint8_t *)t926) + 8);
            gm_t88rcucg1b7uk t928 = gm_l26;
            gm_t53at80pfdus3 t929;
            gm_f_kind_ghvav22((void *)&t929, t927, GM_CONV(gm_csguankko4lmhd, t928));
            gm_t53at80pfdus3 t930 = t929;
            uint8_t t931 = (((GM_LD(uint8_t, ((uint8_t *)&t930) + 0) == ((uint8_t)3ULL)) && (GM_LD(uint16_t, (((uint8_t *)&t930) + 8)) == ((uint16_t)32ULL))));
            if (!t931) goto ie924;
            gm_b16_8 t933;
            GM_ST(void *, (uint8_t *)&t933, (void *)(gm_s63o5blu3j46d));
            GM_ST(uintptr_t, (uint8_t *)&t933 + 8, (uintptr_t)(15));
            gm_b16_8 t934 = t933;
            t923 = t934;
            goto ix924;
ie924:;
            gm_b16_8 t936;
            GM_ST(void *, (uint8_t *)&t936, (void *)(gm_s565fgm0tsngr));
            GM_ST(uintptr_t, (uint8_t *)&t936 + 8, (uintptr_t)(14));
            gm_b16_8 t937 = t936;
            t923 = t937;
ix924:;
            gm_l52 = t923;
        }
        {
            void * t938 = gm_l0;
            gm_t88rcucg1b7uk t939 = gm_l5;
            gm_t6j6smj7qoctu t940;
            gm_b16_8 t941 = gm_l52;
            GM_ST(gm_b16_8, (uint8_t *)&t940 + 0, t941);
            gm_b16_8 t942 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t940 + 16, t942);
            gm_tabb710d9mq2e t943;
            gm_f_format_nm3fl8o((void *)&t943, (void *)NULL, cx, NULL, &t940);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t944 = t943;
            if (((((uint8_t *)&t944))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t944) + 8, gm_src2j6ljeja7ilp, 168, 35);
            gm_b16_8 t945 = GM_LD(gm_b16_8, (((uint8_t *)&t944) + 8));
            gm_b16_8 t946 = t945;
            gm_b16_8 t947 = GM_LD(gm_b16_8, ((uint8_t *)&t946));
            gm_b16_8 t948 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t938, t939, t947);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l53 = t948;
        }
        {
            void * t949 = gm_l0;
            gm_t88rcucg1b7uk t950 = gm_l5;
            gm_b16_8 t951 = gm_l53;
            gm_b16_8 t952 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t949, t950, t951);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t953;
            GM_ST(uint8_t, (uint8_t *)&t953 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t953 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t953 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t953 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t953 + 32, 0);
            ((uint8_t *)&t953)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t953 + 8), t952);
            gm_t5o0tpv5jka2l t954 = t953;
            uint8_t t955 = ((((uint8_t *)&t954))[0] != 0);
            F.ret = t953;
            goto gm_fret;
        }
        goto me343;
mn920:;
ma361:;
        uint8_t t957 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t957 != ((uint8_t)27ULL)) goto mn956;
        {
            gm_b16_8 t959;
            void * t961 = gm_l0;
            void * t962 = GM_LD(void *, (((uint8_t *)t961) + 0));
            void * t963 = (void *)(((uint8_t *)t962) + 8);
            gm_t88rcucg1b7uk t964 = gm_l26;
            gm_t53at80pfdus3 t965;
            gm_f_kind_ghvav22((void *)&t965, t963, GM_CONV(gm_csguankko4lmhd, t964));
            gm_t53at80pfdus3 t966 = t965;
            uint8_t t967 = (((GM_LD(uint8_t, ((uint8_t *)&t966) + 0) == ((uint8_t)3ULL)) && (GM_LD(uint16_t, (((uint8_t *)&t966) + 8)) == ((uint16_t)32ULL))));
            if (!t967) goto ie960;
            gm_b16_8 t969;
            GM_ST(void *, (uint8_t *)&t969, (void *)(gm_svpr02j6er8vd));
            GM_ST(uintptr_t, (uint8_t *)&t969 + 8, (uintptr_t)(14));
            gm_b16_8 t970 = t969;
            t959 = t970;
            goto ix960;
ie960:;
            gm_b16_8 t972;
            GM_ST(void *, (uint8_t *)&t972, (void *)(gm_shhrbleiv8o3j));
            GM_ST(uintptr_t, (uint8_t *)&t972 + 8, (uintptr_t)(13));
            gm_b16_8 t973 = t972;
            t959 = t973;
ix960:;
            gm_l54 = t959;
        }
        {
            void * t974 = gm_l0;
            gm_t88rcucg1b7uk t975 = gm_l5;
            gm_t0uuhaljsf410 t976;
            gm_b16_8 t977 = gm_l54;
            GM_ST(gm_b16_8, (uint8_t *)&t976 + 0, t977);
            gm_b16_8 t978 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t976 + 16, t978);
            uint64_t t979 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t980 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t979 >= (uintptr_t)(t980)) gm_trap_index(t979, t980, gm_src2j6ljeja7ilp, 173, 71);
            gm_b16_8 t981 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t979 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t976 + 32, t981);
            uint64_t t982 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t983 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t982 >= (uintptr_t)(t983)) gm_trap_index(t982, t983, gm_src2j6ljeja7ilp, 173, 86);
            gm_b16_8 t984 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t982 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t976 + 48, t984);
            gm_tabb710d9mq2e t985;
            gm_f_format_261kv0l((void *)&t985, (void *)NULL, cx, NULL, &t976);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t986 = t985;
            if (((((uint8_t *)&t986))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t986) + 8, gm_src2j6ljeja7ilp, 173, 35);
            gm_b16_8 t987 = GM_LD(gm_b16_8, (((uint8_t *)&t986) + 8));
            gm_b16_8 t988 = t987;
            gm_b16_8 t989 = GM_LD(gm_b16_8, ((uint8_t *)&t988));
            gm_b16_8 t990 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t974, t975, t989);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l55 = t990;
        }
        {
            void * t991 = gm_l0;
            gm_t88rcucg1b7uk t992 = gm_l5;
            gm_b16_8 t993 = gm_l55;
            gm_b16_8 t994 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t991, t992, t993);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t995;
            GM_ST(uint8_t, (uint8_t *)&t995 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t995 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t995 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t995 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t995 + 32, 0);
            ((uint8_t *)&t995)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t995 + 8), t994);
            gm_t5o0tpv5jka2l t996 = t995;
            uint8_t t997 = ((((uint8_t *)&t996))[0] != 0);
            F.ret = t995;
            goto gm_fret;
        }
        goto me343;
mn956:;
ma362:;
        uint8_t t1001 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1001 != ((uint8_t)32ULL)) goto pa1000;
        goto po999;
pa1000:;
        uint8_t t1003 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1003 != ((uint8_t)33ULL)) goto pa1002;
        goto po999;
pa1002:;
        goto mn998;
po999:;
        void * t1004 = gm_l0;
        void * t1005 = gm_l1;
        gm_tvdg94bk7aa22 t1006 = gm_l2;
        gm_t88rcucg1b7uk t1007 = gm_l26;
        gm_b16_8 t1008 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_f_rotate_tpbpt1c((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t1004, t1005, t1006, t1007, t1008);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me343;
mn998:;
ma363:;
        uint8_t t1012 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1012 != ((uint8_t)28ULL)) goto pa1011;
        goto po1010;
pa1011:;
        uint8_t t1014 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1014 != ((uint8_t)29ULL)) goto pa1013;
        goto po1010;
pa1013:;
        uint8_t t1016 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1016 != ((uint8_t)30ULL)) goto pa1015;
        goto po1010;
pa1015:;
        uint8_t t1018 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1018 != ((uint8_t)31ULL)) goto pa1017;
        goto po1010;
pa1017:;
        uint8_t t1020 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1020 != ((uint8_t)34ULL)) goto pa1019;
        goto po1010;
pa1019:;
        goto mn1009;
po1010:;
        void * t1021 = gm_l0;
        void * t1022 = gm_l1;
        gm_tvdg94bk7aa22 t1023 = gm_l2;
        gm_t88rcucg1b7uk t1024 = gm_l26;
        gm_b16_8 t1025 = gm_l25;
        gm_f_bit_count_77uni44((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t1021, t1022, t1023, t1024, t1025);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me343;
mn1009:;
ma364:;
        uint8_t t1027 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1027 != ((uint8_t)21ULL)) goto mn1026;
        gm_t9qkh0rsooc8d t1028 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
        gm_l56 = t1028;
        void * t1029 = gm_l0;
        void * t1030 = gm_l1;
        gm_t9qkh0rsooc8d t1031 = gm_l56;
        gm_t88rcucg1b7uk t1032 = gm_l26;
        gm_b16_8 t1033 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_f_pow_s0lbtt4((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t1029, t1030, t1031, t1032, t1033);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me343;
mn1026:;
ma365:;
        uint8_t t1035 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1035 != ((uint8_t)60ULL)) goto mn1034;
        void * t1036 = gm_l0;
        gm_t88rcucg1b7uk t1037 = gm_l5;
        gm_b16_8 t1038 = gm_l25;
        gm_tnpifv20irnph t1039;
        GM_ST(gm_b16_8, ((uint8_t *)&t1039 + 0), t1038);
        gm_tabb710d9mq2e t1040;
        gm_f_format_68bbqsa((void *)&t1040, (void *)NULL, cx, NULL, t1039);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t1041 = t1040;
        if (((((uint8_t *)&t1041))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1041) + 8, gm_src2j6ljeja7ilp, 179, 38);
        gm_b16_8 t1042 = GM_LD(gm_b16_8, (((uint8_t *)&t1041) + 8));
        gm_b16_8 t1043 = t1042;
        gm_b16_8 t1044 = GM_LD(gm_b16_8, ((uint8_t *)&t1043));
        gm_b16_8 t1045 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t1036, t1037, t1044);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t1046;
        GM_ST(uint8_t, (uint8_t *)&t1046 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t1046 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t1046 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t1046 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t1046 + 32, 0);
        ((uint8_t *)&t1046)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t1046 + 8), t1045);
        gm_t5o0tpv5jka2l t1047 = t1046;
        uint8_t t1048 = ((((uint8_t *)&t1047))[0] != 0);
        F.ret = t1046;
        goto gm_fret;
        goto me343;
mn1034:;
ma366:;
        uint8_t t1052 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1052 != ((uint8_t)64ULL)) goto pa1051;
        goto po1050;
pa1051:;
        uint8_t t1054 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1054 != ((uint8_t)65ULL)) goto pa1053;
        goto po1050;
pa1053:;
        uint8_t t1056 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1056 != ((uint8_t)66ULL)) goto pa1055;
        goto po1050;
pa1055:;
        uint8_t t1058 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1058 != ((uint8_t)67ULL)) goto pa1057;
        goto po1050;
pa1057:;
        uint8_t t1060 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1060 != ((uint8_t)68ULL)) goto pa1059;
        goto po1050;
pa1059:;
        uint8_t t1062 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1062 != ((uint8_t)70ULL)) goto pa1061;
        goto po1050;
pa1061:;
        uint8_t t1064 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1064 != ((uint8_t)71ULL)) goto pa1063;
        goto po1050;
pa1063:;
        goto mn1049;
po1050:;
        void * t1065 = gm_l0;
        void * t1066 = gm_l1;
        gm_tvdg94bk7aa22 t1067 = gm_l2;
        gm_b16_8 t1068 = gm_l3;
        gm_b16_8 t1069 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_f_atomic_plj0t12((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t1065, t1066, t1067, t1068, t1069);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me343;
mn1049:;
ma367:;
        uint8_t t1071 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1071 != ((uint8_t)69ULL)) goto mn1070;
        {
            void * t1073 = gm_l0;
            gm_t88rcucg1b7uk t1074 = gm_l26;
            gm_b16_8 t1075 = gm_l25;
            gm_b16_8 t1076 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t1073, t1074, t1075);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l57 = t1076;
        }
        {
            void * t1077 = gm_l0;
            gm_b16_8 t1078 = gm_l57;
            gm_tnpifv20irnph t1079;
            GM_ST(gm_b16_8, ((uint8_t *)&t1079 + 0), t1078);
            gm_b0_1 t1080 = gm_f_linef_di9ea86((void *)NULL, cx, NULL, t1077, t1079);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t1081 = gm_l0;
            gm_b16_8 t1082 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t1081);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t1083;
            GM_ST(uint8_t, (uint8_t *)&t1083 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1083 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1083 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1083 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t1083 + 32, 0);
            ((uint8_t *)&t1083)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t1083 + 8), t1082);
            gm_t5o0tpv5jka2l t1084 = t1083;
            uint8_t t1085 = ((((uint8_t *)&t1084))[0] != 0);
            F.ret = t1083;
            goto gm_fret;
        }
        goto me343;
mn1070:;
ma368:;
        uint8_t t1087 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1087 != ((uint8_t)35ULL)) goto mn1086;
        void * t1088 = gm_l0;
        void * t1089 = gm_l1;
        gm_b16_8 t1090 = gm_l25;
        gm_f_black_box_r6deojc((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t1088, t1089, t1090);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me343;
mn1086:;
ma369:;
        uint8_t t1092 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t1092 != ((uint8_t)62ULL)) goto mn1091;
        {
            void * t1094 = gm_l0;
            void * t1095 = GM_LD(void *, (((uint8_t *)t1094) + 0));
            void * t1096 = (void *)(((uint8_t *)t1095) + 1968);
            void * t1097 = gm_l0;
            void * t1098 = GM_LD(void *, (((uint8_t *)t1097) + 0));
            void * t1099 = (void *)(((uint8_t *)t1098) + 8);
            gm_t88rcucg1b7uk t1100 = gm_l26;
            gm_b16_8 t1101 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t1096, t1099, t1100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l58 = t1101;
        }
        {
            void * t1102 = gm_l0;
            gm_t88rcucg1b7uk t1103 = gm_l26;
            gm_b16_8 t1104 = gm_l25;
            gm_b16_8 t1105 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t1102, t1103, t1104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l59 = t1105;
        }
        {
            void * t1106 = gm_l0;
            gm_t88rcucg1b7uk t1107 = gm_l5;
            gm_tfeab279c16rr t1108;
            gm_b16_8 t1109 = gm_l58;
            GM_ST(gm_b16_8, (uint8_t *)&t1108 + 0, t1109);
            gm_b16_8 t1110 = gm_l59;
            GM_ST(gm_b16_8, (uint8_t *)&t1108 + 16, t1110);
            uint64_t t1111 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t1112 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
            if (t1111 >= (uintptr_t)(t1112)) gm_trap_index(t1111, t1112, gm_src2j6ljeja7ilp, 190, 95);
            gm_b16_8 t1113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 0)) + t1111 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t1108 + 32, t1113);
            gm_tabb710d9mq2e t1114;
            gm_f_format_3vgp4uu((void *)&t1114, (void *)NULL, cx, NULL, &t1108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t1115 = t1114;
            if (((((uint8_t *)&t1115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1115) + 8, gm_src2j6ljeja7ilp, 190, 30);
            gm_b16_8 t1116 = GM_LD(gm_b16_8, (((uint8_t *)&t1115) + 8));
            gm_b16_8 t1117 = t1116;
            gm_b16_8 t1118 = GM_LD(gm_b16_8, ((uint8_t *)&t1117));
            gm_b16_8 t1119 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t1106, t1107, t1118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t1120;
            GM_ST(uint8_t, (uint8_t *)&t1120 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1120 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1120 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1120 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t1120 + 32, 0);
            ((uint8_t *)&t1120)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t1120 + 8), t1119);
            gm_t5o0tpv5jka2l t1121 = t1120;
            uint8_t t1122 = ((((uint8_t *)&t1121))[0] != 0);
            F.ret = t1120;
            goto gm_fret;
        }
        goto me343;
mn1091:;
ma370:;
        void * t1124 = gm_l1;
        gm_tl874t2atnlh3 t1125 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1124) + 208));
        gm_b16_8 t1126;
        GM_ST(void *, (uint8_t *)&t1126, (void *)(gm_sv7lsha1e0i8r));
        GM_ST(uintptr_t, (uint8_t *)&t1126 + 8, (uintptr_t)(12));
        gm_t08olq97rscn3 t1127;
        gm_f_unsupported_531q51r((void *)&t1127, (void *)NULL, cx, NULL, t1125, t1126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t1128;
        GM_ST(uint8_t, (uint8_t *)&t1128 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t1128 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t1128 + 4, 0);
        ((uint8_t *)&t1128)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t1128 + 8), t1127);
        gm_t5o0tpv5jka2l t1129 = t1128;
        uint8_t t1130 = ((((uint8_t *)&t1129))[0] != 0);
        F.ret = t1128;
        goto gm_fret;
        goto me343;
mn1123:;
ma371:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 84, 5);
me343:;
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
__attribute__((aligned(4))) void gm_f_convert_lqrk2n0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_convert_lqrk2n0 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* src */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l6; /* target */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_b16_8 gm_l8; /* loc */
    uint8_t gm_l9; /* src_int */
    gm_b16_8 gm_l10; /* src_val */
    gm_t88rcucg1b7uk gm_l11; /* tag */
    gm_b16_8 gm_l12; /* a */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_b16_8 gm_l15; /* a */
    gm_tuglhjjmj2g58 gm_l16; /* ii */
    gm_b16_8 gm_l17; /* c */
    uint8_t gm_l18; /* signed */
    uint8_t gm_l19; /* si */
    gm_b16_8 gm_l20; /* fits */
    gm_b16_8 gm_l21; /* wrapped */
    gm_b16_8 gm_l22; /* res */
    gm_b16_8 gm_l23; /* w */
    gm_b16_8 gm_l24; /* some */
    gm_b16_8 gm_l25; /* none */
    gm_b16_8 gm_l26; /* min */
    gm_b16_8 gm_l27; /* max */
    gm_b16_8 gm_l28; /* neg */
    void * gm_l29; /* tys */
    gm_t88rcucg1b7uk gm_l30; /* f64_ty */
    gm_b16_8 gm_l31; /* x */
    gm_b16_8 gm_l32; /* rounded */
    gm_b16_8 gm_l33; /* r */
    gm_b16_8 gm_l34; /* lo */
    gm_b16_8 gm_l35; /* hi */
    gm_b16_8 gm_l36; /* in_range */
    gm_b16_8 gm_l37; /* res */
    gm_b16_8 gm_l38; /* w */
    gm_b16_8 gm_l39; /* some */
    gm_b16_8 gm_l40; /* none */
    gm_b16_8 gm_l41; /* min */
    gm_b16_8 gm_l42; /* max */
    gm_b16_8 gm_l43; /* r */
    gm_tcmkompbl09il gm_l44; /* bl */
    gm_tuglhjjmj2g58 gm_l45; /* ui */
    gm_b16_8 gm_l46; /* w */
    gm_b16_8 gm_l47; /* b */
    GM_STACK_CHECK(&F, 8409u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        gm_t88rcucg1b7uk t4 = gm_l5;
        gm_l6 = t4;
    }
    {
        uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
        if (!t6) goto ie5;
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_l5;
            gm_t53at80pfdus3 t13;
            gm_f_kind_ghvav22((void *)&t13, t11, GM_CONV(gm_csguankko4lmhd, t12));
            gm_t53at80pfdus3 t14 = t13;
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
            if (t15 != ((uint8_t)13ULL)) goto ie8;
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t14) + 8));
            gm_l7 = t16;
            {
                gm_t88rcucg1b7uk t18 = gm_l7;
                gm_l6 = t18;
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l1;
        gm_b16_8 t21 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t21;
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(0));
        gm_l10 = t22;
    }
    {
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = (void *)(((uint8_t *)t24) + 8);
        gm_t88rcucg1b7uk t26 = gm_l3;
        gm_t5v8n6qrn4rh4 t27;
        gm_f_repr_53quqki((void *)&t27, (void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t28 = t27;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma32; case ((uint8_t)2ULL): goto ma31; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma33; default: goto ma34; }
ma30:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t38 != ((uint8_t)0ULL)) goto mn37;
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            gm_b16_8 t40 = gm_l4;
            gm_l10 = t40;
        }
        goto me29;
mn37:;
ma31:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t42 != ((uint8_t)2ULL)) goto mn41;
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            gm_b16_8 t44 = gm_l4;
            gm_tnpifv20irnph t45;
            GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t44);
            gm_tabb710d9mq2e t46;
            gm_f_format_p1ca3i8((void *)&t46, (void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t47 = t46;
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_src2j6ljeja7ilp, 522, 23);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
            gm_b16_8 t49 = t48;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_l10 = t50;
        }
        goto me29;
mn41:;
ma32:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t52 != ((uint8_t)1ULL)) goto mn51;
        {
            gm_b16_8 t54 = gm_l4;
            gm_tnpifv20irnph t55;
            GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
            gm_tabb710d9mq2e t56;
            gm_f_format_gp2h96m((void *)&t56, (void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t57 = t56;
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_src2j6ljeja7ilp, 524, 34);
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
            gm_b16_8 t59 = t58;
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
            gm_l10 = t60;
        }
        goto me29;
mn51:;
ma33:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t62 != ((uint8_t)6ULL)) goto mn61;
        void * t63 = gm_l0;
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
        void * t65 = (void *)(((uint8_t *)t64) + 8);
        gm_t88rcucg1b7uk t66 = gm_l3;
        gm_t53at80pfdus3 t67;
        gm_f_kind_ghvav22((void *)&t67, t65, GM_CONV(gm_csguankko4lmhd, t66));
        gm_t53at80pfdus3 t68 = t67;
        uint8_t t69;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
        if (t72 != ((uint8_t)17ULL)) goto mn71;
        t69 = ((uint8_t)1);
        goto me70;
mn71:;
        t69 = ((uint8_t)0);
        goto me70;
mn73:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 525, 18);
me70:;
        if (!t69) goto mn61;
        void * t74 = gm_l0;
        void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
        void * t76 = (void *)(((uint8_t *)t75) + 8);
        gm_t88rcucg1b7uk t77 = gm_l3;
        void * t78 = gm_f_adt_of_gej99vn(t76, GM_CONV(gm_csguankko4lmhd, t77));
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t78) + 24) + 0));
        switch (t84) { case ((uint8_t)0ULL): goto ma81; case ((uint8_t)1ULL): goto ma80; case ((uint8_t)2ULL): goto ma82; default: goto ma82; }
ma80:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t78) + 24) + 0));
        if (t86 != ((uint8_t)1ULL)) goto mn85;
        gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t78) + 24) + 8) + 0));
        gm_l11 = t87;
        {
            void * t89 = gm_l0;
            gm_t88rcucg1b7uk t90 = gm_l3;
            gm_b16_8 t91 = gm_l4;
            gm_b16_8 t92 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t89, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t92;
        }
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l11;
            gm_b16_8 t95 = gm_l12;
            gm_b16_8 t96 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t93, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t96;
        }
        goto me79;
mn85:;
ma81:;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t78) + 24) + 0));
        if (t98 != ((uint8_t)0ULL)) goto mn97;
        gm_tpautr4br7h3l t99 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t78) + 24) + 8));
        F.l13 = t99;
        {
            gm_tkn42u7na3t81 t101 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l13) + 40));
            gm_tkn42u7na3t81 t102 = t101;
            gm_t88rcucg1b7uk t103;
            if (((((uint8_t *)&t102))[4] != 0)) {
                gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t102) + 0));
                t103 = t104;
            } else {
                void * t105 = gm_l1;
                gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t105) + 208));
                void * t107 = gm_l0;
                void * t108 = GM_LD(void *, (((uint8_t *)t107) + 0));
                void * t109 = (void *)(((uint8_t *)t108) + 8);
                gm_t88rcucg1b7uk t110 = gm_l3;
                gm_b16_8 t111 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t109, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t112;
                GM_ST(gm_b16_8, ((uint8_t *)&t112 + 0), t111);
                gm_tabb710d9mq2e t113;
                gm_f_format_m0j4lkh((void *)&t113, (void *)NULL, cx, NULL, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t114 = t113;
                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_src2j6ljeja7ilp, 532, 63);
                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                gm_b16_8 t116 = t115;
                gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                gm_t08olq97rscn3 t118;
                gm_f_unsupported_531q51r((void *)&t118, (void *)NULL, cx, NULL, t106, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t119;
                GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                ((uint8_t *)&t119)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t119 + 8), t118);
                gm_t5o0tpv5jka2l t120 = t119;
                uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
                F.ret = t119;
                goto gm_fret;
            }
            gm_l14 = t103;
        }
        {
            void * t122 = gm_l0;
            gm_t88rcucg1b7uk t123 = gm_l3;
            gm_b16_8 t124 = gm_l4;
            gm_b16_8 t125 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t122, t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t125;
        }
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            void * t126 = gm_l0;
            gm_t88rcucg1b7uk t127 = gm_l14;
            gm_b16_8 t128 = gm_l15;
            gm_b16_8 t129 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t129;
        }
        goto me79;
mn97:;
ma82:;
        void * t131 = gm_l1;
        gm_tl874t2atnlh3 t132 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t131) + 208));
        void * t133 = gm_l0;
        void * t134 = GM_LD(void *, (((uint8_t *)t133) + 0));
        void * t135 = (void *)(((uint8_t *)t134) + 8);
        gm_t88rcucg1b7uk t136 = gm_l3;
        gm_b16_8 t137 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t135, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t138;
        GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
        gm_tabb710d9mq2e t139;
        gm_f_format_m0j4lkh((void *)&t139, (void *)NULL, cx, NULL, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t140 = t139;
        if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 8, gm_src2j6ljeja7ilp, 537, 46);
        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
        gm_b16_8 t142 = t141;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
        gm_t08olq97rscn3 t144;
        gm_f_unsupported_531q51r((void *)&t144, (void *)NULL, cx, NULL, t132, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t145;
        GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
        ((uint8_t *)&t145)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t145 + 8), t144);
        gm_t5o0tpv5jka2l t146 = t145;
        uint8_t t147 = ((((uint8_t *)&t146))[0] != 0);
        F.ret = t145;
        goto gm_fret;
        goto me79;
mn130:;
ma83:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 525, 53);
me79:;
        goto me29;
mn61:;
ma34:;
        void * t149 = gm_l1;
        gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t149) + 208));
        void * t151 = gm_l0;
        void * t152 = GM_LD(void *, (((uint8_t *)t151) + 0));
        void * t153 = (void *)(((uint8_t *)t152) + 8);
        gm_t88rcucg1b7uk t154 = gm_l3;
        gm_b16_8 t155 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t153, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t156;
        GM_ST(gm_b16_8, ((uint8_t *)&t156 + 0), t155);
        gm_tabb710d9mq2e t157;
        gm_f_format_m0j4lkh((void *)&t157, (void *)NULL, cx, NULL, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t158 = t157;
        if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 8, gm_src2j6ljeja7ilp, 539, 42);
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&t158) + 8));
        gm_b16_8 t160 = t159;
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        gm_t08olq97rscn3 t162;
        gm_f_unsupported_531q51r((void *)&t162, (void *)NULL, cx, NULL, t150, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t163;
        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
        ((uint8_t *)&t163)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t163 + 8), t162);
        gm_t5o0tpv5jka2l t164 = t163;
        uint8_t t165 = ((((uint8_t *)&t164))[0] != 0);
        F.ret = t163;
        goto gm_fret;
        goto me29;
mn148:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 515, 5);
me29:;
    }
    {
        void * t166 = gm_l0;
        void * t167 = GM_LD(void *, (((uint8_t *)t166) + 0));
        void * t168 = (void *)(((uint8_t *)t167) + 8);
        gm_t88rcucg1b7uk t169 = gm_l6;
        gm_t5v8n6qrn4rh4 t170;
        gm_f_repr_53quqki((void *)&t170, (void *)NULL, cx, NULL, t168, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t171 = t170;
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&t171) + 0));
        switch (t177) { case ((uint8_t)0ULL): goto ma173; case ((uint8_t)1ULL): goto ma174; case ((uint8_t)2ULL): goto ma175; case ((uint8_t)3ULL): goto ma175; case ((uint8_t)4ULL): goto ma175; case ((uint8_t)5ULL): goto ma175; case ((uint8_t)6ULL): goto ma175; default: goto ma175; }
ma173:;
        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&t171) + 0));
        if (t179 != ((uint8_t)0ULL)) goto mn178;
        {
            void * t181 = gm_l0;
            gm_t88rcucg1b7uk t182 = gm_l6;
            gm_tqcc6dmlne11b t183;
            gm_f_int_info_2tcdf1n((void *)&t183, (void *)NULL, cx, NULL, t181, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t184 = t183;
            if (!((((uint8_t *)&t184))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 543, 19);
            gm_tuglhjjmj2g58 t185 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t184));
            gm_l16 = t185;
        }
        {
            gm_tuglhjjmj2g58 t186 = gm_l16;
            gm_b16_8 t187 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t186));
            gm_l17 = t187;
        }
        {
            uint8_t t189 = gm_l9;
            if (!t189) goto ie188;
            {
                void * t191 = gm_l0;
                void * t192 = GM_LD(void *, (((uint8_t *)t191) + 0));
                void * t193 = (void *)(((uint8_t *)t192) + 8);
                gm_t88rcucg1b7uk t194 = gm_l3;
                gm_t5v8n6qrn4rh4 t195;
                gm_f_repr_53quqki((void *)&t195, (void *)NULL, cx, NULL, t193, t194);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5v8n6qrn4rh4 t196 = t195;
                uint8_t t197;
                uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&t196) + 0));
                if (t200 != ((uint8_t)0ULL)) goto mn199;
                uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)&t196) + 8) + 0));
                gm_l18 = t201;
                uint8_t t202 = gm_l18;
                t197 = t202;
                goto me198;
mn199:;
                t197 = ((uint8_t)1);
                goto me198;
mn203:;
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 546, 28);
me198:;
                gm_l19 = t197;
            }
            {
                gm_tuglhjjmj2g58 t204 = gm_l16;
                gm_b16_8 t205 = gm_l10;
                uint8_t t206 = gm_l19;
                gm_b16_8 t207 = gm_f_arith_fits_7degt3e((void *)NULL, cx, NULL, t204, t205, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t207;
            }
            {
                gm_tuglhjjmj2g58 t208 = gm_l16;
                gm_t6j6smj7qoctu t209;
                gm_b16_8 t210 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t209 + 0, t210);
                gm_b16_8 t211 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t209 + 16, t211);
                gm_tabb710d9mq2e t212;
                gm_f_format_3vtl3te((void *)&t212, (void *)NULL, cx, NULL, &t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t213 = t212;
                if (((((uint8_t *)&t213))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t213) + 8, gm_src2j6ljeja7ilp, 551, 42);
                gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&t213) + 8));
                gm_b16_8 t215 = t214;
                gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                gm_b16_8 t217 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t208, t216);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t217;
            }
            {
                uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t223) { case ((uint8_t)0ULL): goto ma221; case ((uint8_t)1ULL): goto ma221; case ((uint8_t)2ULL): goto ma221; case ((uint8_t)3ULL): goto ma221; case ((uint8_t)4ULL): goto ma221; case ((uint8_t)5ULL): goto ma219; case ((uint8_t)6ULL): goto ma220; case ((uint8_t)7ULL): goto ma221; case ((uint8_t)8ULL): goto ma221; case ((uint8_t)9ULL): goto ma221; case ((uint8_t)10ULL): goto ma221; case ((uint8_t)11ULL): goto ma221; case ((uint8_t)12ULL): goto ma221; case ((uint8_t)13ULL): goto ma221; case ((uint8_t)14ULL): goto ma221; case ((uint8_t)15ULL): goto ma221; case ((uint8_t)16ULL): goto ma221; case ((uint8_t)17ULL): goto ma221; case ((uint8_t)18ULL): goto ma221; case ((uint8_t)19ULL): goto ma221; case ((uint8_t)20ULL): goto ma221; case ((uint8_t)21ULL): goto ma221; case ((uint8_t)22ULL): goto ma221; case ((uint8_t)23ULL): goto ma221; case ((uint8_t)24ULL): goto ma221; case ((uint8_t)25ULL): goto ma221; case ((uint8_t)26ULL): goto ma221; case ((uint8_t)27ULL): goto ma221; case ((uint8_t)28ULL): goto ma221; case ((uint8_t)29ULL): goto ma221; case ((uint8_t)30ULL): goto ma221; case ((uint8_t)31ULL): goto ma221; case ((uint8_t)32ULL): goto ma221; case ((uint8_t)33ULL): goto ma221; case ((uint8_t)34ULL): goto ma221; case ((uint8_t)35ULL): goto ma221; case ((uint8_t)36ULL): goto ma221; case ((uint8_t)37ULL): goto ma221; case ((uint8_t)38ULL): goto ma221; case ((uint8_t)39ULL): goto ma221; case ((uint8_t)40ULL): goto ma221; case ((uint8_t)41ULL): goto ma221; case ((uint8_t)42ULL): goto ma221; case ((uint8_t)43ULL): goto ma221; case ((uint8_t)44ULL): goto ma221; case ((uint8_t)45ULL): goto ma221; case ((uint8_t)46ULL): goto ma221; case ((uint8_t)47ULL): goto ma221; case ((uint8_t)48ULL): goto ma221; case ((uint8_t)49ULL): goto ma221; case ((uint8_t)50ULL): goto ma221; case ((uint8_t)51ULL): goto ma221; case ((uint8_t)52ULL): goto ma221; case ((uint8_t)53ULL): goto ma221; case ((uint8_t)54ULL): goto ma221; case ((uint8_t)55ULL): goto ma221; case ((uint8_t)56ULL): goto ma221; case ((uint8_t)57ULL): goto ma221; case ((uint8_t)58ULL): goto ma221; case ((uint8_t)59ULL): goto ma221; case ((uint8_t)60ULL): goto ma221; case ((uint8_t)61ULL): goto ma221; case ((uint8_t)62ULL): goto ma221; case ((uint8_t)63ULL): goto ma221; case ((uint8_t)64ULL): goto ma221; case ((uint8_t)65ULL): goto ma221; case ((uint8_t)66ULL): goto ma221; case ((uint8_t)67ULL): goto ma221; case ((uint8_t)68ULL): goto ma221; case ((uint8_t)69ULL): goto ma221; case ((uint8_t)70ULL): goto ma221; case ((uint8_t)71ULL): goto ma221; case ((uint8_t)72ULL): goto ma221; case ((uint8_t)73ULL): goto ma221; case ((uint8_t)74ULL): goto ma221; case ((uint8_t)75ULL): goto ma221; case ((uint8_t)76ULL): goto ma221; case ((uint8_t)77ULL): goto ma221; case ((uint8_t)78ULL): goto ma221; case ((uint8_t)79ULL): goto ma221; case ((uint8_t)80ULL): goto ma221; case ((uint8_t)81ULL): goto ma221; default: goto ma221; }
ma219:;
                uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t225 != ((uint8_t)5ULL)) goto mn224;
                {
                    void * t227 = gm_l0;
                    gm_t88rcucg1b7uk t228 = gm_l5;
                    gm_b16_8 t229 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t227, t228);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t229;
                }
                {
                    void * t230 = gm_l0;
                    gm_t88rcucg1b7uk t231 = gm_l6;
                    gm_b16_8 t232 = gm_l21;
                    gm_b16_8 t233 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t230, t231, t232);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t233;
                }
                {
                    void * t234 = gm_l0;
                    gm_t88rcucg1b7uk t235 = gm_l5;
                    gm_b16_8 t236 = gm_l23;
                    gm_b16_8 t237 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t234, t235, t236);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t237;
                }
                {
                    void * t238 = gm_l0;
                    gm_t88rcucg1b7uk t239 = gm_l5;
                    gm_b16_8 t240 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t238, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t240;
                }
                {
                    void * t241 = gm_l0;
                    gm_t0uuhaljsf410 t242;
                    gm_b16_8 t243 = gm_l22;
                    GM_ST(gm_b16_8, (uint8_t *)&t242 + 0, t243);
                    gm_b16_8 t244 = gm_l20;
                    GM_ST(gm_b16_8, (uint8_t *)&t242 + 16, t244);
                    gm_b16_8 t245 = gm_l24;
                    GM_ST(gm_b16_8, (uint8_t *)&t242 + 32, t245);
                    gm_b16_8 t246 = gm_l25;
                    GM_ST(gm_b16_8, (uint8_t *)&t242 + 48, t246);
                    gm_b0_1 t247 = gm_f_linef_6h7r936((void *)NULL, cx, NULL, t241, &t242);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b16_8 t248 = gm_l22;
                    gm_t5o0tpv5jka2l t249;
                    GM_ST(uint8_t, (uint8_t *)&t249 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t249 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t249 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t249 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t249 + 32, 0);
                    ((uint8_t *)&t249)[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&t249 + 8), t248);
                    gm_t5o0tpv5jka2l t250 = t249;
                    uint8_t t251 = ((((uint8_t *)&t250))[0] != 0);
                    F.ret = t249;
                    goto gm_fret;
                }
                goto me218;
mn224:;
ma220:;
                uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t253 != ((uint8_t)6ULL)) goto mn252;
                {
                    void * t255 = gm_l0;
                    gm_t88rcucg1b7uk t256 = gm_l6;
                    gm_tuglhjjmj2g58 t257 = gm_l16;
                    gm_tbci5ospk5nnn t258;
                    gm_f_ii_min_lku7pdi((void *)&t258, (void *)NULL, cx, NULL, t257);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t259 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t255, t256, &t258);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l26 = t259;
                }
                {
                    void * t260 = gm_l0;
                    gm_t88rcucg1b7uk t261 = gm_l6;
                    gm_tuglhjjmj2g58 t262 = gm_l16;
                    gm_tbci5ospk5nnn t263;
                    gm_f_ii_max_gtp5h90((void *)&t263, (void *)NULL, cx, NULL, t262);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t264 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t260, t261, &t263);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t264;
                }
                {
                    gm_b16_8 t265;
                    uint8_t t267 = gm_l19;
                    if (!t267) goto ie266;
                    gm_b16_8 t269 = gm_l10;
                    gm_tnpifv20irnph t270;
                    GM_ST(gm_b16_8, ((uint8_t *)&t270 + 0), t269);
                    gm_tabb710d9mq2e t271;
                    gm_f_format_uhjjs86((void *)&t271, (void *)NULL, cx, NULL, t270);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t272 = t271;
                    if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 8, gm_src2j6ljeja7ilp, 564, 47);
                    gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&t272) + 8));
                    gm_b16_8 t274 = t273;
                    gm_b16_8 t275 = t274;
                    gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
                    t265 = t276;
                    goto ix266;
ie266:;
                    gm_b16_8 t278;
                    GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sicgmpca63jln));
                    GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(1));
                    gm_b16_8 t279 = t278;
                    t265 = t279;
ix266:;
                    gm_l28 = t265;
                }
                {
                    void * t280 = gm_l0;
                    gm_t88rcucg1b7uk t281 = gm_l5;
                    gm_tud7oovfvnf43 t282;
                    gm_b16_8 t283 = gm_l20;
                    GM_ST(gm_b16_8, (uint8_t *)&t282 + 0, t283);
                    gm_b16_8 t284 = gm_l21;
                    GM_ST(gm_b16_8, (uint8_t *)&t282 + 16, t284);
                    gm_b16_8 t285 = gm_l28;
                    GM_ST(gm_b16_8, (uint8_t *)&t282 + 32, t285);
                    gm_b16_8 t286 = gm_l26;
                    GM_ST(gm_b16_8, (uint8_t *)&t282 + 48, t286);
                    gm_b16_8 t287 = gm_l27;
                    GM_ST(gm_b16_8, (uint8_t *)&t282 + 64, t287);
                    gm_tabb710d9mq2e t288;
                    gm_f_format_nkjq1if((void *)&t288, (void *)NULL, cx, NULL, &t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t289 = t288;
                    if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 8, gm_src2j6ljeja7ilp, 565, 42);
                    gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&t289) + 8));
                    gm_b16_8 t291 = t290;
                    gm_b16_8 t292 = GM_LD(gm_b16_8, ((uint8_t *)&t291));
                    gm_b16_8 t293 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t280, t281, t292);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t294;
                    GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t294 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t294 + 32, 0);
                    ((uint8_t *)&t294)[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&t294 + 8), t293);
                    gm_t5o0tpv5jka2l t295 = t294;
                    uint8_t t296 = ((((uint8_t *)&t295))[0] != 0);
                    F.ret = t294;
                    goto gm_fret;
                }
                goto me218;
mn252:;
ma221:;
                void * t298 = gm_l0;
                gm_t88rcucg1b7uk t299 = gm_l5;
                gm_b16_8 t300 = gm_l21;
                gm_b16_8 t301 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t298, t299, t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t302;
                GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t302 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t302 + 32, 0);
                ((uint8_t *)&t302)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t302 + 8), t301);
                gm_t5o0tpv5jka2l t303 = t302;
                uint8_t t304 = ((((uint8_t *)&t303))[0] != 0);
                F.ret = t302;
                goto gm_fret;
                goto me218;
mn297:;
ma222:;
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 552, 17);
me218:;
            }
            goto ix188;
ie188:;
ix188:;
        }
        {
            void * t305 = gm_l0;
            void * t306 = GM_LD(void *, (((uint8_t *)t305) + 0));
            void * t307 = GM_LD(void *, ((((uint8_t *)t306) + 8) + 0));
            void * t308 = (void *)(((uint8_t *)t307) + 8);
            gm_l29 = t308;
        }
        {
            void * t309 = gm_l29;
            gm_t53at80pfdus3 t310;
            memset(&t310, 0, sizeof t310);
            GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t310 + 8), ((uint16_t)64ULL));
            gm_t88rcucg1b7uk t311 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t309, &t310);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t311;
        }
        {
            void * t312 = gm_l0;
            gm_t88rcucg1b7uk t313 = gm_l30;
            gm_b16_8 t314 = gm_l10;
            gm_b16_8 t315 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t312, t313, t314);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l31 = t315;
        }
        {
            gm_b16_8 t316;
            uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t323) { case ((uint8_t)0ULL): goto ma321; case ((uint8_t)1ULL): goto ma321; case ((uint8_t)2ULL): goto ma318; case ((uint8_t)3ULL): goto ma319; case ((uint8_t)4ULL): goto ma320; case ((uint8_t)5ULL): goto ma321; case ((uint8_t)6ULL): goto ma321; case ((uint8_t)7ULL): goto ma321; case ((uint8_t)8ULL): goto ma321; case ((uint8_t)9ULL): goto ma321; case ((uint8_t)10ULL): goto ma321; case ((uint8_t)11ULL): goto ma321; case ((uint8_t)12ULL): goto ma321; case ((uint8_t)13ULL): goto ma321; case ((uint8_t)14ULL): goto ma321; case ((uint8_t)15ULL): goto ma321; case ((uint8_t)16ULL): goto ma321; case ((uint8_t)17ULL): goto ma321; case ((uint8_t)18ULL): goto ma321; case ((uint8_t)19ULL): goto ma321; case ((uint8_t)20ULL): goto ma321; case ((uint8_t)21ULL): goto ma321; case ((uint8_t)22ULL): goto ma321; case ((uint8_t)23ULL): goto ma321; case ((uint8_t)24ULL): goto ma321; case ((uint8_t)25ULL): goto ma321; case ((uint8_t)26ULL): goto ma321; case ((uint8_t)27ULL): goto ma321; case ((uint8_t)28ULL): goto ma321; case ((uint8_t)29ULL): goto ma321; case ((uint8_t)30ULL): goto ma321; case ((uint8_t)31ULL): goto ma321; case ((uint8_t)32ULL): goto ma321; case ((uint8_t)33ULL): goto ma321; case ((uint8_t)34ULL): goto ma321; case ((uint8_t)35ULL): goto ma321; case ((uint8_t)36ULL): goto ma321; case ((uint8_t)37ULL): goto ma321; case ((uint8_t)38ULL): goto ma321; case ((uint8_t)39ULL): goto ma321; case ((uint8_t)40ULL): goto ma321; case ((uint8_t)41ULL): goto ma321; case ((uint8_t)42ULL): goto ma321; case ((uint8_t)43ULL): goto ma321; case ((uint8_t)44ULL): goto ma321; case ((uint8_t)45ULL): goto ma321; case ((uint8_t)46ULL): goto ma321; case ((uint8_t)47ULL): goto ma321; case ((uint8_t)48ULL): goto ma321; case ((uint8_t)49ULL): goto ma321; case ((uint8_t)50ULL): goto ma321; case ((uint8_t)51ULL): goto ma321; case ((uint8_t)52ULL): goto ma321; case ((uint8_t)53ULL): goto ma321; case ((uint8_t)54ULL): goto ma321; case ((uint8_t)55ULL): goto ma321; case ((uint8_t)56ULL): goto ma321; case ((uint8_t)57ULL): goto ma321; case ((uint8_t)58ULL): goto ma321; case ((uint8_t)59ULL): goto ma321; case ((uint8_t)60ULL): goto ma321; case ((uint8_t)61ULL): goto ma321; case ((uint8_t)62ULL): goto ma321; case ((uint8_t)63ULL): goto ma321; case ((uint8_t)64ULL): goto ma321; case ((uint8_t)65ULL): goto ma321; case ((uint8_t)66ULL): goto ma321; case ((uint8_t)67ULL): goto ma321; case ((uint8_t)68ULL): goto ma321; case ((uint8_t)69ULL): goto ma321; case ((uint8_t)70ULL): goto ma321; case ((uint8_t)71ULL): goto ma321; case ((uint8_t)72ULL): goto ma321; case ((uint8_t)73ULL): goto ma321; case ((uint8_t)74ULL): goto ma321; case ((uint8_t)75ULL): goto ma321; case ((uint8_t)76ULL): goto ma321; case ((uint8_t)77ULL): goto ma321; case ((uint8_t)78ULL): goto ma321; case ((uint8_t)79ULL): goto ma321; case ((uint8_t)80ULL): goto ma321; case ((uint8_t)81ULL): goto ma321; default: goto ma321; }
ma318:;
            uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t325 != ((uint8_t)2ULL)) goto mn324;
            gm_b16_8 t326 = gm_l31;
            gm_tnpifv20irnph t327;
            GM_ST(gm_b16_8, ((uint8_t *)&t327 + 0), t326);
            gm_tabb710d9mq2e t328;
            gm_f_format_3s3fqnk((void *)&t328, (void *)NULL, cx, NULL, t327);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t329 = t328;
            if (((((uint8_t *)&t329))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t329) + 8, gm_src2j6ljeja7ilp, 574, 27);
            gm_b16_8 t330 = GM_LD(gm_b16_8, (((uint8_t *)&t329) + 8));
            gm_b16_8 t331 = t330;
            gm_b16_8 t332 = GM_LD(gm_b16_8, ((uint8_t *)&t331));
            t316 = t332;
            goto me317;
mn324:;
ma319:;
            uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t334 != ((uint8_t)3ULL)) goto mn333;
            gm_b16_8 t335 = gm_l31;
            gm_tnpifv20irnph t336;
            GM_ST(gm_b16_8, ((uint8_t *)&t336 + 0), t335);
            gm_tabb710d9mq2e t337;
            gm_f_format_b05vb48((void *)&t337, (void *)NULL, cx, NULL, t336);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t338 = t337;
            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 8, gm_src2j6ljeja7ilp, 575, 27);
            gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&t338) + 8));
            gm_b16_8 t340 = t339;
            gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)&t340));
            t316 = t341;
            goto me317;
mn333:;
ma320:;
            uint8_t t343 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t343 != ((uint8_t)4ULL)) goto mn342;
            gm_b16_8 t344 = gm_l31;
            gm_tnpifv20irnph t345;
            GM_ST(gm_b16_8, ((uint8_t *)&t345 + 0), t344);
            gm_tabb710d9mq2e t346;
            gm_f_format_2qdci62((void *)&t346, (void *)NULL, cx, NULL, t345);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t347 = t346;
            if (((((uint8_t *)&t347))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t347) + 8, gm_src2j6ljeja7ilp, 576, 26);
            gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&t347) + 8));
            gm_b16_8 t349 = t348;
            gm_b16_8 t350 = GM_LD(gm_b16_8, ((uint8_t *)&t349));
            t316 = t350;
            goto me317;
mn342:;
ma321:;
            gm_b16_8 t352 = gm_l31;
            gm_tnpifv20irnph t353;
            GM_ST(gm_b16_8, ((uint8_t *)&t353 + 0), t352);
            gm_tabb710d9mq2e t354;
            gm_f_format_7gtji42((void *)&t354, (void *)NULL, cx, NULL, t353);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t355 = t354;
            if (((((uint8_t *)&t355))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t355) + 8, gm_src2j6ljeja7ilp, 577, 22);
            gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&t355) + 8));
            gm_b16_8 t357 = t356;
            gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&t357));
            t316 = t358;
            goto me317;
mn351:;
ma322:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 573, 31);
me317:;
            gm_l32 = t316;
        }
        {
            void * t359 = gm_l0;
            gm_t88rcucg1b7uk t360 = gm_l30;
            gm_b16_8 t361 = gm_l32;
            gm_b16_8 t362 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t359, t360, t361);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t362;
        }
        {
            gm_tuglhjjmj2g58 t363 = gm_l16;
            gm_tbci5ospk5nnn t364;
            gm_f_ii_min_lku7pdi((void *)&t364, (void *)NULL, cx, NULL, t363);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            double t365 = gm_f_arith_int_to_f64_tiud1hu(&t364);
            gm_tidu2er0d7h95 t366;
            GM_ST(double, ((uint8_t *)&t366 + 0), t365);
            gm_tabb710d9mq2e t367;
            gm_f_format_eucvbnh((void *)&t367, (void *)NULL, cx, NULL, t366);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t368 = t367;
            if (((((uint8_t *)&t368))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t368) + 8, gm_src2j6ljeja7ilp, 580, 19);
            gm_b16_8 t369 = GM_LD(gm_b16_8, (((uint8_t *)&t368) + 8));
            gm_l34 = t369;
        }
        {
            gm_tuglhjjmj2g58 t370 = gm_l16;
            gm_tbci5ospk5nnn t371;
            gm_f_ii_max_gtp5h90((void *)&t371, (void *)NULL, cx, NULL, t370);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t372;
            gm_f_one_08qg1op((void *)&t372, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t373;
            gm_f_add_420lj6o((void *)&t373, (void *)NULL, cx, NULL, &t371, &t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            double t374 = gm_f_arith_int_to_f64_tiud1hu(&t373);
            gm_tidu2er0d7h95 t375;
            GM_ST(double, ((uint8_t *)&t375 + 0), t374);
            gm_tabb710d9mq2e t376;
            gm_f_format_eucvbnh((void *)&t376, (void *)NULL, cx, NULL, t375);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t377 = t376;
            if (((((uint8_t *)&t377))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t377) + 8, gm_src2j6ljeja7ilp, 581, 19);
            gm_b16_8 t378 = GM_LD(gm_b16_8, (((uint8_t *)&t377) + 8));
            gm_l35 = t378;
        }
        {
            gm_tud7oovfvnf43 t379;
            gm_b16_8 t380 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t379 + 0, t380);
            gm_b16_8 t381 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t379 + 16, t381);
            gm_b16_8 t382 = gm_l34;
            GM_ST(gm_b16_8, (uint8_t *)&t379 + 32, t382);
            gm_b16_8 t383 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t379 + 48, t383);
            gm_b16_8 t384 = gm_l35;
            GM_ST(gm_b16_8, (uint8_t *)&t379 + 64, t384);
            gm_tabb710d9mq2e t385;
            gm_f_format_aculdom((void *)&t385, (void *)NULL, cx, NULL, &t379);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t386 = t385;
            if (((((uint8_t *)&t386))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t386) + 8, gm_src2j6ljeja7ilp, 582, 25);
            gm_b16_8 t387 = GM_LD(gm_b16_8, (((uint8_t *)&t386) + 8));
            gm_l36 = t387;
        }
        {
            uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t393) { case ((uint8_t)0ULL): goto ma391; case ((uint8_t)1ULL): goto ma391; case ((uint8_t)2ULL): goto ma391; case ((uint8_t)3ULL): goto ma391; case ((uint8_t)4ULL): goto ma391; case ((uint8_t)5ULL): goto ma389; case ((uint8_t)6ULL): goto ma390; case ((uint8_t)7ULL): goto ma391; case ((uint8_t)8ULL): goto ma391; case ((uint8_t)9ULL): goto ma391; case ((uint8_t)10ULL): goto ma391; case ((uint8_t)11ULL): goto ma391; case ((uint8_t)12ULL): goto ma391; case ((uint8_t)13ULL): goto ma391; case ((uint8_t)14ULL): goto ma391; case ((uint8_t)15ULL): goto ma391; case ((uint8_t)16ULL): goto ma391; case ((uint8_t)17ULL): goto ma391; case ((uint8_t)18ULL): goto ma391; case ((uint8_t)19ULL): goto ma391; case ((uint8_t)20ULL): goto ma391; case ((uint8_t)21ULL): goto ma391; case ((uint8_t)22ULL): goto ma391; case ((uint8_t)23ULL): goto ma391; case ((uint8_t)24ULL): goto ma391; case ((uint8_t)25ULL): goto ma391; case ((uint8_t)26ULL): goto ma391; case ((uint8_t)27ULL): goto ma391; case ((uint8_t)28ULL): goto ma391; case ((uint8_t)29ULL): goto ma391; case ((uint8_t)30ULL): goto ma391; case ((uint8_t)31ULL): goto ma391; case ((uint8_t)32ULL): goto ma391; case ((uint8_t)33ULL): goto ma391; case ((uint8_t)34ULL): goto ma391; case ((uint8_t)35ULL): goto ma391; case ((uint8_t)36ULL): goto ma391; case ((uint8_t)37ULL): goto ma391; case ((uint8_t)38ULL): goto ma391; case ((uint8_t)39ULL): goto ma391; case ((uint8_t)40ULL): goto ma391; case ((uint8_t)41ULL): goto ma391; case ((uint8_t)42ULL): goto ma391; case ((uint8_t)43ULL): goto ma391; case ((uint8_t)44ULL): goto ma391; case ((uint8_t)45ULL): goto ma391; case ((uint8_t)46ULL): goto ma391; case ((uint8_t)47ULL): goto ma391; case ((uint8_t)48ULL): goto ma391; case ((uint8_t)49ULL): goto ma391; case ((uint8_t)50ULL): goto ma391; case ((uint8_t)51ULL): goto ma391; case ((uint8_t)52ULL): goto ma391; case ((uint8_t)53ULL): goto ma391; case ((uint8_t)54ULL): goto ma391; case ((uint8_t)55ULL): goto ma391; case ((uint8_t)56ULL): goto ma391; case ((uint8_t)57ULL): goto ma391; case ((uint8_t)58ULL): goto ma391; case ((uint8_t)59ULL): goto ma391; case ((uint8_t)60ULL): goto ma391; case ((uint8_t)61ULL): goto ma391; case ((uint8_t)62ULL): goto ma391; case ((uint8_t)63ULL): goto ma391; case ((uint8_t)64ULL): goto ma391; case ((uint8_t)65ULL): goto ma391; case ((uint8_t)66ULL): goto ma391; case ((uint8_t)67ULL): goto ma391; case ((uint8_t)68ULL): goto ma391; case ((uint8_t)69ULL): goto ma391; case ((uint8_t)70ULL): goto ma391; case ((uint8_t)71ULL): goto ma391; case ((uint8_t)72ULL): goto ma391; case ((uint8_t)73ULL): goto ma391; case ((uint8_t)74ULL): goto ma391; case ((uint8_t)75ULL): goto ma391; case ((uint8_t)76ULL): goto ma391; case ((uint8_t)77ULL): goto ma391; case ((uint8_t)78ULL): goto ma391; case ((uint8_t)79ULL): goto ma391; case ((uint8_t)80ULL): goto ma391; case ((uint8_t)81ULL): goto ma391; default: goto ma391; }
ma389:;
            uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t395 != ((uint8_t)5ULL)) goto mn394;
            {
                void * t397 = gm_l0;
                gm_t88rcucg1b7uk t398 = gm_l5;
                gm_b16_8 t399 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t397, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t399;
            }
            {
                void * t400 = gm_l0;
                gm_tfeab279c16rr t401;
                gm_b16_8 t402 = gm_l36;
                GM_ST(gm_b16_8, (uint8_t *)&t401 + 0, t402);
                gm_b16_8 t403 = gm_l31;
                GM_ST(gm_b16_8, (uint8_t *)&t401 + 16, t403);
                gm_b16_8 t404 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t401 + 32, t404);
                gm_b0_1 t405 = gm_f_linef_6sel63v((void *)NULL, cx, NULL, t400, &t401);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t406 = gm_l0;
                gm_t88rcucg1b7uk t407 = gm_l6;
                gm_t6j6smj7qoctu t408;
                gm_b16_8 t409 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t408 + 0, t409);
                gm_b16_8 t410 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t408 + 16, t410);
                gm_tabb710d9mq2e t411;
                gm_f_format_3vtl3te((void *)&t411, (void *)NULL, cx, NULL, &t408);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t412 = t411;
                if (((((uint8_t *)&t412))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t412) + 8, gm_src2j6ljeja7ilp, 587, 41);
                gm_b16_8 t413 = GM_LD(gm_b16_8, (((uint8_t *)&t412) + 8));
                gm_b16_8 t414 = t413;
                gm_b16_8 t415 = GM_LD(gm_b16_8, ((uint8_t *)&t414));
                gm_b16_8 t416 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t406, t407, t415);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l38 = t416;
            }
            {
                void * t417 = gm_l0;
                gm_t88rcucg1b7uk t418 = gm_l5;
                gm_b16_8 t419 = gm_l38;
                gm_b16_8 t420 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t417, t418, t419);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l39 = t420;
            }
            {
                void * t421 = gm_l0;
                gm_t6j6smj7qoctu t422;
                gm_b16_8 t423 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t422 + 0, t423);
                gm_b16_8 t424 = gm_l39;
                GM_ST(gm_b16_8, (uint8_t *)&t422 + 16, t424);
                gm_b0_1 t425 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t421, &t422);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t426 = gm_l0;
                gm_b16_8 t427;
                GM_ST(void *, (uint8_t *)&t427, (void *)(gm_s02uuedsru1qu));
                GM_ST(uintptr_t, (uint8_t *)&t427 + 8, (uintptr_t)(8));
                gm_b0_1 t428 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t426, t427);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t429 = gm_l0;
                gm_t88rcucg1b7uk t430 = gm_l5;
                gm_b16_8 t431 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t429, t430);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l40 = t431;
            }
            {
                void * t432 = gm_l0;
                gm_t6j6smj7qoctu t433;
                gm_b16_8 t434 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t433 + 0, t434);
                gm_b16_8 t435 = gm_l40;
                GM_ST(gm_b16_8, (uint8_t *)&t433 + 16, t435);
                gm_b0_1 t436 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t432, &t433);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t437 = gm_l0;
                gm_b16_8 t438;
                GM_ST(void *, (uint8_t *)&t438, (void *)(gm_s2cuflhqkkuaq));
                GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(1));
                gm_b0_1 t439 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t437, t438);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t440 = gm_l37;
                gm_t5o0tpv5jka2l t441;
                GM_ST(uint8_t, (uint8_t *)&t441 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t441 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t441 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t441 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t441 + 32, 0);
                ((uint8_t *)&t441)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t441 + 8), t440);
                gm_t5o0tpv5jka2l t442 = t441;
                uint8_t t443 = ((((uint8_t *)&t442))[0] != 0);
                F.ret = t441;
                goto gm_fret;
            }
            goto me388;
mn394:;
ma390:;
            uint8_t t445 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t445 != ((uint8_t)6ULL)) goto mn444;
            {
                void * t447 = gm_l0;
                gm_t88rcucg1b7uk t448 = gm_l6;
                gm_tuglhjjmj2g58 t449 = gm_l16;
                gm_tbci5ospk5nnn t450;
                gm_f_ii_min_lku7pdi((void *)&t450, (void *)NULL, cx, NULL, t449);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t451 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t447, t448, &t450);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t451;
            }
            {
                void * t452 = gm_l0;
                gm_t88rcucg1b7uk t453 = gm_l6;
                gm_tuglhjjmj2g58 t454 = gm_l16;
                gm_tbci5ospk5nnn t455;
                gm_f_ii_max_gtp5h90((void *)&t455, (void *)NULL, cx, NULL, t454);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t456 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t452, t453, &t455);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l42 = t456;
            }
            {
                void * t457 = gm_l0;
                gm_t88rcucg1b7uk t458 = gm_l5;
                gm_tk6rvhh54g86p t459;
                gm_b16_8 t460 = gm_l31;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 0, t460);
                gm_b16_8 t461 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 16, t461);
                gm_b16_8 t462 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 32, t462);
                gm_b16_8 t463 = gm_l34;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 48, t463);
                gm_b16_8 t464 = gm_l41;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 64, t464);
                gm_b16_8 t465 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 80, t465);
                gm_b16_8 t466 = gm_l35;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 96, t466);
                gm_b16_8 t467 = gm_l42;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 112, t467);
                gm_b16_8 t468 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 128, t468);
                gm_b16_8 t469 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t459 + 144, t469);
                gm_tabb710d9mq2e t470;
                gm_f_format_fh9to9u((void *)&t470, (void *)NULL, cx, NULL, &t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t471 = t470;
                if (((((uint8_t *)&t471))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t471) + 8, gm_src2j6ljeja7ilp, 599, 38);
                gm_b16_8 t472 = GM_LD(gm_b16_8, (((uint8_t *)&t471) + 8));
                gm_b16_8 t473 = t472;
                gm_b16_8 t474 = GM_LD(gm_b16_8, ((uint8_t *)&t473));
                gm_b16_8 t475 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t457, t458, t474);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t476;
                GM_ST(uint8_t, (uint8_t *)&t476 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t476 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t476 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t476 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t476 + 32, 0);
                ((uint8_t *)&t476)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t476 + 8), t475);
                gm_t5o0tpv5jka2l t477 = t476;
                uint8_t t478 = ((((uint8_t *)&t477))[0] != 0);
                F.ret = t476;
                goto gm_fret;
            }
            goto me388;
mn444:;
ma391:;
            {
                void * t482 = gm_l0;
                void * t483 = GM_LD(void *, (((uint8_t *)t482) + 0));
                uint8_t t484 = GM_LD(uint8_t, ((((uint8_t *)t483) + 1136) + 0));
                if (!t484) goto ie481;
                {
                    void * t486 = gm_l0;
                    gm_t6j6smj7qoctu t487;
                    gm_b16_8 t488 = gm_l36;
                    GM_ST(gm_b16_8, (uint8_t *)&t487 + 0, t488);
                    gm_b16_8 t489 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t487 + 16, t489);
                    gm_b0_1 t490 = gm_f_linef_jl8o1sr((void *)NULL, cx, NULL, t486, &t487);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix481;
ie481:;
ix481:;
            }
            {
                void * t491 = gm_l0;
                gm_t88rcucg1b7uk t492 = gm_l5;
                gm_t6j6smj7qoctu t493;
                gm_b16_8 t494 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t493 + 0, t494);
                gm_b16_8 t495 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t493 + 16, t495);
                gm_tabb710d9mq2e t496;
                gm_f_format_3vtl3te((void *)&t496, (void *)NULL, cx, NULL, &t493);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t497 = t496;
                if (((((uint8_t *)&t497))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t497) + 8, gm_src2j6ljeja7ilp, 605, 38);
                gm_b16_8 t498 = GM_LD(gm_b16_8, (((uint8_t *)&t497) + 8));
                gm_b16_8 t499 = t498;
                gm_b16_8 t500 = GM_LD(gm_b16_8, ((uint8_t *)&t499));
                gm_b16_8 t501 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t491, t492, t500);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t502;
                GM_ST(uint8_t, (uint8_t *)&t502 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t502 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t502 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t502 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t502 + 32, 0);
                ((uint8_t *)&t502)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t502 + 8), t501);
                gm_t5o0tpv5jka2l t503 = t502;
                uint8_t t504 = ((((uint8_t *)&t503))[0] != 0);
                F.ret = t502;
                goto gm_fret;
            }
            goto me388;
mn479:;
ma392:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 583, 13);
me388:;
        }
        goto me172;
mn178:;
ma174:;
        uint8_t t506 = GM_LD(uint8_t, (((uint8_t *)&t171) + 0));
        if (t506 != ((uint8_t)1ULL)) goto mn505;
        {
            void * t508 = gm_l0;
            gm_t88rcucg1b7uk t509 = gm_l6;
            gm_b16_8 t510 = gm_l10;
            gm_b16_8 t511 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t508, t509, t510);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t511;
        }
        {
            void * t512 = gm_l0;
            gm_t88rcucg1b7uk t513 = gm_l5;
            gm_b16_8 t514 = gm_l43;
            gm_b16_8 t515 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t512, t513, t514);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t516;
            GM_ST(uint8_t, (uint8_t *)&t516 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t516 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t516 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t516 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t516 + 32, 0);
            ((uint8_t *)&t516)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t516 + 8), t515);
            gm_t5o0tpv5jka2l t517 = t516;
            uint8_t t518 = ((((uint8_t *)&t517))[0] != 0);
            F.ret = t516;
            goto gm_fret;
        }
        goto me172;
mn505:;
ma175:;
        goto me172;
mn519:;
ma176:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 541, 5);
me172:;
    }
    {
        uint8_t t522 = gm_l9;
        if (!t522) goto ie521;
        {
            void * t525 = gm_l0;
            void * t526 = GM_LD(void *, (((uint8_t *)t525) + 0));
            void * t527 = (void *)(((uint8_t *)t526) + 8);
            gm_t88rcucg1b7uk t528 = gm_l6;
            gm_t57euh612ligr t529;
            gm_f_bits_layout_23rect9((void *)&t529, (void *)NULL, cx, NULL, t527, t528);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t57euh612ligr t530 = t529;
            if (!((((uint8_t *)&t530))[24] != 0)) goto ie524;
            gm_tcmkompbl09il t531 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t530));
            gm_l44 = t531;
            {
                void * t533 = gm_l0;
                gm_t88rcucg1b7uk t534 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l44) + 0));
                gm_tqcc6dmlne11b t535;
                gm_f_int_info_2tcdf1n((void *)&t535, (void *)NULL, cx, NULL, t533, t534);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqcc6dmlne11b t536 = t535;
                if (!((((uint8_t *)&t536))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 618, 19);
                gm_tuglhjjmj2g58 t537 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t536));
                gm_l45 = t537;
            }
            {
                gm_tuglhjjmj2g58 t538 = gm_l45;
                gm_t6j6smj7qoctu t539;
                gm_tuglhjjmj2g58 t540 = gm_l45;
                gm_b16_8 t541 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t540));
                GM_ST(gm_b16_8, (uint8_t *)&t539 + 0, t541);
                gm_b16_8 t542 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t539 + 16, t542);
                gm_tabb710d9mq2e t543;
                gm_f_format_3vtl3te((void *)&t543, (void *)NULL, cx, NULL, &t539);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t544 = t543;
                if (((((uint8_t *)&t544))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t544) + 8, gm_src2j6ljeja7ilp, 619, 32);
                gm_b16_8 t545 = GM_LD(gm_b16_8, (((uint8_t *)&t544) + 8));
                gm_b16_8 t546 = t545;
                gm_b16_8 t547 = GM_LD(gm_b16_8, ((uint8_t *)&t546));
                gm_b16_8 t548 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t538, t547);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l46 = t548;
            }
            {
                void * t549 = gm_l0;
                gm_t88rcucg1b7uk t550 = gm_l6;
                gm_b16_8 t551 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t549, t550);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l47 = t551;
            }
            {
                void * t552 = gm_l0;
                gm_tfeab279c16rr t553;
                gm_tuglhjjmj2g58 t554 = gm_l45;
                gm_b16_8 t555 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t554));
                GM_ST(gm_b16_8, (uint8_t *)&t553 + 0, t555);
                gm_b16_8 t556 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t553 + 16, t556);
                gm_b16_8 t557 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t553 + 32, t557);
                gm_b0_1 t558 = gm_f_linef_21h063v((void *)NULL, cx, NULL, t552, &t553);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t559 = gm_l47;
                gm_t5o0tpv5jka2l t560;
                GM_ST(uint8_t, (uint8_t *)&t560 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t560 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t560 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t560 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t560 + 32, 0);
                ((uint8_t *)&t560)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t560 + 8), t559);
                gm_t5o0tpv5jka2l t561 = t560;
                uint8_t t562 = ((((uint8_t *)&t561))[0] != 0);
                F.ret = t560;
                goto gm_fret;
            }
            goto ix524;
ie524:;
ix524:;
        }
        goto ix521;
ie521:;
ix521:;
    }
    {
        void * t563 = gm_l1;
        gm_tl874t2atnlh3 t564 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t563) + 208));
        void * t565 = gm_l0;
        void * t566 = GM_LD(void *, (((uint8_t *)t565) + 0));
        void * t567 = (void *)(((uint8_t *)t566) + 8);
        gm_t88rcucg1b7uk t568 = gm_l6;
        gm_b16_8 t569 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t567, t568);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t570;
        GM_ST(gm_b16_8, ((uint8_t *)&t570 + 0), t569);
        gm_tabb710d9mq2e t571;
        gm_f_format_15kpcel((void *)&t571, (void *)NULL, cx, NULL, t570);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t572 = t571;
        if (((((uint8_t *)&t572))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t572) + 8, gm_src2j6ljeja7ilp, 625, 33);
        gm_b16_8 t573 = GM_LD(gm_b16_8, (((uint8_t *)&t572) + 8));
        gm_b16_8 t574 = t573;
        gm_b16_8 t575 = GM_LD(gm_b16_8, ((uint8_t *)&t574));
        gm_t08olq97rscn3 t576;
        gm_f_unsupported_531q51r((void *)&t576, (void *)NULL, cx, NULL, t564, t575);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t577;
        GM_ST(uint8_t, (uint8_t *)&t577 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t577 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t577 + 4, 0);
        ((uint8_t *)&t577)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t577 + 8), t576);
        gm_t5o0tpv5jka2l t578 = t577;
        uint8_t t579 = ((((uint8_t *)&t578))[0] != 0);
        F.ret = t577;
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
__attribute__((aligned(4))) void gm_f_from_tag_2m7nfln(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_from_tag_2m7nfln F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* at */
    gm_b16_8 gm_l3; /* v0 */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* u */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l7; /* res0 */
    gm_b16_8 gm_l8; /* res */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l13; /* lit */
    gm_b16_8 gm_l14; /* x */
    gm_b16_8 gm_l15; /* s */
    GM_STACK_CHECK(&F, 1303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_t88rcucg1b7uk t8 = gm_l4;
        gm_t53at80pfdus3 t9;
        gm_f_kind_ghvav22((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        gm_t53at80pfdus3 t10 = t9;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t11 != ((uint8_t)13ULL)) goto ie4;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 8));
        gm_l5 = t12;
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l5;
            void * t19 = gm_f_adt_of_gej99vn(t17, GM_CONV(gm_csguankko4lmhd, t18));
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 24) + 0));
            if (t20 != ((uint8_t)1ULL)) goto ie14;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((((uint8_t *)t19) + 24) + 8) + 8));
            gm_l6 = t21;
            {
                void * t23 = gm_l0;
                gm_t88rcucg1b7uk t24 = gm_l4;
                gm_b16_8 t25 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t25;
            }
            {
                void * t26 = gm_l0;
                gm_t88rcucg1b7uk t27 = gm_l4;
                gm_b16_8 t28 = gm_l7;
                gm_b16_8 t29 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t26, t27, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t29;
            }
            {
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_l9 = t31;
                }
                {
                    for (;;) {
                        uint64_t t33 = gm_l10;
                        uint64_t t34 = gm_l9;
                        uint8_t t35 = (t33 < t34);
                        if (!t35) goto lb32;
                        {
                            uint64_t t38 = gm_l10;
                            uint64_t t39 = t38;
                            uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&t39));
                            gm_l11 = t42;
                            {
                                uint64_t t44 = gm_l11;
                                uint64_t t45 = (uintptr_t)t44;
                                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_src2j6ljeja7ilp, 283, 15);
                                gm_tckdobunaulee t47 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t45 * 72));
                                F.l12 = t47;
                            }
                            {
                                uint8_t t49;
                                if ((((((uint8_t *)&F.l12) + 16))[4] != 0)) goto mn51;
                                t49 = ((uint8_t)0);
                                goto me50;
mn51:;
                                t49 = ((uint8_t)1);
                                goto me50;
mn52:;
                                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 284, 12);
me50:;
                                if (!t49) goto ie48;
                                {
                                    goto lc32;
                                }
                                goto ix48;
ie48:;
ix48:;
                            }
                            {
                                void * t54 = gm_l0;
                                gm_t88rcucg1b7uk t55 = gm_l2;
                                gm_b16_8 t56 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t54, t55, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l12) + 24)));
                                if (gm_unw.kind) {
                                    goto gm_ll36;
                                }
                                gm_l13 = t56;
                            }
                            {
                                void * t57 = gm_l0;
                                gm_t6j6smj7qoctu t58;
                                gm_b16_8 t59 = gm_l3;
                                GM_ST(gm_b16_8, (uint8_t *)&t58 + 0, t59);
                                gm_b16_8 t60 = gm_l13;
                                GM_ST(gm_b16_8, (uint8_t *)&t58 + 16, t60);
                                gm_b0_1 t61 = gm_f_linef_156hint((void *)NULL, cx, NULL, t57, &t58);
                                if (gm_unw.kind) {
                                    goto gm_ll36;
                                }
                            }
                            {
                                void * t62 = gm_l0;
                                gm_t88rcucg1b7uk t63 = gm_l5;
                                uint64_t t64 = gm_l11;
                                gm_th2lqbo77pvsc t65;
                                memset(&t65, 0, sizeof t65);
                                gm_b16_8 t66 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t62, t63, t64, &t65);
                                if (gm_unw.kind) {
                                    goto gm_ll36;
                                }
                                gm_l14 = t66;
                            }
                            {
                                void * t67 = gm_l0;
                                gm_t88rcucg1b7uk t68 = gm_l4;
                                gm_b16_8 t69 = gm_l14;
                                gm_b16_8 t70 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t67, t68, t69);
                                if (gm_unw.kind) {
                                    goto gm_ll36;
                                }
                                gm_l15 = t70;
                            }
                            {
                                void * t71 = gm_l0;
                                gm_t6j6smj7qoctu t72;
                                gm_b16_8 t73 = gm_l8;
                                GM_ST(gm_b16_8, (uint8_t *)&t72 + 0, t73);
                                gm_b16_8 t74 = gm_l15;
                                GM_ST(gm_b16_8, (uint8_t *)&t72 + 16, t74);
                                gm_b0_1 t75 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t71, &t72);
                                if (gm_unw.kind) {
                                    goto gm_ll36;
                                }
                            }
                            {
                                void * t76 = gm_l0;
                                gm_b16_8 t77;
                                GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s2cuflhqkkuaq));
                                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
                                gm_b0_1 t78 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t76, t77);
                                if (gm_unw.kind) {
                                    goto gm_ll36;
                                }
                            }
                            goto me40;
mn41:;
                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 282, 5);
me40:;
                        }
lc32:;
                        {
                            uint64_t t80 = gm_l10;
                            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                            uint64_t t83 = r81;
                            gm_l10 = t83;
                        }
                    }
gm_ll36:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
                    }
                    goto gm_lad;
lb32:;
                }
            }
            {
                gm_b16_8 t84 = gm_l8;
                gm_t5o0tpv5jka2l t85;
                GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 32, 0);
                ((uint8_t *)&t85)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 8), t84);
                gm_t5o0tpv5jka2l t86 = t85;
                uint8_t t87 = ((((uint8_t *)&t86))[0] != 0);
                F.ret = t85;
                goto gm_fret;
            }
            goto ix14;
ie14:;
            {
                void * t89 = gm_l1;
                gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t89) + 208));
                gm_b16_8 t91;
                GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s66fk044n1n0k));
                GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(8));
                gm_t08olq97rscn3 t92;
                gm_f_unsupported_531q51r((void *)&t92, (void *)NULL, cx, NULL, t90, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t93;
                GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                ((uint8_t *)&t93)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t93 + 8), t92);
                gm_t5o0tpv5jka2l t94 = t93;
                uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
                F.ret = t93;
                goto gm_fret;
            }
ix14:;
        }
        goto ix4;
ie4:;
        {
            void * t97 = gm_l1;
            gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t97) + 208));
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_s66fk044n1n0k));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(8));
            gm_t08olq97rscn3 t100;
            gm_f_unsupported_531q51r((void *)&t100, (void *)NULL, cx, NULL, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t101;
            GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
            ((uint8_t *)&t101)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t101 + 8), t100);
            gm_t5o0tpv5jka2l t102 = t101;
            uint8_t t103 = ((((uint8_t *)&t102))[0] != 0);
            F.ret = t101;
            goto gm_fret;
        }
ix4:;
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
__attribute__((aligned(4))) void gm_f_pow_s0lbtt4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_pow_s0lbtt4 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* vals */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* v0 */
    gm_b16_8 gm_l7; /* loc */
    gm_tuglhjjmj2g58 gm_l8; /* ii */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* r */
    gm_b16_8 gm_l11; /* o */
    gm_b16_8 gm_l12; /* f */
    gm_b16_8 gm_l13; /* min */
    gm_b16_8 gm_l14; /* max */
    gm_b16_8 gm_l15; /* b */
    gm_b16_8 gm_l16; /* sat */
    GM_STACK_CHECK(&F, 2390u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 341, 11);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t4 * 16));
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l1;
        gm_b16_8 t9 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l3;
        gm_tqcc6dmlne11b t12;
        gm_f_int_info_2tcdf1n((void *)&t12, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqcc6dmlne11b t13 = t12;
        gm_tuglhjjmj2g58 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_tuglhjjmj2g58 t15 = GM_LD(gm_tuglhjjmj2g58, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            void * t16 = gm_l1;
            gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 208));
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s9gk1b0ve64mt));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(9));
            gm_t08olq97rscn3 t19;
            gm_f_unsupported_531q51r((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t20 + 8), t19);
            gm_t5o0tpv5jka2l t21 = t20;
            uint8_t t22 = ((((uint8_t *)&t21))[0] != 0);
            F.ret = t20;
            goto gm_fret;
        }
        gm_l8 = t14;
    }
    {
        gm_tuglhjjmj2g58 t23 = gm_l8;
        gm_b16_8 t24 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t23));
        gm_l9 = t24;
    }
    {
        void * t25 = gm_l0;
        uint32_t t26 = gm_f_fresh_5kokjmg(t25);
        gm_t88rcucg1b7uk t27;
        GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_jagvq3q((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src2j6ljeja7ilp, 345, 10);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_l10 = t30;
    }
    {
        void * t31 = gm_l0;
        uint32_t t32 = gm_f_fresh_5kokjmg(t31);
        gm_t88rcucg1b7uk t33;
        GM_ST(uint32_t, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_v2sfrt0((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t35 = t34;
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src2j6ljeja7ilp, 346, 10);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
        gm_l11 = t36;
    }
    {
        gm_t3877pk7lv748 t37;
        gm_b16_8 t38;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (!t40) goto ie39;
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sitlbs33jqqal));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
        gm_b16_8 t43 = t42;
        t38 = t43;
        goto ix39;
ie39:;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s0m9d22aen6f8));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
        gm_b16_8 t46 = t45;
        t38 = t46;
ix39:;
        GM_ST(gm_b16_8, (uint8_t *)&t37 + 0, t38);
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
        uint64_t r48; bool o49 = __builtin_mul_overflow(t47, ((uint64_t)8ULL), &r48);
        if (o49) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 347, 71);
        uint64_t t50 = r48;
        GM_ST(uint64_t, (uint8_t *)&t37 + 16, t50);
        gm_tabb710d9mq2e t51;
        gm_f_format_gf4f6rt((void *)&t51, (void *)NULL, cx, NULL, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t52 = t51;
        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_src2j6ljeja7ilp, 347, 10);
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
        gm_l12 = t53;
    }
    {
        void * t54 = gm_l0;
        gm_th2d9c7aju8hp t55;
        gm_b16_8 t56 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t55 + 0, t56);
        gm_b16_8 t57 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t55 + 16, t57);
        gm_b16_8 t58 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t55 + 32, t58);
        gm_b16_8 t59 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t55 + 48, t59);
        gm_b16_8 t60 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t55 + 64, t60);
        uint64_t t61 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t61 >= (uintptr_t)(t62)) gm_trap_index(t61, t62, gm_src2j6ljeja7ilp, 348, 85);
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t61 * 16));
        GM_ST(gm_b16_8, (uint8_t *)&t55 + 80, t63);
        gm_b16_8 t64 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t55 + 96, t64);
        gm_b0_1 t65 = gm_f_linef_ahjvru7((void *)NULL, cx, NULL, t54, &t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tuglhjjmj2g58 t67 = gm_l8;
        uint8_t t68 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t67));
        uint8_t t69 = (uint8_t)!t68;
        if (!t69) goto ie66;
        {
            void * t71 = gm_l0;
            gm_t88rcucg1b7uk t72 = gm_l3;
            gm_tuglhjjmj2g58 t73 = gm_l8;
            gm_tbci5ospk5nnn t74;
            gm_f_ii_min_lku7pdi((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t75 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t71, t72, &t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t75;
        }
        {
            void * t76 = gm_l0;
            gm_t88rcucg1b7uk t77 = gm_l3;
            gm_tuglhjjmj2g58 t78 = gm_l8;
            gm_tbci5ospk5nnn t79;
            gm_f_ii_max_gtp5h90((void *)&t79, (void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t80 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t76, t77, &t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t80;
        }
        {
            void * t81 = gm_l0;
            gm_tif3elqk01bdl t82;
            gm_b16_8 t83 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t83);
            gm_b16_8 t84 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 16, t84);
            gm_b16_8 t85 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 32, t85);
            gm_b16_8 t86 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 48, t86);
            gm_b16_8 t87 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 64, t87);
            gm_b16_8 t88 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 80, t88);
            gm_b0_1 t89 = gm_f_linef_calnga5((void *)NULL, cx, NULL, t81, &t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        uint8_t t91 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t91) goto ie90;
        void * t92 = gm_l0;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t93) + 1136) + 0));
        if (!t94) goto ie90;
        {
            void * t96 = gm_l0;
            gm_t88rcucg1b7uk t97 = gm_l3;
            gm_b16_8 t98 = gm_l6;
            gm_b16_8 t99 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t96, t97, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t99;
        }
        {
            void * t100 = gm_l0;
            gm_t8fvq98mjik04 t101;
            memset((uint8_t *)&t101 + 37, 0, 3);
            gm_b16_8 t102 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
            gm_b16_8 t103 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t101 + 16, t103);
            uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
            GM_ST(uint32_t, (uint8_t *)&t101 + 32, t104);
            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            GM_ST(uint8_t, (uint8_t *)&t101 + 36, t105);
            uint64_t t106 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_src2j6ljeja7ilp, 357, 116);
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t106 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t101 + 40, t108);
            gm_b16_8 t109 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t101 + 56, t109);
            gm_b0_1 t110 = gm_f_linef_3utfuce((void *)NULL, cx, NULL, t100, &t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix90;
ie90:;
ix90:;
    }
    {
        gm_t6j6smj7qoctu t111;
        gm_b16_8 t112 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t112);
        uint64_t t113 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_src2j6ljeja7ilp, 359, 56);
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t113 * 16));
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 16, t115);
        gm_tabb710d9mq2e t116;
        gm_f_format_qofaffd((void *)&t116, (void *)NULL, cx, NULL, &t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t117 = t116;
        if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src2j6ljeja7ilp, 359, 12);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
        gm_l16 = t118;
    }
    {
        void * t119 = gm_l0;
        gm_t9qkh0rsooc8d t120 = gm_l2;
        gm_tuglhjjmj2g58 t121 = gm_l8;
        gm_t88rcucg1b7uk t122 = gm_l3;
        gm_t88rcucg1b7uk t123 = gm_l5;
        gm_b16_8 t124 = gm_l10;
        gm_b16_8 t125 = t124;
        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
        gm_b16_8 t127 = gm_l11;
        gm_b16_8 t128 = t127;
        gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
        gm_b16_8 t130 = gm_l16;
        gm_b16_8 t131 = t130;
        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
        gm_b16_8 t133 = gm_l7;
        gm_b16_8 t134;
        GM_ST(void *, (uint8_t *)&t134, (void *)(gm_svpofgjtij7vj));
        GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(3));
        gm_t5o0tpv5jka2l t135;
        gm_f_finish_int_cr0p55i((void *)&t135, (void *)NULL, cx, NULL, t119, t120, t121, t122, t123, t126, t129, t132, t133, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t136 = t135;
        if (((((uint8_t *)&t136))[0] != 0)) {
            gm_t08olq97rscn3 t137 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t136) + 8));
            gm_t5o0tpv5jka2l t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
            ((uint8_t *)&t138)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t138 + 8), t137);
            F.ret = t138;
            goto gm_fret;
        }
        gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&t136) + 8));
        gm_t5o0tpv5jka2l t140;
        GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t140 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t140 + 32, 0);
        ((uint8_t *)&t140)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t140 + 8), t139);
        gm_t5o0tpv5jka2l t141 = t140;
        uint8_t t142 = ((((uint8_t *)&t141))[0] != 0);
        F.ret = t140;
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
__attribute__((aligned(4))) void gm_f_rotate_tpbpt1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_rotate_tpbpt1c F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* vals */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* v0 */
    gm_tuglhjjmj2g58 gm_l7; /* ii */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* k0 */
    gm_b16_8 gm_l10; /* k */
    gm_b16_8 gm_l11; /* c */
    gm_b16_8 gm_l12; /* r */
    gm_b16_8 gm_l13; /* n */
    GM_STACK_CHECK(&F, 1254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_src2j6ljeja7ilp, 298, 11);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t4 * 16));
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_tqcc6dmlne11b t9;
        gm_f_int_info_2tcdf1n((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqcc6dmlne11b t10 = t9;
        if (!((((uint8_t *)&t10))[16] != 0)) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 299, 11);
        gm_tuglhjjmj2g58 t11 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t10));
        gm_l7 = t11;
    }
    {
        gm_t6j6smj7qoctu t12;
        gm_tuglhjjmj2g58 t13 = gm_l7;
        gm_b16_8 t14 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t13));
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 0, t14);
        gm_b16_8 t15 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 16, t15);
        gm_tabb710d9mq2e t16;
        gm_f_format_21vnq4a((void *)&t16, (void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_src2j6ljeja7ilp, 300, 10);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
        gm_l8 = t18;
    }
    {
        uint64_t t19 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src2j6ljeja7ilp, 301, 38);
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 16));
        gm_tnpifv20irnph t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
        gm_tabb710d9mq2e t23;
        gm_f_format_h95c7u1((void *)&t23, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_src2j6ljeja7ilp, 301, 11);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_l9 = t25;
    }
    {
        gm_b16_8 t26;
        uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)32ULL)));
        if (!t28) goto ie27;
        gm_b16_8 t30 = gm_l9;
        gm_b16_8 t31 = t30;
        gm_b16_8 t32 = t31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        t26 = t33;
        goto ix27;
ie27:;
        gm_t5g5vgung6h6s t35;
        memset((uint8_t *)&t35 + 4, 0, 4);
        memset((uint8_t *)&t35 + 28, 0, 4);
        uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
        GM_ST(uint32_t, (uint8_t *)&t35 + 0, t36);
        gm_b16_8 t37 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t35 + 8, t37);
        uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
        GM_ST(uint32_t, (uint8_t *)&t35 + 24, t38);
        gm_tabb710d9mq2e t39;
        gm_f_format_aghnr00((void *)&t39, (void *)NULL, cx, NULL, &t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t40 = t39;
        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_src2j6ljeja7ilp, 303, 46);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
        gm_b16_8 t42 = t41;
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        t26 = t44;
ix27:;
        gm_l10 = t26;
    }
    {
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
        void * t47 = (void *)(((uint8_t *)t46) + 8);
        gm_t88rcucg1b7uk t48 = gm_l5;
        gm_b16_8 t49 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t49;
    }
    {
        void * t50 = gm_l0;
        gm_t88rcucg1b7uk t51 = gm_l5;
        gm_tj4tbafenpsjm t52;
        memset((uint8_t *)&t52 + 52, 0, 4);
        gm_b16_8 t53 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
        gm_b16_8 t54 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t54);
        gm_b16_8 t55 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 32, t55);
        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
        GM_ST(uint32_t, (uint8_t *)&t52 + 48, t56);
        gm_tabb710d9mq2e t57;
        gm_f_format_898275p((void *)&t57, (void *)NULL, cx, NULL, &t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t58 = t57;
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_src2j6ljeja7ilp, 305, 20);
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
        gm_b16_8 t60 = t59;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_b16_8 t62 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t50, t51, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t62;
    }
    {
        gm_tuglhjjmj2g58 t63 = gm_l7;
        gm_b16_8 t64 = gm_l12;
        gm_b16_8 t65 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t65;
    }
    {
        void * t66 = gm_l0;
        gm_t88rcucg1b7uk t67 = gm_l5;
        gm_b16_8 t68 = gm_l13;
        gm_b16_8 t69 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t66, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 32, 0);
        ((uint8_t *)&t70)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 8), t69);
        gm_t5o0tpv5jka2l t71 = t70;
        uint8_t t72 = ((((uint8_t *)&t71))[0] != 0);
        F.ret = t70;
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
__attribute__((aligned(4))) void gm_f_sys_builtin_svdoho3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3) {
    struct gm_fr_sys_builtin_svdoho3 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t9qkh0rsooc8d gm_l2; /* sop */
    gm_b16_8 gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint32_t gm_l5; /* k */
    uint32_t gm_l6; /* k */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* addr */
    gm_b16_8 gm_l9; /* s */
    gm_b16_8 gm_l10; /* some */
    gm_b16_8 gm_l11; /* none */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    void * gm_l16; /* a */
    gm_b16_8 gm_l17; /* v */
    gm_b16_8 gm_l18; /* addr */
    gm_b16_8 gm_l19; /* call */
    GM_STACK_CHECK(&F, 2924u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)44ULL)));
        if (!t5) goto ie4;
        {
            void * t7 = gm_l0;
            uint32_t t8 = gm_f_fresh_5kokjmg(t7);
            gm_l5 = t8;
        }
        {
            void * t9 = gm_l0;
            uint32_t t10 = gm_l5;
            uint32_t t11 = gm_l5;
            uint32_t t12 = gm_l5;
            gm_tl874t2atnlh3 t13;
            GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t10);
            GM_ST(uint32_t, ((uint8_t *)&t13 + 4), t11);
            GM_ST(uint32_t, ((uint8_t *)&t13 + 8), t12);
            gm_b0_1 t14 = gm_f_linef_5q2h82g((void *)NULL, cx, NULL, t9, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t15 = gm_l0;
            gm_t88rcucg1b7uk t16 = gm_l4;
            uint32_t t17 = gm_l5;
            gm_t88rcucg1b7uk t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t17);
            gm_tabb710d9mq2e t19;
            gm_f_format_ucng4n7((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_src2j6ljeja7ilp, 222, 33);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = t21;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            uint32_t t24 = gm_l5;
            gm_t88rcucg1b7uk t25;
            GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t24);
            gm_tabb710d9mq2e t26;
            gm_f_format_3itad0c((void *)&t26, (void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src2j6ljeja7ilp, 222, 59);
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_b16_8 t29 = t28;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_b16_8 t31 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t15, t16, t23, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
            ((uint8_t *)&t32)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
            gm_t5o0tpv5jka2l t33 = t32;
            uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
            F.ret = t32;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t36 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)45ULL)));
        if (!t36) goto ie35;
        {
            void * t38 = gm_l0;
            uint32_t t39 = gm_f_fresh_5kokjmg(t38);
            gm_l6 = t39;
        }
        {
            void * t40 = gm_l0;
            uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src2j6ljeja7ilp, 226, 27);
            void * t43 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t41 * 224);
            gm_t5o0tpv5jka2l t44;
            gm_f_expr_porbc9t((void *)&t44, (void *)NULL, cx, NULL, t40, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t45 = t44;
            if (((((uint8_t *)&t45))[0] != 0)) {
                gm_t08olq97rscn3 t46 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t45) + 8));
                gm_t5o0tpv5jka2l t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                ((uint8_t *)&t47)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t47 + 8), t46);
                F.ret = t47;
                goto gm_fret;
            }
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
            gm_l7 = t48;
        }
        {
            void * t49 = gm_l0;
            uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src2j6ljeja7ilp, 227, 28);
            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t50 * 224) + 0));
            gm_b16_8 t53 = gm_l7;
            gm_b16_8 t54 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t49, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t54;
        }
        {
            void * t55 = gm_l0;
            uint32_t t56 = gm_l6;
            uint32_t t57 = gm_l6;
            gm_tcc1rq4n1avb2 t58;
            GM_ST(uint32_t, ((uint8_t *)&t58 + 0), t56);
            GM_ST(uint32_t, ((uint8_t *)&t58 + 4), t57);
            gm_b0_1 t59 = gm_f_linef_vgjqckp((void *)NULL, cx, NULL, t55, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t60 = gm_l0;
            gm_t133qiba7tvhc t61;
            memset((uint8_t *)&t61 + 4, 0, 4);
            uint32_t t62 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t61 + 0, t62);
            gm_b16_8 t63 = gm_l8;
            gm_b16_8 t64 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t61 + 8, t64);
            void * t65 = gm_l0;
            gm_b16_8 t66 = gm_l8;
            gm_b16_8 t67 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t61 + 24, t67);
            uint32_t t68 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t61 + 40, t68);
            uint32_t t69 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t61 + 44, t69);
            gm_b0_1 t70 = gm_f_linef_7pb5lg6((void *)NULL, cx, NULL, t60, &t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t71 = gm_l0;
            void * t72 = gm_l0;
            void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
            void * t74 = (void *)(((uint8_t *)t73) + 8);
            gm_t88rcucg1b7uk t75 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t76 = gm_l6;
            gm_t88rcucg1b7uk t77;
            GM_ST(uint32_t, ((uint8_t *)&t77 + 0), t76);
            gm_tabb710d9mq2e t78;
            gm_f_format_tvr4di5((void *)&t78, (void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t79 = t78;
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_src2j6ljeja7ilp, 230, 49);
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
            gm_b16_8 t81 = t80;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            uint32_t t83 = gm_l6;
            gm_t88rcucg1b7uk t84;
            GM_ST(uint32_t, ((uint8_t *)&t84 + 0), t83);
            gm_tabb710d9mq2e t85;
            gm_f_format_he769rg((void *)&t85, (void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t86 = t85;
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src2j6ljeja7ilp, 230, 73);
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
            gm_b16_8 t88 = t87;
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            gm_b16_8 t90 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t71, t75, t82, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t90;
        }
        {
            void * t91 = gm_l0;
            gm_t88rcucg1b7uk t92 = gm_l4;
            gm_b16_8 t93 = gm_l9;
            gm_b16_8 t94 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t91, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t94;
        }
        {
            void * t95 = gm_l0;
            gm_t88rcucg1b7uk t96 = gm_l4;
            gm_b16_8 t97 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t95, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t97;
        }
        {
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l4;
            gm_thognv9odov03 t100;
            memset((uint8_t *)&t100 + 4, 0, 4);
            uint32_t t101 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t100 + 0, t101);
            gm_b16_8 t102 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 8, t102);
            gm_b16_8 t103 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t100 + 24, t103);
            gm_tabb710d9mq2e t104;
            gm_f_format_cjdn0sp((void *)&t104, (void *)NULL, cx, NULL, &t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t105 = t104;
            if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_src2j6ljeja7ilp, 233, 26);
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
            gm_b16_8 t107 = t106;
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
            gm_b16_8 t109 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t98, t99, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t110;
            GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t110 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t110 + 32, 0);
            ((uint8_t *)&t110)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t110 + 8), t109);
            gm_t5o0tpv5jka2l t111 = t110;
            uint8_t t112 = ((((uint8_t *)&t111))[0] != 0);
            F.ret = t110;
            goto gm_fret;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        gm_f_list_epvducg((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l13 = t114;
        }
        {
            for (;;) {
                uint64_t t116 = gm_l14;
                uint64_t t117 = gm_l13;
                uint8_t t118 = (t116 < t117);
                if (!t118) goto lb115;
                {
                    uint64_t t121 = gm_l14;
                    uint64_t t122 = t121;
                    uint64_t t125 = GM_LD(uint64_t, ((uint8_t *)&t122));
                    gm_l15 = t125;
                    {
                        uint64_t t127 = gm_l15;
                        uint64_t t128 = (uintptr_t)t127;
                        uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_src2j6ljeja7ilp, 237, 15);
                        void * t130 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t128 * 224);
                        gm_l16 = t130;
                    }
                    {
                        void * t131 = gm_l0;
                        void * t132 = gm_l16;
                        gm_t5o0tpv5jka2l t133;
                        gm_f_expr_porbc9t((void *)&t133, (void *)NULL, cx, NULL, t131, t132);
                        if (gm_unw.kind) {
                            goto gm_ll119;
                        }
                        gm_t5o0tpv5jka2l t134 = t133;
                        if (((((uint8_t *)&t134))[0] != 0)) {
                            gm_t08olq97rscn3 t135 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t134) + 8));
                            gm_t5o0tpv5jka2l t136;
                            GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                            ((uint8_t *)&t136)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t136 + 8), t135);
                            F.ret = t136;
                            goto gm_fret;
                        }
                        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                        gm_l17 = t137;
                    }
                    {
                        void * t139 = gm_l0;
                        void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
                        void * t141 = (void *)(((uint8_t *)t140) + 8);
                        void * t142 = gm_l16;
                        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t142) + 0));
                        gm_t53at80pfdus3 t144;
                        gm_f_kind_ghvav22((void *)&t144, t141, GM_CONV(gm_csguankko4lmhd, t143));
                        gm_t53at80pfdus3 t145 = t144;
                        uint8_t t146;
                        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t145) + 0));
                        if (t149 != ((uint8_t)10ULL)) goto mn148;
                        t146 = ((uint8_t)1);
                        goto me147;
mn148:;
                        t146 = ((uint8_t)0);
                        goto me147;
mn150:;
                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 239, 12);
me147:;
                        if (!t146) goto ie138;
                        {
                            void * t152 = gm_l0;
                            void * t153 = gm_l16;
                            gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t153) + 0));
                            gm_b16_8 t155 = gm_l17;
                            gm_b16_8 t156 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t152, t154, t155);
                            if (gm_unw.kind) {
                                goto gm_ll119;
                            }
                            gm_l18 = t156;
                        }
                        {
                            void * t157 = (void *)((uint8_t *)&F.l12);
                            gm_t6j6smj7qoctu t158;
                            gm_b16_8 t159 = gm_l18;
                            gm_b16_8 t160 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t159);
                            if (gm_unw.kind) {
                                goto gm_ll119;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t158 + 0, t160);
                            void * t161 = gm_l0;
                            gm_b16_8 t162 = gm_l18;
                            gm_b16_8 t163 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t161, t162);
                            if (gm_unw.kind) {
                                goto gm_ll119;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t158 + 16, t163);
                            gm_tabb710d9mq2e t164;
                            gm_f_format_00b54tv((void *)&t164, (void *)NULL, cx, NULL, &t158);
                            if (gm_unw.kind) {
                                goto gm_ll119;
                            }
                            gm_tabb710d9mq2e t165 = t164;
                            if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 8, gm_src2j6ljeja7ilp, 241, 24);
                            gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&t165) + 8));
                            gm_b16_8 t167 = t166;
                            gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&t167));
                            gm_tftb1odc739t5 t169 = gm_f_push_3ef746b(t157, t168);
                            gm_tftb1odc739t5 t170 = t169;
                            if (((((uint8_t *)&t170))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t170) + 1, gm_src2j6ljeja7ilp, 241, 13);
                            gm_b0_1 t171 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 1));
                        }
                        goto ix138;
ie138:;
                        {
                            void * t173 = (void *)((uint8_t *)&F.l12);
                            gm_b16_8 t174 = gm_l17;
                            gm_tftb1odc739t5 t175 = gm_f_push_3ef746b(t173, t174);
                            gm_tftb1odc739t5 t176 = t175;
                            if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_src2j6ljeja7ilp, 243, 13);
                            gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
                        }
ix138:;
                    }
                    goto me123;
mn124:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 236, 5);
me123:;
                }
lc115:;
                {
                    uint64_t t179 = gm_l14;
                    uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                    uint64_t t182 = r180;
                    gm_l14 = t182;
                }
            }
gm_ll119:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb115; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc115; }
            }
            goto gm_lad;
lb115:;
        }
    }
    {
        gm_t6j6smj7qoctu t183;
        gm_t9qkh0rsooc8d t184 = gm_l2;
        gm_b16_8 t185 = gm_f_sys_name_pcdoujk(t184);
        GM_ST(gm_b16_8, (uint8_t *)&t183 + 0, t185);
        gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        gm_b16_8 t187 = t186;
        gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
        gm_b16_8 t189;
        GM_ST(void *, (uint8_t *)&t189, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t190;
        gm_f_join_sfknljb((void *)&t190, (void *)NULL, cx, NULL, t188, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t191 = t190;
        if (((((uint8_t *)&t191))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t191) + 8, gm_src2j6ljeja7ilp, 246, 56);
        gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&t191) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t183 + 16, t192);
        gm_tabb710d9mq2e t193;
        gm_f_format_m7gv7a8((void *)&t193, (void *)NULL, cx, NULL, &t183);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t194 = t193;
        if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 8, gm_src2j6ljeja7ilp, 246, 13);
        gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&t194) + 8));
        gm_l19 = t195;
    }
    {
        void * t197 = gm_l0;
        void * t198 = GM_LD(void *, (((uint8_t *)t197) + 0));
        void * t199 = (void *)(((uint8_t *)t198) + 8);
        gm_t88rcucg1b7uk t200 = gm_l4;
        uint8_t t201 = gm_f_is_unit_17uennd(t199, GM_CONV(gm_csguankko4lmhd, t200));
        if (!t201) goto ie196;
        {
            void * t203 = gm_l0;
            gm_b16_8 t204 = gm_l19;
            gm_tnpifv20irnph t205;
            GM_ST(gm_b16_8, ((uint8_t *)&t205 + 0), t204);
            gm_b0_1 t206 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t203, t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t207 = gm_l0;
            gm_b16_8 t208 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t209;
            GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t209 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t209 + 32, 0);
            ((uint8_t *)&t209)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t209 + 8), t208);
            gm_t5o0tpv5jka2l t210 = t209;
            uint8_t t211 = ((((uint8_t *)&t210))[0] != 0);
            F.ret = t209;
            goto gm_fret;
        }
        goto ix196;
ie196:;
ix196:;
    }
    {
        void * t212 = gm_l0;
        gm_t88rcucg1b7uk t213 = gm_l4;
        gm_b16_8 t214 = gm_l19;
        gm_b16_8 t215 = t214;
        gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
        gm_b16_8 t217 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t212, t213, t216);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t218;
        GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t218 + 32, 0);
        ((uint8_t *)&t218)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t218 + 8), t217);
        gm_t5o0tpv5jka2l t219 = t218;
        uint8_t t220 = ((((uint8_t *)&t219))[0] != 0);
        F.ret = t218;
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

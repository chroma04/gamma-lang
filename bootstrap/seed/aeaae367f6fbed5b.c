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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; } gm_t0j6181meiu4s;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t f3; uint8_t p4[3]; gm_b16_8 f4; gm_b16_8 f5; } gm_t8fvq98mjik04;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_t0j6181meiu4s) == 72 && _Alignof(gm_t0j6181meiu4s) == 8 && offsetof(gm_t0j6181meiu4s, f0) == 0 && offsetof(gm_t0j6181meiu4s, f1) == 16 && offsetof(gm_t0j6181meiu4s, f2) == 32 && offsetof(gm_t0j6181meiu4s, f3) == 48 && offsetof(gm_t0j6181meiu4s, f4) == 64, "layout of `(string, string, string, string, u64)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t8fvq98mjik04) == 72 && _Alignof(gm_t8fvq98mjik04) == 8 && offsetof(gm_t8fvq98mjik04, f0) == 0 && offsetof(gm_t8fvq98mjik04, f1) == 16 && offsetof(gm_t8fvq98mjik04, f2) == 32 && offsetof(gm_t8fvq98mjik04, f3) == 36 && offsetof(gm_t8fvq98mjik04, f4) == 40 && offsetof(gm_t8fvq98mjik04, f5) == 56, "layout of `([]u8, string, u32, bool, string, string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
struct gm_fr_fn_4hh2oe6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_52g9cul {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_64h9jod {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8g1vqf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b6b2hdm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_coqeoli {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_f95ec3e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kdhucvo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nranked {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tnsealv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_349v8tu {
    gm_frame h;
    void * l4; /* out */
    gm_b0_1 ret;
};
struct gm_fr_linef_35fm5u5 {
    gm_frame h;
    void * l4; /* out */
    gm_b0_1 ret;
};
struct gm_fr_linef_3dvovo0 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    void * l4; /* out */
    gm_tfeab279c16rr l5; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_3fol08t {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    void * l4; /* out */
    gm_tftksr4op114e l5; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_3gi7f6b {
    gm_frame h;
    void * l4; /* out */
    gm_b0_1 ret;
};
struct gm_fr_linef_3utfuce {
    gm_frame h;
    gm_t8fvq98mjik04 l1; /* args */
    void * l4; /* out */
    gm_t8fvq98mjik04 l5; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_j9naoa6 {
    gm_frame h;
    void * l4; /* out */
    gm_b0_1 ret;
};
struct gm_fr_linef_jl1vuq9 {
    gm_frame h;
    void * l4; /* out */
    gm_b0_1 ret;
};
struct gm_fr_linef_jl8o1sr {
    gm_frame h;
    void * l4; /* out */
    gm_b0_1 ret;
};
struct gm_fr_linef_jq6ksk1 {
    gm_frame h;
    gm_t0j6181meiu4s l1; /* args */
    void * l4; /* out */
    gm_t0j6181meiu4s l5; /* <tmp> */
    gm_b0_1 ret;
};
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4hh2oe6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_52g9cul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64h9jod(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8g1vqf2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b6b2hdm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_coqeoli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f95ec3e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kdhucvo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nranked(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tnsealv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_349v8tu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_35fm5u5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3dvovo0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3fol08t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3gi7f6b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3utfuce(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_j9naoa6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl1vuq9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl8o1sr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jq6ksk1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0s45q9kur545[15];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4ou57tmjn6c7[39];
extern const uint8_t gm_s51lfi73niqcd[62];
extern const uint8_t gm_s52jn4s18md6n[16];
extern const uint8_t gm_seqrg7cf0sejb[33];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg71mqv9mtu78[33];
extern const uint8_t gm_shesnhdok5fgk[55];
extern const uint8_t gm_sjojf6rrbl49u[6];
extern const uint8_t gm_skj1a463v050q[79];
extern const uint8_t gm_skjfb4faajq73[13];
extern const uint8_t gm_slnfks4sl1aoc[8];
extern const uint8_t gm_snkldl7hr4htt[4];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_spffvcegd6os6[6];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sq12h87ngj5nl[9];
extern const char gm_srcmok80ho8hh01[];
extern const uint8_t gm_sudnegb3umvac[12];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_ty929lennfd3;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4hh2oe6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4hh2oe6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_jl1vuq9 *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_52g9cul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_52g9cul F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_3gi7f6b *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64h9jod(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_64h9jod F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_3utfuce *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8g1vqf2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8g1vqf2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_3fol08t *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b6b2hdm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b6b2hdm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_35fm5u5 *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_coqeoli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_coqeoli F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_jq6ksk1 *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f95ec3e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_f95ec3e F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_j9naoa6 *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kdhucvo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kdhucvo F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_3dvovo0 *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nranked(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nranked F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_349v8tu *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tnsealv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tnsealv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_linef_jl8o1sr *)((&F.h)->up))->l4));
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 446, 24);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4hh2oe6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_52g9cul(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_64h9jod(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8g1vqf2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b6b2hdm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_coqeoli(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f95ec3e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kdhucvo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nranked(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tnsealv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_349v8tu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_349v8tu F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tnpifv20irnph gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_tnpifv20irnph t30 = gm_l1;
            gm_l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_nranked, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t33 = t32;
            gm_closure t34 = t31;
            gm_node *ccx35 = t34.cx;
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_s51lfi73niqcd, 61)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_s4a980fahtblo, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 184);
        gm_tftb1odc739t5 t38 = gm_f_push_t83rg2m(t37, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t39 = t38;
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_35fm5u5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_35fm5u5 F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_t6j6smj7qoctu t30 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_b6b2hdm, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
            gm_b16_8 t35 = t34;
            gm_closure t36 = t31;
            gm_node *ccx37 = t36.cx;
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tybihgc69jv3, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 184);
        gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t39, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3dvovo0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3dvovo0 F;
    void * gm_l0; /* fg */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_tfeab279c16rr t30 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a1));
            F.l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_kdhucvo, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
            gm_b16_8 t37 = t36;
            gm_closure t38 = t31;
            gm_node *ccx39 = t38.cx;
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_sudnegb3umvac, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t38.code, t38.env, ccx39, &gm_tybihgc69jv3, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t38.code, t38.env, ccx39, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t38.code, t38.env, ccx39, &gm_ty0iqiubfm7e, ((uint8_t *)&t37), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_sq12h87ngj5nl, 8)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 184);
        gm_tftb1odc739t5 t42 = gm_f_push_t83rg2m(t41, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3fol08t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3fol08t F;
    void * gm_l0; /* fg */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 329u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_tftksr4op114e t30 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a1));
            F.l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_8g1vqf2, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
            gm_b16_8 t35 = t34;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
            uint64_t t37 = t36;
            gm_closure t38 = t31;
            gm_node *ccx39 = t38.cx;
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_s4ou57tmjn6c7, 38)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t38.code, t38.env, ccx39, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_sg71mqv9mtu78, 32)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t38.code, t38.env, ccx39, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_s0m0qaokk51g0, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t38.code, t38.env, ccx39, &gm_tyb2nno7n67r, ((uint8_t *)&t37), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t38.code, t38.env, ccx39, gm_s0s45q9kur545, 14)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 184);
        gm_tftb1odc739t5 t42 = gm_f_push_t83rg2m(t41, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3gi7f6b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3gi7f6b F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_t6j6smj7qoctu t30 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_52g9cul, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
            gm_b16_8 t35 = t34;
            gm_closure t36 = t31;
            gm_node *ccx37 = t36.cx;
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_spffvcegd6os6, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_slnfks4sl1aoc, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tybihgc69jv3, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 184);
        gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t39, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3utfuce(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3utfuce F;
    void * gm_l0; /* fg */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 419u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_t8fvq98mjik04 t30 = GM_LD(gm_t8fvq98mjik04, ((uint8_t *)a1));
            F.l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_64h9jod, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
            gm_b16_8 t35 = t34;
            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 32));
            uint32_t t37 = t36;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 36));
            uint8_t t39 = t38;
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 56));
            gm_b16_8 t43 = t42;
            gm_closure t44 = t31;
            gm_node *ccx45 = t44.cx;
            if (gm_pf0aqhubr4up(t44.code, t44.env, ccx45, gm_sns1hl83hdlcn, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t44.code, t44.env, ccx45, &gm_tybihgc69jv3, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t44.code, t44.env, ccx45, gm_seqrg7cf0sejb, 32)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t44.code, t44.env, ccx45, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t44.code, t44.env, ccx45, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t44.code, t44.env, ccx45, &gm_ty467d5cm1d5, ((uint8_t *)&t37), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t44.code, t44.env, ccx45, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t44.code, t44.env, ccx45, &gm_ty929lennfd3, ((uint8_t *)&t39), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t44.code, t44.env, ccx45, gm_skjfb4faajq73, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t44.code, t44.env, ccx45, &gm_ty0iqiubfm7e, ((uint8_t *)&t41), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t44.code, t44.env, ccx45, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t44.code, t44.env, ccx45, &gm_ty0iqiubfm7e, ((uint8_t *)&t43), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t44.code, t44.env, ccx45, gm_s4a980fahtblo, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t46 = gm_l0;
        void * t47 = (void *)(((uint8_t *)t46) + 184);
        gm_tftb1odc739t5 t48 = gm_f_push_t83rg2m(t47, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_j9naoa6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t4ce0jd3tdro4 a1) {
    struct gm_fr_linef_j9naoa6 F;
    void * gm_l0; /* fg */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t4ce0jd3tdro4 gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_t4ce0jd3tdro4 t30 = gm_l1;
            gm_l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_f95ec3e, (void *)&F.h, cx});
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
            uint64_t t33 = t32;
            gm_closure t34 = t31;
            gm_node *ccx35 = t34.cx;
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_sjojf6rrbl49u, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty6groq37ram, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 184);
        gm_tftb1odc739t5 t38 = gm_f_push_t83rg2m(t37, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t39 = t38;
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl1vuq9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_jl1vuq9 F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tnpifv20irnph gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_tnpifv20irnph t30 = gm_l1;
            gm_l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_4hh2oe6, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t33 = t32;
            gm_closure t34 = t31;
            gm_node *ccx35 = t34.cx;
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_skj1a463v050q, 78)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t34.code, t34.env, ccx35, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t34.code, t34.env, ccx35, gm_s4a980fahtblo, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 184);
        gm_tftb1odc739t5 t38 = gm_f_push_t83rg2m(t37, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t39 = t38;
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl8o1sr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_jl8o1sr F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_t6j6smj7qoctu t30 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
            gm_l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_tnsealv, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
            gm_b16_8 t35 = t34;
            gm_closure t36 = t31;
            gm_node *ccx37 = t36.cx;
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_spffvcegd6os6, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tybihgc69jv3, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_shesnhdok5fgk, 54)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_s4a980fahtblo, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 184);
        gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t39, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jq6ksk1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_jq6ksk1 F;
    void * gm_l0; /* fg */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 425u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 443, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 443, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        F.l4 = t28;
    }
    {
        {
            gm_t0j6181meiu4s t30 = GM_LD(gm_t0j6181meiu4s, ((uint8_t *)a1));
            F.l5 = t30;
        }
        {
            gm_closure t31 = ((gm_closure){(void *)gm_f_fn_coqeoli, (void *)&F.h, cx});
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 32));
            gm_b16_8 t37 = t36;
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 48));
            gm_b16_8 t39 = t38;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 64));
            uint64_t t41 = t40;
            gm_closure t42 = t31;
            gm_node *ccx43 = t42.cx;
            if (gm_pf0aqhubr4up(t42.code, t42.env, ccx43, gm_sns1hl83hdlcn, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t42.code, t42.env, ccx43, &gm_ty0iqiubfm7e, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t42.code, t42.env, ccx43, gm_s52jn4s18md6n, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t42.code, t42.env, ccx43, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t42.code, t42.env, ccx43, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t42.code, t42.env, ccx43, &gm_ty0iqiubfm7e, ((uint8_t *)&t37), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t42.code, t42.env, ccx43, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t42.code, t42.env, ccx43, &gm_ty0iqiubfm7e, ((uint8_t *)&t39), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t42.code, t42.env, ccx43, gm_snkldl7hr4htt, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t42.code, t42.env, ccx43, &gm_tyb2nno7n67r, ((uint8_t *)&t41), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t42.code, t42.env, ccx43, gm_s4a980fahtblo, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        void * t44 = gm_l0;
        void * t45 = (void *)(((uint8_t *)t44) + 184);
        gm_tftb1odc739t5 t46 = gm_f_push_t83rg2m(t45, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t47 = t46;
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcmok80ho8hh01, 447, 5);
        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
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

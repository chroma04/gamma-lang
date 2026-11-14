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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tcrdb1inj22os;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[1]; uint32_t f5; } gm_th32vq80aukjr;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th32vq80aukjr) == 32 && _Alignof(gm_th32vq80aukjr) == 8 && offsetof(gm_th32vq80aukjr, f0) == 0 && offsetof(gm_th32vq80aukjr, f1) == 8 && offsetof(gm_th32vq80aukjr, f2) == 24 && offsetof(gm_th32vq80aukjr, f3) == 25 && offsetof(gm_th32vq80aukjr, f4) == 26 && offsetof(gm_th32vq80aukjr, f5) == 28, "layout of `(u8, string, bool, bool, bool, u32)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; uint32_t f2; uint8_t pe[4]; } gm_tk0navpiff9ca;
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_tk0navpiff9ca) == 56 && _Alignof(gm_tk0navpiff9ca) == 8 && offsetof(gm_tk0navpiff9ca, f0) == 0 && offsetof(gm_tk0navpiff9ca, f1) == 8 && offsetof(gm_tk0navpiff9ca, f2) == 48, "layout of `Dump`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
struct gm_fr_binop_text_t4cv0b4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_builtin_text_od38u7h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_coercion_text_g32oppe {
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
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_escaped_k5fm4jl {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_flavor_text_3ffnbt7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_label_9hbn37h {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* args */
    gm_b16_8 ret;
};
struct gm_fr_fn_pfpd9oj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_text_75tuacb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_place_lj97s4p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_rmw_text_te4ol3u {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_show_rat_1s624db {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sink_text_s4o8tr0 {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_b16_8 ret;
};
struct gm_fr_soa_op_text_ud90n2l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_by_egbtonb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_spec_text_fr9884d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sys_text_9qjnpra {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_target_text_4kebb7t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tir_dump_ppa6e9g {
    gm_frame h;
    void * l0; /* s */
    gm_trqfvmjtt4sij l1; /* order */
    gm_tk0navpiff9ca l6; /* d */
    gm_b16_8 ret;
};
struct gm_fr_write_arm_4vimkqo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_block_q5mpqjf {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* names */
    gm_b0_1 ret;
};
struct gm_fr_write_conds_k9e57un {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_expr_cftucor {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* kids */
    gm_tbci5ospk5nnn l6; /* v */
    gm_t1cs2fjnrqap1 l48; /* lit */
    gm_trqfvmjtt4sij l49; /* labels */
    gm_t710lsrq1m8gk l53; /* w */
    gm_tsth9qu1dv0g3 l69; /* callee */
    gm_t710lsrq1m8gk l110; /* x */
    gm_b0_1 ret;
};
struct gm_fr_write_fn_jkakt7l {
    gm_frame h;
    gm_tsoie1ppgdvcg l11; /* l */
    gm_trqfvmjtt4sij l13; /* names */
    gm_b0_1 ret;
};
struct gm_fr_write_if_oqjiok5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_line_d6lkps7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_loop_u0ja07u {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* x */
    gm_b0_1 ret;
};
struct gm_fr_write_pat_4qu9dcc {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* lo */
    gm_tbci5ospk5nnn l7; /* hi */
    gm_b0_1 ret;
};
struct gm_fr_write_text_h6e1ibp {
    gm_frame h;
    gm_tntn2tf15puib l1; /* w */
    gm_trqfvmjtt4sij l2; /* h */
    gm_b16_8 ret;
};
struct gm_fr_write_with_j37nil7 {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* w */
    gm_trqfvmjtt4sij l11; /* names */
    gm_tvc83pqnepc2e l15; /* hd */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_t4cv0b4(void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_builtin_text_od38u7h(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_coercion_text_g32oppe(void *env, gm_node *cx, gm_node *inst, gm_tmn7n0n9raou5 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_bytes_25mo226(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_flavor_text_3ffnbt7(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_label_9hbn37h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_pfpd9oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_02b5heb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0969kd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0gd6afr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_0n7d8jn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_16dq22e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_1d88j1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_208te7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_2lgoflc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_32bp3b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3669u6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3aao56v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_41c1370(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4i3nb5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0);
__attribute__((aligned(4))) void gm_f_format_556kaa4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5m6a3eh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5up8eqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6b7mjf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6h8qssq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6na8imi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_7316bju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7ejl94l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_85biid9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8k3m4s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8vmpj9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9g7fct9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_9nv39ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_a4vi4f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aq84o6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_arnesr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_booci4l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_c2ann39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_c726p1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cqr3tc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cuuc21t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ejli3qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fi775k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fp763nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_galn15q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_glqqt6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_gnbipfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i37o6qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i4fsdjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i7k9v76(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_irh7ehn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ka8hqpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kj3qi5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
__attribute__((aligned(4))) void gm_f_format_lde77u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lderfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lefnhqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_licpkoa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_llf38gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lpj4rro(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nva8q4r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_oc8ipo6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_olqcc5i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p2bh4bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p7ogfqk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pcmnhpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pvddll3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qcp7nf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qhad3qp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r0u4lpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_r8pfaos(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r9ba7g9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ree2v5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rvu96av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sqbp092(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sugh2p3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t0vntpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t725p24(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vmi8kfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vprps7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_introsort_l0ks50g(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_text_75tuacb(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_place_lj97s4p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_rmw_text_te4ol3u(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sink_text_s4o8tr0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_op_text_ud90n2l(void *env, gm_node *cx, gm_node *inst, gm_tesaltf0mhcki a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_egbtonb(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_spec_text_fr9884d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_text_9qjnpra(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_text_4kebb7t(void *env, gm_node *cx, gm_node *inst, gm_te9hrk0u35rev a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tir_dump_ppa6e9g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7h6pth0(void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7ilko9q(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_arm_4vimkqo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_block_q5mpqjf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_conds_k9e57un(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_expr_cftucor(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_fn_jkakt7l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_if_oqjiok5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_line_d6lkps7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_loop_u0ja07u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_pat_4qu9dcc(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t5i5r03(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_write_text_h6e1ibp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uebhmoa(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_with_j37nil7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s00pihipe0jga[6];
extern const uint8_t gm_s026u6q948evn[5];
extern const uint8_t gm_s0eqrtq5mqb3a[16];
extern const uint8_t gm_s0kvnv9teor3q[4];
extern const uint8_t gm_s0lh54534p3vd[10];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0ve0dba4rtj8[13];
extern const uint8_t gm_s187rb18i1kgk[4];
extern const uint8_t gm_s19069udlvhni[9];
extern const uint8_t gm_s19ndlkgdtviu[6];
extern const uint8_t gm_s1gf327eapo1j[11];
extern const uint8_t gm_s1j5448m6ruti[10];
extern const uint8_t gm_s1js4dn5e65ja[11];
extern const uint8_t gm_s1n20vngt0mkv[3];
extern const uint8_t gm_s1q2nkavemrqf[9];
extern const uint8_t gm_s1q9ms0kviddj[4];
extern const uint8_t gm_s2289h15tkpi1[12];
extern const uint8_t gm_s24chpb914bep[6];
extern const uint8_t gm_s28ijm46j20ds[8];
extern const uint8_t gm_s2a9t8smkevg0[4];
extern const uint8_t gm_s2fdsodctojm1[10];
extern const uint8_t gm_s2fu5nkek0lrq[7];
extern const uint8_t gm_s2h8cr50573go[8];
extern const uint8_t gm_s2iprgandb78l[8];
extern const uint8_t gm_s2kmbpuutp9qq[5];
extern const uint8_t gm_s2ma5cp41jk1a[7];
extern const uint8_t gm_s2nl2e5anglov[4];
extern const uint8_t gm_s2uphqpvb2vhh[4];
extern const uint8_t gm_s2uqljf8sumcn[11];
extern const uint8_t gm_s35gfshei80n5[8];
extern const uint8_t gm_s3ng5nguvd452[5];
extern const uint8_t gm_s3o5gaeteeggu[5];
extern const uint8_t gm_s3ol5q9br79vf[8];
extern const uint8_t gm_s3opc5ivp6e66[4];
extern const uint8_t gm_s3s50a53ci7rk[7];
extern const uint8_t gm_s49o1oiv7k0po[4];
extern const uint8_t gm_s4bdmssec9aiu[4];
extern const uint8_t gm_s4g3rq58567m6[8];
extern const uint8_t gm_s4gilalcmdvu3[8];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s4lt93qhapike[4];
extern const uint8_t gm_s55ccd7do7rbs[11];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s57rgvjb5mj5a[8];
extern const uint8_t gm_s5c5548ll8taj[10];
extern const uint8_t gm_s5dbqnns2epkg[6];
extern const uint8_t gm_s5e0seq0he7ou[10];
extern const uint8_t gm_s5k0vuc678fh1[8];
extern const uint8_t gm_s5n5e484glelv[5];
extern const uint8_t gm_s5udr3eget84q[6];
extern const uint8_t gm_s6aamtn65omr1[7];
extern const uint8_t gm_s6ag7edtaqqri[5];
extern const uint8_t gm_s6edba3m7mc9a[6];
extern const uint8_t gm_s6ipt60dh9o1j[11];
extern const uint8_t gm_s6nm27ehg597q[7];
extern const uint8_t gm_s6ofccoi3ursa[12];
extern const uint8_t gm_s6p9ub6gu603m[7];
extern const uint8_t gm_s6qbnfe2f321h[4];
extern const uint8_t gm_s76ekntop342o[8];
extern const uint8_t gm_s76ud0alkjfse[4];
extern const uint8_t gm_s7d4tu5fkkg12[11];
extern const uint8_t gm_s7dkbj8tvt9e3[7];
extern const uint8_t gm_s7i80akiq0d95[5];
extern const uint8_t gm_s7m3rbk4l87ah[15];
extern const uint8_t gm_s7rltjaeh634d[11];
extern const uint8_t gm_s7u6ij52clodh[7];
extern const uint8_t gm_s85hqf14lbkns[8];
extern const uint8_t gm_s85jtgs7au5b5[9];
extern const uint8_t gm_s8gtcifve79ai[9];
extern const uint8_t gm_s8pk5tcnrk6p2[8];
extern const uint8_t gm_s8s6shfnaqahd[7];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s93umj3p0tala[5];
extern const uint8_t gm_s96j6o9smoqdu[8];
extern const uint8_t gm_s9dgf2prvg715[6];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9h93c7asacna[7];
extern const uint8_t gm_s9i2ld3a42lap[8];
extern const uint8_t gm_s9o8gb20t6jtp[9];
extern const uint8_t gm_s9v13eck7gubi[4];
extern const uint8_t gm_sa0alba1kin3k[8];
extern const uint8_t gm_sa1pcp92k3aj3[7];
extern const uint8_t gm_sa53epbg9iarq[10];
extern const uint8_t gm_sa5s0jools6a2[9];
extern const uint8_t gm_sa8dse4jk5ibv[10];
extern const uint8_t gm_sa902c1tbtg0i[4];
extern const uint8_t gm_sacua9qc75rts[7];
extern const uint8_t gm_sah1jmr01nidj[5];
extern const uint8_t gm_sai3vjkob4s2v[10];
extern const uint8_t gm_sajbccb18c45d[6];
extern const uint8_t gm_sakmuvnjjp217[10];
extern const uint8_t gm_sbckpj1jsdt28[13];
extern const uint8_t gm_sbcq15b9em6fm[9];
extern const uint8_t gm_sbek54n4a6h2o[7];
extern const uint8_t gm_sbgepjj6hgjej[4];
extern const uint8_t gm_sbnms71r2jqr5[7];
extern const uint8_t gm_sbqq5iijjahf9[5];
extern const uint8_t gm_sbsb3nmbfgbv2[4];
extern const uint8_t gm_sbuhie0vb9t62[9];
extern const uint8_t gm_sc03pp1oj3q4k[7];
extern const uint8_t gm_sc24ftfhihd1h[4];
extern const uint8_t gm_sc5qopgd7dm66[8];
extern const uint8_t gm_scb853lmgr58k[6];
extern const uint8_t gm_scd599tsljaee[5];
extern const uint8_t gm_sdak257j1qgl8[6];
extern const uint8_t gm_sdft8ov6bb7s6[8];
extern const uint8_t gm_sdg7bdfivjinb[3];
extern const uint8_t gm_sdh1dnevn1j10[8];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_sdq6l7oddjqfe[12];
extern const uint8_t gm_sds03accef1j8[6];
extern const uint8_t gm_se0a26478itki[7];
extern const uint8_t gm_se0ivu8cf46o8[12];
extern const uint8_t gm_se19nf1421e37[4];
extern const uint8_t gm_sedrsudco1u16[10];
extern const uint8_t gm_seeo26ergtcha[5];
extern const uint8_t gm_seg9l1usqbs3j[4];
extern const uint8_t gm_segr993uh2crm[11];
extern const uint8_t gm_sel2c30qvm74f[7];
extern const uint8_t gm_seodvmll3gsjn[4];
extern const uint8_t gm_sepu95c7o773o[6];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_setcl7798ov89[4];
extern const uint8_t gm_sf5llhuhdlajp[9];
extern const uint8_t gm_sfch9mo7lqp8l[3];
extern const uint8_t gm_sfd0mn3sol25u[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfid9en449upv[11];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sgfufhs3psibd[4];
extern const uint8_t gm_sgk015tlfp2cu[5];
extern const uint8_t gm_sgm565ktbj9ua[12];
extern const uint8_t gm_sgo00f0mafbnd[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sgtriqnjufjbf[10];
extern const uint8_t gm_sh11qqm8nqu68[8];
extern const uint8_t gm_sh4e4kqha06b3[7];
extern const uint8_t gm_sh4niqnmh73co[6];
extern const uint8_t gm_shdnhcpvptc1q[5];
extern const uint8_t gm_shkq4botdoed7[6];
extern const uint8_t gm_shohh3180gu26[7];
extern const uint8_t gm_shtuc6se4lilq[7];
extern const uint8_t gm_shvp36midgf58[10];
extern const uint8_t gm_shvpku7ssg8d7[11];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_si4k9tiofr8t2[7];
extern const uint8_t gm_si9h59pjqftat[5];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sij2r6jhpquej[12];
extern const uint8_t gm_sim8pu6stc37a[11];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_siqgrmkn8n4vn[8];
extern const uint8_t gm_sismsc98l6u4e[4];
extern const uint8_t gm_sit8u7uppaik9[7];
extern const uint8_t gm_sivsapeh482sm[8];
extern const uint8_t gm_sj1m58fkg956m[8];
extern const uint8_t gm_sj696i9uo1a9s[14];
extern const uint8_t gm_sjbhp9l7seful[7];
extern const uint8_t gm_sjbl17pjsjm48[5];
extern const uint8_t gm_sjdrosvte680q[12];
extern const uint8_t gm_sjegrp8u9r3n3[8];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sjfcbh429vmot[9];
extern const uint8_t gm_sjg1ri23hm753[6];
extern const uint8_t gm_sjourjtj9e3to[9];
extern const uint8_t gm_sk2b59ovo43qg[6];
extern const uint8_t gm_sk5s72mtujop8[5];
extern const uint8_t gm_sk721elh8ouv2[11];
extern const uint8_t gm_sk88tp2337lf6[13];
extern const uint8_t gm_skapbb3maknat[5];
extern const uint8_t gm_skbgm82aqj3q0[6];
extern const uint8_t gm_skissj10nclhr[6];
extern const uint8_t gm_skl3b78lo1gln[5];
extern const uint8_t gm_sksprenpp79ih[11];
extern const uint8_t gm_sl37uvr16qsu8[11];
extern const uint8_t gm_slgrfk23llffe[13];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_slmii6hjigrcr[7];
extern const uint8_t gm_sln5qir3tgi7j[10];
extern const uint8_t gm_slnhe1h36jmub[9];
extern const uint8_t gm_slov3heeajhqn[12];
extern const uint8_t gm_sluco9qbar70k[10];
extern const uint8_t gm_sm017vofcj044[12];
extern const uint8_t gm_sm0gs6tv176le[8];
extern const uint8_t gm_sm3dios51ece1[10];
extern const uint8_t gm_smgioupbmjajr[14];
extern const uint8_t gm_smmus0krp2svr[6];
extern const uint8_t gm_smpl91sh0k8v4[9];
extern const uint8_t gm_smtmd013g2edr[4];
extern const uint8_t gm_smuec3qfsdh82[10];
extern const uint8_t gm_sn581p3lbgrjg[9];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_snaq7hvenqsum[4];
extern const uint8_t gm_snbhl8dsqq3ln[7];
extern const uint8_t gm_sngdu4l8vh3l8[5];
extern const uint8_t gm_snq4uaiieftl8[6];
extern const uint8_t gm_snstimc3n04u0[12];
extern const uint8_t gm_so1u9tqjfpsmc[9];
extern const uint8_t gm_so2sjlodq81qn[13];
extern const uint8_t gm_so3oq96gr0k5r[10];
extern const uint8_t gm_so8fkrgrobcsk[9];
extern const uint8_t gm_sobcm12sogdng[5];
extern const uint8_t gm_soe4gvhg9ei97[4];
extern const uint8_t gm_soerl6rds8b9v[12];
extern const uint8_t gm_sofbtko7t8taa[5];
extern const uint8_t gm_solj59tk4ri3k[9];
extern const uint8_t gm_sovel03lmfl8u[9];
extern const uint8_t gm_sp2smaof0i4j2[10];
extern const uint8_t gm_sp4uhnkunj2cg[3];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spoh8ft9n8oqt[4];
extern const uint8_t gm_spr274eleu2lj[4];
extern const uint8_t gm_spskhsf5tnedj[10];
extern const uint8_t gm_sq0h7jjc6dtk5[4];
extern const uint8_t gm_sqa0b48i8mvk7[6];
extern const uint8_t gm_sqflguscq1uc5[6];
extern const uint8_t gm_sqfu5kl4vat80[9];
extern const uint8_t gm_sqhhm0h7b3asd[6];
extern const uint8_t gm_sqhkrul9t68sa[8];
extern const uint8_t gm_sqlk0h0kae56e[8];
extern const uint8_t gm_sqrv416075ito[3];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr899mamktpvo[7];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const uint8_t gm_src5778gpfoh9[9];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srco4ljquuk5d87[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srjd0sq8k3d7s[11];
extern const uint8_t gm_srmrjv19jgvks[11];
extern const uint8_t gm_srntlv7ksi1vk[16];
extern const uint8_t gm_sromjese8e416[14];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_ss27a20sarlvg[7];
extern const uint8_t gm_ss308e7joejdo[13];
extern const uint8_t gm_ss6dev5u7tl99[12];
extern const uint8_t gm_ss869lnvmj3m8[6];
extern const uint8_t gm_sse9dgkusbtnf[5];
extern const uint8_t gm_ssgeom7733jik[5];
extern const uint8_t gm_ssgn9mppmu3vl[16];
extern const uint8_t gm_ssigpbgi88it3[4];
extern const uint8_t gm_ssjmv3618n779[10];
extern const uint8_t gm_sstrtuk2b4jdg[10];
extern const uint8_t gm_st0l9v6pp5i7b[14];
extern const uint8_t gm_st1pc9bsku00q[9];
extern const uint8_t gm_st7pul9bg6mqu[8];
extern const uint8_t gm_st87fk7ojs4l8[6];
extern const uint8_t gm_st8ki5f85lc2j[13];
extern const uint8_t gm_std59a9b6lpnn[10];
extern const uint8_t gm_stdefoi5sk91c[12];
extern const uint8_t gm_stehcnejvoo24[6];
extern const uint8_t gm_stft4dc50tdce[8];
extern const uint8_t gm_stgqtu1f7bco8[8];
extern const uint8_t gm_stidgejoe9ul2[11];
extern const uint8_t gm_stivl0smu8eq0[9];
extern const uint8_t gm_stn52kuiq0jk2[12];
extern const uint8_t gm_stou1l3qp844f[10];
extern const uint8_t gm_su3ahsl9p584f[9];
extern const uint8_t gm_sugq7dl55ocon[7];
extern const uint8_t gm_sukutl224cgkm[8];
extern const uint8_t gm_sun4qq3op5opf[7];
extern const uint8_t gm_supvu1ruutbov[9];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_sv3olmne7h13s[4];
extern const uint8_t gm_sv8lqdk5nsv8i[5];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const uint8_t gm_svqq3k9j6h1lu[8];
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_escaped_k5fm4jl F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* bytes */
    GM_STACK_CHECK(&F, 147u);
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
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srco4ljquuk5d87, 408, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        gm_b16_8 t6 = gm_l1;
        void * t7 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t8 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_rat_1s624db F;
    GM_STACK_CHECK(&F, 73u);
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
        gm_b0_1 t3 = gm_f_write_v26t591((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 137u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_egbtonb(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_egbtonb F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_l0ks50g(t2, t3);
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
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_t4cv0b4(void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0) {
    struct gm_fr_binop_text_t4cv0b4 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK(&F, 674u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t12;
    gm_t9qkh0rsooc8d t13 = gm_l1;
    gm_b16_8 t14 = gm_f_flavor_text_3ffnbt7(t13);
    gm_tnpifv20irnph t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
    gm_tabb710d9mq2e t16;
    gm_f_format_0n7d8jn((void *)&t16, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srco4ljquuk5d87, 414, 16);
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
    t1 = t18;
    goto me2;
mn10:;
ma4:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)1ULL)) goto mn19;
    gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l2 = t21;
    gm_t9qkh0rsooc8d t22 = gm_l2;
    gm_b16_8 t23 = gm_f_flavor_text_3ffnbt7(t22);
    gm_tnpifv20irnph t24;
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
    gm_tabb710d9mq2e t25;
    gm_f_format_lderfbr((void *)&t25, (void *)NULL, cx, NULL, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srco4ljquuk5d87, 415, 16);
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    t1 = t27;
    goto me2;
mn19:;
ma5:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t29 != ((uint8_t)2ULL)) goto mn28;
    gm_t9qkh0rsooc8d t30 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l3 = t30;
    gm_t9qkh0rsooc8d t31 = gm_l3;
    gm_b16_8 t32 = gm_f_flavor_text_3ffnbt7(t31);
    gm_tnpifv20irnph t33;
    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
    gm_tabb710d9mq2e t34;
    gm_f_format_olqcc5i((void *)&t34, (void *)NULL, cx, NULL, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srco4ljquuk5d87, 416, 16);
    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
    t1 = t36;
    goto me2;
mn28:;
ma6:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)3ULL)) goto mn37;
    gm_t9qkh0rsooc8d t39 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l4 = t39;
    gm_t9qkh0rsooc8d t40 = gm_l4;
    gm_b16_8 t41 = gm_f_flavor_text_3ffnbt7(t40);
    gm_tnpifv20irnph t42;
    GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
    gm_tabb710d9mq2e t43;
    gm_f_format_sqbp092((void *)&t43, (void *)NULL, cx, NULL, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srco4ljquuk5d87, 417, 16);
    gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
    t1 = t45;
    goto me2;
mn37:;
ma7:;
    gm_b16_8 t47;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t64) { case ((uint8_t)0ULL): goto ma49; case ((uint8_t)1ULL): goto ma50; case ((uint8_t)2ULL): goto ma51; case ((uint8_t)3ULL): goto ma52; case ((uint8_t)4ULL): goto ma53; case ((uint8_t)5ULL): goto ma54; case ((uint8_t)6ULL): goto ma55; case ((uint8_t)7ULL): goto ma56; case ((uint8_t)8ULL): goto ma57; case ((uint8_t)9ULL): goto ma58; case ((uint8_t)10ULL): goto ma59; case ((uint8_t)11ULL): goto ma60; case ((uint8_t)12ULL): goto ma61; case ((uint8_t)13ULL): goto ma62; default: goto ma63; }
ma49:;
    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t66 != ((uint8_t)0ULL)) goto mn65;
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_spr274eleu2lj));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(3));
    t47 = t67;
    goto me48;
mn65:;
ma50:;
    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t69 != ((uint8_t)1ULL)) goto mn68;
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_ssigpbgi88it3));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(3));
    t47 = t70;
    goto me48;
mn68:;
ma51:;
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t72 != ((uint8_t)2ULL)) goto mn71;
    gm_b16_8 t73;
    GM_ST(void *, (uint8_t *)&t73, (void *)(gm_spoh8ft9n8oqt));
    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(3));
    t47 = t73;
    goto me48;
mn71:;
ma52:;
    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t75 != ((uint8_t)3ULL)) goto mn74;
    gm_b16_8 t76;
    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_smtmd013g2edr));
    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(3));
    t47 = t76;
    goto me48;
mn74:;
ma53:;
    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t78 != ((uint8_t)4ULL)) goto mn77;
    gm_b16_8 t79;
    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s2uphqpvb2vhh));
    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(3));
    t47 = t79;
    goto me48;
mn77:;
ma54:;
    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t81 != ((uint8_t)5ULL)) goto mn80;
    gm_b16_8 t82;
    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_snaq7hvenqsum));
    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(3));
    t47 = t82;
    goto me48;
mn80:;
ma55:;
    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t84 != ((uint8_t)6ULL)) goto mn83;
    gm_b16_8 t85;
    GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sc24ftfhihd1h));
    GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(3));
    t47 = t85;
    goto me48;
mn83:;
ma56:;
    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t87 != ((uint8_t)7ULL)) goto mn86;
    gm_b16_8 t88;
    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_snbhl8dsqq3ln));
    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
    t47 = t88;
    goto me48;
mn86:;
ma57:;
    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t90 != ((uint8_t)8ULL)) goto mn89;
    gm_b16_8 t91;
    GM_ST(void *, (uint8_t *)&t91, (void *)(gm_snq4uaiieftl8));
    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(5));
    t47 = t91;
    goto me48;
mn89:;
ma58:;
    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t93 != ((uint8_t)9ULL)) goto mn92;
    gm_b16_8 t94;
    GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sel2c30qvm74f));
    GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(6));
    t47 = t94;
    goto me48;
mn92:;
ma59:;
    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t96 != ((uint8_t)10ULL)) goto mn95;
    gm_b16_8 t97;
    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sqrv416075ito));
    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
    t47 = t97;
    goto me48;
mn95:;
ma60:;
    uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t99 != ((uint8_t)11ULL)) goto mn98;
    gm_b16_8 t100;
    GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s1n20vngt0mkv));
    GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(2));
    t47 = t100;
    goto me48;
mn98:;
ma61:;
    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t102 != ((uint8_t)12ULL)) goto mn101;
    gm_b16_8 t103;
    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sp4uhnkunj2cg));
    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(2));
    t47 = t103;
    goto me48;
mn101:;
ma62:;
    uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t105 != ((uint8_t)13ULL)) goto mn104;
    gm_b16_8 t106;
    GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sdg7bdfivjinb));
    GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(2));
    t47 = t106;
    goto me48;
mn104:;
ma63:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 418, 10);
me48:;
    t1 = t47;
    goto me2;
mn46:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 413, 58);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_builtin_text_od38u7h(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0) {
    struct gm_fr_builtin_text_od38u7h F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* r */
    gm_t9qkh0rsooc8d gm_l3; /* s */
    uint32_t gm_l4; /* outputs */
    uint32_t gm_l5; /* inputs */
    uint32_t gm_l6; /* id */
    uint32_t gm_l7; /* id */
    GM_STACK_CHECK(&F, 1983u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma3; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma9; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma9; case ((uint8_t)59ULL): goto ma9; case ((uint8_t)60ULL): goto ma9; case ((uint8_t)61ULL): goto ma9; case ((uint8_t)62ULL): goto ma9; case ((uint8_t)63ULL): goto ma9; case ((uint8_t)64ULL): goto ma9; case ((uint8_t)65ULL): goto ma9; case ((uint8_t)66ULL): goto ma9; case ((uint8_t)67ULL): goto ma9; case ((uint8_t)68ULL): goto ma4; case ((uint8_t)69ULL): goto ma9; case ((uint8_t)70ULL): goto ma9; case ((uint8_t)71ULL): goto ma9; case ((uint8_t)72ULL): goto ma5; case ((uint8_t)73ULL): goto ma9; case ((uint8_t)74ULL): goto ma9; case ((uint8_t)75ULL): goto ma9; case ((uint8_t)76ULL): goto ma9; case ((uint8_t)77ULL): goto ma9; case ((uint8_t)78ULL): goto ma9; case ((uint8_t)79ULL): goto ma6; case ((uint8_t)80ULL): goto ma7; case ((uint8_t)81ULL): goto ma8; default: goto ma9; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)21ULL)) goto mn12;
    gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t14;
    gm_t9qkh0rsooc8d t15 = gm_l1;
    gm_b16_8 t16 = gm_f_flavor_text_3ffnbt7(t15);
    gm_tnpifv20irnph t17;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
    gm_tabb710d9mq2e t18;
    gm_f_format_qcp7nf9((void *)&t18, (void *)NULL, cx, NULL, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srco4ljquuk5d87, 572, 16);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    t1 = t20;
    goto me2;
mn12:;
ma4:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)68ULL)) goto mn21;
    gm_t9qkh0rsooc8d t23 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
    gm_l2 = t23;
    gm_t9qkh0rsooc8d t24 = gm_l2;
    gm_b16_8 t25 = gm_f_rmw_text_te4ol3u(t24);
    gm_tnpifv20irnph t26;
    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
    gm_tabb710d9mq2e t27;
    gm_f_format_aq84o6u((void *)&t27, (void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srco4ljquuk5d87, 573, 22);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
    t1 = t29;
    goto me2;
mn21:;
ma5:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)72ULL)) goto mn30;
    gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
    gm_l3 = t32;
    gm_t9qkh0rsooc8d t33 = gm_l3;
    gm_b16_8 t34 = gm_f_sys_text_9qjnpra(t33);
    gm_tnpifv20irnph t35;
    GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
    gm_tabb710d9mq2e t36;
    gm_f_format_556kaa4((void *)&t36, (void *)NULL, cx, NULL, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srco4ljquuk5d87, 574, 16);
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
    t1 = t38;
    goto me2;
mn30:;
ma6:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)79ULL)) goto mn39;
    uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 0));
    gm_l4 = t41;
    uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
    gm_l5 = t42;
    uint32_t t43 = gm_l4;
    uint32_t t44 = gm_l5;
    gm_tcc1rq4n1avb2 t45;
    GM_ST(uint32_t, ((uint8_t *)&t45 + 0), t43);
    GM_ST(uint32_t, ((uint8_t *)&t45 + 4), t44);
    gm_tabb710d9mq2e t46;
    gm_f_format_c2ann39((void *)&t46, (void *)NULL, cx, NULL, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srco4ljquuk5d87, 575, 30);
    gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
    t1 = t48;
    goto me2;
mn39:;
ma7:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)80ULL)) goto mn49;
    uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l6 = t51;
    uint32_t t52 = gm_l6;
    gm_t88rcucg1b7uk t53;
    GM_ST(uint32_t, ((uint8_t *)&t53 + 0), t52);
    gm_tabb710d9mq2e t54;
    gm_f_format_9g7fct9((void *)&t54, (void *)NULL, cx, NULL, t53);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srco4ljquuk5d87, 576, 26);
    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
    t1 = t56;
    goto me2;
mn49:;
ma8:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t58 != ((uint8_t)81ULL)) goto mn57;
    uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l7 = t59;
    uint32_t t60 = gm_l7;
    gm_t88rcucg1b7uk t61;
    GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t60);
    gm_tabb710d9mq2e t62;
    gm_f_format_ree2v5u((void *)&t62, (void *)NULL, cx, NULL, t61);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srco4ljquuk5d87, 577, 19);
    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
    t1 = t64;
    goto me2;
mn57:;
ma9:;
    gm_b16_8 t66;
    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t151) { case ((uint8_t)0ULL): goto ma68; case ((uint8_t)1ULL): goto ma69; case ((uint8_t)2ULL): goto ma70; case ((uint8_t)3ULL): goto ma71; case ((uint8_t)4ULL): goto ma72; case ((uint8_t)5ULL): goto ma73; case ((uint8_t)6ULL): goto ma74; case ((uint8_t)7ULL): goto ma75; case ((uint8_t)8ULL): goto ma76; case ((uint8_t)9ULL): goto ma77; case ((uint8_t)10ULL): goto ma78; case ((uint8_t)11ULL): goto ma79; case ((uint8_t)12ULL): goto ma80; case ((uint8_t)13ULL): goto ma81; case ((uint8_t)14ULL): goto ma82; case ((uint8_t)15ULL): goto ma83; case ((uint8_t)16ULL): goto ma84; case ((uint8_t)17ULL): goto ma85; case ((uint8_t)18ULL): goto ma86; case ((uint8_t)19ULL): goto ma87; case ((uint8_t)20ULL): goto ma88; case ((uint8_t)21ULL): goto ma89; case ((uint8_t)22ULL): goto ma90; case ((uint8_t)23ULL): goto ma91; case ((uint8_t)24ULL): goto ma92; case ((uint8_t)25ULL): goto ma93; case ((uint8_t)26ULL): goto ma94; case ((uint8_t)27ULL): goto ma95; case ((uint8_t)28ULL): goto ma96; case ((uint8_t)29ULL): goto ma97; case ((uint8_t)30ULL): goto ma98; case ((uint8_t)31ULL): goto ma99; case ((uint8_t)32ULL): goto ma100; case ((uint8_t)33ULL): goto ma101; case ((uint8_t)34ULL): goto ma102; case ((uint8_t)35ULL): goto ma103; case ((uint8_t)36ULL): goto ma104; case ((uint8_t)37ULL): goto ma105; case ((uint8_t)38ULL): goto ma106; case ((uint8_t)39ULL): goto ma107; case ((uint8_t)40ULL): goto ma108; case ((uint8_t)41ULL): goto ma109; case ((uint8_t)42ULL): goto ma110; case ((uint8_t)43ULL): goto ma111; case ((uint8_t)44ULL): goto ma112; case ((uint8_t)45ULL): goto ma113; case ((uint8_t)46ULL): goto ma114; case ((uint8_t)47ULL): goto ma115; case ((uint8_t)48ULL): goto ma116; case ((uint8_t)49ULL): goto ma117; case ((uint8_t)50ULL): goto ma118; case ((uint8_t)51ULL): goto ma119; case ((uint8_t)52ULL): goto ma120; case ((uint8_t)53ULL): goto ma121; case ((uint8_t)54ULL): goto ma122; case ((uint8_t)55ULL): goto ma123; case ((uint8_t)56ULL): goto ma124; case ((uint8_t)57ULL): goto ma125; case ((uint8_t)58ULL): goto ma126; case ((uint8_t)59ULL): goto ma127; case ((uint8_t)60ULL): goto ma128; case ((uint8_t)61ULL): goto ma129; case ((uint8_t)62ULL): goto ma130; case ((uint8_t)63ULL): goto ma131; case ((uint8_t)64ULL): goto ma132; case ((uint8_t)65ULL): goto ma133; case ((uint8_t)66ULL): goto ma134; case ((uint8_t)67ULL): goto ma135; case ((uint8_t)68ULL): goto ma136; case ((uint8_t)69ULL): goto ma137; case ((uint8_t)70ULL): goto ma138; case ((uint8_t)71ULL): goto ma139; case ((uint8_t)72ULL): goto ma140; case ((uint8_t)73ULL): goto ma141; case ((uint8_t)74ULL): goto ma142; case ((uint8_t)75ULL): goto ma143; case ((uint8_t)76ULL): goto ma144; case ((uint8_t)77ULL): goto ma145; case ((uint8_t)78ULL): goto ma146; case ((uint8_t)79ULL): goto ma147; case ((uint8_t)80ULL): goto ma148; case ((uint8_t)81ULL): goto ma149; default: goto ma150; }
ma68:;
    uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t153 != ((uint8_t)0ULL)) goto mn152;
    gm_b16_8 t154;
    GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sivsapeh482sm));
    GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(7));
    t66 = t154;
    goto me67;
mn152:;
ma69:;
    uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t156 != ((uint8_t)1ULL)) goto mn155;
    gm_b16_8 t157;
    GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sk2b59ovo43qg));
    GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(5));
    t66 = t157;
    goto me67;
mn155:;
ma70:;
    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t159 != ((uint8_t)2ULL)) goto mn158;
    gm_b16_8 t160;
    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_s9dgf2prvg715));
    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(5));
    t66 = t160;
    goto me67;
mn158:;
ma71:;
    uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t162 != ((uint8_t)3ULL)) goto mn161;
    gm_b16_8 t163;
    GM_ST(void *, (uint8_t *)&t163, (void *)(gm_scb853lmgr58k));
    GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(5));
    t66 = t163;
    goto me67;
mn161:;
ma72:;
    uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t165 != ((uint8_t)4ULL)) goto mn164;
    gm_b16_8 t166;
    GM_ST(void *, (uint8_t *)&t166, (void *)(gm_seeo26ergtcha));
    GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(4));
    t66 = t166;
    goto me67;
mn164:;
ma73:;
    uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t168 != ((uint8_t)5ULL)) goto mn167;
    gm_b16_8 t169;
    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_sdft8ov6bb7s6));
    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(7));
    t66 = t169;
    goto me67;
mn167:;
ma74:;
    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t171 != ((uint8_t)6ULL)) goto mn170;
    gm_b16_8 t172;
    GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sv3olmne7h13s));
    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(3));
    t66 = t172;
    goto me67;
mn170:;
ma75:;
    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t174 != ((uint8_t)7ULL)) goto mn173;
    gm_b16_8 t175;
    GM_ST(void *, (uint8_t *)&t175, (void *)(gm_skapbb3maknat));
    GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(4));
    t66 = t175;
    goto me67;
mn173:;
ma76:;
    uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t177 != ((uint8_t)8ULL)) goto mn176;
    gm_b16_8 t178;
    GM_ST(void *, (uint8_t *)&t178, (void *)(gm_scd599tsljaee));
    GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(4));
    t66 = t178;
    goto me67;
mn176:;
ma77:;
    uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t180 != ((uint8_t)9ULL)) goto mn179;
    gm_b16_8 t181;
    GM_ST(void *, (uint8_t *)&t181, (void *)(gm_s2kmbpuutp9qq));
    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(4));
    t66 = t181;
    goto me67;
mn179:;
ma78:;
    uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t183 != ((uint8_t)10ULL)) goto mn182;
    gm_b16_8 t184;
    GM_ST(void *, (uint8_t *)&t184, (void *)(gm_s7u6ij52clodh));
    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(6));
    t66 = t184;
    goto me67;
mn182:;
ma79:;
    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t186 != ((uint8_t)11ULL)) goto mn185;
    gm_b16_8 t187;
    GM_ST(void *, (uint8_t *)&t187, (void *)(gm_sun4qq3op5opf));
    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(6));
    t66 = t187;
    goto me67;
mn185:;
ma80:;
    uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t189 != ((uint8_t)12ULL)) goto mn188;
    gm_b16_8 t190;
    GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s2h8cr50573go));
    GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(7));
    t66 = t190;
    goto me67;
mn188:;
ma81:;
    uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t192 != ((uint8_t)13ULL)) goto mn191;
    gm_b16_8 t193;
    GM_ST(void *, (uint8_t *)&t193, (void *)(gm_s00pihipe0jga));
    GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(5));
    t66 = t193;
    goto me67;
mn191:;
ma82:;
    uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t195 != ((uint8_t)14ULL)) goto mn194;
    gm_b16_8 t196;
    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s3opc5ivp6e66));
    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(3));
    t66 = t196;
    goto me67;
mn194:;
ma83:;
    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t198 != ((uint8_t)15ULL)) goto mn197;
    gm_b16_8 t199;
    GM_ST(void *, (uint8_t *)&t199, (void *)(gm_sj1m58fkg956m));
    GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(7));
    t66 = t199;
    goto me67;
mn197:;
ma84:;
    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t201 != ((uint8_t)16ULL)) goto mn200;
    gm_b16_8 t202;
    GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s187rb18i1kgk));
    GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(3));
    t66 = t202;
    goto me67;
mn200:;
ma85:;
    uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t204 != ((uint8_t)17ULL)) goto mn203;
    gm_b16_8 t205;
    GM_ST(void *, (uint8_t *)&t205, (void *)(gm_sgo00f0mafbnd));
    GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(3));
    t66 = t205;
    goto me67;
mn203:;
ma86:;
    uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t207 != ((uint8_t)18ULL)) goto mn206;
    gm_b16_8 t208;
    GM_ST(void *, (uint8_t *)&t208, (void *)(gm_s5udr3eget84q));
    GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(5));
    t66 = t208;
    goto me67;
mn206:;
ma87:;
    uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t210 != ((uint8_t)19ULL)) goto mn209;
    gm_b16_8 t211;
    GM_ST(void *, (uint8_t *)&t211, (void *)(gm_sfd0mn3sol25u));
    GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(3));
    t66 = t211;
    goto me67;
mn209:;
ma88:;
    uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t213 != ((uint8_t)20ULL)) goto mn212;
    gm_b16_8 t214;
    GM_ST(void *, (uint8_t *)&t214, (void *)(gm_s85hqf14lbkns));
    GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(7));
    t66 = t214;
    goto me67;
mn212:;
ma89:;
    uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t216 != ((uint8_t)21ULL)) goto mn215;
    gm_b16_8 t217;
    GM_ST(void *, (uint8_t *)&t217, (void *)(gm_s6qbnfe2f321h));
    GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(3));
    t66 = t217;
    goto me67;
mn215:;
ma90:;
    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t219 != ((uint8_t)22ULL)) goto mn218;
    gm_b16_8 t220;
    GM_ST(void *, (uint8_t *)&t220, (void *)(gm_sds03accef1j8));
    GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(5));
    t66 = t220;
    goto me67;
mn218:;
ma91:;
    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t222 != ((uint8_t)23ULL)) goto mn221;
    gm_b16_8 t223;
    GM_ST(void *, (uint8_t *)&t223, (void *)(gm_s24chpb914bep));
    GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(5));
    t66 = t223;
    goto me67;
mn221:;
ma92:;
    uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t225 != ((uint8_t)24ULL)) goto mn224;
    gm_b16_8 t226;
    GM_ST(void *, (uint8_t *)&t226, (void *)(gm_su3ahsl9p584f));
    GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(8));
    t66 = t226;
    goto me67;
mn224:;
ma93:;
    uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t228 != ((uint8_t)25ULL)) goto mn227;
    gm_b16_8 t229;
    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_s28ijm46j20ds));
    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(7));
    t66 = t229;
    goto me67;
mn227:;
ma94:;
    uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t231 != ((uint8_t)26ULL)) goto mn230;
    gm_b16_8 t232;
    GM_ST(void *, (uint8_t *)&t232, (void *)(gm_sv8lqdk5nsv8i));
    GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(4));
    t66 = t232;
    goto me67;
mn230:;
ma95:;
    uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t234 != ((uint8_t)27ULL)) goto mn233;
    gm_b16_8 t235;
    GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s1q9ms0kviddj));
    GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(3));
    t66 = t235;
    goto me67;
mn233:;
ma96:;
    uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t237 != ((uint8_t)28ULL)) goto mn236;
    gm_b16_8 t238;
    GM_ST(void *, (uint8_t *)&t238, (void *)(gm_s2nl2e5anglov));
    GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(3));
    t66 = t238;
    goto me67;
mn236:;
ma97:;
    uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t240 != ((uint8_t)29ULL)) goto mn239;
    gm_b16_8 t241;
    GM_ST(void *, (uint8_t *)&t241, (void *)(gm_s0kvnv9teor3q));
    GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(3));
    t66 = t241;
    goto me67;
mn239:;
ma98:;
    uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t243 != ((uint8_t)30ULL)) goto mn242;
    gm_b16_8 t244;
    GM_ST(void *, (uint8_t *)&t244, (void *)(gm_solj59tk4ri3k));
    GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(8));
    t66 = t244;
    goto me67;
mn242:;
ma99:;
    uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t246 != ((uint8_t)31ULL)) goto mn245;
    gm_b16_8 t247;
    GM_ST(void *, (uint8_t *)&t247, (void *)(gm_sqhhm0h7b3asd));
    GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(5));
    t66 = t247;
    goto me67;
mn245:;
ma100:;
    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t249 != ((uint8_t)32ULL)) goto mn248;
    gm_b16_8 t250;
    GM_ST(void *, (uint8_t *)&t250, (void *)(gm_s3ng5nguvd452));
    GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(4));
    t66 = t250;
    goto me67;
mn248:;
ma101:;
    uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t252 != ((uint8_t)33ULL)) goto mn251;
    gm_b16_8 t253;
    GM_ST(void *, (uint8_t *)&t253, (void *)(gm_sjbl17pjsjm48));
    GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(4));
    t66 = t253;
    goto me67;
mn251:;
ma102:;
    uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t255 != ((uint8_t)34ULL)) goto mn254;
    gm_b16_8 t256;
    GM_ST(void *, (uint8_t *)&t256, (void *)(gm_sl37uvr16qsu8));
    GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(10));
    t66 = t256;
    goto me67;
mn254:;
ma103:;
    uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t258 != ((uint8_t)35ULL)) goto mn257;
    gm_b16_8 t259;
    GM_ST(void *, (uint8_t *)&t259, (void *)(gm_s1q2nkavemrqf));
    GM_ST(uintptr_t, (uint8_t *)&t259 + 8, (uintptr_t)(8));
    t66 = t259;
    goto me67;
mn257:;
ma104:;
    uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t261 != ((uint8_t)36ULL)) goto mn260;
    gm_b16_8 t262;
    GM_ST(void *, (uint8_t *)&t262, (void *)(gm_s2a9t8smkevg0));
    GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(3));
    t66 = t262;
    goto me67;
mn260:;
ma105:;
    uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t264 != ((uint8_t)37ULL)) goto mn263;
    gm_b16_8 t265;
    GM_ST(void *, (uint8_t *)&t265, (void *)(gm_sa1pcp92k3aj3));
    GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(6));
    t66 = t265;
    goto me67;
mn263:;
ma106:;
    uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t267 != ((uint8_t)38ULL)) goto mn266;
    gm_b16_8 t268;
    GM_ST(void *, (uint8_t *)&t268, (void *)(gm_shkq4botdoed7));
    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(5));
    t66 = t268;
    goto me67;
mn266:;
ma107:;
    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t270 != ((uint8_t)39ULL)) goto mn269;
    gm_b16_8 t271;
    GM_ST(void *, (uint8_t *)&t271, (void *)(gm_sse9dgkusbtnf));
    GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(4));
    t66 = t271;
    goto me67;
mn269:;
ma108:;
    uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t273 != ((uint8_t)40ULL)) goto mn272;
    gm_b16_8 t274;
    GM_ST(void *, (uint8_t *)&t274, (void *)(gm_si4k9tiofr8t2));
    GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(6));
    t66 = t274;
    goto me67;
mn272:;
ma109:;
    uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t276 != ((uint8_t)41ULL)) goto mn275;
    gm_b16_8 t277;
    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_s6aamtn65omr1));
    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(6));
    t66 = t277;
    goto me67;
mn275:;
ma110:;
    uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t279 != ((uint8_t)42ULL)) goto mn278;
    gm_b16_8 t280;
    GM_ST(void *, (uint8_t *)&t280, (void *)(gm_s8s6shfnaqahd));
    GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(6));
    t66 = t280;
    goto me67;
mn278:;
ma111:;
    uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t282 != ((uint8_t)43ULL)) goto mn281;
    gm_b16_8 t283;
    GM_ST(void *, (uint8_t *)&t283, (void *)(gm_sbnms71r2jqr5));
    GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(6));
    t66 = t283;
    goto me67;
mn281:;
ma112:;
    uint8_t t285 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t285 != ((uint8_t)44ULL)) goto mn284;
    gm_b16_8 t286;
    GM_ST(void *, (uint8_t *)&t286, (void *)(gm_ss27a20sarlvg));
    GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(6));
    t66 = t286;
    goto me67;
mn284:;
ma113:;
    uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t288 != ((uint8_t)45ULL)) goto mn287;
    gm_b16_8 t289;
    GM_ST(void *, (uint8_t *)&t289, (void *)(gm_sbek54n4a6h2o));
    GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(6));
    t66 = t289;
    goto me67;
mn287:;
ma114:;
    uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t291 != ((uint8_t)46ULL)) goto mn290;
    gm_b16_8 t292;
    GM_ST(void *, (uint8_t *)&t292, (void *)(gm_std59a9b6lpnn));
    GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(9));
    t66 = t292;
    goto me67;
mn290:;
ma115:;
    uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t294 != ((uint8_t)47ULL)) goto mn293;
    gm_b16_8 t295;
    GM_ST(void *, (uint8_t *)&t295, (void *)(gm_ssjmv3618n779));
    GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(9));
    t66 = t295;
    goto me67;
mn293:;
ma116:;
    uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t297 != ((uint8_t)48ULL)) goto mn296;
    gm_b16_8 t298;
    GM_ST(void *, (uint8_t *)&t298, (void *)(gm_smuec3qfsdh82));
    GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(9));
    t66 = t298;
    goto me67;
mn296:;
ma117:;
    uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t300 != ((uint8_t)49ULL)) goto mn299;
    gm_b16_8 t301;
    GM_ST(void *, (uint8_t *)&t301, (void *)(gm_s57rgvjb5mj5a));
    GM_ST(uintptr_t, (uint8_t *)&t301 + 8, (uintptr_t)(7));
    t66 = t301;
    goto me67;
mn299:;
ma118:;
    uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t303 != ((uint8_t)50ULL)) goto mn302;
    gm_b16_8 t304;
    GM_ST(void *, (uint8_t *)&t304, (void *)(gm_s76ekntop342o));
    GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(7));
    t66 = t304;
    goto me67;
mn302:;
ma119:;
    uint8_t t306 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t306 != ((uint8_t)51ULL)) goto mn305;
    gm_b16_8 t307;
    GM_ST(void *, (uint8_t *)&t307, (void *)(gm_sm3dios51ece1));
    GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(9));
    t66 = t307;
    goto me67;
mn305:;
ma120:;
    uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t309 != ((uint8_t)52ULL)) goto mn308;
    gm_b16_8 t310;
    GM_ST(void *, (uint8_t *)&t310, (void *)(gm_s9h93c7asacna));
    GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(6));
    t66 = t310;
    goto me67;
mn308:;
ma121:;
    uint8_t t312 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t312 != ((uint8_t)53ULL)) goto mn311;
    gm_b16_8 t313;
    GM_ST(void *, (uint8_t *)&t313, (void *)(gm_s8pk5tcnrk6p2));
    GM_ST(uintptr_t, (uint8_t *)&t313 + 8, (uintptr_t)(7));
    t66 = t313;
    goto me67;
mn311:;
ma122:;
    uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t315 != ((uint8_t)54ULL)) goto mn314;
    gm_b16_8 t316;
    GM_ST(void *, (uint8_t *)&t316, (void *)(gm_svqq3k9j6h1lu));
    GM_ST(uintptr_t, (uint8_t *)&t316 + 8, (uintptr_t)(7));
    t66 = t316;
    goto me67;
mn314:;
ma123:;
    uint8_t t318 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t318 != ((uint8_t)55ULL)) goto mn317;
    gm_b16_8 t319;
    GM_ST(void *, (uint8_t *)&t319, (void *)(gm_sdh1dnevn1j10));
    GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(7));
    t66 = t319;
    goto me67;
mn317:;
ma124:;
    uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t321 != ((uint8_t)56ULL)) goto mn320;
    gm_b16_8 t322;
    GM_ST(void *, (uint8_t *)&t322, (void *)(gm_sij2r6jhpquej));
    GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(11));
    t66 = t322;
    goto me67;
mn320:;
ma125:;
    uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t324 != ((uint8_t)57ULL)) goto mn323;
    gm_b16_8 t325;
    GM_ST(void *, (uint8_t *)&t325, (void *)(gm_stft4dc50tdce));
    GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(7));
    t66 = t325;
    goto me67;
mn323:;
ma126:;
    uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t327 != ((uint8_t)58ULL)) goto mn326;
    gm_b16_8 t328;
    GM_ST(void *, (uint8_t *)&t328, (void *)(gm_sa0alba1kin3k));
    GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(7));
    t66 = t328;
    goto me67;
mn326:;
ma127:;
    uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t330 != ((uint8_t)59ULL)) goto mn329;
    gm_b16_8 t331;
    GM_ST(void *, (uint8_t *)&t331, (void *)(gm_s9o8gb20t6jtp));
    GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(8));
    t66 = t331;
    goto me67;
mn329:;
ma128:;
    uint8_t t333 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t333 != ((uint8_t)60ULL)) goto mn332;
    gm_b16_8 t334;
    GM_ST(void *, (uint8_t *)&t334, (void *)(gm_sh11qqm8nqu68));
    GM_ST(uintptr_t, (uint8_t *)&t334 + 8, (uintptr_t)(7));
    t66 = t334;
    goto me67;
mn332:;
ma129:;
    uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t336 != ((uint8_t)61ULL)) goto mn335;
    gm_b16_8 t337;
    GM_ST(void *, (uint8_t *)&t337, (void *)(gm_s2fu5nkek0lrq));
    GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(6));
    t66 = t337;
    goto me67;
mn335:;
ma130:;
    uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t339 != ((uint8_t)62ULL)) goto mn338;
    gm_b16_8 t340;
    GM_ST(void *, (uint8_t *)&t340, (void *)(gm_s7i80akiq0d95));
    GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(4));
    t66 = t340;
    goto me67;
mn338:;
ma131:;
    uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t342 != ((uint8_t)63ULL)) goto mn341;
    gm_b16_8 t343;
    GM_ST(void *, (uint8_t *)&t343, (void *)(gm_stn52kuiq0jk2));
    GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(11));
    t66 = t343;
    goto me67;
mn341:;
ma132:;
    uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t345 != ((uint8_t)64ULL)) goto mn344;
    gm_b16_8 t346;
    GM_ST(void *, (uint8_t *)&t346, (void *)(gm_srjd0sq8k3d7s));
    GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(10));
    t66 = t346;
    goto me67;
mn344:;
ma133:;
    uint8_t t348 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t348 != ((uint8_t)65ULL)) goto mn347;
    gm_b16_8 t349;
    GM_ST(void *, (uint8_t *)&t349, (void *)(gm_sdq6l7oddjqfe));
    GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(11));
    t66 = t349;
    goto me67;
mn347:;
ma134:;
    uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t351 != ((uint8_t)66ULL)) goto mn350;
    gm_b16_8 t352;
    GM_ST(void *, (uint8_t *)&t352, (void *)(gm_s6ipt60dh9o1j));
    GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(10));
    t66 = t352;
    goto me67;
mn350:;
ma135:;
    uint8_t t354 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t354 != ((uint8_t)67ULL)) goto mn353;
    gm_b16_8 t355;
    GM_ST(void *, (uint8_t *)&t355, (void *)(gm_sa53epbg9iarq));
    GM_ST(uintptr_t, (uint8_t *)&t355 + 8, (uintptr_t)(9));
    t66 = t355;
    goto me67;
mn353:;
ma136:;
    uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t357 != ((uint8_t)68ULL)) goto mn356;
    gm_b16_8 t358;
    GM_ST(void *, (uint8_t *)&t358, (void *)(gm_s2fdsodctojm1));
    GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(9));
    t66 = t358;
    goto me67;
mn356:;
ma137:;
    uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t360 != ((uint8_t)69ULL)) goto mn359;
    gm_b16_8 t361;
    GM_ST(void *, (uint8_t *)&t361, (void *)(gm_skbgm82aqj3q0));
    GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(5));
    t66 = t361;
    goto me67;
mn359:;
ma138:;
    uint8_t t363 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t363 != ((uint8_t)70ULL)) goto mn362;
    gm_b16_8 t364;
    GM_ST(void *, (uint8_t *)&t364, (void *)(gm_sk88tp2337lf6));
    GM_ST(uintptr_t, (uint8_t *)&t364 + 8, (uintptr_t)(12));
    t66 = t364;
    goto me67;
mn362:;
ma139:;
    uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t366 != ((uint8_t)71ULL)) goto mn365;
    gm_b16_8 t367;
    GM_ST(void *, (uint8_t *)&t367, (void *)(gm_st0l9v6pp5i7b));
    GM_ST(uintptr_t, (uint8_t *)&t367 + 8, (uintptr_t)(13));
    t66 = t367;
    goto me67;
mn365:;
ma140:;
    uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t369 != ((uint8_t)72ULL)) goto mn368;
    gm_b16_8 t370;
    GM_ST(void *, (uint8_t *)&t370, (void *)(gm_sa902c1tbtg0i));
    GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(3));
    t66 = t370;
    goto me67;
mn368:;
ma141:;
    uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t372 != ((uint8_t)73ULL)) goto mn371;
    gm_b16_8 t373;
    GM_ST(void *, (uint8_t *)&t373, (void *)(gm_ss869lnvmj3m8));
    GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(5));
    t66 = t373;
    goto me67;
mn371:;
ma142:;
    uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t375 != ((uint8_t)74ULL)) goto mn374;
    gm_b16_8 t376;
    GM_ST(void *, (uint8_t *)&t376, (void *)(gm_s1j5448m6ruti));
    GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(9));
    t66 = t376;
    goto me67;
mn374:;
ma143:;
    uint8_t t378 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t378 != ((uint8_t)75ULL)) goto mn377;
    gm_b16_8 t379;
    GM_ST(void *, (uint8_t *)&t379, (void *)(gm_src5778gpfoh9));
    GM_ST(uintptr_t, (uint8_t *)&t379 + 8, (uintptr_t)(8));
    t66 = t379;
    goto me67;
mn377:;
ma144:;
    uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t381 != ((uint8_t)76ULL)) goto mn380;
    gm_b16_8 t382;
    GM_ST(void *, (uint8_t *)&t382, (void *)(gm_smpl91sh0k8v4));
    GM_ST(uintptr_t, (uint8_t *)&t382 + 8, (uintptr_t)(8));
    t66 = t382;
    goto me67;
mn380:;
ma145:;
    uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t384 != ((uint8_t)77ULL)) goto mn383;
    gm_b16_8 t385;
    GM_ST(void *, (uint8_t *)&t385, (void *)(gm_s7rltjaeh634d));
    GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(10));
    t66 = t385;
    goto me67;
mn383:;
ma146:;
    uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t387 != ((uint8_t)78ULL)) goto mn386;
    gm_b16_8 t388;
    GM_ST(void *, (uint8_t *)&t388, (void *)(gm_s5c5548ll8taj));
    GM_ST(uintptr_t, (uint8_t *)&t388 + 8, (uintptr_t)(9));
    t66 = t388;
    goto me67;
mn386:;
ma147:;
    uint8_t t390 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t390 != ((uint8_t)79ULL)) goto mn389;
    gm_b16_8 t391;
    GM_ST(void *, (uint8_t *)&t391, (void *)(gm_seodvmll3gsjn));
    GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(3));
    t66 = t391;
    goto me67;
mn389:;
ma148:;
    uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t393 != ((uint8_t)80ULL)) goto mn392;
    gm_b16_8 t394;
    GM_ST(void *, (uint8_t *)&t394, (void *)(gm_st8ki5f85lc2j));
    GM_ST(uintptr_t, (uint8_t *)&t394 + 8, (uintptr_t)(12));
    t66 = t394;
    goto me67;
mn392:;
ma149:;
    uint8_t t396 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t396 != ((uint8_t)81ULL)) goto mn395;
    gm_b16_8 t397;
    GM_ST(void *, (uint8_t *)&t397, (void *)(gm_sepu95c7o773o));
    GM_ST(uintptr_t, (uint8_t *)&t397 + 8, (uintptr_t)(5));
    t66 = t397;
    goto me67;
mn395:;
ma150:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 578, 10);
me67:;
    t1 = t66;
    goto me2;
mn65:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 571, 62);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_coercion_text_g32oppe(void *env, gm_node *cx, gm_node *inst, gm_tmn7n0n9raou5 a0) {
    struct gm_fr_coercion_text_g32oppe F;
    gm_tmn7n0n9raou5 gm_l0; /* c */
    uint32_t gm_l1; /* v */
    GM_STACK_CHECK(&F, 367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)4ULL)) goto mn3;
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    uint32_t t6 = gm_l1;
    gm_t88rcucg1b7uk t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t6);
    gm_tabb710d9mq2e t8;
    gm_f_format_41c1370((void *)&t8, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srco4ljquuk5d87, 424, 20);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    t1 = t10;
    goto me2;
mn3:;
    gm_b16_8 t12;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t28) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma17; case ((uint8_t)4ULL): goto ma18; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma20; case ((uint8_t)7ULL): goto ma21; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma23; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma25; case ((uint8_t)12ULL): goto ma26; default: goto ma27; }
ma14:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)0ULL)) goto mn29;
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_stivl0smu8eq0));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(8));
    t12 = t31;
    goto me13;
mn29:;
ma15:;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t33 != ((uint8_t)1ULL)) goto mn32;
    gm_b16_8 t34;
    GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s2uqljf8sumcn));
    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(10));
    t12 = t34;
    goto me13;
mn32:;
ma16:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t36 != ((uint8_t)2ULL)) goto mn35;
    gm_b16_8 t37;
    GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sbckpj1jsdt28));
    GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(12));
    t12 = t37;
    goto me13;
mn35:;
ma17:;
    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t39 != ((uint8_t)3ULL)) goto mn38;
    gm_b16_8 t40;
    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s6p9ub6gu603m));
    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(6));
    t12 = t40;
    goto me13;
mn38:;
ma18:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t42 != ((uint8_t)4ULL)) goto mn41;
    gm_b16_8 t43;
    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s4g3rq58567m6));
    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(7));
    t12 = t43;
    goto me13;
mn41:;
ma19:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t45 != ((uint8_t)5ULL)) goto mn44;
    gm_b16_8 t46;
    GM_ST(void *, (uint8_t *)&t46, (void *)(gm_stou1l3qp844f));
    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(9));
    t12 = t46;
    goto me13;
mn44:;
ma20:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t48 != ((uint8_t)6ULL)) goto mn47;
    gm_b16_8 t49;
    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_st1pc9bsku00q));
    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(8));
    t12 = t49;
    goto me13;
mn47:;
ma21:;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t51 != ((uint8_t)7ULL)) goto mn50;
    gm_b16_8 t52;
    GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s0eqrtq5mqb3a));
    GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(15));
    t12 = t52;
    goto me13;
mn50:;
ma22:;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t54 != ((uint8_t)8ULL)) goto mn53;
    gm_b16_8 t55;
    GM_ST(void *, (uint8_t *)&t55, (void *)(gm_se0ivu8cf46o8));
    GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(11));
    t12 = t55;
    goto me13;
mn53:;
ma23:;
    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t57 != ((uint8_t)9ULL)) goto mn56;
    gm_b16_8 t58;
    GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s96j6o9smoqdu));
    GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(7));
    t12 = t58;
    goto me13;
mn56:;
ma24:;
    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t60 != ((uint8_t)10ULL)) goto mn59;
    gm_b16_8 t61;
    GM_ST(void *, (uint8_t *)&t61, (void *)(gm_srmrjv19jgvks));
    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(10));
    t12 = t61;
    goto me13;
mn59:;
ma25:;
    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t63 != ((uint8_t)11ULL)) goto mn62;
    gm_b16_8 t64;
    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s55ccd7do7rbs));
    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(10));
    t12 = t64;
    goto me13;
mn62:;
ma26:;
    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t66 != ((uint8_t)12ULL)) goto mn65;
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sim8pu6stc37a));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(10));
    t12 = t67;
    goto me13;
mn65:;
ma27:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 425, 10);
me13:;
    t1 = t12;
    goto me2;
mn11:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 423, 63);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_flavor_text_3ffnbt7(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flavor_text_3ffnbt7 F;
    gm_t9qkh0rsooc8d gm_l0; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sdft8ov6bb7s6));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(7));
    t1 = t11;
    goto me2;
mn9:;
ma4:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)1ULL)) goto mn12;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_ssgeom7733jik));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
    t1 = t14;
    goto me2;
mn12:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sv3olmne7h13s));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
    t1 = t17;
    goto me2;
mn15:;
ma6:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)3ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_setcl7798ov89));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
    t1 = t20;
    goto me2;
mn18:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 421, 47);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_label_9hbn37h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_fn_label_9hbn37h F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* sig */
    gm_tiilmfd16v3fc gm_l3; /* inst */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* a */
    GM_STACK_CHECK(&F, 715u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srco4ljquuk5d87, 44, 13);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136);
        gm_l2 = t7;
    }
    {
        void * t9 = gm_l2;
        if (!(((((uint8_t *)t9) + 96))[24] != 0)) goto ie8;
        gm_tiilmfd16v3fc t10 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t9) + 96));
        gm_l3 = t10;
        {
            gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
                gm_l5 = t13;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t15 = gm_l6;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t17 = (t15 < t16);
                    if (!t17) goto lb14;
                    {
                        uint64_t t20 = gm_l6;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t21 * 4));
                        gm_l7 = t25;
                        {
                            void * t27 = (void *)((uint8_t *)&F.l4);
                            void * t28 = gm_l0;
                            gm_t88rcucg1b7uk t29 = gm_l7;
                            gm_b16_8 t30 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t28, t29);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            gm_tftb1odc739t5 t31 = gm_f_push_3ef746b(t27, t30);
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srco4ljquuk5d87, 47, 30);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        goto me23;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 47, 9);
me23:;
                    }
lc14:;
                    {
                        uint64_t t34 = gm_l6;
                        uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                        uint64_t t37 = r35;
                        gm_l6 = t37;
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
            gm_t6j6smj7qoctu t38;
            void * t39 = gm_l0;
            uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t41 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t41);
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t45;
            gm_f_join_sfknljb((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srco4ljquuk5d87, 48, 65);
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t38 + 16, t46);
            gm_tabb710d9mq2e t47;
            gm_f_format_licpkoa((void *)&t47, (void *)NULL, cx, NULL, &t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srco4ljquuk5d87, 48, 16);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            uint8_t t50 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t52 = gm_l2;
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 16));
        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t55 = (gm_eqff85ac10o3(((uint8_t *)&t53), ((uint8_t *)&t54)));
        if (!t55) goto ie51;
        {
            void * t57 = gm_l2;
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t57) + 0));
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        gm_t6j6smj7qoctu t60;
        void * t61 = gm_l2;
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t61) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t62);
        void * t63 = gm_l0;
        void * t64 = gm_l2;
        gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 16));
        gm_b16_8 t66 = gm_f_place_lj97s4p((void *)NULL, cx, NULL, t63, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t66);
        gm_tabb710d9mq2e t67;
        gm_f_format_1d88j1c((void *)&t67, (void *)NULL, cx, NULL, &t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srco4ljquuk5d87, 51, 12);
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
        uint8_t t70 = 0;
        F.ret = t69;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_pfpd9oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_pfpd9oj F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_tl874t2atnlh3 gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_tir_dump_ppa6e9g *)((&F.h)->up))->l0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t5 >= (uintptr_t)(t7)) gm_trap_index(t5, t7, gm_srco4ljquuk5d87, 29, 14);
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t5 * 136) + 16));
        gm_l2 = t8;
    }
    {
        void * t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)t9));
        uint64_t t11 = (uint64_t)t10;
        uint64_t t12 = (uintptr_t)t11;
        void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_tir_dump_ppa6e9g *)((&F.h)->up))->l0));
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 344) + 0) + 8);
        if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srco4ljquuk5d87, 30, 14);
        gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 344) + 0)) + t12 * 136) + 16));
        gm_l3 = t15;
    }
    {
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        uint8_t t19 = (!(t17 == t18));
        if (!t19) goto ie16;
        {
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t23 = (t21 < t22);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint8_t t28 = (!(t26 == t27));
        if (!t28) goto ie25;
        {
            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint8_t t32 = (t30 < t31);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
        uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t37 = (!(t35 == t36));
        if (!t37) goto ie34;
        {
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t41 = (t39 < t40);
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t43 = gm_l0;
        uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)t43));
        void * t45 = gm_l1;
        uint32_t t46 = GM_LD(uint32_t, ((uint8_t *)t45));
        uint8_t t47 = (t44 < t46);
        uint8_t t48 = 0;
        F.ret = t47;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_text_75tuacb(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_text_75tuacb F;
    void * gm_l0; /* d */
    uint32_t gm_l1; /* l */
    gm_b16_8 gm_l2; /* locals */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 48));
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 384) + 0) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_srco4ljquuk5d87, 106, 15);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 384) + 0)) + t5 * 64) + 24));
        gm_l2 = t9;
    }
    {
        uint32_t t11 = gm_l1;
        uint64_t t12 = (uint64_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t14 = (t12 < t13);
        if (!t14) goto ie10;
        {
            gm_tiilmfd16v3fc t16;
            memset((uint8_t *)&t16 + 4, 0, 4);
            uint32_t t17 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&t16 + 0, t17);
            uint32_t t18 = gm_l1;
            uint64_t t19 = (uint64_t)t18;
            uint64_t t20 = (uintptr_t)t19;
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srco4ljquuk5d87, 107, 62);
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 40) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t16 + 8, t22);
            gm_tabb710d9mq2e t23;
            gm_f_format_fp763nd((void *)&t23, (void *)NULL, cx, NULL, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srco4ljquuk5d87, 107, 39);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint32_t t27 = gm_l1;
        gm_t88rcucg1b7uk t28;
        GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t27);
        gm_tabb710d9mq2e t29;
        gm_f_format_irh7ehn((void *)&t29, (void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srco4ljquuk5d87, 108, 12);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_place_lj97s4p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_place_lj97s4p F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* file */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t6 = gm_f_file_o15fqku(t4, t5);
        gm_l2 = t6;
    }
    {
        void * t7 = gm_l2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t9 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t7, t8));
        gm_l3 = t9;
    }
    {
        gm_t5hdnnftkup6m t10;
        void * t11 = gm_l2;
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t12);
        uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        GM_ST(uint32_t, (uint8_t *)&t10 + 16, t13);
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        GM_ST(uint32_t, (uint8_t *)&t10 + 20, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_45lt1qt((void *)&t15, (void *)NULL, cx, NULL, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srco4ljquuk5d87, 58, 12);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        uint8_t t18 = 0;
        F.ret = t17;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rmw_text_te4ol3u(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rmw_text_te4ol3u F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
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
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_spr274eleu2lj));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
    t1 = t14;
    goto me2;
mn12:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_ssigpbgi88it3));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
    t1 = t17;
    goto me2;
mn15:;
ma5:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)2ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s4bdmssec9aiu));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
    t1 = t20;
    goto me2;
mn18:;
ma6:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)3ULL)) goto mn21;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfch9mo7lqp8l));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
    t1 = t23;
    goto me2;
mn21:;
ma7:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t25 != ((uint8_t)4ULL)) goto mn24;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sismsc98l6u4e));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
    t1 = t26;
    goto me2;
mn24:;
ma8:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)5ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sgo00f0mafbnd));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
    t1 = t29;
    goto me2;
mn27:;
ma9:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)6ULL)) goto mn30;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s187rb18i1kgk));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
    t1 = t32;
    goto me2;
mn30:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 581, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sink_text_s4o8tr0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_sink_text_s4o8tr0 F;
    void * gm_l0; /* d */
    gm_tiilmfd16v3fc gm_l2; /* e */
    uint32_t gm_l3; /* op */
    GM_STACK_CHECK(&F, 262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_tiilmfd16v3fc t5 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l1) + 8) + 0));
    gm_l2 = t5;
    uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 8) + 24));
    gm_l3 = t6;
    gm_tommf4j9upomc t7;
    memset((uint8_t *)&t7 + 20, 0, 4);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    void * t11 = (void *)((uint8_t *)&gm_l2);
    gm_b16_8 t12 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t12);
    uint32_t t13 = gm_l3;
    GM_ST(uint32_t, (uint8_t *)&t7 + 16, t13);
    gm_tabb710d9mq2e t14;
    gm_f_format_pvddll3((void *)&t14, (void *)NULL, cx, NULL, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srco4ljquuk5d87, 117, 24);
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    t1 = t16;
    goto me2;
mn3:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
    if (t18 != ((uint8_t)1ULL)) goto mn17;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s579fb7l5l6df));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
    t1 = t19;
    goto me2;
mn17:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 116, 73);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_op_text_ud90n2l(void *env, gm_node *cx, gm_node *inst, gm_tesaltf0mhcki a0) {
    struct gm_fr_soa_op_text_ud90n2l F;
    gm_tesaltf0mhcki gm_l0; /* op */
    gm_toh0ootodhjue gm_l1; /* x */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)3ULL)) goto mn3;
    gm_toh0ootodhjue t5 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t5;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 1));
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 2));
    gm_toh0ootodhjue t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), t6);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 1), t7);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 2), t8);
    gm_tabb710d9mq2e t10;
    gm_f_format_4i3nb5n((void *)&t10, (void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srco4ljquuk5d87, 586, 18);
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    t1 = t12;
    goto me2;
mn3:;
    gm_b16_8 t14;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t25) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma23; default: goto ma24; }
ma16:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t27 != ((uint8_t)0ULL)) goto mn26;
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s2a9t8smkevg0));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
    t14 = t28;
    goto me15;
mn26:;
ma17:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)1ULL)) goto mn29;
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sbgepjj6hgjej));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(3));
    t14 = t31;
    goto me15;
mn29:;
ma18:;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t33 != ((uint8_t)2ULL)) goto mn32;
    gm_b16_8 t34;
    GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s4lt93qhapike));
    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(3));
    t14 = t34;
    goto me15;
mn32:;
ma19:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t36 != ((uint8_t)3ULL)) goto mn35;
    gm_b16_8 t37;
    GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sqa0b48i8mvk7));
    GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(5));
    t14 = t37;
    goto me15;
mn35:;
ma20:;
    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t39 != ((uint8_t)4ULL)) goto mn38;
    gm_b16_8 t40;
    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s35gfshei80n5));
    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(7));
    t14 = t40;
    goto me15;
mn38:;
ma21:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t42 != ((uint8_t)5ULL)) goto mn41;
    gm_b16_8 t43;
    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s3o5gaeteeggu));
    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(4));
    t14 = t43;
    goto me15;
mn41:;
ma22:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t45 != ((uint8_t)6ULL)) goto mn44;
    gm_b16_8 t46;
    GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sah1jmr01nidj));
    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
    t14 = t46;
    goto me15;
mn44:;
ma23:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t48 != ((uint8_t)7ULL)) goto mn47;
    gm_b16_8 t49;
    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_supvu1ruutbov));
    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(8));
    t14 = t49;
    goto me15;
mn47:;
ma24:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 587, 10);
me15:;
    t1 = t14;
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 585, 59);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_spec_text_fr9884d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_spec_text_fr9884d F;
    void * gm_l0; /* spec */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* align */
    gm_b16_8 gm_l3; /* kind */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK(&F, 493u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; default: goto ma9; }
ma5:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sbqq5iijjahf9));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
        t3 = t13;
        goto me4;
mn11:;
ma6:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s5n5e484glelv));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
        t3 = t16;
        goto me4;
mn14:;
ma7:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t18 != ((uint8_t)2ULL)) goto mn17;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s6edba3m7mc9a));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(5));
        t3 = t19;
        goto me4;
mn17:;
ma8:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 1) + 0));
        if (t21 != ((uint8_t)3ULL)) goto mn20;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s2ma5cp41jk1a));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(6));
        t3 = t22;
        goto me4;
mn20:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 430, 14);
me4:;
        gm_l2 = t3;
    }
    {
        void * t23 = gm_l0;
        gm_b16_8 t24;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        switch (t34) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma28; case ((uint8_t)3ULL): goto ma29; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma31; case ((uint8_t)6ULL): goto ma32; default: goto ma33; }
ma26:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t36 != ((uint8_t)0ULL)) goto mn35;
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sc5qopgd7dm66));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(7));
        t24 = t37;
        goto me25;
mn35:;
ma27:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t39 != ((uint8_t)1ULL)) goto mn38;
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sbsb3nmbfgbv2));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(3));
        t24 = t40;
        goto me25;
mn38:;
ma28:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t42 != ((uint8_t)2ULL)) goto mn41;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s85jtgs7au5b5));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(8));
        t24 = t43;
        goto me25;
mn41:;
ma29:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t45 != ((uint8_t)3ULL)) goto mn44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s49o1oiv7k0po));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(3));
        t24 = t46;
        goto me25;
mn44:;
ma30:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t48 != ((uint8_t)4ULL)) goto mn47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_seg9l1usqbs3j));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
        t24 = t49;
        goto me25;
mn47:;
ma31:;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t51 != ((uint8_t)5ULL)) goto mn50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sgfufhs3psibd));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(3));
        t24 = t52;
        goto me25;
mn50:;
ma32:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t23) + 20) + 0));
        if (t54 != ((uint8_t)6ULL)) goto mn53;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sajbccb18c45d));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(5));
        t24 = t55;
        goto me25;
mn53:;
ma33:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 436, 13);
me25:;
        gm_l3 = t24;
    }
    {
        void * t56 = gm_l1;
        gm_th32vq80aukjr t57;
        memset((uint8_t *)&t57 + 1, 0, 7);
        memset((uint8_t *)&t57 + 27, 0, 1);
        void * t58 = gm_l0;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t58) + 0));
        GM_ST(uint8_t, (uint8_t *)&t57 + 0, t59);
        gm_b16_8 t60 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 8, t60);
        void * t61 = gm_l0;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t61) + 2));
        GM_ST(uint8_t, (uint8_t *)&t57 + 24, t62);
        void * t63 = gm_l0;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 3));
        GM_ST(uint8_t, (uint8_t *)&t57 + 25, t64);
        void * t65 = gm_l0;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t65) + 4));
        GM_ST(uint8_t, (uint8_t *)&t57 + 26, t66);
        void * t67 = gm_l0;
        uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)t67) + 8));
        GM_ST(uint32_t, (uint8_t *)&t57 + 28, t68);
        gm_tftb1odc739t5 t69 = gm_f_write_t5i5r03(t56, &t57);
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srco4ljquuk5d87, 445, 5);
        gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
    }
    {
        void * t72 = gm_l0;
        if (!(((((uint8_t *)t72) + 12))[4] != 0)) goto ie71;
        uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)t72) + 12));
        gm_l4 = t73;
        {
            void * t75 = gm_l1;
            uint32_t t76 = gm_l4;
            gm_t88rcucg1b7uk t77;
            GM_ST(uint32_t, ((uint8_t *)&t77 + 0), t76);
            gm_tftb1odc739t5 t78 = gm_f_write_7h6pth0(t75, t77);
            if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srco4ljquuk5d87, 447, 9);
            gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
        }
        goto ix71;
ie71:;
        {
            void * t81 = gm_l1;
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s026u6q948evn));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t83 = gm_f_extend_0cs85gs(t81, t82);
            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srco4ljquuk5d87, 449, 9);
            gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
        }
ix71:;
    }
    {
        void * t85 = gm_l1;
        gm_b16_8 t86 = gm_l3;
        gm_tnpifv20irnph t87;
        GM_ST(gm_b16_8, ((uint8_t *)&t87 + 0), t86);
        gm_tftb1odc739t5 t88 = gm_f_write_7ilko9q(t85, t87);
        if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_srco4ljquuk5d87, 451, 5);
        gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_text_9qjnpra(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_text_9qjnpra F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 985u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t59) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma25; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma29; case ((uint8_t)27ULL): goto ma30; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma32; case ((uint8_t)30ULL): goto ma33; case ((uint8_t)31ULL): goto ma34; case ((uint8_t)32ULL): goto ma35; case ((uint8_t)33ULL): goto ma36; case ((uint8_t)34ULL): goto ma37; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma39; case ((uint8_t)37ULL): goto ma40; case ((uint8_t)38ULL): goto ma41; case ((uint8_t)39ULL): goto ma42; case ((uint8_t)40ULL): goto ma43; case ((uint8_t)41ULL): goto ma44; case ((uint8_t)42ULL): goto ma45; case ((uint8_t)43ULL): goto ma46; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma48; case ((uint8_t)46ULL): goto ma49; case ((uint8_t)47ULL): goto ma50; case ((uint8_t)48ULL): goto ma51; case ((uint8_t)49ULL): goto ma52; case ((uint8_t)50ULL): goto ma53; case ((uint8_t)51ULL): goto ma54; case ((uint8_t)52ULL): goto ma55; case ((uint8_t)53ULL): goto ma56; case ((uint8_t)54ULL): goto ma57; default: goto ma58; }
ma3:;
    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t61 != ((uint8_t)0ULL)) goto mn60;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s93umj3p0tala));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(4));
    t1 = t62;
    goto me2;
mn60:;
ma4:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t64 != ((uint8_t)1ULL)) goto mn63;
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_slnhe1h36jmub));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(8));
    t1 = t65;
    goto me2;
mn63:;
ma5:;
    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t67 != ((uint8_t)2ULL)) goto mn66;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sa8dse4jk5ibv));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(9));
    t1 = t68;
    goto me2;
mn66:;
ma6:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t70 != ((uint8_t)3ULL)) goto mn69;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_smmus0krp2svr));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(5));
    t1 = t71;
    goto me2;
mn69:;
ma7:;
    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t73 != ((uint8_t)4ULL)) goto mn72;
    gm_b16_8 t74;
    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sa5s0jools6a2));
    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(8));
    t1 = t74;
    goto me2;
mn72:;
ma8:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t76 != ((uint8_t)5ULL)) goto mn75;
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sacua9qc75rts));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(6));
    t1 = t77;
    goto me2;
mn75:;
ma9:;
    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t79 != ((uint8_t)6ULL)) goto mn78;
    gm_b16_8 t80;
    GM_ST(void *, (uint8_t *)&t80, (void *)(gm_siqgrmkn8n4vn));
    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(7));
    t1 = t80;
    goto me2;
mn78:;
ma10:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t82 != ((uint8_t)7ULL)) goto mn81;
    gm_b16_8 t83;
    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sc03pp1oj3q4k));
    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(6));
    t1 = t83;
    goto me2;
mn81:;
ma11:;
    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t85 != ((uint8_t)8ULL)) goto mn84;
    gm_b16_8 t86;
    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s9i2ld3a42lap));
    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(7));
    t1 = t86;
    goto me2;
mn84:;
ma12:;
    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t88 != ((uint8_t)9ULL)) goto mn87;
    gm_b16_8 t89;
    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sjbhp9l7seful));
    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(6));
    t1 = t89;
    goto me2;
mn87:;
ma13:;
    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t91 != ((uint8_t)10ULL)) goto mn90;
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sit8u7uppaik9));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(6));
    t1 = t92;
    goto me2;
mn90:;
ma14:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t94 != ((uint8_t)11ULL)) goto mn93;
    gm_b16_8 t95;
    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sf5llhuhdlajp));
    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(8));
    t1 = t95;
    goto me2;
mn93:;
ma15:;
    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t97 != ((uint8_t)12ULL)) goto mn96;
    gm_b16_8 t98;
    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_so8fkrgrobcsk));
    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(8));
    t1 = t98;
    goto me2;
mn96:;
ma16:;
    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t100 != ((uint8_t)13ULL)) goto mn99;
    gm_b16_8 t101;
    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_so3oq96gr0k5r));
    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(9));
    t1 = t101;
    goto me2;
mn99:;
ma17:;
    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t103 != ((uint8_t)14ULL)) goto mn102;
    gm_b16_8 t104;
    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_snstimc3n04u0));
    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(11));
    t1 = t104;
    goto me2;
mn102:;
ma18:;
    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t106 != ((uint8_t)15ULL)) goto mn105;
    gm_b16_8 t107;
    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_shvp36midgf58));
    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(9));
    t1 = t107;
    goto me2;
mn105:;
ma19:;
    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t109 != ((uint8_t)16ULL)) goto mn108;
    gm_b16_8 t110;
    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_slov3heeajhqn));
    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(11));
    t1 = t110;
    goto me2;
mn108:;
ma20:;
    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t112 != ((uint8_t)17ULL)) goto mn111;
    gm_b16_8 t113;
    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_segr993uh2crm));
    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(10));
    t1 = t113;
    goto me2;
mn111:;
ma21:;
    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t115 != ((uint8_t)18ULL)) goto mn114;
    gm_b16_8 t116;
    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_s3ol5q9br79vf));
    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(7));
    t1 = t116;
    goto me2;
mn114:;
ma22:;
    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t118 != ((uint8_t)19ULL)) goto mn117;
    gm_b16_8 t119;
    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sk721elh8ouv2));
    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(10));
    t1 = t119;
    goto me2;
mn117:;
ma23:;
    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t121 != ((uint8_t)20ULL)) goto mn120;
    gm_b16_8 t122;
    GM_ST(void *, (uint8_t *)&t122, (void *)(gm_s6nm27ehg597q));
    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(6));
    t1 = t122;
    goto me2;
mn120:;
ma24:;
    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t124 != ((uint8_t)21ULL)) goto mn123;
    gm_b16_8 t125;
    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_stehcnejvoo24));
    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(5));
    t1 = t125;
    goto me2;
mn123:;
ma25:;
    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t127 != ((uint8_t)22ULL)) goto mn126;
    gm_b16_8 t128;
    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s6ofccoi3ursa));
    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(11));
    t1 = t128;
    goto me2;
mn126:;
ma26:;
    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t130 != ((uint8_t)23ULL)) goto mn129;
    gm_b16_8 t131;
    GM_ST(void *, (uint8_t *)&t131, (void *)(gm_stidgejoe9ul2));
    GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(10));
    t1 = t131;
    goto me2;
mn129:;
ma27:;
    uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t133 != ((uint8_t)24ULL)) goto mn132;
    gm_b16_8 t134;
    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sai3vjkob4s2v));
    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(9));
    t1 = t134;
    goto me2;
mn132:;
ma28:;
    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t136 != ((uint8_t)25ULL)) goto mn135;
    gm_b16_8 t137;
    GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sgtriqnjufjbf));
    GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(9));
    t1 = t137;
    goto me2;
mn135:;
ma29:;
    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t139 != ((uint8_t)26ULL)) goto mn138;
    gm_b16_8 t140;
    GM_ST(void *, (uint8_t *)&t140, (void *)(gm_smgioupbmjajr));
    GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(13));
    t1 = t140;
    goto me2;
mn138:;
ma30:;
    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t142 != ((uint8_t)27ULL)) goto mn141;
    gm_b16_8 t143;
    GM_ST(void *, (uint8_t *)&t143, (void *)(gm_s1js4dn5e65ja));
    GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(10));
    t1 = t143;
    goto me2;
mn141:;
ma31:;
    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t145 != ((uint8_t)28ULL)) goto mn144;
    gm_b16_8 t146;
    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s5e0seq0he7ou));
    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(9));
    t1 = t146;
    goto me2;
mn144:;
ma32:;
    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t148 != ((uint8_t)29ULL)) goto mn147;
    gm_b16_8 t149;
    GM_ST(void *, (uint8_t *)&t149, (void *)(gm_so2sjlodq81qn));
    GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(12));
    t1 = t149;
    goto me2;
mn147:;
ma33:;
    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t151 != ((uint8_t)30ULL)) goto mn150;
    gm_b16_8 t152;
    GM_ST(void *, (uint8_t *)&t152, (void *)(gm_spskhsf5tnedj));
    GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(9));
    t1 = t152;
    goto me2;
mn150:;
ma34:;
    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t154 != ((uint8_t)31ULL)) goto mn153;
    gm_b16_8 t155;
    GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sqlk0h0kae56e));
    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(7));
    t1 = t155;
    goto me2;
mn153:;
ma35:;
    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t157 != ((uint8_t)32ULL)) goto mn156;
    gm_b16_8 t158;
    GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s4gilalcmdvu3));
    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(7));
    t1 = t158;
    goto me2;
mn156:;
ma36:;
    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t160 != ((uint8_t)33ULL)) goto mn159;
    gm_b16_8 t161;
    GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sjfcbh429vmot));
    GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(8));
    t1 = t161;
    goto me2;
mn159:;
ma37:;
    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t163 != ((uint8_t)34ULL)) goto mn162;
    gm_b16_8 t164;
    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_soerl6rds8b9v));
    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(11));
    t1 = t164;
    goto me2;
mn162:;
ma38:;
    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t166 != ((uint8_t)35ULL)) goto mn165;
    gm_b16_8 t167;
    GM_ST(void *, (uint8_t *)&t167, (void *)(gm_s7m3rbk4l87ah));
    GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(14));
    t1 = t167;
    goto me2;
mn165:;
ma39:;
    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t169 != ((uint8_t)36ULL)) goto mn168;
    gm_b16_8 t170;
    GM_ST(void *, (uint8_t *)&t170, (void *)(gm_sm0gs6tv176le));
    GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(7));
    t1 = t170;
    goto me2;
mn168:;
ma40:;
    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t172 != ((uint8_t)37ULL)) goto mn171;
    gm_b16_8 t173;
    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_s1gf327eapo1j));
    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(10));
    t1 = t173;
    goto me2;
mn171:;
ma41:;
    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t175 != ((uint8_t)38ULL)) goto mn174;
    gm_b16_8 t176;
    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sgm565ktbj9ua));
    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(11));
    t1 = t176;
    goto me2;
mn174:;
ma42:;
    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t178 != ((uint8_t)39ULL)) goto mn177;
    gm_b16_8 t179;
    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sjdrosvte680q));
    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(11));
    t1 = t179;
    goto me2;
mn177:;
ma43:;
    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t181 != ((uint8_t)40ULL)) goto mn180;
    gm_b16_8 t182;
    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_stdefoi5sk91c));
    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(11));
    t1 = t182;
    goto me2;
mn180:;
ma44:;
    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t184 != ((uint8_t)41ULL)) goto mn183;
    gm_b16_8 t185;
    GM_ST(void *, (uint8_t *)&t185, (void *)(gm_so1u9tqjfpsmc));
    GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(8));
    t1 = t185;
    goto me2;
mn183:;
ma45:;
    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t187 != ((uint8_t)42ULL)) goto mn186;
    gm_b16_8 t188;
    GM_ST(void *, (uint8_t *)&t188, (void *)(gm_s19069udlvhni));
    GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(8));
    t1 = t188;
    goto me2;
mn186:;
ma46:;
    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t190 != ((uint8_t)43ULL)) goto mn189;
    gm_b16_8 t191;
    GM_ST(void *, (uint8_t *)&t191, (void *)(gm_s2289h15tkpi1));
    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(11));
    t1 = t191;
    goto me2;
mn189:;
ma47:;
    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t193 != ((uint8_t)44ULL)) goto mn192;
    gm_b16_8 t194;
    GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sqhkrul9t68sa));
    GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(7));
    t1 = t194;
    goto me2;
mn192:;
ma48:;
    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t196 != ((uint8_t)45ULL)) goto mn195;
    gm_b16_8 t197;
    GM_ST(void *, (uint8_t *)&t197, (void *)(gm_shtuc6se4lilq));
    GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(6));
    t1 = t197;
    goto me2;
mn195:;
ma49:;
    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t199 != ((uint8_t)46ULL)) goto mn198;
    gm_b16_8 t200;
    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_s5k0vuc678fh1));
    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(7));
    t1 = t200;
    goto me2;
mn198:;
ma50:;
    uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t202 != ((uint8_t)47ULL)) goto mn201;
    gm_b16_8 t203;
    GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sm017vofcj044));
    GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(11));
    t1 = t203;
    goto me2;
mn201:;
ma51:;
    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t205 != ((uint8_t)48ULL)) goto mn204;
    gm_b16_8 t206;
    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_s7d4tu5fkkg12));
    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(10));
    t1 = t206;
    goto me2;
mn204:;
ma52:;
    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t208 != ((uint8_t)49ULL)) goto mn207;
    gm_b16_8 t209;
    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_ss308e7joejdo));
    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(12));
    t1 = t209;
    goto me2;
mn207:;
ma53:;
    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t211 != ((uint8_t)50ULL)) goto mn210;
    gm_b16_8 t212;
    GM_ST(void *, (uint8_t *)&t212, (void *)(gm_ss6dev5u7tl99));
    GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(11));
    t1 = t212;
    goto me2;
mn210:;
ma54:;
    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t214 != ((uint8_t)51ULL)) goto mn213;
    gm_b16_8 t215;
    GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sakmuvnjjp217));
    GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(9));
    t1 = t215;
    goto me2;
mn213:;
ma55:;
    uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t217 != ((uint8_t)52ULL)) goto mn216;
    gm_b16_8 t218;
    GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sln5qir3tgi7j));
    GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(9));
    t1 = t218;
    goto me2;
mn216:;
ma56:;
    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t220 != ((uint8_t)53ULL)) goto mn219;
    gm_b16_8 t221;
    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sqfu5kl4vat80));
    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(8));
    t1 = t221;
    goto me2;
mn219:;
ma57:;
    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t223 != ((uint8_t)54ULL)) goto mn222;
    gm_b16_8 t224;
    GM_ST(void *, (uint8_t *)&t224, (void *)(gm_sbcq15b9em6fm));
    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(8));
    t1 = t224;
    goto me2;
mn222:;
ma58:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 583, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_target_text_4kebb7t(void *env, gm_node *cx, gm_node *inst, gm_te9hrk0u35rev a0) {
    struct gm_fr_target_text_4kebb7t F;
    gm_te9hrk0u35rev gm_l0; /* t */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* label */
    GM_STACK_CHECK(&F, 166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 0));
    gm_l1 = t5;
    uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
    gm_l2 = t6;
    uint32_t t7 = gm_l1;
    uint32_t t8 = gm_l2;
    gm_tcc1rq4n1avb2 t9;
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t7);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 4), t8);
    gm_tabb710d9mq2e t10;
    gm_f_format_kj3qi5h((void *)&t10, (void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srco4ljquuk5d87, 112, 30);
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    t1 = t12;
    goto me2;
mn3:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t14 != ((uint8_t)1ULL)) goto mn13;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_srri8c6al1v1g));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
    t1 = t15;
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 111, 59);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_tir_dump_ppa6e9g(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_tir_dump_ppa6e9g F;
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* sig */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* f */
    GM_STACK_CHECK(&F, 523u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = F.l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 344) + 0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l4 = t14;
                    {
                        uint64_t t16 = gm_l4;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = F.l0;
                        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 344) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srco4ljquuk5d87, 23, 17);
                        void * t20 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 344) + 0)) + t17 * 136);
                        gm_l5 = t20;
                    }
                    {
                        uint8_t t22 = 0;
                        uint8_t t23 = 0;
                        void * t24 = gm_l5;
                        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 16));
                        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        uint8_t t27 = (gm_eqff85ac10o3(((uint8_t *)&t25), ((uint8_t *)&t26)));
                        if (!t27) {
                            void * t28 = gm_l5;
                            uint8_t t29 = (((GM_LD(uintptr_t, (((uint8_t *)t28) + 0) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)t28) + 0)), gm_stgqtu1f7bco8, 7) == 0));
                            t23 = t29;
                        } else t23 = 1;
                        if (!t23) {
                            void * t30 = gm_l5;
                            uint8_t t31;
                            if ((((((uint8_t *)t30) + 96))[24] != 0)) goto mn33;
                            t31 = ((uint8_t)0);
                            goto me32;
mn33:;
                            t31 = ((uint8_t)1);
                            goto me32;
mn34:;
                            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 24, 65);
me32:;
                            t22 = t31;
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
                        void * t37 = F.l0;
                        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
                        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
                        void * t40 = gm_l5;
                        uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t40) + 16) + 0));
                        void * t42 = gm_f_file_o15fqku(t39, t41);
                        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 0));
                        gm_b16_8 t44;
                        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_slmii6hjigrcr));
                        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
                        uint8_t t45 = gm_f_starts_with_6vvt01e(t43, t44);
                        if (!t45) goto ie36;
                        {
                            goto lc5;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    {
                        void * t47 = (void *)((uint8_t *)&F.l1);
                        uint64_t t48 = gm_l4;
                        uint32_t t49 = (uint32_t)t48;
                        gm_tftb1odc739t5 t50 = gm_f_push_im9d4ro(t47, t49);
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srco4ljquuk5d87, 26, 9);
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 22, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t53 = gm_l3;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l3 = t56;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_closure t58 = ((gm_closure){(void *)gm_f_fn_pfpd9oj, (void *)&F.h, cx});
        gm_f_sort_by_egbtonb(t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memset((uint8_t *)&F.l6 + 52, 0, 4);
        void * t59 = F.l0;
        GM_ST(void *, (uint8_t *)&F.l6 + 0, t59);
        gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l6 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&F.l6 + 48, ((uint32_t)0ULL));
    }
    {
        {
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l7 = t61;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t63 = gm_l8;
                uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t65 = (t63 < t64);
                if (!t65) goto lb62;
                {
                    uint64_t t68 = gm_l8;
                    uint64_t t69 = (uintptr_t)t68;
                    uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint32_t t71 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t69 * 4));
                    uint32_t t74 = GM_LD(uint32_t, ((uint8_t *)&t71));
                    gm_l9 = t74;
                    {
                        void * t76 = (void *)((uint8_t *)&F.l6);
                        uint32_t t77 = gm_l9;
                        gm_b0_1 t78 = gm_f_write_fn_jkakt7l((void *)NULL, cx, NULL, t76, t77);
                        if (gm_unw.kind) {
                            goto gm_ll66;
                        }
                    }
                    goto me72;
mn73:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 37, 5);
me72:;
                }
lc62:;
                {
                    uint64_t t80 = gm_l8;
                    uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                    uint64_t t83 = r81;
                    gm_l8 = t83;
                }
            }
gm_ll66:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb62; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc62; }
            }
            goto gm_lad;
lb62:;
        }
    }
    {
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 8) + 0));
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        uint8_t t86 = 0;
        F.ret = t85;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_arm_4vimkqo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_arm_4vimkqo F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* a */
    void * gm_l3; /* g */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s76ud0alkjfse));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        gm_b0_1 t5 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        uint64_t t7 = gm_l1;
        uint64_t r8; bool o9 = __builtin_add_overflow(t7, ((uint64_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 518, 17);
        uint64_t t10 = r8;
        void * t11 = gm_l2;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        gm_b0_1 t13 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t6, t10, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l2;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 8));
        if (!(GM_LD(void *, ((uint8_t *)&t16)) != 0)) goto ie14;
        void * t17 = GM_LD(void *, ((uint8_t *)&t16));
        gm_l3 = t17;
        {
            void * t19 = gm_l0;
            uint64_t t20 = gm_l1;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            if (o22) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 520, 22);
            uint64_t t23 = r21;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sqflguscq1uc5));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(5));
            gm_b0_1 t25 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t19, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t26 = gm_l0;
            uint64_t t27 = gm_l1;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)2ULL), &r28);
            if (o29) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 521, 22);
            uint64_t t30 = r28;
            void * t31 = gm_l3;
            gm_b0_1 t32 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t26, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t33 = gm_l0;
        uint64_t t34 = gm_l1;
        uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
        if (o36) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 523, 18);
        uint64_t t37 = r35;
        void * t38 = gm_l2;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 16));
        gm_b0_1 t40 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t33, t37, t39);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_block_q5mpqjf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_block_q5mpqjf F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    uint32_t gm_l6; /* l */
    uint32_t gm_l7; /* l */
    void * gm_l8; /* x */
    gm_b16_8 gm_l9; /* ls */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t9805qtonbsag gm_l14; /* l */
    uint32_t gm_l15; /* id */
    void * gm_l16; /* p */
    void * gm_l17; /* v */
    gm_b16_8 gm_l18; /* ps */
    void * gm_l19; /* v */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    void * gm_l22; /* p */
    void * gm_l23; /* x */
    void * gm_l24; /* x */
    uint8_t gm_l25; /* is_err */
    void * gm_l26; /* x */
    void * gm_l27; /* t */
    GM_STACK_CHECK(&F, 1756u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l2;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        switch (t27) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma25; default: goto ma26; }
ma19:;
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t29 != ((uint8_t)0ULL)) goto mn28;
                        uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l6 = t30;
                        if ((GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8)) != 0)) goto mn28;
                        void * t31 = gm_l0;
                        uint64_t t32 = gm_l1;
                        void * t33 = gm_l0;
                        uint32_t t34 = gm_l6;
                        gm_b16_8 t35 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t33, t34);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tnpifv20irnph t36;
                        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
                        gm_tabb710d9mq2e t37;
                        gm_f_format_cqr3tc0((void *)&t37, (void *)NULL, cx, NULL, t36);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srco4ljquuk5d87, 457, 50);
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                        gm_b0_1 t40 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t31, t32, t39);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn28:;
ma20:;
                        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t42 != ((uint8_t)0ULL)) goto mn41;
                        uint32_t t43 = GM_LD(uint32_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t43;
                        if (!(GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8)) != 0)) goto mn41;
                        void * t44 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l8 = t44;
                        {
                            void * t46 = gm_l0;
                            uint64_t t47 = gm_l1;
                            void * t48 = gm_l0;
                            uint32_t t49 = gm_l7;
                            gm_b16_8 t50 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tnpifv20irnph t51;
                            GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t50);
                            gm_tabb710d9mq2e t52;
                            gm_f_format_cqr3tc0((void *)&t52, (void *)NULL, cx, NULL, t51);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_srco4ljquuk5d87, 459, 37);
                            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
                            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                            gm_b0_1 t55 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t46, t47, t54);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t56 = gm_l0;
                            uint64_t t57 = gm_l1;
                            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                            if (o59) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 460, 30);
                            uint64_t t60 = r58;
                            void * t61 = gm_l8;
                            gm_b0_1 t62 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t56, t60, t61);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn41:;
ma21:;
                        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t64 != ((uint8_t)1ULL)) goto mn63;
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l9 = t65;
                        void * t66 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 16));
                        gm_l10 = t66;
                        {
                            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            {
                                gm_b16_8 t69 = gm_l9;
                                gm_l12 = t69;
                            }
                            {
                                gm_l13 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t71 = gm_l13;
                                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    uint8_t t73 = (t71 < t72);
                                    if (!t73) goto lb70;
                                    {
                                        uint64_t t76 = gm_l13;
                                        uint64_t t77 = (uintptr_t)t76;
                                        uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        gm_t9805qtonbsag t81 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t77 * 8));
                                        gm_l14 = t81;
                                        {
                                            void * t83 = (void *)((uint8_t *)&F.l11);
                                            gm_b16_8 t84;
                                            if (!((((uint8_t *)&gm_l14))[4] != 0)) goto ie85;
                                            uint32_t t86 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                            gm_l15 = t86;
                                            void * t88 = gm_l0;
                                            uint32_t t89 = gm_l15;
                                            gm_b16_8 t90 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t88, t89);
                                            if (gm_unw.kind) {
                                                goto gm_ll74;
                                            }
                                            gm_b16_8 t91 = t90;
                                            t84 = t91;
                                            goto ix85;
ie85:;
                                            gm_b16_8 t93;
                                            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sfdbfrv46lihd));
                                            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(1));
                                            gm_b16_8 t94 = t93;
                                            t84 = t94;
ix85:;
                                            gm_tftb1odc739t5 t95 = gm_f_push_3ef746b(t83, t84);
                                            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srco4ljquuk5d87, 464, 31);
                                            gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                                        }
                                        goto me79;
mn80:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 464, 17);
me79:;
                                    }
lc70:;
                                    {
                                        uint64_t t98 = gm_l13;
                                        uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                                        uint64_t t101 = r99;
                                        gm_l13 = t101;
                                    }
                                }
gm_ll74:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                                }
                                goto gm_ll9;
lb70:;
                            }
                        }
                        {
                            void * t102 = gm_l0;
                            uint64_t t103 = gm_l1;
                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                            gm_b16_8 t106;
                            GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t107;
                            gm_f_join_sfknljb((void *)&t107, (void *)NULL, cx, NULL, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srco4ljquuk5d87, 465, 64);
                            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
                            gm_tnpifv20irnph t109;
                            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                            gm_tabb710d9mq2e t110;
                            gm_f_format_qhad3qp((void *)&t110, (void *)NULL, cx, NULL, t109);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_srco4ljquuk5d87, 465, 37);
                            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                            gm_b0_1 t113 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t102, t103, t112);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t114 = gm_l0;
                            uint64_t t115 = gm_l1;
                            uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                            if (o117) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 466, 30);
                            uint64_t t118 = r116;
                            void * t119 = gm_l10;
                            gm_b0_1 t120 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t114, t118, t119);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn63:;
ma22:;
                        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t122 != ((uint8_t)2ULL)) goto mn121;
                        void * t123 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l16 = t123;
                        void * t124 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l17 = t124;
                        {
                            void * t126 = gm_l0;
                            uint64_t t127 = gm_l1;
                            gm_b16_8 t128;
                            GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sugq7dl55ocon));
                            GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(6));
                            gm_b0_1 t129 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t126, t127, t128);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t130 = gm_l0;
                            uint64_t t131 = gm_l1;
                            uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                            if (o133) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 470, 30);
                            uint64_t t134 = r132;
                            void * t135 = gm_l16;
                            gm_b0_1 t136 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t130, t134, t135);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t137 = gm_l0;
                            uint64_t t138 = gm_l1;
                            uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                            if (o140) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 471, 30);
                            uint64_t t141 = r139;
                            void * t142 = gm_l17;
                            gm_b0_1 t143 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t137, t141, t142);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn121:;
ma23:;
                        uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t145 != ((uint8_t)3ULL)) goto mn144;
                        gm_b16_8 t146 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l18 = t146;
                        void * t147 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 16));
                        gm_l19 = t147;
                        {
                            void * t149 = gm_l0;
                            uint64_t t150 = gm_l1;
                            gm_b16_8 t151;
                            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_s0ve0dba4rtj8));
                            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(12));
                            gm_b0_1 t152 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t149, t150, t151);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            {
                                gm_b16_8 t154 = gm_l18;
                                gm_l20 = t154;
                            }
                            {
                                gm_l21 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t156 = gm_l21;
                                    uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    uint8_t t158 = (t156 < t157);
                                    if (!t158) goto lb155;
                                    {
                                        uint64_t t161 = gm_l21;
                                        uint64_t t162 = (uintptr_t)t161;
                                        uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                        void * t164 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t162 * 8));
                                        void * t167 = GM_LD(void *, ((uint8_t *)&t164));
                                        gm_l22 = t167;
                                        {
                                            void * t170 = gm_l22;
                                            if (!(GM_LD(void *, ((uint8_t *)&t170)) != 0)) goto ie169;
                                            void * t171 = GM_LD(void *, ((uint8_t *)&t170));
                                            gm_l23 = t171;
                                            {
                                                void * t173 = gm_l0;
                                                uint64_t t174 = gm_l1;
                                                uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                                                if (o176) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 477, 38);
                                                uint64_t t177 = r175;
                                                void * t178 = gm_l23;
                                                gm_b0_1 t179 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t173, t177, t178);
                                                if (gm_unw.kind) {
                                                    goto gm_ll159;
                                                }
                                            }
                                            goto ix169;
ie169:;
                                            {
                                                void * t181 = gm_l0;
                                                uint64_t t182 = gm_l1;
                                                uint64_t r183; bool o184 = __builtin_add_overflow(t182, ((uint64_t)1ULL), &r183);
                                                if (o184) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 479, 38);
                                                uint64_t t185 = r183;
                                                gm_b16_8 t186;
                                                GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sfdbfrv46lihd));
                                                GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(1));
                                                gm_b0_1 t187 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t181, t185, t186);
                                                if (gm_unw.kind) {
                                                    goto gm_ll159;
                                                }
                                            }
ix169:;
                                        }
                                        goto me165;
mn166:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 475, 17);
me165:;
                                    }
lc155:;
                                    {
                                        uint64_t t189 = gm_l21;
                                        uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                        uint64_t t192 = r190;
                                        gm_l21 = t192;
                                    }
                                }
gm_ll159:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb155; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc155; }
                                }
                                goto gm_ll9;
lb155:;
                            }
                        }
                        {
                            void * t193 = gm_l0;
                            uint64_t t194 = gm_l1;
                            uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                            if (o196) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 482, 30);
                            uint64_t t197 = r195;
                            void * t198 = gm_l19;
                            gm_b0_1 t199 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t193, t197, t198);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn144:;
ma24:;
                        uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t201 != ((uint8_t)4ULL)) goto mn200;
                        void * t202 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                        gm_l24 = t202;
                        {
                            void * t204 = gm_l0;
                            uint64_t t205 = gm_l1;
                            gm_b16_8 t206;
                            GM_ST(void *, (uint8_t *)&t206, (void *)(gm_sk5s72mtujop8));
                            GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(4));
                            gm_b0_1 t207 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t204, t205, t206);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t208 = gm_l0;
                            uint64_t t209 = gm_l1;
                            uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1ULL), &r210);
                            if (o211) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 486, 30);
                            uint64_t t212 = r210;
                            void * t213 = gm_l24;
                            gm_b0_1 t214 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t208, t212, t213);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn200:;
ma25:;
                        uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t216 != ((uint8_t)5ULL)) goto mn215;
                        uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l25 = t217;
                        void * t218 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l26 = t218;
                        {
                            void * t220 = gm_l0;
                            uint64_t t221 = gm_l1;
                            gm_b16_8 t222;
                            uint8_t t224 = gm_l25;
                            if (!t224) goto ie223;
                            gm_b16_8 t226;
                            GM_ST(void *, (uint8_t *)&t226, (void *)(gm_ser8ip7vtbv6c));
                            GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(8));
                            gm_b16_8 t227 = t226;
                            t222 = t227;
                            goto ix223;
ie223:;
                            gm_b16_8 t229;
                            GM_ST(void *, (uint8_t *)&t229, (void *)(gm_sllcm1pqagh28));
                            GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(5));
                            gm_b16_8 t230 = t229;
                            t222 = t230;
ix223:;
                            gm_b0_1 t231 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t220, t221, t222);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t232 = gm_l0;
                            uint64_t t233 = gm_l1;
                            uint64_t r234; bool o235 = __builtin_add_overflow(t233, ((uint64_t)1ULL), &r234);
                            if (o235) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 490, 30);
                            uint64_t t236 = r234;
                            void * t237 = gm_l26;
                            gm_b0_1 t238 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t232, t236, t237);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn215:;
ma26:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 456, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 455, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t240 = gm_l4;
                    uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                    uint64_t t243 = r241;
                    gm_l4 = t243;
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
        void * t245 = gm_l2;
        void * t246 = GM_LD(void *, (((uint8_t *)t245) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t246)) != 0)) goto ie244;
        void * t247 = GM_LD(void *, ((uint8_t *)&t246));
        gm_l27 = t247;
        {
            void * t249 = gm_l0;
            uint64_t t250 = gm_l1;
            gm_b16_8 t251;
            GM_ST(void *, (uint8_t *)&t251, (void *)(gm_skl3b78lo1gln));
            GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(4));
            gm_b0_1 t252 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t249, t250, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t253 = gm_l0;
            uint64_t t254 = gm_l1;
            uint64_t r255; bool o256 = __builtin_add_overflow(t254, ((uint64_t)1ULL), &r255);
            if (o256) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 496, 22);
            uint64_t t257 = r255;
            void * t258 = gm_l27;
            gm_b0_1 t259 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t253, t257, t258);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix244;
ie244:;
ix244:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_conds_k9e57un(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_write_conds_k9e57un F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l2; /* cond */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* x */
    void * gm_l7; /* p */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 387u);
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
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                        gm_l6 = t20;
                        {
                            void * t22 = gm_l0;
                            uint64_t t23 = gm_l1;
                            gm_b16_8 t24;
                            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sobcm12sogdng));
                            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(4));
                            gm_b0_1 t25 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t22, t23, t24);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t26 = gm_l0;
                            uint64_t t27 = gm_l1;
                            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 505, 30);
                            uint64_t t30 = r28;
                            void * t31 = gm_l6;
                            gm_b0_1 t32 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t26, t30, t31);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn18:;
                        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t34 != ((uint8_t)1ULL)) goto mn33;
                        void * t35 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t35;
                        void * t36 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l8 = t36;
                        {
                            void * t38 = gm_l0;
                            uint64_t t39 = gm_l1;
                            gm_b16_8 t40;
                            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sfid9en449upv));
                            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(10));
                            gm_b0_1 t41 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t38, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t42 = gm_l0;
                            uint64_t t43 = gm_l1;
                            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                            if (o45) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 509, 29);
                            uint64_t t46 = r44;
                            void * t47 = gm_l7;
                            gm_b0_1 t48 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t42, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t49 = gm_l0;
                            uint64_t t50 = gm_l1;
                            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                            if (o52) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 510, 30);
                            uint64_t t53 = r51;
                            void * t54 = gm_l8;
                            gm_b0_1 t55 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t49, t53, t54);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn33:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 502, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 501, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t57 = gm_l4;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l4 = t60;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_expr_cftucor(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_expr_cftucor F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* e */
    void * gm_l3; /* s */
    gm_b16_8 gm_l4; /* ty */
    double gm_l7; /* x */
    void * gm_l8; /* r */
    gm_tandvbt63uk7n gm_l9; /* x */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l13; /* c */
    uint8_t gm_l14; /* b */
    gm_b16_8 gm_l15; /* b */
    gm_b16_8 gm_l16; /* b */
    uint32_t gm_l17; /* p */
    gm_t88rcucg1b7uk gm_l18; /* of */
    uint8_t gm_l19; /* align */
    gm_t88rcucg1b7uk gm_l20; /* of */
    uint8_t gm_l21; /* max */
    gm_t88rcucg1b7uk gm_l22; /* of */
    gm_t88rcucg1b7uk gm_l23; /* a */
    gm_t88rcucg1b7uk gm_l24; /* b */
    uint8_t gm_l25; /* negated */
    gm_b16_8 gm_l26; /* name */
    gm_tl874t2atnlh3 gm_l27; /* span */
    uint32_t gm_l28; /* l */
    gm_b16_8 gm_l29; /* name */
    uint32_t gm_l30; /* levels */
    uint32_t gm_l31; /* l */
    void * gm_l32; /* x */
    void * gm_l33; /* base */
    uint32_t gm_l34; /* index */
    void * gm_l35; /* base */
    void * gm_l36; /* index */
    uint8_t gm_l37; /* checked */
    gm_tao63pk3r18v5 gm_l38; /* sl */
    void * gm_l39; /* x */
    void * gm_l40; /* x */
    void * gm_l41; /* x */
    void * gm_l42; /* x */
    void * gm_l43; /* x */
    void * gm_l44; /* x */
    uint32_t gm_l45; /* f */
    uint32_t gm_l46; /* f */
    uint32_t gm_l47; /* h */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint32_t gm_l52; /* c */
    gm_t9qkh0rsooc8d gm_l54; /* op */
    void * gm_l55; /* x */
    gm_ts6oqppjh0ba3 gm_l56; /* op */
    void * gm_l57; /* a */
    void * gm_l58; /* b */
    uint8_t gm_l59; /* is_and */
    void * gm_l60; /* a */
    void * gm_l61; /* b */
    gm_tmn7n0n9raou5 gm_l62; /* kind */
    void * gm_l63; /* x */
    gm_tvdg94bk7aa22 gm_l64; /* op */
    gm_b16_8 gm_l65; /* args */
    uint64_t gm_l66; /* <tmp> */
    uint64_t gm_l67; /* <tmp> */
    uint64_t gm_l68; /* i */
    gm_b16_8 gm_l70; /* args */
    uint32_t gm_l71; /* f */
    gm_tiilmfd16v3fc gm_l72; /* eff */
    uint32_t gm_l73; /* op */
    void * gm_l74; /* c */
    gm_b16_8 gm_l75; /* h */
    uint64_t gm_l76; /* <tmp> */
    uint64_t gm_l77; /* <tmp> */
    uint64_t gm_l78; /* i */
    gm_b16_8 gm_l79; /* xs */
    uint64_t gm_l80; /* <tmp> */
    uint64_t gm_l81; /* <tmp> */
    uint64_t gm_l82; /* i */
    gm_b16_8 gm_l83; /* xs */
    uint64_t gm_l84; /* <tmp> */
    uint64_t gm_l85; /* <tmp> */
    uint64_t gm_l86; /* i */
    void * gm_l87; /* x */
    gm_ttggq7ng4kp3p gm_l88; /* v */
    void * gm_l89; /* p */
    void * gm_l90; /* x */
    gm_t3dcoc7077t1q gm_l91; /* x */
    uint32_t gm_l92; /* c */
    gm_t083kd7b1kkag gm_l93; /* x */
    uint32_t gm_l94; /* v */
    void * gm_l95; /* x */
    uint8_t gm_l96; /* is_result */
    void * gm_l97; /* a */
    void * gm_l98; /* b */
    uint8_t gm_l99; /* negated */
    void * gm_l100; /* a */
    void * gm_l101; /* b */
    uint8_t gm_l102; /* negated */
    void * gm_l103; /* scrutinee */
    gm_b16_8 gm_l104; /* arms */
    gm_b16_8 gm_l105; /* <tmp> */
    uint64_t gm_l106; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l107; /* a */
    void * gm_l108; /* b */
    gm_trl2khmuphncd gm_l109; /* x */
    gm_teo9d4mncjfdu gm_l111; /* x */
    void * gm_l112; /* v */
    gm_te9hrk0u35rev gm_l113; /* t */
    uint32_t gm_l114; /* levels */
    void * gm_l115; /* v */
    void * gm_l116; /* x */
    void * gm_l117; /* k */
    void * gm_l118; /* v */
    void * gm_l119; /* k */
    void * gm_l120; /* w */
    void * gm_l121; /* x */
    uint64_t gm_l122; /* <tmp> */
    uint64_t gm_l123; /* <tmp> */
    uint64_t gm_l124; /* i */
    void * gm_l125; /* w */
    void * gm_l126; /* x */
    void * gm_l127; /* a */
    gm_b16_8 gm_l128; /* head */
    gm_b16_8 gm_l129; /* <tmp> */
    uint64_t gm_l130; /* <tmp> */
    void * gm_l131; /* k */
    GM_STACK_CHECK(&F, 10055u);
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
        void * t4 = gm_l3;
        void * t5 = gm_l2;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        gm_b16_8 t7 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t7;
    }
    {
        gm_f_list_s1kpkpr((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = gm_l2;
        gm_b16_8 t9;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        switch (t71) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma23; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma25; case ((uint8_t)14ULL): goto ma26; case ((uint8_t)15ULL): goto ma27; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma28; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma31; case ((uint8_t)20ULL): goto ma32; case ((uint8_t)21ULL): goto ma33; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma35; case ((uint8_t)24ULL): goto ma36; case ((uint8_t)25ULL): goto ma37; case ((uint8_t)26ULL): goto ma38; case ((uint8_t)27ULL): goto ma39; case ((uint8_t)28ULL): goto ma40; case ((uint8_t)29ULL): goto ma42; case ((uint8_t)30ULL): goto ma43; case ((uint8_t)31ULL): goto ma44; case ((uint8_t)32ULL): goto ma45; case ((uint8_t)33ULL): goto ma46; case ((uint8_t)34ULL): goto ma47; case ((uint8_t)35ULL): goto ma48; case ((uint8_t)36ULL): goto ma49; case ((uint8_t)37ULL): goto ma50; case ((uint8_t)38ULL): goto ma51; case ((uint8_t)39ULL): goto ma52; case ((uint8_t)40ULL): goto ma55; case ((uint8_t)41ULL): goto ma53; case ((uint8_t)42ULL): goto ma54; case ((uint8_t)43ULL): goto ma56; case ((uint8_t)44ULL): goto ma58; case ((uint8_t)45ULL): goto ma57; case ((uint8_t)46ULL): goto ma59; case ((uint8_t)47ULL): goto ma60; case ((uint8_t)48ULL): goto ma61; case ((uint8_t)49ULL): goto ma62; case ((uint8_t)50ULL): goto ma63; case ((uint8_t)51ULL): goto ma64; case ((uint8_t)52ULL): goto ma65; case ((uint8_t)53ULL): goto ma66; case ((uint8_t)54ULL): goto ma67; case ((uint8_t)55ULL): goto ma68; case ((uint8_t)56ULL): goto ma69; case ((uint8_t)57ULL): goto ma14; case ((uint8_t)58ULL): goto ma41; default: goto ma70; }
ma11:;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t73 != ((uint8_t)0ULL)) goto mn72;
        gm_tbci5ospk5nnn t74 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t8) + 8) + 8));
        F.l6 = t74;
        gm_b16_8 t75 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t76;
        GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
        gm_tabb710d9mq2e t77;
        gm_f_format_ejli3qf((void *)&t77, (void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srco4ljquuk5d87, 126, 20);
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
        t9 = t79;
        goto me10;
mn72:;
ma12:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t81 != ((uint8_t)1ULL)) goto mn80;
        double t82 = GM_LD(double, ((((uint8_t *)t8) + 8) + 8));
        gm_l7 = t82;
        double t83 = gm_l7;
        uint64_t t84;
        memset(&t84, 0, sizeof t84); memcpy(&t84, ((uint8_t *)&t83), sizeof t84 < 8 ? sizeof t84 : 8);
        gm_t4ce0jd3tdro4 t85;
        GM_ST(uint64_t, ((uint8_t *)&t85 + 0), t84);
        gm_tabb710d9mq2e t86;
        gm_f_format_booci4l((void *)&t86, (void *)NULL, cx, NULL, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_srco4ljquuk5d87, 127, 22);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        t9 = t88;
        goto me10;
mn80:;
ma13:;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t90 != ((uint8_t)2ULL)) goto mn89;
        void * t91 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l8 = t91;
        void * t92 = gm_l8;
        gm_b16_8 t93 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t92)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t94;
        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t93);
        gm_tabb710d9mq2e t95;
        gm_f_format_nva8q4r((void *)&t95, (void *)NULL, cx, NULL, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_srco4ljquuk5d87, 128, 29);
        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
        t9 = t97;
        goto me10;
mn89:;
ma14:;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t99 != ((uint8_t)57ULL)) goto mn98;
        gm_tandvbt63uk7n t100 = GM_LD(gm_tandvbt63uk7n, ((((uint8_t *)t8) + 8) + 8));
        gm_l9 = t100;
        {
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                uint64_t t103 = GM_LD(uintptr_t, (((uint8_t *)&gm_l9) + 8) + 8);
                gm_l10 = t103;
            }
            {
                for (;;) {
                    uint64_t t105 = gm_l11;
                    uint64_t t106 = gm_l10;
                    uint8_t t107 = (t105 < t106);
                    if (!t107) goto lb104;
                    {
                        uint64_t t110 = gm_l11;
                        uint64_t t113 = GM_LD(uint64_t, ((uint8_t *)&t110));
                        gm_l12 = t113;
                        {
                            void * t115 = (void *)((uint8_t *)&F.l5);
                            uint64_t t116 = gm_l12;
                            uint64_t t117 = (uintptr_t)t116;
                            uint64_t t118 = GM_LD(uintptr_t, (((uint8_t *)&gm_l9) + 8) + 8);
                            if (t117 >= (uintptr_t)(t118)) gm_trap_index(t117, t118, gm_srco4ljquuk5d87, 130, 49);
                            void * t119 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l9) + 8)) + t117 * 88);
                            gm_tftb1odc739t5 t120 = gm_f_push_pf02nv6(t115, t119);
                            if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_srco4ljquuk5d87, 130, 38);
                            gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
                        }
                        goto me111;
mn112:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 130, 13);
me111:;
                    }
lc104:;
                    {
                        uint64_t t123 = gm_l11;
                        uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                        uint64_t t126 = r124;
                        gm_l11 = t126;
                    }
                }
lb104:;
            }
        }
        gm_tesaltf0mhcki t127 = GM_LD(gm_tesaltf0mhcki, (((uint8_t *)&gm_l9) + 0));
        gm_b16_8 t128 = gm_f_soa_op_text_ud90n2l((void *)NULL, cx, NULL, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t129;
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
        gm_tabb710d9mq2e t130;
        gm_f_format_t0vntpb((void *)&t130, (void *)NULL, cx, NULL, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_srco4ljquuk5d87, 131, 13);
        gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
        gm_b16_8 t132 = t131;
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
        t9 = t133;
        goto me10;
mn98:;
ma15:;
        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t135 != ((uint8_t)3ULL)) goto mn134;
        void * t136 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l13 = t136;
        gm_t6j6smj7qoctu t137;
        void * t138 = gm_l13;
        gm_b16_8 t139 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t138) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t139);
        void * t140 = gm_l13;
        gm_b16_8 t141 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t140) + 96)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t141);
        gm_tabb710d9mq2e t142;
        gm_f_format_0969kd8((void *)&t142, (void *)NULL, cx, NULL, &t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 8, gm_srco4ljquuk5d87, 133, 31);
        gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
        t9 = t144;
        goto me10;
mn134:;
ma16:;
        uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t146 != ((uint8_t)4ULL)) goto mn145;
        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l14 = t147;
        uint8_t t148 = gm_l14;
        gm_t9qkh0rsooc8d t149;
        GM_ST(uint8_t, ((uint8_t *)&t149 + 0), t148);
        gm_tabb710d9mq2e t150;
        gm_f_format_as0b1t3((void *)&t150, (void *)NULL, cx, NULL, t149);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 8, gm_srco4ljquuk5d87, 134, 21);
        gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&t150) + 8));
        gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
        t9 = t152;
        goto me10;
mn145:;
ma17:;
        uint8_t t154 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t154 != ((uint8_t)5ULL)) goto mn153;
        gm_b16_8 t155 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l15 = t155;
        gm_b16_8 t156;
        GM_ST(void *, (uint8_t *)&t156, (void *)(gm_shdnhcpvptc1q));
        GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(4));
        gm_b16_8 t157 = gm_l15;
        gm_b16_8 t158 = gm_f_escaped_k5fm4jl((void *)NULL, cx, NULL, t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t9 = t158;
        goto me10;
mn153:;
ma18:;
        uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t160 != ((uint8_t)6ULL)) goto mn159;
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l16 = t161;
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sdak257j1qgl8));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(5));
        gm_b16_8 t163 = gm_l16;
        gm_b16_8 t164 = gm_f_escaped_k5fm4jl((void *)NULL, cx, NULL, t162, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t9 = t164;
        goto me10;
mn159:;
ma19:;
        uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t166 != ((uint8_t)7ULL)) goto mn165;
        gm_b16_8 t167;
        GM_ST(void *, (uint8_t *)&t167, (void *)(gm_squedv51820an));
        GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(4));
        t9 = t167;
        goto me10;
mn165:;
ma20:;
        uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t169 != ((uint8_t)8ULL)) goto mn168;
        gm_b16_8 t170;
        GM_ST(void *, (uint8_t *)&t170, (void *)(gm_sr899mamktpvo));
        GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(6));
        t9 = t170;
        goto me10;
mn168:;
ma21:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t172 != ((uint8_t)9ULL)) goto mn171;
        uint32_t t173 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l17 = t173;
        void * t174 = gm_l0;
        void * t175 = GM_LD(void *, (((uint8_t *)t174) + 0));
        void * t176 = (void *)(((uint8_t *)t175) + 8);
        uint32_t t177 = gm_l17;
        gm_b16_8 t178 = gm_f_param_name_rbnop5l(t176, t177);
        gm_tnpifv20irnph t179;
        GM_ST(gm_b16_8, ((uint8_t *)&t179 + 0), t178);
        gm_tabb710d9mq2e t180;
        gm_f_format_r9ba7g9((void *)&t180, (void *)NULL, cx, NULL, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t180))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t180) + 8, gm_srco4ljquuk5d87, 139, 27);
        gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&t180) + 8));
        gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
        t9 = t182;
        goto me10;
mn171:;
ma22:;
        uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t184 != ((uint8_t)10ULL)) goto mn183;
        gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l18 = t185;
        uint8_t t186 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l19 = t186;
        gm_t6j6smj7qoctu t187;
        gm_b16_8 t188;
        uint8_t t190 = gm_l19;
        if (!t190) goto ie189;
        gm_b16_8 t192;
        GM_ST(void *, (uint8_t *)&t192, (void *)(gm_s8gtcifve79ai));
        GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(8));
        gm_b16_8 t193 = t192;
        t188 = t193;
        goto ix189;
ie189:;
        gm_b16_8 t195;
        GM_ST(void *, (uint8_t *)&t195, (void *)(gm_sjegrp8u9r3n3));
        GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(7));
        gm_b16_8 t196 = t195;
        t188 = t196;
ix189:;
        GM_ST(gm_b16_8, (uint8_t *)&t187 + 0, t188);
        void * t197 = gm_l3;
        gm_t88rcucg1b7uk t198 = gm_l18;
        gm_b16_8 t199 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t197, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t187 + 16, t199);
        gm_tabb710d9mq2e t200;
        gm_f_format_2lgoflc((void *)&t200, (void *)NULL, cx, NULL, &t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t200) + 8, gm_srco4ljquuk5d87, 140, 31);
        gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&t200) + 8));
        gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
        t9 = t202;
        goto me10;
mn183:;
ma23:;
        uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t204 != ((uint8_t)11ULL)) goto mn203;
        gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l20 = t205;
        uint8_t t206 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l21 = t206;
        gm_t6j6smj7qoctu t207;
        gm_b16_8 t208;
        uint8_t t210 = gm_l21;
        if (!t210) goto ie209;
        gm_b16_8 t212;
        GM_ST(void *, (uint8_t *)&t212, (void *)(gm_s0lh54534p3vd));
        GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(9));
        gm_b16_8 t213 = t212;
        t208 = t213;
        goto ix209;
ie209:;
        gm_b16_8 t215;
        GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sp2smaof0i4j2));
        GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(9));
        gm_b16_8 t216 = t215;
        t208 = t216;
ix209:;
        GM_ST(gm_b16_8, (uint8_t *)&t207 + 0, t208);
        void * t217 = gm_l3;
        gm_t88rcucg1b7uk t218 = gm_l20;
        gm_b16_8 t219 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t217, t218);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t207 + 16, t219);
        gm_tabb710d9mq2e t220;
        gm_f_format_2lgoflc((void *)&t220, (void *)NULL, cx, NULL, &t207);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 8, gm_srco4ljquuk5d87, 141, 31);
        gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&t220) + 8));
        gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
        t9 = t222;
        goto me10;
mn203:;
ma24:;
        uint8_t t224 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t224 != ((uint8_t)12ULL)) goto mn223;
        gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t8) + 8) + 8));
        gm_l22 = t225;
        void * t226 = gm_l3;
        gm_t88rcucg1b7uk t227 = gm_l22;
        gm_b16_8 t228 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t226, t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t229;
        GM_ST(gm_b16_8, ((uint8_t *)&t229 + 0), t228);
        gm_tabb710d9mq2e t230;
        gm_f_format_t725p24((void *)&t230, (void *)NULL, cx, NULL, t229);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 8, gm_srco4ljquuk5d87, 142, 26);
        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&t230) + 8));
        gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
        t9 = t232;
        goto me10;
mn223:;
ma25:;
        uint8_t t234 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t234 != ((uint8_t)13ULL)) goto mn233;
        gm_t88rcucg1b7uk t235 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l23 = t235;
        gm_t88rcucg1b7uk t236 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l24 = t236;
        uint8_t t237 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l25 = t237;
        gm_tfeab279c16rr t238;
        gm_b16_8 t239;
        uint8_t t241 = gm_l25;
        if (!t241) goto ie240;
        gm_b16_8 t243;
        GM_ST(void *, (uint8_t *)&t243, (void *)(gm_sngdu4l8vh3l8));
        GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(4));
        gm_b16_8 t244 = t243;
        t239 = t244;
        goto ix240;
ie240:;
        gm_b16_8 t246;
        GM_ST(void *, (uint8_t *)&t246, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(0));
        gm_b16_8 t247 = t246;
        t239 = t247;
ix240:;
        GM_ST(gm_b16_8, (uint8_t *)&t238 + 0, t239);
        void * t248 = gm_l3;
        gm_t88rcucg1b7uk t249 = gm_l23;
        gm_b16_8 t250 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t248, t249);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t238 + 16, t250);
        void * t251 = gm_l3;
        gm_t88rcucg1b7uk t252 = gm_l24;
        gm_b16_8 t253 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t251, t252);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t238 + 32, t253);
        gm_tabb710d9mq2e t254;
        gm_f_format_arnesr1((void *)&t254, (void *)NULL, cx, NULL, &t238);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 8, gm_srco4ljquuk5d87, 143, 35);
        gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&t254) + 8));
        gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
        t9 = t256;
        goto me10;
mn233:;
ma26:;
        uint8_t t258 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t258 != ((uint8_t)14ULL)) goto mn257;
        gm_b16_8 t259 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l26 = t259;
        gm_tl874t2atnlh3 t260 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l27 = t260;
        gm_t6j6smj7qoctu t261;
        gm_b16_8 t262 = gm_l26;
        GM_ST(gm_b16_8, (uint8_t *)&t261 + 0, t262);
        void * t263 = gm_l3;
        gm_tl874t2atnlh3 t264 = gm_l27;
        gm_b16_8 t265 = gm_f_place_lj97s4p((void *)NULL, cx, NULL, t263, t264);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t261 + 16, t265);
        gm_tabb710d9mq2e t266;
        gm_f_format_5up8eqa((void *)&t266, (void *)NULL, cx, NULL, &t261);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t266))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t266) + 8, gm_srco4ljquuk5d87, 144, 31);
        gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&t266) + 8));
        gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
        t9 = t268;
        goto me10;
mn257:;
ma27:;
        uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t270 != ((uint8_t)15ULL)) goto mn269;
        uint32_t t271 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l28 = t271;
        void * t272 = gm_l0;
        uint32_t t273 = gm_l28;
        gm_b16_8 t274 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t272, t273);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t275;
        GM_ST(gm_b16_8, ((uint8_t *)&t275 + 0), t274);
        gm_tabb710d9mq2e t276;
        gm_f_format_32bp3b4((void *)&t276, (void *)NULL, cx, NULL, t275);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t276))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t276) + 8, gm_srco4ljquuk5d87, 145, 22);
        gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&t276) + 8));
        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
        t9 = t278;
        goto me10;
mn269:;
ma28:;
        uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t280 != ((uint8_t)17ULL)) goto mn279;
        gm_b16_8 t281 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l29 = t281;
        gm_b16_8 t282 = gm_l29;
        gm_tnpifv20irnph t283;
        GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t282);
        gm_tabb710d9mq2e t284;
        gm_f_format_fi775k0((void *)&t284, (void *)NULL, cx, NULL, t283);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 8, gm_srco4ljquuk5d87, 146, 29);
        gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&t284) + 8));
        gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
        t9 = t286;
        goto me10;
mn279:;
ma29:;
        uint8_t t288 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t288 != ((uint8_t)16ULL)) goto mn287;
        uint32_t t289 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l30 = t289;
        uint32_t t290 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 4));
        gm_l31 = t290;
        uint32_t t291 = gm_l30;
        uint32_t t292 = gm_l31;
        gm_tcc1rq4n1avb2 t293;
        GM_ST(uint32_t, ((uint8_t *)&t293 + 0), t291);
        GM_ST(uint32_t, ((uint8_t *)&t293 + 4), t292);
        gm_tabb710d9mq2e t294;
        gm_f_format_16dq22e((void *)&t294, (void *)NULL, cx, NULL, t293);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t294))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t294) + 8, gm_srco4ljquuk5d87, 147, 30);
        gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&t294) + 8));
        gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
        t9 = t296;
        goto me10;
mn287:;
ma30:;
        uint8_t t298 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t298 != ((uint8_t)18ULL)) goto mn297;
        void * t299 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l32 = t299;
        {
            void * t301 = (void *)((uint8_t *)&F.l5);
            void * t302 = gm_l32;
            gm_tftb1odc739t5 t303 = gm_f_push_pf02nv6(t301, t302);
            if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 1, gm_srco4ljquuk5d87, 149, 13);
            gm_b0_1 t304 = GM_LD(gm_b0_1, (((uint8_t *)&t303) + 1));
        }
        gm_b16_8 t305;
        GM_ST(void *, (uint8_t *)&t305, (void *)(gm_sh4niqnmh73co));
        GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(5));
        gm_b16_8 t306 = t305;
        t9 = t306;
        goto me10;
mn297:;
ma31:;
        uint8_t t308 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t308 != ((uint8_t)19ULL)) goto mn307;
        void * t309 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l33 = t309;
        uint32_t t310 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l34 = t310;
        {
            void * t312 = (void *)((uint8_t *)&F.l5);
            void * t313 = gm_l33;
            gm_tftb1odc739t5 t314 = gm_f_push_pf02nv6(t312, t313);
            if (((((uint8_t *)&t314))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t314) + 1, gm_srco4ljquuk5d87, 153, 13);
            gm_b0_1 t315 = GM_LD(gm_b0_1, (((uint8_t *)&t314) + 1));
        }
        uint32_t t316 = gm_l34;
        gm_t88rcucg1b7uk t317;
        GM_ST(uint32_t, ((uint8_t *)&t317 + 0), t316);
        gm_tabb710d9mq2e t318;
        gm_f_format_208te7b((void *)&t318, (void *)NULL, cx, NULL, t317);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 8, gm_srco4ljquuk5d87, 154, 13);
        gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&t318) + 8));
        gm_b16_8 t320 = t319;
        gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&t320));
        t9 = t321;
        goto me10;
mn307:;
ma32:;
        uint8_t t323 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t323 != ((uint8_t)20ULL)) goto mn322;
        void * t324 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l35 = t324;
        void * t325 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l36 = t325;
        uint8_t t326 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l37 = t326;
        {
            void * t328 = (void *)((uint8_t *)&F.l5);
            void * t329 = gm_l35;
            gm_tftb1odc739t5 t330 = gm_f_push_pf02nv6(t328, t329);
            if (((((uint8_t *)&t330))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t330) + 1, gm_srco4ljquuk5d87, 157, 13);
            gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t330) + 1));
        }
        {
            void * t332 = (void *)((uint8_t *)&F.l5);
            void * t333 = gm_l36;
            gm_tftb1odc739t5 t334 = gm_f_push_pf02nv6(t332, t333);
            if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 1, gm_srco4ljquuk5d87, 158, 13);
            gm_b0_1 t335 = GM_LD(gm_b0_1, (((uint8_t *)&t334) + 1));
        }
        gm_b16_8 t336;
        uint8_t t338 = gm_l37;
        if (!t338) goto ie337;
        gm_b16_8 t340;
        GM_ST(void *, (uint8_t *)&t340, (void *)(gm_s5dbqnns2epkg));
        GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(5));
        gm_b16_8 t341 = t340;
        t336 = t341;
        goto ix337;
ie337:;
        gm_b16_8 t343;
        GM_ST(void *, (uint8_t *)&t343, (void *)(gm_ssgn9mppmu3vl));
        GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(15));
        gm_b16_8 t344 = t343;
        t336 = t344;
ix337:;
        gm_b16_8 t345 = t336;
        t9 = t345;
        goto me10;
mn322:;
ma33:;
        uint8_t t347 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t347 != ((uint8_t)21ULL)) goto mn346;
        gm_tao63pk3r18v5 t348 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t8) + 8) + 8));
        gm_l38 = t348;
        {
            void * t350 = (void *)((uint8_t *)&F.l5);
            void * t351 = GM_LD(void *, (((uint8_t *)&gm_l38) + 0));
            gm_tftb1odc739t5 t352 = gm_f_push_pf02nv6(t350, t351);
            if (((((uint8_t *)&t352))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t352) + 1, gm_srco4ljquuk5d87, 162, 13);
            gm_b0_1 t353 = GM_LD(gm_b0_1, (((uint8_t *)&t352) + 1));
        }
        {
            void * t355 = GM_LD(void *, (((uint8_t *)&gm_l38) + 8));
            if (!(GM_LD(void *, ((uint8_t *)&t355)) != 0)) goto ie354;
            void * t356 = GM_LD(void *, ((uint8_t *)&t355));
            gm_l39 = t356;
            {
                void * t358 = (void *)((uint8_t *)&F.l5);
                void * t359 = gm_l39;
                gm_tftb1odc739t5 t360 = gm_f_push_pf02nv6(t358, t359);
                if (((((uint8_t *)&t360))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t360) + 1, gm_srco4ljquuk5d87, 163, 29);
                gm_b0_1 t361 = GM_LD(gm_b0_1, (((uint8_t *)&t360) + 1));
            }
            goto ix354;
ie354:;
ix354:;
        }
        {
            void * t363 = GM_LD(void *, (((uint8_t *)&gm_l38) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t363)) != 0)) goto ie362;
            void * t364 = GM_LD(void *, ((uint8_t *)&t363));
            gm_l40 = t364;
            {
                void * t366 = (void *)((uint8_t *)&F.l5);
                void * t367 = gm_l40;
                gm_tftb1odc739t5 t368 = gm_f_push_pf02nv6(t366, t367);
                if (((((uint8_t *)&t368))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t368) + 1, gm_srco4ljquuk5d87, 164, 29);
                gm_b0_1 t369 = GM_LD(gm_b0_1, (((uint8_t *)&t368) + 1));
            }
            goto ix362;
ie362:;
ix362:;
        }
        gm_tfeab279c16rr t370;
        gm_b16_8 t371;
        void * t373 = GM_LD(void *, (((uint8_t *)&gm_l38) + 8));
        uint8_t t374;
        if ((GM_LD(void *, ((uint8_t *)&t373)) != 0)) goto mn376;
        t374 = ((uint8_t)0);
        goto me375;
mn376:;
        t374 = ((uint8_t)1);
        goto me375;
mn377:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 165, 42);
me375:;
        if (!t374) goto ie372;
        gm_b16_8 t379;
        GM_ST(void *, (uint8_t *)&t379, (void *)(gm_sq0h7jjc6dtk5));
        GM_ST(uintptr_t, (uint8_t *)&t379 + 8, (uintptr_t)(3));
        gm_b16_8 t380 = t379;
        t371 = t380;
        goto ix372;
ie372:;
        gm_b16_8 t382;
        GM_ST(void *, (uint8_t *)&t382, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t382 + 8, (uintptr_t)(0));
        gm_b16_8 t383 = t382;
        t371 = t383;
ix372:;
        GM_ST(gm_b16_8, (uint8_t *)&t370 + 0, t371);
        gm_b16_8 t384;
        void * t386 = GM_LD(void *, (((uint8_t *)&gm_l38) + 16));
        uint8_t t387;
        if ((GM_LD(void *, ((uint8_t *)&t386)) != 0)) goto mn389;
        t387 = ((uint8_t)0);
        goto me388;
mn389:;
        t387 = ((uint8_t)1);
        goto me388;
mn390:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 165, 86);
me388:;
        if (!t387) goto ie385;
        gm_b16_8 t392;
        GM_ST(void *, (uint8_t *)&t392, (void *)(gm_s9v13eck7gubi));
        GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(3));
        gm_b16_8 t393 = t392;
        t384 = t393;
        goto ix385;
ie385:;
        gm_b16_8 t395;
        GM_ST(void *, (uint8_t *)&t395, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t395 + 8, (uintptr_t)(0));
        gm_b16_8 t396 = t395;
        t384 = t396;
ix385:;
        GM_ST(gm_b16_8, (uint8_t *)&t370 + 16, t384);
        gm_b16_8 t397;
        uint8_t t399 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 24));
        if (!t399) goto ie398;
        gm_b16_8 t401;
        GM_ST(void *, (uint8_t *)&t401, (void *)(gm_sksprenpp79ih));
        GM_ST(uintptr_t, (uint8_t *)&t401 + 8, (uintptr_t)(10));
        gm_b16_8 t402 = t401;
        t397 = t402;
        goto ix398;
ie398:;
        gm_b16_8 t404;
        GM_ST(void *, (uint8_t *)&t404, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t404 + 8, (uintptr_t)(0));
        gm_b16_8 t405 = t404;
        t397 = t405;
ix398:;
        GM_ST(gm_b16_8, (uint8_t *)&t370 + 32, t397);
        gm_tabb710d9mq2e t406;
        gm_f_format_oc8ipo6((void *)&t406, (void *)NULL, cx, NULL, &t370);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t406))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t406) + 8, gm_srco4ljquuk5d87, 165, 13);
        gm_b16_8 t407 = GM_LD(gm_b16_8, (((uint8_t *)&t406) + 8));
        gm_b16_8 t408 = t407;
        gm_b16_8 t409 = GM_LD(gm_b16_8, ((uint8_t *)&t408));
        t9 = t409;
        goto me10;
mn346:;
ma34:;
        uint8_t t411 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t411 != ((uint8_t)22ULL)) goto mn410;
        void * t412 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l41 = t412;
        {
            void * t414 = (void *)((uint8_t *)&F.l5);
            void * t415 = gm_l41;
            gm_tftb1odc739t5 t416 = gm_f_push_pf02nv6(t414, t415);
            if (((((uint8_t *)&t416))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t416) + 1, gm_srco4ljquuk5d87, 168, 13);
            gm_b0_1 t417 = GM_LD(gm_b0_1, (((uint8_t *)&t416) + 1));
        }
        gm_b16_8 t418;
        GM_ST(void *, (uint8_t *)&t418, (void *)(gm_sstrtuk2b4jdg));
        GM_ST(uintptr_t, (uint8_t *)&t418 + 8, (uintptr_t)(9));
        gm_b16_8 t419 = t418;
        t9 = t419;
        goto me10;
mn410:;
ma35:;
        uint8_t t421 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t421 != ((uint8_t)23ULL)) goto mn420;
        void * t422 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l42 = t422;
        {
            void * t424 = (void *)((uint8_t *)&F.l5);
            void * t425 = gm_l42;
            gm_tftb1odc739t5 t426 = gm_f_push_pf02nv6(t424, t425);
            if (((((uint8_t *)&t426))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t426) + 1, gm_srco4ljquuk5d87, 172, 13);
            gm_b0_1 t427 = GM_LD(gm_b0_1, (((uint8_t *)&t426) + 1));
        }
        gm_b16_8 t428;
        GM_ST(void *, (uint8_t *)&t428, (void *)(gm_sedrsudco1u16));
        GM_ST(uintptr_t, (uint8_t *)&t428 + 8, (uintptr_t)(9));
        gm_b16_8 t429 = t428;
        t9 = t429;
        goto me10;
mn420:;
ma36:;
        uint8_t t431 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t431 != ((uint8_t)24ULL)) goto mn430;
        void * t432 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l43 = t432;
        {
            void * t434 = (void *)((uint8_t *)&F.l5);
            void * t435 = gm_l43;
            gm_tftb1odc739t5 t436 = gm_f_push_pf02nv6(t434, t435);
            if (((((uint8_t *)&t436))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t436) + 1, gm_srco4ljquuk5d87, 176, 13);
            gm_b0_1 t437 = GM_LD(gm_b0_1, (((uint8_t *)&t436) + 1));
        }
        gm_b16_8 t438;
        GM_ST(void *, (uint8_t *)&t438, (void *)(gm_sukutl224cgkm));
        GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(7));
        gm_b16_8 t439 = t438;
        t9 = t439;
        goto me10;
mn430:;
ma37:;
        uint8_t t441 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t441 != ((uint8_t)25ULL)) goto mn440;
        void * t442 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l44 = t442;
        {
            void * t444 = (void *)((uint8_t *)&F.l5);
            void * t445 = gm_l44;
            gm_tftb1odc739t5 t446 = gm_f_push_pf02nv6(t444, t445);
            if (((((uint8_t *)&t446))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t446) + 1, gm_srco4ljquuk5d87, 180, 13);
            gm_b0_1 t447 = GM_LD(gm_b0_1, (((uint8_t *)&t446) + 1));
        }
        gm_b16_8 t448;
        GM_ST(void *, (uint8_t *)&t448, (void *)(gm_slgrfk23llffe));
        GM_ST(uintptr_t, (uint8_t *)&t448 + 8, (uintptr_t)(12));
        gm_b16_8 t449 = t448;
        t9 = t449;
        goto me10;
mn440:;
ma38:;
        uint8_t t451 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t451 != ((uint8_t)26ULL)) goto mn450;
        uint32_t t452 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l45 = t452;
        void * t453 = gm_l3;
        uint32_t t454 = gm_l45;
        gm_b16_8 t455 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t453, t454);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t456;
        GM_ST(gm_b16_8, ((uint8_t *)&t456 + 0), t455);
        gm_tabb710d9mq2e t457;
        gm_f_format_sugh2p3((void *)&t457, (void *)NULL, cx, NULL, t456);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t457))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t457) + 8, gm_srco4ljquuk5d87, 183, 22);
        gm_b16_8 t458 = GM_LD(gm_b16_8, (((uint8_t *)&t457) + 8));
        gm_b16_8 t459 = GM_LD(gm_b16_8, ((uint8_t *)&t458));
        t9 = t459;
        goto me10;
mn450:;
ma39:;
        uint8_t t461 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t461 != ((uint8_t)27ULL)) goto mn460;
        uint32_t t462 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l46 = t462;
        void * t463 = gm_l3;
        uint32_t t464 = gm_l46;
        gm_b16_8 t465 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t463, t464);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t466;
        GM_ST(gm_b16_8, ((uint8_t *)&t466 + 0), t465);
        gm_tabb710d9mq2e t467;
        gm_f_format_ka8hqpj((void *)&t467, (void *)NULL, cx, NULL, t466);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t467))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t467) + 8, gm_srco4ljquuk5d87, 184, 24);
        gm_b16_8 t468 = GM_LD(gm_b16_8, (((uint8_t *)&t467) + 8));
        gm_b16_8 t469 = GM_LD(gm_b16_8, ((uint8_t *)&t468));
        t9 = t469;
        goto me10;
mn460:;
ma40:;
        uint8_t t471 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t471 != ((uint8_t)28ULL)) goto mn470;
        uint32_t t472 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l47 = t472;
        {
            uint32_t t474 = gm_l47;
            uint64_t t475 = (uint64_t)t474;
            uint64_t t476 = (uintptr_t)t475;
            void * t477 = gm_l3;
            uint64_t t478 = GM_LD(uintptr_t, ((((uint8_t *)t477) + 504) + 0) + 8);
            if (t476 >= (uintptr_t)(t478)) gm_trap_index(t476, t478, gm_srco4ljquuk5d87, 186, 20);
            gm_t1cs2fjnrqap1 t479 = GM_LD(gm_t1cs2fjnrqap1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t477) + 504) + 0)) + t476 * 40));
            F.l48 = t479;
        }
        {
            gm_f_list_epvducg((void *)&F.l49, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t481 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 24));
                gm_l50 = t481;
            }
            {
                gm_l51 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t483 = gm_l51;
                    uint64_t t484 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                    uint8_t t485 = (t483 < t484);
                    if (!t485) goto lb482;
                    {
                        uint64_t t488 = gm_l51;
                        uint64_t t489 = (uintptr_t)t488;
                        uint64_t t490 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                        uint32_t t491 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t489 * 4));
                        uint32_t t494 = GM_LD(uint32_t, ((uint8_t *)&t491));
                        gm_l52 = t494;
                        {
                            void * t496 = (void *)((uint8_t *)&F.l49);
                            void * t497 = gm_l3;
                            uint32_t t498 = gm_l52;
                            gm_b16_8 t499 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t497, t498);
                            if (gm_unw.kind) {
                                goto gm_ll486;
                            }
                            gm_tftb1odc739t5 t500 = gm_f_push_3ef746b(t496, t499);
                            if (((((uint8_t *)&t500))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t500) + 1, gm_srco4ljquuk5d87, 188, 36);
                            gm_b0_1 t501 = GM_LD(gm_b0_1, (((uint8_t *)&t500) + 1));
                        }
                        goto me492;
mn493:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 188, 13);
me492:;
                    }
lc482:;
                    {
                        uint64_t t503 = gm_l51;
                        uint64_t r504; bool o505 = __builtin_add_overflow(t503, ((uint64_t)1ULL), &r504);
                        uint64_t t506 = r504;
                        gm_l51 = t506;
                    }
                }
gm_ll486:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb482; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc482; }
                }
                goto gm_lad;
lb482:;
            }
        }
        gm_t6j6smj7qoctu t507;
        void * t508 = gm_l3;
        void * t509 = (void *)(((uint8_t *)t508) + 8);
        void * t510 = (void *)(((uint8_t *)&F.l48) + 0);
        gm_b16_8 t511 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t509, t510);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t507 + 0, t511);
        gm_b16_8 t512 = GM_LD(gm_b16_8, (((uint8_t *)&F.l49) + 0));
        gm_b16_8 t513 = GM_LD(gm_b16_8, ((uint8_t *)&t512));
        gm_b16_8 t514;
        GM_ST(void *, (uint8_t *)&t514, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t514 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t515;
        gm_f_join_sfknljb((void *)&t515, (void *)NULL, cx, NULL, t513, t514);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t515))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t515) + 8, gm_srco4ljquuk5d87, 189, 77);
        gm_b16_8 t516 = GM_LD(gm_b16_8, (((uint8_t *)&t515) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t507 + 16, t516);
        gm_tabb710d9mq2e t517;
        gm_f_format_cuuc21t((void *)&t517, (void *)NULL, cx, NULL, &t507);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t517))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t517) + 8, gm_srco4ljquuk5d87, 189, 13);
        gm_b16_8 t518 = GM_LD(gm_b16_8, (((uint8_t *)&t517) + 8));
        gm_b16_8 t519 = t518;
        gm_b16_8 t520 = GM_LD(gm_b16_8, ((uint8_t *)&t519));
        t9 = t520;
        goto me10;
mn470:;
ma41:;
        uint8_t t522 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t522 != ((uint8_t)58ULL)) goto mn521;
        gm_t710lsrq1m8gk t523 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t8) + 8) + 8));
        F.l53 = t523;
        {
            void * t525 = gm_l0;
            uint64_t t526 = gm_l1;
            gm_b16_8 t527 = gm_l4;
            gm_b0_1 t528 = gm_f_write_with_j37nil7((void *)NULL, cx, NULL, t525, t526, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l53)), t527);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t529;
        uint8_t t530 = 0;
        F.ret = t529;
        goto gm_fret;
        goto me10;
mn521:;
ma42:;
        uint8_t t532 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t532 != ((uint8_t)29ULL)) goto mn531;
        gm_t9qkh0rsooc8d t533 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l54 = t533;
        void * t534 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l55 = t534;
        {
            void * t536 = (void *)((uint8_t *)&F.l5);
            void * t537 = gm_l55;
            gm_tftb1odc739t5 t538 = gm_f_push_pf02nv6(t536, t537);
            if (((((uint8_t *)&t538))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t538) + 1, gm_srco4ljquuk5d87, 196, 13);
            gm_b0_1 t539 = GM_LD(gm_b0_1, (((uint8_t *)&t538) + 1));
        }
        gm_b16_8 t540;
        uint8_t t546 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        switch (t546) { case ((uint8_t)0ULL): goto ma542; case ((uint8_t)1ULL): goto ma543; case ((uint8_t)2ULL): goto ma544; default: goto ma545; }
ma542:;
        uint8_t t548 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        if (t548 != ((uint8_t)0ULL)) goto mn547;
        gm_b16_8 t549;
        GM_ST(void *, (uint8_t *)&t549, (void *)(gm_se19nf1421e37));
        GM_ST(uintptr_t, (uint8_t *)&t549 + 8, (uintptr_t)(3));
        t540 = t549;
        goto me541;
mn547:;
ma543:;
        uint8_t t551 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        if (t551 != ((uint8_t)1ULL)) goto mn550;
        gm_b16_8 t552;
        GM_ST(void *, (uint8_t *)&t552, (void *)(gm_shohh3180gu26));
        GM_ST(uintptr_t, (uint8_t *)&t552 + 8, (uintptr_t)(6));
        t540 = t552;
        goto me541;
mn550:;
ma544:;
        uint8_t t554 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
        if (t554 != ((uint8_t)2ULL)) goto mn553;
        gm_b16_8 t555;
        GM_ST(void *, (uint8_t *)&t555, (void *)(gm_soe4gvhg9ei97));
        GM_ST(uintptr_t, (uint8_t *)&t555 + 8, (uintptr_t)(3));
        t540 = t555;
        goto me541;
mn553:;
ma545:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 197, 36);
me541:;
        gm_tnpifv20irnph t556;
        GM_ST(gm_b16_8, ((uint8_t *)&t556 + 0), t540);
        gm_tabb710d9mq2e t557;
        gm_f_format_r8pfaos((void *)&t557, (void *)NULL, cx, NULL, t556);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t557))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t557) + 8, gm_srco4ljquuk5d87, 197, 13);
        gm_b16_8 t558 = GM_LD(gm_b16_8, (((uint8_t *)&t557) + 8));
        gm_b16_8 t559 = t558;
        gm_b16_8 t560 = GM_LD(gm_b16_8, ((uint8_t *)&t559));
        t9 = t560;
        goto me10;
mn531:;
ma43:;
        uint8_t t562 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t562 != ((uint8_t)30ULL)) goto mn561;
        gm_ts6oqppjh0ba3 t563 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l56 = t563;
        void * t564 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l57 = t564;
        void * t565 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l58 = t565;
        {
            void * t567 = (void *)((uint8_t *)&F.l5);
            void * t568 = gm_l57;
            gm_tftb1odc739t5 t569 = gm_f_push_pf02nv6(t567, t568);
            if (((((uint8_t *)&t569))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t569) + 1, gm_srco4ljquuk5d87, 204, 13);
            gm_b0_1 t570 = GM_LD(gm_b0_1, (((uint8_t *)&t569) + 1));
        }
        {
            void * t571 = (void *)((uint8_t *)&F.l5);
            void * t572 = gm_l58;
            gm_tftb1odc739t5 t573 = gm_f_push_pf02nv6(t571, t572);
            if (((((uint8_t *)&t573))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t573) + 1, gm_srco4ljquuk5d87, 205, 13);
            gm_b0_1 t574 = GM_LD(gm_b0_1, (((uint8_t *)&t573) + 1));
        }
        gm_ts6oqppjh0ba3 t575 = gm_l56;
        gm_b16_8 t576 = gm_f_binop_text_t4cv0b4((void *)NULL, cx, NULL, t575);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t577;
        GM_ST(gm_b16_8, ((uint8_t *)&t577 + 0), t576);
        gm_tabb710d9mq2e t578;
        gm_f_format_85biid9((void *)&t578, (void *)NULL, cx, NULL, t577);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t578))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t578) + 8, gm_srco4ljquuk5d87, 206, 13);
        gm_b16_8 t579 = GM_LD(gm_b16_8, (((uint8_t *)&t578) + 8));
        gm_b16_8 t580 = t579;
        gm_b16_8 t581 = GM_LD(gm_b16_8, ((uint8_t *)&t580));
        t9 = t581;
        goto me10;
mn561:;
ma44:;
        uint8_t t583 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t583 != ((uint8_t)31ULL)) goto mn582;
        uint8_t t584 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l59 = t584;
        void * t585 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l60 = t585;
        void * t586 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l61 = t586;
        {
            void * t588 = (void *)((uint8_t *)&F.l5);
            void * t589 = gm_l60;
            gm_tftb1odc739t5 t590 = gm_f_push_pf02nv6(t588, t589);
            if (((((uint8_t *)&t590))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t590) + 1, gm_srco4ljquuk5d87, 209, 13);
            gm_b0_1 t591 = GM_LD(gm_b0_1, (((uint8_t *)&t590) + 1));
        }
        {
            void * t592 = (void *)((uint8_t *)&F.l5);
            void * t593 = gm_l61;
            gm_tftb1odc739t5 t594 = gm_f_push_pf02nv6(t592, t593);
            if (((((uint8_t *)&t594))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t594) + 1, gm_srco4ljquuk5d87, 210, 13);
            gm_b0_1 t595 = GM_LD(gm_b0_1, (((uint8_t *)&t594) + 1));
        }
        gm_b16_8 t596;
        uint8_t t598 = gm_l59;
        if (!t598) goto ie597;
        gm_b16_8 t600;
        GM_ST(void *, (uint8_t *)&t600, (void *)(gm_s920dpvvv6cej));
        GM_ST(uintptr_t, (uint8_t *)&t600 + 8, (uintptr_t)(3));
        gm_b16_8 t601 = t600;
        t596 = t601;
        goto ix597;
ie597:;
        gm_b16_8 t603;
        GM_ST(void *, (uint8_t *)&t603, (void *)(gm_spi47lg4tr80j));
        GM_ST(uintptr_t, (uint8_t *)&t603 + 8, (uintptr_t)(2));
        gm_b16_8 t604 = t603;
        t596 = t604;
ix597:;
        gm_b16_8 t605 = t596;
        t9 = t605;
        goto me10;
mn582:;
ma45:;
        uint8_t t607 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t607 != ((uint8_t)32ULL)) goto mn606;
        gm_tmn7n0n9raou5 t608 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l62 = t608;
        void * t609 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l63 = t609;
        {
            void * t611 = (void *)((uint8_t *)&F.l5);
            void * t612 = gm_l63;
            gm_tftb1odc739t5 t613 = gm_f_push_pf02nv6(t611, t612);
            if (((((uint8_t *)&t613))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t613) + 1, gm_srco4ljquuk5d87, 214, 13);
            gm_b0_1 t614 = GM_LD(gm_b0_1, (((uint8_t *)&t613) + 1));
        }
        gm_tmn7n0n9raou5 t615 = gm_l62;
        gm_b16_8 t616 = gm_f_coercion_text_g32oppe((void *)NULL, cx, NULL, t615);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t617;
        GM_ST(gm_b16_8, ((uint8_t *)&t617 + 0), t616);
        gm_tabb710d9mq2e t618;
        gm_f_format_c726p1u((void *)&t618, (void *)NULL, cx, NULL, t617);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t618))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t618) + 8, gm_srco4ljquuk5d87, 215, 13);
        gm_b16_8 t619 = GM_LD(gm_b16_8, (((uint8_t *)&t618) + 8));
        gm_b16_8 t620 = t619;
        gm_b16_8 t621 = GM_LD(gm_b16_8, ((uint8_t *)&t620));
        t9 = t621;
        goto me10;
mn606:;
ma46:;
        uint8_t t623 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t623 != ((uint8_t)33ULL)) goto mn622;
        gm_tvdg94bk7aa22 t624 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l64 = t624;
        gm_b16_8 t625 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l65 = t625;
        {
            {
                gm_l67 = ((uint64_t)0ULL);
            }
            {
                uint64_t t628 = GM_LD(uintptr_t, ((uint8_t *)&gm_l65) + 8);
                gm_l66 = t628;
            }
            {
                for (;;) {
                    uint64_t t630 = gm_l67;
                    uint64_t t631 = gm_l66;
                    uint8_t t632 = (t630 < t631);
                    if (!t632) goto lb629;
                    {
                        uint64_t t635 = gm_l67;
                        uint64_t t638 = GM_LD(uint64_t, ((uint8_t *)&t635));
                        gm_l68 = t638;
                        {
                            void * t640 = (void *)((uint8_t *)&F.l5);
                            uint64_t t641 = gm_l68;
                            uint64_t t642 = (uintptr_t)t641;
                            uint64_t t643 = GM_LD(uintptr_t, ((uint8_t *)&gm_l65) + 8);
                            if (t642 >= (uintptr_t)(t643)) gm_trap_index(t642, t643, gm_srco4ljquuk5d87, 218, 47);
                            void * t644 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l65)) + t642 * 88);
                            gm_tftb1odc739t5 t645 = gm_f_push_pf02nv6(t640, t644);
                            if (((((uint8_t *)&t645))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t645) + 1, gm_srco4ljquuk5d87, 218, 36);
                            gm_b0_1 t646 = GM_LD(gm_b0_1, (((uint8_t *)&t645) + 1));
                        }
                        goto me636;
mn637:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 218, 13);
me636:;
                    }
lc629:;
                    {
                        uint64_t t648 = gm_l67;
                        uint64_t r649; bool o650 = __builtin_add_overflow(t648, ((uint64_t)1ULL), &r649);
                        uint64_t t651 = r649;
                        gm_l67 = t651;
                    }
                }
lb629:;
            }
        }
        gm_tvdg94bk7aa22 t652 = gm_l64;
        gm_b16_8 t653 = gm_f_builtin_text_od38u7h((void *)NULL, cx, NULL, t652);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t654;
        GM_ST(gm_b16_8, ((uint8_t *)&t654 + 0), t653);
        gm_tabb710d9mq2e t655;
        gm_f_format_02b5heb((void *)&t655, (void *)NULL, cx, NULL, t654);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t655))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t655) + 8, gm_srco4ljquuk5d87, 219, 13);
        gm_b16_8 t656 = GM_LD(gm_b16_8, (((uint8_t *)&t655) + 8));
        gm_b16_8 t657 = t656;
        gm_b16_8 t658 = GM_LD(gm_b16_8, ((uint8_t *)&t657));
        t9 = t658;
        goto me10;
mn622:;
ma47:;
        uint8_t t660 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t660 != ((uint8_t)34ULL)) goto mn659;
        gm_tsth9qu1dv0g3 t661 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t8) + 8) + 8) + 0));
        F.l69 = t661;
        gm_b16_8 t662 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 40));
        gm_l70 = t662;
        {
            gm_b16_8 t664;
            uint8_t t670 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            switch (t670) { case ((uint8_t)0ULL): goto ma666; case ((uint8_t)1ULL): goto ma667; case ((uint8_t)2ULL): goto ma668; default: goto ma669; }
ma666:;
            uint8_t t672 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            if (t672 != ((uint8_t)0ULL)) goto mn671;
            uint32_t t673 = GM_LD(uint32_t, (((uint8_t *)&F.l69) + 8));
            gm_l71 = t673;
            void * t674 = gm_l3;
            uint32_t t675 = gm_l71;
            gm_b16_8 t676 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t674, t675);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t677;
            GM_ST(gm_b16_8, ((uint8_t *)&t677 + 0), t676);
            gm_tabb710d9mq2e t678;
            gm_f_format_vmi8kfe((void *)&t678, (void *)NULL, cx, NULL, t677);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t678))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t678) + 8, gm_srco4ljquuk5d87, 223, 27);
            gm_b16_8 t679 = GM_LD(gm_b16_8, (((uint8_t *)&t678) + 8));
            gm_b16_8 t680 = GM_LD(gm_b16_8, ((uint8_t *)&t679));
            t664 = t680;
            goto me665;
mn671:;
ma667:;
            uint8_t t682 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            if (t682 != ((uint8_t)1ULL)) goto mn681;
            gm_tiilmfd16v3fc t683 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l69) + 8) + 0));
            gm_l72 = t683;
            uint32_t t684 = GM_LD(uint32_t, ((((uint8_t *)&F.l69) + 8) + 24));
            gm_l73 = t684;
            gm_tommf4j9upomc t685;
            memset((uint8_t *)&t685 + 20, 0, 4);
            void * t686 = gm_l3;
            void * t687 = (void *)(((uint8_t *)t686) + 8);
            void * t688 = (void *)((uint8_t *)&gm_l72);
            gm_b16_8 t689 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t687, t688);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t685 + 0, t689);
            uint32_t t690 = gm_l73;
            GM_ST(uint32_t, (uint8_t *)&t685 + 16, t690);
            gm_tabb710d9mq2e t691;
            gm_f_format_5m6a3eh((void *)&t691, (void *)NULL, cx, NULL, &t685);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t691))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t691) + 8, gm_srco4ljquuk5d87, 224, 38);
            gm_b16_8 t692 = GM_LD(gm_b16_8, (((uint8_t *)&t691) + 8));
            gm_b16_8 t693 = GM_LD(gm_b16_8, ((uint8_t *)&t692));
            t664 = t693;
            goto me665;
mn681:;
ma668:;
            uint8_t t695 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
            if (t695 != ((uint8_t)2ULL)) goto mn694;
            void * t696 = GM_LD(void *, (((uint8_t *)&F.l69) + 8));
            gm_l74 = t696;
            {
                void * t698 = (void *)((uint8_t *)&F.l5);
                void * t699 = gm_l74;
                gm_tftb1odc739t5 t700 = gm_f_push_pf02nv6(t698, t699);
                if (((((uint8_t *)&t700))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t700) + 1, gm_srco4ljquuk5d87, 226, 21);
                gm_b0_1 t701 = GM_LD(gm_b0_1, (((uint8_t *)&t700) + 1));
            }
            gm_b16_8 t702;
            GM_ST(void *, (uint8_t *)&t702, (void *)(gm_sj696i9uo1a9s));
            GM_ST(uintptr_t, (uint8_t *)&t702 + 8, (uintptr_t)(13));
            gm_b16_8 t703 = t702;
            t664 = t703;
            goto me665;
mn694:;
ma669:;
            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 222, 25);
me665:;
            gm_l75 = t664;
        }
        {
            {
                gm_l77 = ((uint64_t)0ULL);
            }
            {
                uint64_t t705 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                gm_l76 = t705;
            }
            {
                for (;;) {
                    uint64_t t707 = gm_l77;
                    uint64_t t708 = gm_l76;
                    uint8_t t709 = (t707 < t708);
                    if (!t709) goto lb706;
                    {
                        uint64_t t712 = gm_l77;
                        uint64_t t715 = GM_LD(uint64_t, ((uint8_t *)&t712));
                        gm_l78 = t715;
                        {
                            void * t717 = (void *)((uint8_t *)&F.l5);
                            uint64_t t718 = gm_l78;
                            uint64_t t719 = (uintptr_t)t718;
                            uint64_t t720 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                            if (t719 >= (uintptr_t)(t720)) gm_trap_index(t719, t720, gm_srco4ljquuk5d87, 230, 47);
                            void * t721 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t719 * 88);
                            gm_tftb1odc739t5 t722 = gm_f_push_pf02nv6(t717, t721);
                            if (((((uint8_t *)&t722))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t722) + 1, gm_srco4ljquuk5d87, 230, 36);
                            gm_b0_1 t723 = GM_LD(gm_b0_1, (((uint8_t *)&t722) + 1));
                        }
                        goto me713;
mn714:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 230, 13);
me713:;
                    }
lc706:;
                    {
                        uint64_t t725 = gm_l77;
                        uint64_t r726; bool o727 = __builtin_add_overflow(t725, ((uint64_t)1ULL), &r726);
                        uint64_t t728 = r726;
                        gm_l77 = t728;
                    }
                }
lb706:;
            }
        }
        gm_b16_8 t729 = gm_l75;
        gm_b16_8 t730 = t729;
        t9 = t730;
        goto me10;
mn659:;
ma48:;
        uint8_t t732 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t732 != ((uint8_t)35ULL)) goto mn731;
        gm_b16_8 t733 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l79 = t733;
        {
            {
                gm_l81 = ((uint64_t)0ULL);
            }
            {
                uint64_t t736 = GM_LD(uintptr_t, ((uint8_t *)&gm_l79) + 8);
                gm_l80 = t736;
            }
            {
                for (;;) {
                    uint64_t t738 = gm_l81;
                    uint64_t t739 = gm_l80;
                    uint8_t t740 = (t738 < t739);
                    if (!t740) goto lb737;
                    {
                        uint64_t t743 = gm_l81;
                        uint64_t t746 = GM_LD(uint64_t, ((uint8_t *)&t743));
                        gm_l82 = t746;
                        {
                            void * t748 = (void *)((uint8_t *)&F.l5);
                            uint64_t t749 = gm_l82;
                            uint64_t t750 = (uintptr_t)t749;
                            uint64_t t751 = GM_LD(uintptr_t, ((uint8_t *)&gm_l79) + 8);
                            if (t750 >= (uintptr_t)(t751)) gm_trap_index(t750, t751, gm_srco4ljquuk5d87, 234, 45);
                            void * t752 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l79)) + t750 * 88);
                            gm_tftb1odc739t5 t753 = gm_f_push_pf02nv6(t748, t752);
                            if (((((uint8_t *)&t753))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t753) + 1, gm_srco4ljquuk5d87, 234, 34);
                            gm_b0_1 t754 = GM_LD(gm_b0_1, (((uint8_t *)&t753) + 1));
                        }
                        goto me744;
mn745:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 234, 13);
me744:;
                    }
lc737:;
                    {
                        uint64_t t756 = gm_l81;
                        uint64_t r757; bool o758 = __builtin_add_overflow(t756, ((uint64_t)1ULL), &r757);
                        uint64_t t759 = r757;
                        gm_l81 = t759;
                    }
                }
lb737:;
            }
        }
        gm_b16_8 t760;
        GM_ST(void *, (uint8_t *)&t760, (void *)(gm_sluco9qbar70k));
        GM_ST(uintptr_t, (uint8_t *)&t760 + 8, (uintptr_t)(9));
        gm_b16_8 t761 = t760;
        t9 = t761;
        goto me10;
mn731:;
ma49:;
        uint8_t t763 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t763 != ((uint8_t)36ULL)) goto mn762;
        gm_b16_8 t764 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
        gm_l83 = t764;
        {
            {
                gm_l85 = ((uint64_t)0ULL);
            }
            {
                uint64_t t767 = GM_LD(uintptr_t, ((uint8_t *)&gm_l83) + 8);
                gm_l84 = t767;
            }
            {
                for (;;) {
                    uint64_t t769 = gm_l85;
                    uint64_t t770 = gm_l84;
                    uint8_t t771 = (t769 < t770);
                    if (!t771) goto lb768;
                    {
                        uint64_t t774 = gm_l85;
                        uint64_t t777 = GM_LD(uint64_t, ((uint8_t *)&t774));
                        gm_l86 = t777;
                        {
                            void * t779 = (void *)((uint8_t *)&F.l5);
                            uint64_t t780 = gm_l86;
                            uint64_t t781 = (uintptr_t)t780;
                            uint64_t t782 = GM_LD(uintptr_t, ((uint8_t *)&gm_l83) + 8);
                            if (t781 >= (uintptr_t)(t782)) gm_trap_index(t781, t782, gm_srco4ljquuk5d87, 238, 45);
                            void * t783 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l83)) + t781 * 88);
                            gm_tftb1odc739t5 t784 = gm_f_push_pf02nv6(t779, t783);
                            if (((((uint8_t *)&t784))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t784) + 1, gm_srco4ljquuk5d87, 238, 34);
                            gm_b0_1 t785 = GM_LD(gm_b0_1, (((uint8_t *)&t784) + 1));
                        }
                        goto me775;
mn776:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 238, 13);
me775:;
                    }
lc768:;
                    {
                        uint64_t t787 = gm_l85;
                        uint64_t r788; bool o789 = __builtin_add_overflow(t787, ((uint64_t)1ULL), &r788);
                        uint64_t t790 = r788;
                        gm_l85 = t790;
                    }
                }
lb768:;
            }
        }
        gm_b16_8 t791;
        GM_ST(void *, (uint8_t *)&t791, (void *)(gm_st87fk7ojs4l8));
        GM_ST(uintptr_t, (uint8_t *)&t791 + 8, (uintptr_t)(5));
        gm_b16_8 t792 = t791;
        t9 = t792;
        goto me10;
mn762:;
ma50:;
        uint8_t t794 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t794 != ((uint8_t)37ULL)) goto mn793;
        void * t795 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l87 = t795;
        {
            void * t797 = (void *)((uint8_t *)&F.l5);
            void * t798 = gm_l87;
            gm_tftb1odc739t5 t799 = gm_f_push_pf02nv6(t797, t798);
            if (((((uint8_t *)&t799))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t799) + 1, gm_srco4ljquuk5d87, 242, 13);
            gm_b0_1 t800 = GM_LD(gm_b0_1, (((uint8_t *)&t799) + 1));
        }
        gm_b16_8 t801;
        GM_ST(void *, (uint8_t *)&t801, (void *)(gm_shvpku7ssg8d7));
        GM_ST(uintptr_t, (uint8_t *)&t801 + 8, (uintptr_t)(10));
        gm_b16_8 t802 = t801;
        t9 = t802;
        goto me10;
mn793:;
ma51:;
        uint8_t t804 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t804 != ((uint8_t)38ULL)) goto mn803;
        gm_ttggq7ng4kp3p t805 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t8) + 8) + 8));
        gm_l88 = t805;
        {
            void * t808 = GM_LD(void *, (((uint8_t *)&gm_l88) + 8));
            if (!(GM_LD(void *, ((uint8_t *)&t808)) != 0)) goto ie807;
            void * t809 = GM_LD(void *, ((uint8_t *)&t808));
            gm_l89 = t809;
            {
                void * t811 = (void *)((uint8_t *)&F.l5);
                void * t812 = gm_l89;
                gm_tftb1odc739t5 t813 = gm_f_push_pf02nv6(t811, t812);
                if (((((uint8_t *)&t813))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t813) + 1, gm_srco4ljquuk5d87, 246, 33);
                gm_b0_1 t814 = GM_LD(gm_b0_1, (((uint8_t *)&t813) + 1));
            }
            goto ix807;
ie807:;
ix807:;
        }
        uint32_t t815 = GM_LD(uint32_t, (((uint8_t *)&gm_l88) + 0));
        gm_t88rcucg1b7uk t816;
        GM_ST(uint32_t, ((uint8_t *)&t816 + 0), t815);
        gm_tabb710d9mq2e t817;
        gm_f_format_lefnhqh((void *)&t817, (void *)NULL, cx, NULL, t816);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t817))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t817) + 8, gm_srco4ljquuk5d87, 247, 13);
        gm_b16_8 t818 = GM_LD(gm_b16_8, (((uint8_t *)&t817) + 8));
        gm_b16_8 t819 = t818;
        gm_b16_8 t820 = GM_LD(gm_b16_8, ((uint8_t *)&t819));
        t9 = t820;
        goto me10;
mn803:;
ma52:;
        uint8_t t822 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t822 != ((uint8_t)39ULL)) goto mn821;
        void * t823 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l90 = t823;
        {
            void * t825 = (void *)((uint8_t *)&F.l5);
            void * t826 = gm_l90;
            gm_tftb1odc739t5 t827 = gm_f_push_pf02nv6(t825, t826);
            if (((((uint8_t *)&t827))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t827) + 1, gm_srco4ljquuk5d87, 250, 13);
            gm_b0_1 t828 = GM_LD(gm_b0_1, (((uint8_t *)&t827) + 1));
        }
        gm_b16_8 t829;
        GM_ST(void *, (uint8_t *)&t829, (void *)(gm_sn581p3lbgrjg));
        GM_ST(uintptr_t, (uint8_t *)&t829 + 8, (uintptr_t)(8));
        gm_b16_8 t830 = t829;
        t9 = t830;
        goto me10;
mn821:;
ma53:;
        uint8_t t832 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t832 != ((uint8_t)41ULL)) goto mn831;
        gm_t3dcoc7077t1q t833 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t8) + 8) + 8));
        gm_l91 = t833;
        {
            void * t835 = (void *)((uint8_t *)&F.l5);
            void * t836 = GM_LD(void *, (((uint8_t *)&gm_l91) + 0));
            gm_tftb1odc739t5 t837 = gm_f_push_pf02nv6(t835, t836);
            if (((((uint8_t *)&t837))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t837) + 1, gm_srco4ljquuk5d87, 254, 13);
            gm_b0_1 t838 = GM_LD(gm_b0_1, (((uint8_t *)&t837) + 1));
        }
        {
            void * t839 = (void *)((uint8_t *)&F.l5);
            void * t840 = GM_LD(void *, (((uint8_t *)&gm_l91) + 16));
            gm_tftb1odc739t5 t841 = gm_f_push_pf02nv6(t839, t840);
            if (((((uint8_t *)&t841))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t841) + 1, gm_srco4ljquuk5d87, 255, 13);
            gm_b0_1 t842 = GM_LD(gm_b0_1, (((uint8_t *)&t841) + 1));
        }
        gm_b16_8 t843;
        if (!(((((uint8_t *)&gm_l91) + 8))[4] != 0)) goto ie844;
        uint32_t t845 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 8));
        gm_l92 = t845;
        void * t847 = gm_l0;
        uint32_t t848 = gm_l92;
        gm_b16_8 t849 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t847, t848);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t850;
        GM_ST(gm_b16_8, ((uint8_t *)&t850 + 0), t849);
        gm_tabb710d9mq2e t851;
        gm_f_format_i4fsdjb((void *)&t851, (void *)NULL, cx, NULL, t850);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t851))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t851) + 8, gm_srco4ljquuk5d87, 256, 33);
        gm_b16_8 t852 = GM_LD(gm_b16_8, (((uint8_t *)&t851) + 8));
        gm_b16_8 t853 = t852;
        gm_b16_8 t854 = GM_LD(gm_b16_8, ((uint8_t *)&t853));
        t843 = t854;
        goto ix844;
ie844:;
        gm_b16_8 t856;
        GM_ST(void *, (uint8_t *)&t856, (void *)(gm_s9es2tos1bhfp));
        GM_ST(uintptr_t, (uint8_t *)&t856 + 8, (uintptr_t)(4));
        gm_b16_8 t857 = t856;
        t843 = t857;
ix844:;
        gm_b16_8 t858 = t843;
        t9 = t858;
        goto me10;
mn831:;
ma54:;
        uint8_t t860 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t860 != ((uint8_t)42ULL)) goto mn859;
        gm_t083kd7b1kkag t861 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t8) + 8) + 8));
        gm_l93 = t861;
        {
            void * t863 = (void *)((uint8_t *)&F.l5);
            void * t864 = GM_LD(void *, (((uint8_t *)&gm_l93) + 0));
            gm_tftb1odc739t5 t865 = gm_f_push_pf02nv6(t863, t864);
            if (((((uint8_t *)&t865))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t865) + 1, gm_srco4ljquuk5d87, 259, 13);
            gm_b0_1 t866 = GM_LD(gm_b0_1, (((uint8_t *)&t865) + 1));
        }
        gm_b16_8 t867;
        if (!(((((uint8_t *)&gm_l93) + 8))[4] != 0)) goto ie868;
        uint32_t t869 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 8));
        gm_l94 = t869;
        uint32_t t871 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 16));
        uint32_t t872 = gm_l94;
        gm_tcc1rq4n1avb2 t873;
        GM_ST(uint32_t, ((uint8_t *)&t873 + 0), t871);
        GM_ST(uint32_t, ((uint8_t *)&t873 + 4), t872);
        gm_tabb710d9mq2e t874;
        gm_f_format_0gd6afr((void *)&t874, (void *)NULL, cx, NULL, t873);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t874))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t874) + 8, gm_srco4ljquuk5d87, 260, 30);
        gm_b16_8 t875 = GM_LD(gm_b16_8, (((uint8_t *)&t874) + 8));
        gm_b16_8 t876 = t875;
        gm_b16_8 t877 = GM_LD(gm_b16_8, ((uint8_t *)&t876));
        t867 = t877;
        goto ix868;
ie868:;
        uint32_t t879 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 16));
        gm_t88rcucg1b7uk t880;
        GM_ST(uint32_t, ((uint8_t *)&t880 + 0), t879);
        gm_tabb710d9mq2e t881;
        gm_f_format_glqqt6c((void *)&t881, (void *)NULL, cx, NULL, t880);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t881))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t881) + 8, gm_srco4ljquuk5d87, 260, 82);
        gm_b16_8 t882 = GM_LD(gm_b16_8, (((uint8_t *)&t881) + 8));
        gm_b16_8 t883 = t882;
        gm_b16_8 t884 = GM_LD(gm_b16_8, ((uint8_t *)&t883));
        t867 = t884;
ix868:;
        gm_b16_8 t885 = t867;
        t9 = t885;
        goto me10;
mn859:;
ma55:;
        uint8_t t887 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t887 != ((uint8_t)40ULL)) goto mn886;
        void * t888 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l95 = t888;
        uint8_t t889 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l96 = t889;
        {
            void * t891 = (void *)((uint8_t *)&F.l5);
            void * t892 = gm_l95;
            gm_tftb1odc739t5 t893 = gm_f_push_pf02nv6(t891, t892);
            if (((((uint8_t *)&t893))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t893) + 1, gm_srco4ljquuk5d87, 263, 13);
            gm_b0_1 t894 = GM_LD(gm_b0_1, (((uint8_t *)&t893) + 1));
        }
        gm_b16_8 t895;
        uint8_t t897 = gm_l96;
        if (!t897) goto ie896;
        gm_b16_8 t899;
        GM_ST(void *, (uint8_t *)&t899, (void *)(gm_sromjese8e416));
        GM_ST(uintptr_t, (uint8_t *)&t899 + 8, (uintptr_t)(13));
        gm_b16_8 t900 = t899;
        t895 = t900;
        goto ix896;
ie896:;
        gm_b16_8 t902;
        GM_ST(void *, (uint8_t *)&t902, (void *)(gm_srntlv7ksi1vk));
        GM_ST(uintptr_t, (uint8_t *)&t902 + 8, (uintptr_t)(15));
        gm_b16_8 t903 = t902;
        t895 = t903;
ix896:;
        gm_b16_8 t904 = t895;
        t9 = t904;
        goto me10;
mn886:;
ma56:;
        uint8_t t906 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t906 != ((uint8_t)43ULL)) goto mn905;
        void * t907 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l97 = t907;
        void * t908 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l98 = t908;
        uint8_t t909 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l99 = t909;
        {
            void * t911 = (void *)((uint8_t *)&F.l5);
            void * t912 = gm_l97;
            gm_tftb1odc739t5 t913 = gm_f_push_pf02nv6(t911, t912);
            if (((((uint8_t *)&t913))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t913) + 1, gm_srco4ljquuk5d87, 267, 13);
            gm_b0_1 t914 = GM_LD(gm_b0_1, (((uint8_t *)&t913) + 1));
        }
        {
            void * t915 = (void *)((uint8_t *)&F.l5);
            void * t916 = gm_l98;
            gm_tftb1odc739t5 t917 = gm_f_push_pf02nv6(t915, t916);
            if (((((uint8_t *)&t917))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t917) + 1, gm_srco4ljquuk5d87, 268, 13);
            gm_b0_1 t918 = GM_LD(gm_b0_1, (((uint8_t *)&t917) + 1));
        }
        gm_b16_8 t919;
        uint8_t t921 = gm_l99;
        if (!t921) goto ie920;
        gm_b16_8 t923;
        GM_ST(void *, (uint8_t *)&t923, (void *)(gm_s3s50a53ci7rk));
        GM_ST(uintptr_t, (uint8_t *)&t923 + 8, (uintptr_t)(6));
        gm_b16_8 t924 = t923;
        t919 = t924;
        goto ix920;
ie920:;
        gm_b16_8 t926;
        GM_ST(void *, (uint8_t *)&t926, (void *)(gm_sgpdmvc9s0d2p));
        GM_ST(uintptr_t, (uint8_t *)&t926 + 8, (uintptr_t)(2));
        gm_b16_8 t927 = t926;
        t919 = t927;
ix920:;
        gm_b16_8 t928 = t919;
        t9 = t928;
        goto me10;
mn905:;
ma57:;
        uint8_t t930 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t930 != ((uint8_t)45ULL)) goto mn929;
        void * t931 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l100 = t931;
        void * t932 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l101 = t932;
        uint8_t t933 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l102 = t933;
        {
            void * t935 = (void *)((uint8_t *)&F.l5);
            void * t936 = gm_l100;
            gm_tftb1odc739t5 t937 = gm_f_push_pf02nv6(t935, t936);
            if (((((uint8_t *)&t937))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t937) + 1, gm_srco4ljquuk5d87, 272, 13);
            gm_b0_1 t938 = GM_LD(gm_b0_1, (((uint8_t *)&t937) + 1));
        }
        {
            void * t939 = (void *)((uint8_t *)&F.l5);
            void * t940 = gm_l101;
            gm_tftb1odc739t5 t941 = gm_f_push_pf02nv6(t939, t940);
            if (((((uint8_t *)&t941))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t941) + 1, gm_srco4ljquuk5d87, 273, 13);
            gm_b0_1 t942 = GM_LD(gm_b0_1, (((uint8_t *)&t941) + 1));
        }
        gm_b16_8 t943;
        uint8_t t945 = gm_l102;
        if (!t945) goto ie944;
        gm_b16_8 t947;
        GM_ST(void *, (uint8_t *)&t947, (void *)(gm_sh4e4kqha06b3));
        GM_ST(uintptr_t, (uint8_t *)&t947 + 8, (uintptr_t)(6));
        gm_b16_8 t948 = t947;
        t943 = t948;
        goto ix944;
ie944:;
        gm_b16_8 t950;
        GM_ST(void *, (uint8_t *)&t950, (void *)(gm_svbhcvlahd4n2));
        GM_ST(uintptr_t, (uint8_t *)&t950 + 8, (uintptr_t)(2));
        gm_b16_8 t951 = t950;
        t943 = t951;
ix944:;
        gm_b16_8 t952 = t943;
        t9 = t952;
        goto me10;
mn929:;
ma58:;
        uint8_t t954 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t954 != ((uint8_t)44ULL)) goto mn953;
        void * t955 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l103 = t955;
        gm_b16_8 t956 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l104 = t956;
        {
            void * t958 = gm_l0;
            uint64_t t959 = gm_l1;
            gm_b16_8 t960 = gm_l4;
            gm_tnpifv20irnph t961;
            GM_ST(gm_b16_8, ((uint8_t *)&t961 + 0), t960);
            gm_tabb710d9mq2e t962;
            gm_f_format_vprps7i((void *)&t962, (void *)NULL, cx, NULL, t961);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t962))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t962) + 8, gm_srco4ljquuk5d87, 277, 33);
            gm_b16_8 t963 = GM_LD(gm_b16_8, (((uint8_t *)&t962) + 8));
            gm_b16_8 t964 = GM_LD(gm_b16_8, ((uint8_t *)&t963));
            gm_b0_1 t965 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t958, t959, t964);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t966 = gm_l0;
            uint64_t t967 = gm_l1;
            uint64_t r968; bool o969 = __builtin_add_overflow(t967, ((uint64_t)1ULL), &r968);
            if (o969) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 278, 26);
            uint64_t t970 = r968;
            void * t971 = gm_l103;
            gm_b0_1 t972 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t966, t970, t971);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t974 = gm_l104;
                gm_l105 = t974;
            }
            {
                gm_l106 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t976 = gm_l106;
                    uint64_t t977 = GM_LD(uintptr_t, ((uint8_t *)&gm_l105) + 8);
                    uint8_t t978 = (t976 < t977);
                    if (!t978) goto lb975;
                    {
                        uint64_t t981 = gm_l106;
                        uint64_t t982 = (uintptr_t)t981;
                        uint64_t t983 = GM_LD(uintptr_t, ((uint8_t *)&gm_l105) + 8);
                        gm_tbbqefuhv2r8h t986 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l105)) + t982 * 24));
                        gm_l107 = t986;
                        {
                            void * t988 = gm_l0;
                            uint64_t t989 = gm_l1;
                            uint64_t r990; bool o991 = __builtin_add_overflow(t989, ((uint64_t)1ULL), &r990);
                            if (o991) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 279, 41);
                            uint64_t t992 = r990;
                            void * t993 = (void *)((uint8_t *)&gm_l107);
                            gm_b0_1 t994 = gm_f_write_arm_4vimkqo((void *)NULL, cx, NULL, t988, t992, t993);
                            if (gm_unw.kind) {
                                goto gm_ll979;
                            }
                        }
                        goto me984;
mn985:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 279, 13);
me984:;
                    }
lc975:;
                    {
                        uint64_t t996 = gm_l106;
                        uint64_t r997; bool o998 = __builtin_add_overflow(t996, ((uint64_t)1ULL), &r997);
                        uint64_t t999 = r997;
                        gm_l106 = t999;
                    }
                }
gm_ll979:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb975; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc975; }
                }
                goto gm_lad;
lb975:;
            }
        }
        gm_b0_1 t1000;
        uint8_t t1001 = 0;
        F.ret = t1000;
        goto gm_fret;
        goto me10;
mn953:;
ma59:;
        uint8_t t1003 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1003 != ((uint8_t)46ULL)) goto mn1002;
        void * t1004 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l108 = t1004;
        {
            void * t1006 = gm_l0;
            uint64_t t1007 = gm_l1;
            gm_b16_8 t1008 = gm_l4;
            gm_tnpifv20irnph t1009;
            GM_ST(gm_b16_8, ((uint8_t *)&t1009 + 0), t1008);
            gm_tabb710d9mq2e t1010;
            gm_f_format_rvu96av((void *)&t1010, (void *)NULL, cx, NULL, t1009);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1010))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1010) + 8, gm_srco4ljquuk5d87, 283, 33);
            gm_b16_8 t1011 = GM_LD(gm_b16_8, (((uint8_t *)&t1010) + 8));
            gm_b16_8 t1012 = GM_LD(gm_b16_8, ((uint8_t *)&t1011));
            gm_b0_1 t1013 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t1006, t1007, t1012);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t1014 = gm_l0;
            uint64_t t1015 = gm_l1;
            uint64_t r1016; bool o1017 = __builtin_add_overflow(t1015, ((uint64_t)1ULL), &r1016);
            if (o1017) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 284, 27);
            uint64_t t1018 = r1016;
            void * t1019 = gm_l108;
            gm_b0_1 t1020 = gm_f_write_block_q5mpqjf((void *)NULL, cx, NULL, t1014, t1018, t1019);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t1021;
        uint8_t t1022 = 0;
        F.ret = t1021;
        goto gm_fret;
        goto me10;
mn1002:;
ma60:;
        uint8_t t1024 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1024 != ((uint8_t)47ULL)) goto mn1023;
        gm_trl2khmuphncd t1025 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t8) + 8) + 8));
        gm_l109 = t1025;
        {
            void * t1027 = gm_l0;
            uint64_t t1028 = gm_l1;
            gm_b16_8 t1029 = gm_l4;
            gm_b0_1 t1030 = gm_f_write_if_oqjiok5((void *)NULL, cx, NULL, t1027, t1028, &(*(gm_trl2khmuphncd *)((uint8_t *)&gm_l109)), t1029);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t1031;
        uint8_t t1032 = 0;
        F.ret = t1031;
        goto gm_fret;
        goto me10;
mn1023:;
ma61:;
        uint8_t t1034 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1034 != ((uint8_t)48ULL)) goto mn1033;
        gm_t710lsrq1m8gk t1035 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t8) + 8) + 8));
        F.l110 = t1035;
        {
            void * t1037 = gm_l0;
            uint64_t t1038 = gm_l1;
            gm_b16_8 t1039 = gm_l4;
            gm_b0_1 t1040 = gm_f_write_loop_u0ja07u((void *)NULL, cx, NULL, t1037, t1038, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l110)), t1039);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t1041;
        uint8_t t1042 = 0;
        F.ret = t1041;
        goto gm_fret;
        goto me10;
mn1033:;
ma62:;
        uint8_t t1044 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1044 != ((uint8_t)49ULL)) goto mn1043;
        gm_teo9d4mncjfdu t1045 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t8) + 8) + 8));
        gm_l111 = t1045;
        {
            void * t1048 = GM_LD(void *, (((uint8_t *)&gm_l111) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t1048)) != 0)) goto ie1047;
            void * t1049 = GM_LD(void *, ((uint8_t *)&t1048));
            gm_l112 = t1049;
            {
                void * t1051 = (void *)((uint8_t *)&F.l5);
                void * t1052 = gm_l112;
                gm_tftb1odc739t5 t1053 = gm_f_push_pf02nv6(t1051, t1052);
                if (((((uint8_t *)&t1053))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1053) + 1, gm_srco4ljquuk5d87, 296, 31);
                gm_b0_1 t1054 = GM_LD(gm_b0_1, (((uint8_t *)&t1053) + 1));
            }
            goto ix1047;
ie1047:;
ix1047:;
        }
        gm_te9hrk0u35rev t1055 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)&gm_l111) + 0));
        gm_b16_8 t1056 = gm_f_target_text_4kebb7t((void *)NULL, cx, NULL, t1055);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t1057;
        GM_ST(gm_b16_8, ((uint8_t *)&t1057 + 0), t1056);
        gm_tabb710d9mq2e t1058;
        gm_f_format_galn15q((void *)&t1058, (void *)NULL, cx, NULL, t1057);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t1058))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1058) + 8, gm_srco4ljquuk5d87, 297, 13);
        gm_b16_8 t1059 = GM_LD(gm_b16_8, (((uint8_t *)&t1058) + 8));
        gm_b16_8 t1060 = t1059;
        gm_b16_8 t1061 = GM_LD(gm_b16_8, ((uint8_t *)&t1060));
        t9 = t1061;
        goto me10;
mn1043:;
ma63:;
        uint8_t t1063 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1063 != ((uint8_t)50ULL)) goto mn1062;
        gm_te9hrk0u35rev t1064 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t8) + 8) + 8));
        gm_l113 = t1064;
        gm_te9hrk0u35rev t1065 = gm_l113;
        gm_b16_8 t1066 = gm_f_target_text_4kebb7t((void *)NULL, cx, NULL, t1065);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t1067;
        GM_ST(gm_b16_8, ((uint8_t *)&t1067 + 0), t1066);
        gm_tabb710d9mq2e t1068;
        gm_f_format_pcmnhpk((void *)&t1068, (void *)NULL, cx, NULL, t1067);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t1068))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1068) + 8, gm_srco4ljquuk5d87, 299, 25);
        gm_b16_8 t1069 = GM_LD(gm_b16_8, (((uint8_t *)&t1068) + 8));
        gm_b16_8 t1070 = GM_LD(gm_b16_8, ((uint8_t *)&t1069));
        t9 = t1070;
        goto me10;
mn1062:;
ma64:;
        uint8_t t1072 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1072 != ((uint8_t)51ULL)) goto mn1071;
        uint32_t t1073 = GM_LD(uint32_t, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l114 = t1073;
        void * t1074 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l115 = t1074;
        {
            void * t1076 = (void *)((uint8_t *)&F.l5);
            void * t1077 = gm_l115;
            gm_tftb1odc739t5 t1078 = gm_f_push_pf02nv6(t1076, t1077);
            if (((((uint8_t *)&t1078))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1078) + 1, gm_srco4ljquuk5d87, 301, 13);
            gm_b0_1 t1079 = GM_LD(gm_b0_1, (((uint8_t *)&t1078) + 1));
        }
        uint32_t t1080 = gm_l114;
        gm_t88rcucg1b7uk t1081;
        GM_ST(uint32_t, ((uint8_t *)&t1081 + 0), t1080);
        gm_tabb710d9mq2e t1082;
        gm_f_format_6na8imi((void *)&t1082, (void *)NULL, cx, NULL, t1081);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t1082))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1082) + 8, gm_srco4ljquuk5d87, 302, 13);
        gm_b16_8 t1083 = GM_LD(gm_b16_8, (((uint8_t *)&t1082) + 8));
        gm_b16_8 t1084 = t1083;
        gm_b16_8 t1085 = GM_LD(gm_b16_8, ((uint8_t *)&t1084));
        t9 = t1085;
        goto me10;
mn1071:;
ma65:;
        uint8_t t1087 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1087 != ((uint8_t)52ULL)) goto mn1086;
        void * t1088 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l116 = t1088;
        {
            void * t1090 = (void *)((uint8_t *)&F.l5);
            void * t1091 = gm_l116;
            gm_tftb1odc739t5 t1092 = gm_f_push_pf02nv6(t1090, t1091);
            if (((((uint8_t *)&t1092))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1092) + 1, gm_srco4ljquuk5d87, 305, 13);
            gm_b0_1 t1093 = GM_LD(gm_b0_1, (((uint8_t *)&t1092) + 1));
        }
        gm_b16_8 t1094;
        GM_ST(void *, (uint8_t *)&t1094, (void *)(gm_sinmpfqtbo9in));
        GM_ST(uintptr_t, (uint8_t *)&t1094 + 8, (uintptr_t)(4));
        gm_b16_8 t1095 = t1094;
        t9 = t1095;
        goto me10;
mn1086:;
ma66:;
        uint8_t t1097 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1097 != ((uint8_t)53ULL)) goto mn1096;
        void * t1098 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l117 = t1098;
        void * t1099 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l118 = t1099;
        {
            void * t1101 = (void *)((uint8_t *)&F.l5);
            void * t1102 = gm_l117;
            gm_tftb1odc739t5 t1103 = gm_f_push_pf02nv6(t1101, t1102);
            if (((((uint8_t *)&t1103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1103) + 1, gm_srco4ljquuk5d87, 309, 13);
            gm_b0_1 t1104 = GM_LD(gm_b0_1, (((uint8_t *)&t1103) + 1));
        }
        {
            void * t1105 = (void *)((uint8_t *)&F.l5);
            void * t1106 = gm_l118;
            gm_tftb1odc739t5 t1107 = gm_f_push_pf02nv6(t1105, t1106);
            if (((((uint8_t *)&t1107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1107) + 1, gm_srco4ljquuk5d87, 310, 13);
            gm_b0_1 t1108 = GM_LD(gm_b0_1, (((uint8_t *)&t1107) + 1));
        }
        gm_b16_8 t1109;
        GM_ST(void *, (uint8_t *)&t1109, (void *)(gm_sn6sqlran7h3r));
        GM_ST(uintptr_t, (uint8_t *)&t1109 + 8, (uintptr_t)(6));
        gm_b16_8 t1110 = t1109;
        t9 = t1110;
        goto me10;
mn1096:;
ma67:;
        uint8_t t1112 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1112 != ((uint8_t)54ULL)) goto mn1111;
        void * t1113 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l119 = t1113;
        {
            void * t1115 = (void *)((uint8_t *)&F.l5);
            void * t1116 = gm_l119;
            gm_tftb1odc739t5 t1117 = gm_f_push_pf02nv6(t1115, t1116);
            if (((((uint8_t *)&t1117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1117) + 1, gm_srco4ljquuk5d87, 314, 13);
            gm_b0_1 t1118 = GM_LD(gm_b0_1, (((uint8_t *)&t1117) + 1));
        }
        gm_b16_8 t1119;
        GM_ST(void *, (uint8_t *)&t1119, (void *)(gm_si1m7q6ia0hto));
        GM_ST(uintptr_t, (uint8_t *)&t1119 + 8, (uintptr_t)(6));
        gm_b16_8 t1120 = t1119;
        t9 = t1120;
        goto me10;
mn1111:;
ma68:;
        uint8_t t1122 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1122 != ((uint8_t)55ULL)) goto mn1121;
        void * t1123 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l120 = t1123;
        {
            void * t1126 = gm_l120;
            uint8_t t1127 = GM_LD(uint8_t, ((((uint8_t *)t1126) + 0) + 0));
            if (t1127 != ((uint8_t)1ULL)) goto ie1125;
            void * t1128 = GM_LD(void *, ((((uint8_t *)t1126) + 0) + 8));
            gm_l121 = t1128;
            {
                void * t1130 = (void *)((uint8_t *)&F.l5);
                void * t1131 = gm_l121;
                gm_tftb1odc739t5 t1132 = gm_f_push_pf02nv6(t1130, t1131);
                if (((((uint8_t *)&t1132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1132) + 1, gm_srco4ljquuk5d87, 318, 35);
                gm_b0_1 t1133 = GM_LD(gm_b0_1, (((uint8_t *)&t1132) + 1));
            }
            goto ix1125;
ie1125:;
ix1125:;
        }
        {
            {
                gm_l123 = ((uint64_t)0ULL);
            }
            {
                void * t1135 = gm_l120;
                uint64_t t1136 = GM_LD(uintptr_t, (((uint8_t *)t1135) + 72) + 8);
                gm_l122 = t1136;
            }
            {
                for (;;) {
                    uint64_t t1138 = gm_l123;
                    uint64_t t1139 = gm_l122;
                    uint8_t t1140 = (t1138 < t1139);
                    if (!t1140) goto lb1137;
                    {
                        uint64_t t1143 = gm_l123;
                        uint64_t t1146 = GM_LD(uint64_t, ((uint8_t *)&t1143));
                        gm_l124 = t1146;
                        {
                            void * t1148 = (void *)((uint8_t *)&F.l5);
                            uint64_t t1149 = gm_l124;
                            uint64_t t1150 = (uintptr_t)t1149;
                            void * t1151 = gm_l120;
                            uint64_t t1152 = GM_LD(uintptr_t, (((uint8_t *)t1151) + 72) + 8);
                            if (t1150 >= (uintptr_t)(t1152)) gm_trap_index(t1150, t1152, gm_srco4ljquuk5d87, 319, 49);
                            void * t1153 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t1151) + 72)) + t1150 * 88);
                            gm_tftb1odc739t5 t1154 = gm_f_push_pf02nv6(t1148, t1153);
                            if (((((uint8_t *)&t1154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1154) + 1, gm_srco4ljquuk5d87, 319, 38);
                            gm_b0_1 t1155 = GM_LD(gm_b0_1, (((uint8_t *)&t1154) + 1));
                        }
                        goto me1144;
mn1145:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 319, 13);
me1144:;
                    }
lc1137:;
                    {
                        uint64_t t1157 = gm_l123;
                        uint64_t r1158; bool o1159 = __builtin_add_overflow(t1157, ((uint64_t)1ULL), &r1158);
                        uint64_t t1160 = r1158;
                        gm_l123 = t1160;
                    }
                }
lb1137:;
            }
        }
        void * t1161 = gm_l0;
        void * t1162 = gm_l120;
        gm_b16_8 t1163 = gm_f_write_text_h6e1ibp((void *)NULL, cx, NULL, t1161, &(*(gm_tntn2tf15puib *)((uint8_t *)t1162)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t1164 = t1163;
        t9 = t1164;
        goto me10;
mn1121:;
ma69:;
        uint8_t t1166 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t1166 != ((uint8_t)56ULL)) goto mn1165;
        void * t1167 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l125 = t1167;
        {
            void * t1170 = gm_l125;
            uint8_t t1171 = GM_LD(uint8_t, ((((uint8_t *)t1170) + 0) + 0));
            if (t1171 != ((uint8_t)1ULL)) goto ie1169;
            void * t1172 = GM_LD(void *, ((((uint8_t *)t1170) + 0) + 8));
            gm_l126 = t1172;
            {
                void * t1174 = (void *)((uint8_t *)&F.l5);
                void * t1175 = gm_l126;
                gm_tftb1odc739t5 t1176 = gm_f_push_pf02nv6(t1174, t1175);
                if (((((uint8_t *)&t1176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1176) + 1, gm_srco4ljquuk5d87, 323, 35);
                gm_b0_1 t1177 = GM_LD(gm_b0_1, (((uint8_t *)&t1176) + 1));
            }
            goto ix1169;
ie1169:;
ix1169:;
        }
        {
            void * t1178 = (void *)((uint8_t *)&F.l5);
            void * t1179 = gm_l125;
            void * t1180 = GM_LD(void *, (((uint8_t *)t1179) + 40));
            gm_tftb1odc739t5 t1181 = gm_f_push_pf02nv6(t1178, t1180);
            if (((((uint8_t *)&t1181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1181) + 1, gm_srco4ljquuk5d87, 324, 13);
            gm_b0_1 t1182 = GM_LD(gm_b0_1, (((uint8_t *)&t1181) + 1));
        }
        {
            void * t1184 = gm_l125;
            void * t1185 = GM_LD(void *, (((uint8_t *)t1184) + 48));
            if (!(GM_LD(void *, ((uint8_t *)&t1185)) != 0)) goto ie1183;
            void * t1186 = GM_LD(void *, ((uint8_t *)&t1185));
            gm_l127 = t1186;
            {
                void * t1188 = (void *)((uint8_t *)&F.l5);
                void * t1189 = gm_l127;
                gm_tftb1odc739t5 t1190 = gm_f_push_pf02nv6(t1188, t1189);
                if (((((uint8_t *)&t1190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1190) + 1, gm_srco4ljquuk5d87, 325, 30);
                gm_b0_1 t1191 = GM_LD(gm_b0_1, (((uint8_t *)&t1190) + 1));
            }
            goto ix1183;
ie1183:;
ix1183:;
        }
        gm_t6j6smj7qoctu t1192;
        void * t1193 = gm_l0;
        void * t1194 = gm_l125;
        gm_b16_8 t1195 = gm_f_sink_text_s4o8tr0((void *)NULL, cx, NULL, t1193, &(*(gm_th1g69d7vpipg *)(((uint8_t *)t1194) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t1192 + 0, t1195);
        void * t1196 = gm_l0;
        void * t1197 = gm_l125;
        uint32_t t1198 = GM_LD(uint32_t, (((uint8_t *)t1197) + 56));
        gm_b16_8 t1199 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t1196, t1198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t1192 + 16, t1199);
        gm_tabb710d9mq2e t1200;
        gm_f_format_llf38gp((void *)&t1200, (void *)NULL, cx, NULL, &t1192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t1200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1200) + 8, gm_srco4ljquuk5d87, 326, 13);
        gm_b16_8 t1201 = GM_LD(gm_b16_8, (((uint8_t *)&t1200) + 8));
        gm_b16_8 t1202 = t1201;
        gm_b16_8 t1203 = GM_LD(gm_b16_8, ((uint8_t *)&t1202));
        t9 = t1203;
        goto me10;
mn1165:;
ma70:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 125, 20);
me10:;
        gm_l128 = t9;
    }
    {
        void * t1204 = gm_l0;
        uint64_t t1205 = gm_l1;
        gm_t6j6smj7qoctu t1206;
        gm_b16_8 t1207 = gm_l128;
        GM_ST(gm_b16_8, (uint8_t *)&t1206 + 0, t1207);
        gm_b16_8 t1208 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t1206 + 16, t1208);
        gm_tabb710d9mq2e t1209;
        gm_f_format_r0u4lpe((void *)&t1209, (void *)NULL, cx, NULL, &t1206);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t1209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1209) + 8, gm_srco4ljquuk5d87, 329, 25);
        gm_b16_8 t1210 = GM_LD(gm_b16_8, (((uint8_t *)&t1209) + 8));
        gm_b16_8 t1211 = GM_LD(gm_b16_8, ((uint8_t *)&t1210));
        gm_b0_1 t1212 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t1204, t1205, t1211);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t1214 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l129 = t1214;
        }
        {
            gm_l130 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t1216 = gm_l130;
                uint64_t t1217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l129) + 8);
                uint8_t t1218 = (t1216 < t1217);
                if (!t1218) goto lb1215;
                {
                    uint64_t t1221 = gm_l130;
                    uint64_t t1222 = (uintptr_t)t1221;
                    uint64_t t1223 = GM_LD(uintptr_t, ((uint8_t *)&gm_l129) + 8);
                    void * t1224 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l129)) + t1222 * 8));
                    void * t1227 = GM_LD(void *, ((uint8_t *)&t1224));
                    gm_l131 = t1227;
                    {
                        void * t1229 = gm_l0;
                        uint64_t t1230 = gm_l1;
                        uint64_t r1231; bool o1232 = __builtin_add_overflow(t1230, ((uint64_t)1ULL), &r1231);
                        if (o1232) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 330, 40);
                        uint64_t t1233 = r1231;
                        void * t1234 = gm_l131;
                        gm_b0_1 t1235 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t1229, t1233, t1234);
                        if (gm_unw.kind) {
                            goto gm_ll1219;
                        }
                    }
                    goto me1225;
mn1226:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 330, 5);
me1225:;
                }
lc1215:;
                {
                    uint64_t t1237 = gm_l130;
                    uint64_t r1238; bool o1239 = __builtin_add_overflow(t1237, ((uint64_t)1ULL), &r1238);
                    uint64_t t1240 = r1238;
                    gm_l130 = t1240;
                }
            }
gm_ll1219:;
            if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1215; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1215; }
            }
            goto gm_lad;
lb1215:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_fn_jkakt7l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_write_fn_jkakt7l F;
    void * gm_l0; /* d */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* s */
    void * gm_l3; /* sig */
    void * gm_l4; /* def */
    gm_b16_8 gm_l5; /* kind */
    gm_b16_8 gm_l6; /* head */
    uint32_t gm_l7; /* p */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint8_t gm_l12; /* param */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    void * gm_l17; /* b */
    GM_STACK_CHECK(&F, 1722u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        GM_ST(uint32_t, (((uint8_t *)t2) + 48), t3);
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        gm_l2 = t5;
    }
    {
        uint32_t t6 = gm_l1;
        uint64_t t7 = (uint64_t)t6;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 344) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srco4ljquuk5d87, 77, 13);
        void * t11 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 344) + 0)) + t8 * 136);
        gm_l3 = t11;
    }
    {
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l2;
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 384) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srco4ljquuk5d87, 78, 13);
        void * t17 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 384) + 0)) + t14 * 64);
        gm_l4 = t17;
    }
    {
        void * t18 = gm_l4;
        gm_b16_8 t19;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma24; default: goto ma25; }
ma21:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t28 != ((uint8_t)0ULL)) goto mn27;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s19ndlkgdtviu));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(5));
        t19 = t29;
        goto me20;
mn27:;
ma22:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t31 != ((uint8_t)1ULL)) goto mn30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s4lo69slu4igt));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(7));
        t19 = t32;
        goto me20;
mn30:;
ma23:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t34 != ((uint8_t)2ULL)) goto mn33;
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s7dkbj8tvt9e3));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(6));
        t19 = t35;
        goto me20;
mn33:;
ma24:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t37 != ((uint8_t)3ULL)) goto mn36;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sv0fbf330lmcd));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(7));
        t19 = t38;
        goto me20;
mn36:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 79, 13);
me20:;
        gm_l5 = t19;
    }
    {
        gm_t6j6smj7qoctu t39;
        void * t40 = gm_l2;
        uint32_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t42);
        gm_b16_8 t43 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t39 + 16, t43);
        gm_tabb710d9mq2e t44;
        gm_f_format_9nv39ua((void *)&t44, (void *)NULL, cx, NULL, &t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srco4ljquuk5d87, 85, 13);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
        gm_l6 = t45;
    }
    {
        void * t47 = gm_l4;
        if (!(((((uint8_t *)t47) + 48))[4] != 0)) goto ie46;
        uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)t47) + 48));
        gm_l7 = t48;
        {
            gm_t6j6smj7qoctu t50;
            gm_b16_8 t51 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t50 + 0, t51);
            void * t52 = gm_l2;
            uint32_t t53 = gm_l7;
            gm_b16_8 t54 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t50 + 16, t54);
            gm_tabb710d9mq2e t55;
            gm_f_format_p2bh4bb((void *)&t55, (void *)NULL, cx, NULL, &t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srco4ljquuk5d87, 86, 33);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
            gm_l6 = t56;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t57 = gm_l0;
        gm_b16_8 t58 = gm_l6;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_b0_1 t60 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t57, ((uint64_t)0ULL), t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t62 = gm_l4;
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t62) + 24));
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
            gm_l8 = t64;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t66 = gm_l9;
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t68 = (t66 < t67);
                if (!t68) goto lb65;
                {
                    gm_t5im2v50m3hk6 t71;
                    uint64_t t72 = gm_l9;
                    GM_ST(uint64_t, (uint8_t *)&t71 + 0, t72);
                    uint64_t t73 = gm_l9;
                    uint64_t t74 = (uintptr_t)t73;
                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    memcpy((uint8_t *)&t71 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t74 * 40), 40);
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&t71) + 0));
                    gm_l10 = t78;
                    F.l11 = (*(gm_tsoie1ppgdvcg *)(((uint8_t *)&t71) + 8));
                    {
                        void * t80 = gm_l4;
                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)t80) + 8));
                        gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                        uint64_t t83 = gm_l10;
                        uint32_t t84 = (uint32_t)t83;
                        gm_tf5srh4mp4b3i t85 = gm_f_index_of_41b084m(t82, t84);
                        uint8_t t86;
                        if (((((uint8_t *)&t85))[8] != 0)) goto mn88;
                        t86 = ((uint8_t)0);
                        goto me87;
mn88:;
                        t86 = ((uint8_t)1);
                        goto me87;
mn89:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 89, 18);
me87:;
                        gm_l12 = t86;
                    }
                    {
                        void * t90 = gm_l0;
                        gm_tcrdb1inj22os t91;
                        gm_b16_8 t92;
                        uint8_t t94 = gm_l12;
                        if (!t94) goto ie93;
                        gm_b16_8 t96;
                        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_skissj10nclhr));
                        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(5));
                        gm_b16_8 t97 = t96;
                        t92 = t97;
                        goto ix93;
ie93:;
                        gm_b16_8 t99;
                        GM_ST(void *, (uint8_t *)&t99, (void *)(gm_sjg1ri23hm753));
                        GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(5));
                        gm_b16_8 t100 = t99;
                        t92 = t100;
ix93:;
                        GM_ST(gm_b16_8, (uint8_t *)&t91 + 0, t92);
                        uint64_t t101 = gm_l10;
                        GM_ST(uint64_t, (uint8_t *)&t91 + 16, t101);
                        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t91 + 24, t102);
                        void * t103 = gm_l2;
                        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_b16_8 t105 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t103, t104);
                        if (gm_unw.kind) {
                            goto gm_ll69;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t91 + 40, t105);
                        gm_b16_8 t106;
                        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 20));
                        if (!t108) goto ie107;
                        gm_b16_8 t110;
                        GM_ST(void *, (uint8_t *)&t110, (void *)(gm_s6ag7edtaqqri));
                        GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(4));
                        gm_b16_8 t111 = t110;
                        t106 = t111;
                        goto ix107;
ie107:;
                        gm_b16_8 t113;
                        GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(0));
                        gm_b16_8 t114 = t113;
                        t106 = t114;
ix107:;
                        GM_ST(gm_b16_8, (uint8_t *)&t91 + 56, t106);
                        gm_tabb710d9mq2e t115;
                        gm_f_format_i7k9v76((void *)&t115, (void *)NULL, cx, NULL, &t91);
                        if (gm_unw.kind) {
                            goto gm_ll69;
                        }
                        if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 8, gm_srco4ljquuk5d87, 90, 25);
                        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
                        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                        gm_b0_1 t118 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t90, ((uint64_t)1ULL), t117);
                        if (gm_unw.kind) {
                            goto gm_ll69;
                        }
                    }
                    goto me76;
mn77:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 88, 5);
me76:;
                }
lc65:;
                {
                    uint64_t t120 = gm_l9;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    uint64_t t123 = r121;
                    gm_l9 = t123;
                }
            }
gm_ll69:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb65; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc65; }
            }
            goto gm_lad;
lb65:;
        }
    }
    {
        void * t124 = gm_l0;
        void * t125 = gm_l2;
        void * t126 = gm_l3;
        gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t126) + 56));
        gm_b16_8 t128 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t125, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t129;
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
        gm_tabb710d9mq2e t130;
        gm_f_format_8k3m4s1((void *)&t130, (void *)NULL, cx, NULL, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_srco4ljquuk5d87, 92, 21);
        gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
        gm_b0_1 t133 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t124, ((uint64_t)1ULL), t132);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t135 = gm_l3;
        uint64_t t136 = GM_LD(uintptr_t, ((((uint8_t *)t135) + 64) + 0) + 8);
        uint8_t t137 = (t136 > ((uint64_t)0ULL));
        if (!t137) goto ie134;
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
                void * t140 = gm_l3;
                uint64_t t141 = GM_LD(uintptr_t, ((((uint8_t *)t140) + 64) + 0) + 8);
                gm_l14 = t141;
            }
            {
                for (;;) {
                    uint64_t t143 = gm_l15;
                    uint64_t t144 = gm_l14;
                    uint8_t t145 = (t143 < t144);
                    if (!t145) goto lb142;
                    {
                        uint64_t t148 = gm_l15;
                        uint64_t t151 = GM_LD(uint64_t, ((uint8_t *)&t148));
                        gm_l16 = t151;
                        {
                            void * t153 = (void *)((uint8_t *)&F.l13);
                            void * t154 = gm_l2;
                            void * t155 = (void *)(((uint8_t *)t154) + 8);
                            uint64_t t156 = gm_l16;
                            uint64_t t157 = (uintptr_t)t156;
                            void * t158 = gm_l3;
                            uint64_t t159 = GM_LD(uintptr_t, ((((uint8_t *)t158) + 64) + 0) + 8);
                            if (t157 >= (uintptr_t)(t159)) gm_trap_index(t157, t159, gm_srco4ljquuk5d87, 95, 74);
                            void * t160 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t158) + 64) + 0)) + t157 * 24);
                            gm_b16_8 t161 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t155, t160);
                            if (gm_unw.kind) {
                                goto gm_ll146;
                            }
                            gm_tftb1odc739t5 t162 = gm_f_push_3ef746b(t153, t161);
                            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_srco4ljquuk5d87, 95, 47);
                            gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
                        }
                        goto me149;
mn150:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 95, 9);
me149:;
                    }
lc142:;
                    {
                        uint64_t t165 = gm_l15;
                        uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                        uint64_t t168 = r166;
                        gm_l15 = t168;
                    }
                }
gm_ll146:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb142; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc142; }
                }
                goto gm_lad;
lb142:;
            }
        }
        {
            void * t169 = gm_l0;
            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
            gm_b16_8 t172;
            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_s0m0qaokk51g0));
            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(3));
            gm_tabb710d9mq2e t173;
            gm_f_join_sfknljb((void *)&t173, (void *)NULL, cx, NULL, t171, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 8, gm_srco4ljquuk5d87, 96, 48);
            gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&t173) + 8));
            gm_tnpifv20irnph t175;
            GM_ST(gm_b16_8, ((uint8_t *)&t175 + 0), t174);
            gm_tabb710d9mq2e t176;
            gm_f_format_7316bju((void *)&t176, (void *)NULL, cx, NULL, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 8, gm_srco4ljquuk5d87, 96, 25);
            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t176) + 8));
            gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
            gm_b0_1 t179 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t169, ((uint64_t)1ULL), t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix134;
ie134:;
ix134:;
    }
    {
        void * t181 = gm_l4;
        void * t182 = GM_LD(void *, (((uint8_t *)t181) + 40));
        if (!(GM_LD(void *, ((uint8_t *)&t182)) != 0)) goto ie180;
        void * t183 = GM_LD(void *, ((uint8_t *)&t182));
        gm_l17 = t183;
        {
            void * t185 = gm_l0;
            void * t186 = gm_l17;
            gm_b0_1 t187 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t185, ((uint64_t)1ULL), t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix180;
ie180:;
        {
            void * t189 = gm_l0;
            gm_b16_8 t190;
            GM_ST(void *, (uint8_t *)&t190, (void *)(gm_st7pul9bg6mqu));
            GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(7));
            gm_b0_1 t191 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t189, ((uint64_t)1ULL), t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix180:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_if_oqjiok5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_if_oqjiok5 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_trl2khmuphncd gm_l2; /* x */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* el */
    GM_STACK_CHECK(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_b16_8 t4 = gm_l3;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_7ejl94l((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srco4ljquuk5d87, 364, 25);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_b0_1 t9 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t2, t3, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = gm_l0;
        uint64_t t11 = gm_l1;
        uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 365, 19);
        uint64_t t14 = r12;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_b0_1 t17 = gm_f_write_conds_k9e57un((void *)NULL, cx, NULL, t10, t14, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = gm_l1;
        uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
        if (o21) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 366, 18);
        uint64_t t22 = r20;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sofbtko7t8taa));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
        gm_b0_1 t24 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t18, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = gm_l1;
        uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)2ULL), &r27);
        if (o28) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 367, 18);
        uint64_t t29 = r27;
        void * t30 = GM_LD(void *, (((uint8_t *)a2) + 16));
        gm_b0_1 t31 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t25, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t33 = GM_LD(void *, (((uint8_t *)a2) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t33)) != 0)) goto ie32;
        void * t34 = GM_LD(void *, ((uint8_t *)&t33));
        gm_l4 = t34;
        {
            void * t36 = gm_l0;
            uint64_t t37 = gm_l1;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            if (o39) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 369, 22);
            uint64_t t40 = r38;
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
            gm_b0_1 t42 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t36, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t43 = gm_l0;
            uint64_t t44 = gm_l1;
            uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)2ULL), &r45);
            if (o46) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 370, 22);
            uint64_t t47 = r45;
            void * t48 = gm_l4;
            gm_b0_1 t49 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t43, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix32;
ie32:;
ix32:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_line_d6lkps7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_write_line_d6lkps7 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l2; /* text */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 183u);
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
            uint64_t t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = gm_l3;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    {
                        void * t14 = gm_l0;
                        void * t15 = (void *)(((uint8_t *)t14) + 8);
                        gm_b16_8 t16;
                        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sgankvlme129s));
                        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t17 = gm_f_extend_0cs85gs(t15, t16);
                        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srco4ljquuk5d87, 69, 25);
                        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 69, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t20 = gm_l4;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    uint64_t t23 = r21;
                    gm_l4 = t23;
                }
            }
lb4:;
        }
    }
    {
        void * t24 = gm_l0;
        void * t25 = (void *)(((uint8_t *)t24) + 8);
        gm_b16_8 t26 = gm_l2;
        gm_tftb1odc739t5 t27 = gm_f_extend_0cs85gs(t25, t26);
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srco4ljquuk5d87, 70, 5);
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
    }
    {
        void * t29 = gm_l0;
        void * t30 = (void *)(((uint8_t *)t29) + 8);
        gm_tftb1odc739t5 t31 = gm_f_push_t83rg2m(t30, ((uint8_t)10ULL));
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srco4ljquuk5d87, 71, 5);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_loop_u0ja07u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_loop_u0ja07u F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* st */
    GM_STACK_CHECK(&F, 372u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_tiilmfd16v3fc t4;
        memset((uint8_t *)&t4 + 4, 0, 4);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
        GM_ST(uint32_t, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 8, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_lde77u1((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srco4ljquuk5d87, 376, 25);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t2, t3, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_add_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 377, 19);
        uint64_t t15 = r13;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_b0_1 t18 = gm_f_write_conds_k9e57un((void *)NULL, cx, NULL, t11, t15, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = gm_l1;
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 378, 18);
        uint64_t t23 = r21;
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sgk015tlfp2cu));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(4));
        gm_b0_1 t25 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t19, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t26 = gm_l0;
        uint64_t t27 = gm_l1;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)2ULL), &r28);
        if (o29) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 379, 19);
        uint64_t t30 = r28;
        void * t31 = GM_LD(void *, (((uint8_t *)a2) + 24));
        gm_b0_1 t32 = gm_f_write_block_q5mpqjf((void *)NULL, cx, NULL, t26, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t34 = GM_LD(void *, (((uint8_t *)a2) + 32));
        if (!(GM_LD(void *, ((uint8_t *)&t34)) != 0)) goto ie33;
        void * t35 = GM_LD(void *, ((uint8_t *)&t34));
        gm_l4 = t35;
        {
            void * t37 = gm_l0;
            uint64_t t38 = gm_l1;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 381, 22);
            uint64_t t41 = r39;
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_si9h59pjqftat));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
            gm_b0_1 t43 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t37, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t44 = gm_l0;
            uint64_t t45 = gm_l1;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)2ULL), &r46);
            if (o47) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 382, 22);
            uint64_t t48 = r46;
            void * t49 = gm_l4;
            gm_b0_1 t50 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t44, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix33;
ie33:;
ix33:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_pat_4qu9dcc(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_pat_4qu9dcc F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* p */
    gm_b16_8 gm_l3; /* ty */
    uint32_t gm_l4; /* l */
    void * gm_l5; /* x */
    uint8_t gm_l8; /* inclusive */
    void * gm_l9; /* q */
    void * gm_l10; /* q */
    void * gm_l11; /* q */
    gm_b16_8 gm_l12; /* name */
    uint32_t gm_l13; /* v */
    uint32_t gm_l14; /* v */
    void * gm_l15; /* q */
    gm_b16_8 gm_l16; /* fs */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l19; /* f */
    gm_b16_8 gm_l20; /* ps */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    void * gm_l23; /* q */
    gm_b16_8 gm_l24; /* head */
    GM_STACK_CHECK(&F, 2110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t4) + 0));
        gm_b16_8 t6 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l2;
        gm_b16_8 t8;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma19; default: goto ma20; }
ma10:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t23 != ((uint8_t)0ULL)) goto mn22;
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sbuhie0vb9t62));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(8));
        t8 = t24;
        goto me9;
mn22:;
ma11:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t26 != ((uint8_t)1ULL)) goto mn25;
        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t7) + 8) + 8));
        gm_l4 = t27;
        void * t28 = gm_l0;
        uint32_t t29 = gm_l4;
        gm_b16_8 t30 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
        gm_tabb710d9mq2e t32;
        gm_f_format_lpj4rro((void *)&t32, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srco4ljquuk5d87, 530, 21);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        t8 = t34;
        goto me9;
mn25:;
ma12:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t36 != ((uint8_t)2ULL)) goto mn35;
        void * t37 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l5 = t37;
        {
            void * t39 = gm_l0;
            uint64_t t40 = gm_l1;
            gm_b16_8 t41 = gm_l3;
            gm_tnpifv20irnph t42;
            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
            gm_tabb710d9mq2e t43;
            gm_f_format_8vmpj9s((void *)&t43, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srco4ljquuk5d87, 532, 33);
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_b0_1 t46 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t39, t40, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t47 = gm_l0;
            uint64_t t48 = gm_l1;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            if (o50) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 533, 26);
            uint64_t t51 = r49;
            void * t52 = gm_l5;
            gm_b0_1 t53 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t47, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t54;
        uint8_t t55 = 0;
        F.ret = t54;
        goto gm_fret;
        goto me9;
mn35:;
ma13:;
        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t57 != ((uint8_t)3ULL)) goto mn56;
        gm_tbci5ospk5nnn t58 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t7) + 8) + 8) + 0));
        F.l6 = t58;
        gm_tbci5ospk5nnn t59 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t7) + 8) + 8) + 48));
        F.l7 = t59;
        uint8_t t60 = GM_LD(uint8_t, (((((uint8_t *)t7) + 8) + 8) + 96));
        gm_l8 = t60;
        gm_tfeab279c16rr t61;
        gm_b16_8 t62 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t61 + 0, t62);
        gm_b16_8 t63 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t61 + 16, t63);
        gm_b16_8 t64;
        uint8_t t66 = gm_l8;
        if (!t66) goto ie65;
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sksprenpp79ih));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(10));
        gm_b16_8 t69 = t68;
        t64 = t69;
        goto ix65;
ie65:;
        gm_b16_8 t71;
        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(0));
        gm_b16_8 t72 = t71;
        t64 = t72;
ix65:;
        GM_ST(gm_b16_8, (uint8_t *)&t61 + 32, t64);
        gm_tabb710d9mq2e t73;
        gm_f_format_6h8qssq((void *)&t73, (void *)NULL, cx, NULL, &t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srco4ljquuk5d87, 536, 38);
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        t8 = t75;
        goto me9;
mn56:;
ma14:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t77 != ((uint8_t)4ULL)) goto mn76;
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sovel03lmfl8u));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(8));
        t8 = t78;
        goto me9;
mn76:;
ma15:;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t82 != ((uint8_t)5ULL)) goto pa81;
        void * t83 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l9 = t83;
        goto po80;
pa81:;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t85 != ((uint8_t)6ULL)) goto pa84;
        void * t86 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l9 = t86;
        goto po80;
pa84:;
        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t88 != ((uint8_t)7ULL)) goto pa87;
        void * t89 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
        gm_l9 = t89;
        goto po80;
pa87:;
        goto mn79;
po80:;
        {
            void * t91 = gm_l2;
            gm_b16_8 t92;
            uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 0));
            switch (t98) { case ((uint8_t)0ULL): goto ma96; case ((uint8_t)1ULL): goto ma96; case ((uint8_t)2ULL): goto ma96; case ((uint8_t)3ULL): goto ma96; case ((uint8_t)4ULL): goto ma96; case ((uint8_t)5ULL): goto ma94; case ((uint8_t)6ULL): goto ma95; case ((uint8_t)7ULL): goto ma96; case ((uint8_t)8ULL): goto ma96; case ((uint8_t)9ULL): goto ma96; case ((uint8_t)10ULL): goto ma96; default: goto ma96; }
ma94:;
            uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 0));
            if (t100 != ((uint8_t)5ULL)) goto mn99;
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sjourjtj9e3to));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(8));
            t92 = t101;
            goto me93;
mn99:;
ma95:;
            uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 0));
            if (t103 != ((uint8_t)6ULL)) goto mn102;
            gm_b16_8 t104;
            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_se0a26478itki));
            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(6));
            t92 = t104;
            goto me93;
mn102:;
ma96:;
            gm_b16_8 t106;
            GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s2iprgandb78l));
            GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(7));
            t92 = t106;
            goto me93;
mn105:;
ma97:;
            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 539, 21);
me93:;
            gm_l12 = t92;
        }
        {
            void * t107 = gm_l0;
            uint64_t t108 = gm_l1;
            gm_t6j6smj7qoctu t109;
            gm_b16_8 t110 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t109 + 0, t110);
            gm_b16_8 t111 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t109 + 16, t111);
            gm_tabb710d9mq2e t112;
            gm_f_format_r0u4lpe((void *)&t112, (void *)NULL, cx, NULL, &t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srco4ljquuk5d87, 544, 33);
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            gm_b0_1 t115 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t107, t108, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t116 = gm_l0;
            uint64_t t117 = gm_l1;
            uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
            if (o119) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 545, 25);
            uint64_t t120 = r118;
            void * t121 = gm_l9;
            gm_b0_1 t122 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t116, t120, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t123;
        uint8_t t124 = 0;
        F.ret = t123;
        goto gm_fret;
        goto me9;
mn79:;
ma16:;
        uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t126 != ((uint8_t)8ULL)) goto mn125;
        uint32_t t127 = GM_LD(uint32_t, (((((uint8_t *)t7) + 8) + 8) + 0));
        gm_l13 = t127;
        if ((GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8)) != 0)) goto mn125;
        uint32_t t128 = gm_l13;
        gm_t88rcucg1b7uk t129;
        GM_ST(uint32_t, ((uint8_t *)&t129 + 0), t128);
        gm_tabb710d9mq2e t130;
        gm_f_format_3669u6h((void *)&t130, (void *)NULL, cx, NULL, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_srco4ljquuk5d87, 548, 30);
        gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
        t8 = t132;
        goto me9;
mn125:;
ma17:;
        uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t134 != ((uint8_t)8ULL)) goto mn133;
        uint32_t t135 = GM_LD(uint32_t, (((((uint8_t *)t7) + 8) + 8) + 0));
        gm_l14 = t135;
        if (!(GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8)) != 0)) goto mn133;
        void * t136 = GM_LD(void *, (((((uint8_t *)t7) + 8) + 8) + 8));
        gm_l15 = t136;
        {
            void * t138 = gm_l0;
            uint64_t t139 = gm_l1;
            gm_tiilmfd16v3fc t140;
            memset((uint8_t *)&t140 + 4, 0, 4);
            uint32_t t141 = gm_l14;
            GM_ST(uint32_t, (uint8_t *)&t140 + 0, t141);
            gm_b16_8 t142 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t140 + 8, t142);
            gm_tabb710d9mq2e t143;
            gm_f_format_p7ogfqk((void *)&t143, (void *)NULL, cx, NULL, &t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_srco4ljquuk5d87, 550, 33);
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
            gm_b0_1 t146 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t138, t139, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t147 = gm_l0;
            uint64_t t148 = gm_l1;
            uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
            if (o150) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 551, 25);
            uint64_t t151 = r149;
            void * t152 = gm_l15;
            gm_b0_1 t153 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t147, t151, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_b0_1 t154;
        uint8_t t155 = 0;
        F.ret = t154;
        goto gm_fret;
        goto me9;
mn133:;
ma18:;
        uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t157 != ((uint8_t)9ULL)) goto mn156;
        gm_b16_8 t158 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 8) + 8));
        gm_l16 = t158;
        {
            void * t160 = gm_l0;
            uint64_t t161 = gm_l1;
            gm_b16_8 t162 = gm_l3;
            gm_tnpifv20irnph t163;
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 0), t162);
            gm_tabb710d9mq2e t164;
            gm_f_format_a4vi4f7((void *)&t164, (void *)NULL, cx, NULL, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 8, gm_srco4ljquuk5d87, 555, 33);
            gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
            gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
            gm_b0_1 t167 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t160, t161, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t169 = gm_l16;
                gm_l17 = t169;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t171 = gm_l18;
                    uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t173 = (t171 < t172);
                    if (!t173) goto lb170;
                    {
                        uint64_t t176 = gm_l18;
                        uint64_t t177 = (uintptr_t)t176;
                        uint64_t t178 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_ttggq7ng4kp3p t181 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t177 * 16));
                        gm_l19 = t181;
                        {
                            void * t183 = gm_l0;
                            uint64_t t184 = gm_l1;
                            uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                            if (o186) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 557, 30);
                            uint64_t t187 = r185;
                            uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 0));
                            gm_t88rcucg1b7uk t189;
                            GM_ST(uint32_t, ((uint8_t *)&t189 + 0), t188);
                            gm_tabb710d9mq2e t190;
                            gm_f_format_208te7b((void *)&t190, (void *)NULL, cx, NULL, t189);
                            if (gm_unw.kind) {
                                goto gm_ll174;
                            }
                            if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 8, gm_srco4ljquuk5d87, 557, 41);
                            gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&t190) + 8));
                            gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                            gm_b0_1 t193 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t183, t187, t192);
                            if (gm_unw.kind) {
                                goto gm_ll174;
                            }
                        }
                        {
                            void * t194 = gm_l0;
                            uint64_t t195 = gm_l1;
                            uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)2ULL), &r196);
                            if (o197) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 558, 29);
                            uint64_t t198 = r196;
                            void * t199 = GM_LD(void *, (((uint8_t *)&gm_l19) + 8));
                            gm_b0_1 t200 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t194, t198, t199);
                            if (gm_unw.kind) {
                                goto gm_ll174;
                            }
                        }
                        goto me179;
mn180:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 556, 13);
me179:;
                    }
lc170:;
                    {
                        uint64_t t202 = gm_l18;
                        uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                        uint64_t t205 = r203;
                        gm_l18 = t205;
                    }
                }
gm_ll174:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb170; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc170; }
                }
                goto gm_lad;
lb170:;
            }
        }
        gm_b0_1 t206;
        uint8_t t207 = 0;
        F.ret = t206;
        goto gm_fret;
        goto me9;
mn156:;
ma19:;
        uint8_t t209 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
        if (t209 != ((uint8_t)10ULL)) goto mn208;
        gm_b16_8 t210 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 8) + 8));
        gm_l20 = t210;
        {
            void * t212 = gm_l0;
            uint64_t t213 = gm_l1;
            gm_b16_8 t214 = gm_l3;
            gm_tnpifv20irnph t215;
            GM_ST(gm_b16_8, ((uint8_t *)&t215 + 0), t214);
            gm_tabb710d9mq2e t216;
            gm_f_format_6b7mjf6((void *)&t216, (void *)NULL, cx, NULL, t215);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 8, gm_srco4ljquuk5d87, 563, 33);
            gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&t216) + 8));
            gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
            gm_b0_1 t219 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t212, t213, t218);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t221 = gm_l20;
                gm_l21 = t221;
            }
            {
                gm_l22 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t223 = gm_l22;
                    uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    uint8_t t225 = (t223 < t224);
                    if (!t225) goto lb222;
                    {
                        uint64_t t228 = gm_l22;
                        uint64_t t229 = (uintptr_t)t228;
                        uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        void * t231 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t229 * 8));
                        void * t234 = GM_LD(void *, ((uint8_t *)&t231));
                        gm_l23 = t234;
                        {
                            void * t236 = gm_l0;
                            uint64_t t237 = gm_l1;
                            uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
                            if (o239) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 564, 39);
                            uint64_t t240 = r238;
                            void * t241 = gm_l23;
                            gm_b0_1 t242 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t236, t240, t241);
                            if (gm_unw.kind) {
                                goto gm_ll226;
                            }
                        }
                        goto me232;
mn233:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 564, 13);
me232:;
                    }
lc222:;
                    {
                        uint64_t t244 = gm_l22;
                        uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                        uint64_t t247 = r245;
                        gm_l22 = t247;
                    }
                }
gm_ll226:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb222; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc222; }
                }
                goto gm_lad;
lb222:;
            }
        }
        gm_b0_1 t248;
        uint8_t t249 = 0;
        F.ret = t248;
        goto gm_fret;
        goto me9;
mn208:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 528, 20);
me9:;
        gm_l24 = t8;
    }
    {
        void * t250 = gm_l0;
        uint64_t t251 = gm_l1;
        gm_t6j6smj7qoctu t252;
        gm_b16_8 t253 = gm_l24;
        GM_ST(gm_b16_8, (uint8_t *)&t252 + 0, t253);
        gm_b16_8 t254 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t252 + 16, t254);
        gm_tabb710d9mq2e t255;
        gm_f_format_r0u4lpe((void *)&t255, (void *)NULL, cx, NULL, &t252);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 8, gm_srco4ljquuk5d87, 568, 25);
        gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
        gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
        gm_b0_1 t258 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t250, t251, t257);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_write_text_h6e1ibp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_write_text_h6e1ibp F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* p */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* spec */
    GM_STACK_CHECK(&F, 468u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        void * t3 = gm_l0;
        gm_b16_8 t4 = gm_f_sink_text_s4o8tr0((void *)NULL, cx, NULL, t3, &(*(gm_th1g69d7vpipg *)(((uint8_t *)&F.l1) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tftb1odc739t5 t6 = gm_f_write_uebhmoa(t2, t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srco4ljquuk5d87, 389, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        {
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 40));
            gm_l3 = t9;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l4;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l4;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t17 * 16));
                    gm_l5 = t21;
                    {
                        void * t23 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t24 = gm_f_push_t83rg2m(t23, ((uint8_t)32ULL));
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srco4ljquuk5d87, 391, 9);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    {
                        gm_b16_8 t26 = gm_l5;
                        void * t27 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t28 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t26, t27);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 390, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t30 = gm_l4;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l4 = t33;
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
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 56) + 8);
            gm_l6 = t35;
        }
        {
            for (;;) {
                uint64_t t37 = gm_l7;
                uint64_t t38 = gm_l6;
                uint8_t t39 = (t37 < t38);
                if (!t39) goto lb36;
                {
                    uint64_t t42 = gm_l7;
                    uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)&t42));
                    gm_l8 = t45;
                    {
                        uint64_t t47 = gm_l8;
                        uint64_t t48 = (uintptr_t)t47;
                        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 56) + 8);
                        if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srco4ljquuk5d87, 395, 18);
                        void * t50 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l1) + 56)) + t48 * 24);
                        gm_l9 = t50;
                    }
                    {
                        void * t52 = gm_l9;
                        uint8_t t53 = gm_f_is_default_spec_cdd8jng(t52);
                        if (!t53) goto ie51;
                        {
                            void * t55 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t56;
                            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sjeopa747clpb));
                            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(3));
                            gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t55, t56);
                            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srco4ljquuk5d87, 397, 13);
                            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                        }
                        goto ix51;
ie51:;
                        {
                            void * t60 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t61 = gm_f_push_t83rg2m(t60, ((uint8_t)32ULL));
                            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srco4ljquuk5d87, 399, 13);
                            gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
                        }
                        {
                            void * t63 = gm_l9;
                            void * t64 = (void *)((uint8_t *)&F.l2);
                            gm_b0_1 t65 = gm_f_spec_text_fr9884d((void *)NULL, cx, NULL, t63, t64);
                            if (gm_unw.kind) {
                                goto gm_ll40;
                            }
                        }
ix51:;
                    }
                    goto me43;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 394, 5);
me43:;
                }
lc36:;
                {
                    uint64_t t67 = gm_l7;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l7 = t70;
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
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        uint8_t t73 = 0;
        F.ret = t72;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_with_j37nil7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_with_j37nil7 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* s */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t4lnf71mruqpt gm_l7; /* h */
    void * gm_l8; /* x */
    gm_b16_8 gm_l9; /* decls */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint32_t gm_l14; /* i */
    void * gm_l16; /* x */
    GM_STACK_CHECK(&F, 1230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = gm_l1;
        gm_tiilmfd16v3fc t6;
        memset((uint8_t *)&t6 + 4, 0, 4);
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
        GM_ST(uint32_t, (uint8_t *)&t6 + 0, t7);
        gm_b16_8 t8 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 8, t8);
        gm_tabb710d9mq2e t9;
        gm_f_format_gnbipfi((void *)&t9, (void *)NULL, cx, NULL, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srco4ljquuk5d87, 336, 25);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b0_1 t12 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t4, t5, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
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
                    gm_t4lnf71mruqpt t26 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t22 * 32));
                    gm_l7 = t26;
                    {
                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t30 != ((uint8_t)0ULL)) goto mn29;
                        void * t31 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        gm_l8 = t31;
                        {
                            void * t33 = gm_l0;
                            uint64_t t34 = gm_l1;
                            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                            if (o36) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 340, 30);
                            uint64_t t37 = r35;
                            gm_b16_8 t38;
                            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sic5vsema0r36));
                            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(7));
                            gm_b0_1 t39 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t33, t37, t38);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                        }
                        {
                            void * t40 = gm_l0;
                            uint64_t t41 = gm_l1;
                            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)2ULL), &r42);
                            if (o43) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 341, 30);
                            uint64_t t44 = r42;
                            void * t45 = gm_l8;
                            gm_b0_1 t46 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t40, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                        }
                        goto me28;
mn29:;
                        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t48 != ((uint8_t)1ULL)) goto mn47;
                        gm_b16_8 t49 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l9 = t49;
                        void * t50 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 16));
                        gm_l10 = t50;
                        {
                            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                        }
                        {
                            {
                                gm_b16_8 t53 = gm_l9;
                                gm_l12 = t53;
                            }
                            {
                                gm_l13 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t55 = gm_l13;
                                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    uint8_t t57 = (t55 < t56);
                                    if (!t57) goto lb54;
                                    {
                                        uint64_t t60 = gm_l13;
                                        uint64_t t61 = (uintptr_t)t60;
                                        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        uint32_t t63 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t61 * 4));
                                        uint32_t t66 = GM_LD(uint32_t, ((uint8_t *)&t63));
                                        gm_l14 = t66;
                                        {
                                            uint32_t t68 = gm_l14;
                                            uint64_t t69 = (uint64_t)t68;
                                            uint64_t t70 = (uintptr_t)t69;
                                            void * t71 = gm_l4;
                                            uint64_t t72 = GM_LD(uintptr_t, ((((uint8_t *)t71) + 544) + 0) + 8);
                                            if (t70 >= (uintptr_t)(t72)) gm_trap_index(t70, t72, gm_srco4ljquuk5d87, 346, 27);
                                            gm_tvc83pqnepc2e t73 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t71) + 544) + 0)) + t70 * 64));
                                            F.l15 = t73;
                                        }
                                        {
                                            void * t74 = (void *)((uint8_t *)&F.l11);
                                            gm_t6j6smj7qoctu t75;
                                            void * t76 = gm_l4;
                                            void * t77 = (void *)(((uint8_t *)t76) + 8);
                                            void * t78 = (void *)(((uint8_t *)&F.l15) + 0);
                                            gm_b16_8 t79 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t77, t78);
                                            if (gm_unw.kind) {
                                                goto gm_ll58;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t75 + 0, t79);
                                            void * t80 = gm_l4;
                                            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 24));
                                            gm_b16_8 t82 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t80, t81);
                                            if (gm_unw.kind) {
                                                goto gm_ll58;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t75 + 16, t82);
                                            gm_tabb710d9mq2e t83;
                                            gm_f_format_3aao56v((void *)&t83, (void *)NULL, cx, NULL, &t75);
                                            if (gm_unw.kind) {
                                                goto gm_ll58;
                                            }
                                            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srco4ljquuk5d87, 347, 32);
                                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
                                            gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                                            gm_tftb1odc739t5 t86 = gm_f_push_3ef746b(t74, t85);
                                            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srco4ljquuk5d87, 347, 21);
                                            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                                        }
                                        goto me64;
mn65:;
                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 345, 17);
me64:;
                                    }
lc54:;
                                    {
                                        uint64_t t89 = gm_l13;
                                        uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                                        uint64_t t92 = r90;
                                        gm_l13 = t92;
                                    }
                                }
gm_ll58:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb54; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc54; }
                                }
                                goto gm_ll19;
lb54:;
                            }
                        }
                        {
                            void * t93 = gm_l0;
                            uint64_t t94 = gm_l1;
                            uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                            if (o96) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 349, 30);
                            uint64_t t97 = r95;
                            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                            gm_b16_8 t100;
                            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t101;
                            gm_f_join_sfknljb((void *)&t101, (void *)NULL, cx, NULL, t99, t100);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                            if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 8, gm_srco4ljquuk5d87, 349, 67);
                            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t101) + 8));
                            gm_tnpifv20irnph t103;
                            GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
                            gm_tabb710d9mq2e t104;
                            gm_f_format_i37o6qg((void *)&t104, (void *)NULL, cx, NULL, t103);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_srco4ljquuk5d87, 349, 41);
                            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
                            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                            gm_b0_1 t107 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t93, t97, t106);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                        }
                        {
                            void * t108 = gm_l0;
                            uint64_t t109 = gm_l1;
                            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)2ULL), &r110);
                            if (o111) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 350, 30);
                            uint64_t t112 = r110;
                            void * t113 = gm_l10;
                            gm_b0_1 t114 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t108, t112, t113);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                        }
                        goto me28;
mn47:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 338, 9);
me28:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 337, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t116 = gm_l6;
                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                    uint64_t t119 = r117;
                    gm_l6 = t119;
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
        void * t121 = GM_LD(void *, (((uint8_t *)a2) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t121)) != 0)) goto ie120;
        void * t122 = GM_LD(void *, ((uint8_t *)&t121));
        gm_l16 = t122;
        {
            void * t124 = gm_l0;
            uint64_t t125 = gm_l1;
            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
            if (o127) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 355, 22);
            uint64_t t128 = r126;
            gm_b16_8 t129;
            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sdl8mgtvebfcs));
            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(2));
            gm_b0_1 t130 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t124, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t131 = gm_l0;
            uint64_t t132 = gm_l1;
            uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)2ULL), &r133);
            if (o134) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 356, 22);
            uint64_t t135 = r133;
            void * t136 = gm_l16;
            gm_b0_1 t137 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t131, t135, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix120;
ie120:;
ix120:;
    }
    {
        void * t138 = gm_l0;
        uint64_t t139 = gm_l1;
        uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
        if (o141) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 358, 18);
        uint64_t t142 = r140;
        gm_b16_8 t143;
        GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sgk015tlfp2cu));
        GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(4));
        gm_b0_1 t144 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t138, t142, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t145 = gm_l0;
        uint64_t t146 = gm_l1;
        uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)2ULL), &r147);
        if (o148) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 359, 18);
        uint64_t t149 = r147;
        void * t150 = GM_LD(void *, (((uint8_t *)a2) + 32));
        gm_b0_1 t151 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t145, t149, t150);
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

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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { int64_t a[4]; } gm_t767tucha9lsh;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; int64_t v1; } f1; } gm_t11mbkfrj7jag;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_t4ce0jd3tdro4 f0; uint8_t f1; uint8_t pe[7]; } gm_t6ie8iajlnpt1;
_Static_assert(sizeof(gm_t767tucha9lsh) == 32 && _Alignof(gm_t767tucha9lsh) == 8 && offsetof(gm_t767tucha9lsh, a) == 0, "layout of `[4]i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; uint16_t v1; } f1; } gm_tvcqs9u9iqpct;
_Static_assert(sizeof(gm_t11mbkfrj7jag) == 16 && _Alignof(gm_t11mbkfrj7jag) == 8 && offsetof(gm_t11mbkfrj7jag, f0) == 0 && offsetof(gm_t11mbkfrj7jag, f1) == 8, "layout of `IoError!i64`");
typedef struct { gm_t617vr72bd7b3 f0; uint8_t f1; uint8_t pe[7]; } gm_t11t9d8s5diq9;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6ie8iajlnpt1) == 16 && _Alignof(gm_t6ie8iajlnpt1) == 8 && offsetof(gm_t6ie8iajlnpt1, f0) == 0 && offsetof(gm_t6ie8iajlnpt1, f1) == 8, "layout of `?Duration`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tb40qn7hk0tl9 f0; uint8_t f1; uint8_t pe[7]; } gm_th6bh7gc364t4;
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tvcqs9u9iqpct) == 12 && _Alignof(gm_tvcqs9u9iqpct) == 4 && offsetof(gm_tvcqs9u9iqpct, f0) == 0 && offsetof(gm_tvcqs9u9iqpct, f1) == 4, "layout of `IoError!u16`");
_Static_assert(sizeof(gm_t11t9d8s5diq9) == 24 && _Alignof(gm_t11t9d8s5diq9) == 8 && offsetof(gm_t11t9d8s5diq9, f0) == 0 && offsetof(gm_t11t9d8s5diq9, f1) == 16, "layout of `?DirEntry`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_th6bh7gc364t4) == 48 && _Alignof(gm_th6bh7gc364t4) == 8 && offsetof(gm_th6bh7gc364t4, f0) == 0 && offsetof(gm_th6bh7gc364t4, f1) == 40, "layout of `?SourceLocation`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t11t9d8s5diq9 v1; } f1; } gm_tiuq7k5hm0kak;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
typedef struct { uint64_t f0; gm_th6bh7gc364t4 f1; } gm_ts5u7codfocvn;
_Static_assert(sizeof(gm_tiuq7k5hm0kak) == 32 && _Alignof(gm_tiuq7k5hm0kak) == 8 && offsetof(gm_tiuq7k5hm0kak, f0) == 0 && offsetof(gm_tiuq7k5hm0kak, f1) == 8, "layout of `IoError!?DirEntry`");
typedef struct { gm_ts5u7codfocvn f0; uint8_t f1; uint8_t pe[7]; } gm_tl9turq9ndgsc;
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
_Static_assert(sizeof(gm_ts5u7codfocvn) == 56 && _Alignof(gm_ts5u7codfocvn) == 8 && offsetof(gm_ts5u7codfocvn, f0) == 0 && offsetof(gm_ts5u7codfocvn, f1) == 8, "layout of `LiveBlock`");
_Static_assert(sizeof(gm_tl9turq9ndgsc) == 64 && _Alignof(gm_tl9turq9ndgsc) == 8 && offsetof(gm_tl9turq9ndgsc, f0) == 0 && offsetof(gm_tl9turq9ndgsc, f1) == 56, "layout of `?LiveBlock`");
struct gm_fr_accept_lrhmdkt {
    gm_frame h;
    gm_t62ebbi2i5rkd ret;
};
struct gm_fr_canonical_7njb3e6 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_chars_232hkk6 {
    gm_frame h;
    uint64_t l1; /* n */
    uint64_t ret;
    gm_b0_1 w0;
};
struct gm_fr_close_dir_8brh3n1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_in_fnkagkd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_km5664u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_listener_26qaf3o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_ujrct9b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_connect_09fd74a {
    gm_frame h;
    gm_t62ebbi2i5rkd ret;
};
struct gm_fr_current_n0t6630 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_cwd_5pl4j79 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_fill_rj3q5to {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_d80h595 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_ptr_o6r20s1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_heap_alloc_3fj7sbi {
    gm_frame h;
    void * ret;
};
struct gm_fr_heap_allocator_gihki37 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_heap_free_5m809t7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_heap_resize_493jqcr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_io_error_h43olo2 {
    gm_frame h;
    gm_toe5fmr3vvf4d ret;
};
struct gm_fr_io_handle_ja5u8vm {
    gm_frame h;
    gm_t11mbkfrj7jag ret;
};
struct gm_fr_io_result_olh5erq {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_io_unit_ugl1g10 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_kill_mbomb45 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_kind_from_3c6rbh2 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_listen_etnhveu {
    gm_frame h;
    gm_t62ebbi2i5rkd ret;
};
struct gm_fr_local_port_472tpmj {
    gm_frame h;
    gm_tvcqs9u9iqpct ret;
};
struct gm_fr_lstat_iuafs2d {
    gm_frame h;
    gm_tih6h4avv5nrb ret;
};
struct gm_fr_make_dir_n7ma24t {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_next_entry_cv8e316 {
    gm_frame h;
    gm_tiuq7k5hm0kak ret;
};
struct gm_fr_now_bonkgq9 {
    gm_frame h;
    gm_t4ce0jd3tdro4 ret;
};
struct gm_fr_open_dir_c8tj9tn {
    gm_frame h;
    gm_t62ebbi2i5rkd ret;
};
struct gm_fr_open_ebv8fp2 {
    gm_frame h;
    gm_t62ebbi2i5rkd ret;
};
struct gm_fr_peer_tsk9h0n {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_read_72hbrkd {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_read_jvbmtq1 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_read_link_dr0564a {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_read_nba4chq {
    gm_frame h;
    gm_tlbmih0s72in3 ret;
};
struct gm_fr_recv_g45gd8b {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_remove_0dgs92k {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_remove_dir_66s86sk {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_rename_6qkr5pp {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_runes_5kk1v3e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_seek_r2chmaj {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_send_39j26gc {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_set_mode_rr5vrev {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_set_modified_52hvf3d {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_set_timeouts_dubfjtu {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_shutdown_96qqd59 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_sleep_d1vga5f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_spawn_d42ov39 {
    gm_frame h;
    gm_t32dckm4jfarl l2; /* o */
    gm_t62ebbi2i5rkd ret;
};
struct gm_fr_stat_ak3228i {
    gm_frame h;
    gm_tih6h4avv5nrb ret;
};
struct gm_fr_stdio_flags_p5ii4hd {
    gm_frame h;
    gm_t32dckm4jfarl l0; /* o */
    uint32_t ret;
};
struct gm_fr_symlink_6kqaken {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_sync_85b2nq9 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_system_io_mcjcl9m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_system_output_eg72lom {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_tag_78nb3u9 {
    gm_frame h;
    gm_tb40qn7hk0tl9 l2; /* site */
    gm_b0_1 ret;
};
struct gm_fr_timeout_ns_khm4a6r {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_track_alloc_ef9b553 {
    gm_frame h;
    void * ret;
};
struct gm_fr_track_free_n3n5osq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_track_resize_3vjj8df {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_truncate_47rrin9 {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_try_wait_qpdkkhq {
    gm_frame h;
    gm_tr6norheal11n ret;
};
struct gm_fr_wait_j3dkg61 {
    gm_frame h;
    gm_tlmjd3nndlf3r ret;
};
struct gm_fr_wall_fid7j1o {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_write_86jor9g {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_write_nkervm9 {
    gm_frame h;
    gm_thf5kr0is9u9i ret;
};
struct gm_fr_write_p0k32d5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_tltks68 {
    gm_frame h;
    gm_b0_1 ret;
};
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_accept_lrhmdkt(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_canonical_7njb3e6(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_dir_8brh3n1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_in_fnkagkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_km5664u(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_listener_26qaf3o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_ujrct9b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_connect_09fd74a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint16_t a2);
inline __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_current_n0t6630(void *env, gm_node *cx, gm_node *inst, gm_t3mf58gv9kf3n a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_cwd_5pl4j79(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fill_rj3q5to(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_58mj7o5(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
inline __attribute__((aligned(4))) void * gm_f_get_ptr_o6r20s1(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void * gm_f_heap_alloc_3fj7sbi(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
inline __attribute__((aligned(4))) void gm_f_heap_free_5m809t7(void * a0, void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) uint8_t gm_f_heap_resize_493jqcr(void * a0, void * a1, uint64_t a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) gm_toe5fmr3vvf4d gm_f_io_error_h43olo2(int64_t a0);
inline __attribute__((aligned(4))) gm_t11mbkfrj7jag gm_f_io_handle_ja5u8vm(int64_t a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_io_result_olh5erq(int64_t a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_io_unit_ugl1g10(int64_t a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_kill_mbomb45(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_kind_from_3c6rbh2(int64_t a0);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_listen_etnhveu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint16_t a2);
__attribute__((aligned(4))) gm_tvcqs9u9iqpct gm_f_local_port_472tpmj(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) void gm_f_lstat_iuafs2d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_n7ma24t(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_next_entry_cv8e316(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t4ce0jd3tdro4 gm_f_now_bonkgq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_open_dir_c8tj9tn(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_open_ebv8fp2(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_t4q2b5ibsrdik a2);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_peer_tsk9h0n(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_6kg1l5a(void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_72hbrkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_jvbmtq1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_link_dr0564a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_read_nba4chq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_recv_g45gd8b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_0dgs92k(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_dir_66s86sk(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_remove_f5r5ejv(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_rename_6qkr5pp(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_runes_5kk1v3e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_runes_5kk1v3e_ssomsr2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_seek_r2chmaj(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, int64_t a2, gm_t9qkh0rsooc8d a3);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_send_39j26gc(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_mode_rr5vrev(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_modified_52hvf3d(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, int64_t a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_timeouts_dubfjtu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_shutdown_96qqd59(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sleep_d1vga5f(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_d42ov39(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_stat_ak3228i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_stdio_flags_p5ii4hd(const void * a0);
__attribute__((aligned(4))) uint32_t gm_f_stdio_flags_p5ii4hd_c(gm_t32dckm4jfarl a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_symlink_6kqaken(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_sync_85b2nq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void);
__attribute__((aligned(4))) void gm_f_tag_78nb3u9(void * a0, gm_cs6mnftt383dnt a1, const void * a2);
__attribute__((aligned(4))) void gm_f_tag_78nb3u9_c(void * a0, gm_cs6mnftt383dnt a1, gm_tb40qn7hk0tl9 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_timeout_ns_khm4a6r(gm_t6ie8iajlnpt1 a0);
__attribute__((aligned(4))) void * gm_f_track_alloc_ef9b553(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_track_free_n3n5osq(void * a0, void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) uint8_t gm_f_track_resize_3vjj8df(void * a0, void * a1, uint64_t a2, uint64_t a3, uint64_t a4);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_truncate_47rrin9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, uint64_t a2);
__attribute__((aligned(4))) gm_tr6norheal11n gm_f_try_wait_qpdkkhq(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) gm_tlmjd3nndlf3r gm_f_wait_j3dkg61(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) int64_t gm_f_wall_fid7j1o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_86jor9g(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_nkervm9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_p0k32d5(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_tltks68(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
extern uint8_t gm_cf807b860br[24] __attribute__((aligned(8)));
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgr7vbgb5cqmg[];
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_canonical_7njb3e6(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_canonical_7njb3e6 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = gm_l2;
    gm_b16_8 t4 = t3;
    int64_t t5 = gm_fs_canonical((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), (void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8));
    gm_thf5kr0is9u9i t6 = gm_f_io_result_olh5erq(t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_dir_8brh3n1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_dir_8brh3n1 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* d */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_fs_close_dir(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_in_fnkagkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_in_fnkagkd F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_proc_close_in(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_km5664u(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_km5664u F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_fs_close(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_listener_26qaf3o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_listener_26qaf3o F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* l */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_net_close(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_ujrct9b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_close_ujrct9b F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
            int64_t t4 = gm_net_close(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_current_n0t6630(void *env, gm_node *cx, gm_node *inst, gm_t3mf58gv9kf3n a0) {
    struct gm_fr_current_n0t6630 F;
    gm_t3mf58gv9kf3n gm_l0; /* self */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3mf58gv9kf3n t1 = gm_l0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_cwd_5pl4j79(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_cwd_5pl4j79 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    int64_t t3 = gm_fs_cwd((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fill_rj3q5to(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_fill_rj3q5to F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    gm_rand_fill((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_for_body_d80h595 F;
    uint32_t gm_l0; /* <item> */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint32_t t2 = t1;
    {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 206, 25);
        uint64_t t9 = r7;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1), t9);
    }
    goto me3;
mn4:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 206, 5);
me3:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_get_ptr_o6r20s1(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ptr_o6r20s1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_58mj7o5(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 127, 13);
        void * t13 = (void *)(((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 72) + 16);
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_heap_alloc_3fj7sbi(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_alloc_3fj7sbi F;
    void * gm_l0; /* data */
    uint64_t gm_l1; /* size */
    uint64_t gm_l2; /* align */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = gm_l2;
    if ((t2) == 0 || ((t2) & ((t2) - 1)) != 0) gm_trap("allocation alignment is not a power of two", gm_src2ah81i3aa8hm, 603, 65);
    void * t3 = gm_heap_alloc(t1, t2);
    void * t4 = t3;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_allocator_gihki37 F;
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t1 = (void *)((uint8_t *)gm_cgd1khc5sal);
    void * t2 = (void *)((uint8_t *)gm_cgd1khc5sal);
    gm_t3mf58gv9kf3n t3;
    GM_ST(void *, ((uint8_t *)&t3 + 0), t1);
    GM_ST(void *, ((uint8_t *)&t3 + 8), t2);
    F.ret = t3;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_heap_free_5m809t7(void * a0, void * a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_free_5m809t7 F;
    void * gm_l0; /* data */
    void * gm_l1; /* p */
    uint64_t gm_l2; /* size */
    uint64_t gm_l3; /* align */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        uint64_t t3 = gm_l2;
        uint64_t t4 = gm_l3;
        gm_heap_free(t2, t3, t4, gm_src2ah81i3aa8hm, 608, 5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_heap_resize_493jqcr(void * a0, void * a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_resize_493jqcr F;
    void * gm_l0; /* data */
    void * gm_l1; /* p */
    uint64_t gm_l2; /* old */
    uint64_t gm_l3; /* new */
    uint64_t gm_l4; /* align */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    F.ret = ((uint8_t)0);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t11mbkfrj7jag gm_f_io_handle_ja5u8vm(int64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_io_handle_ja5u8vm F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        int64_t t3 = gm_l0;
        uint8_t t4 = (t3 >= ((int64_t)0LL));
        if (!t4) goto ie2;
        {
            int64_t t6 = gm_l0;
            gm_t11mbkfrj7jag t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t7 + 8), t6);
            gm_t11mbkfrj7jag t8 = t7;
            uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        int64_t t10 = gm_l0;
        gm_toe5fmr3vvf4d t11 = gm_f_io_error_h43olo2(t10);
        gm_t11mbkfrj7jag t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
        ((uint8_t *)&t12)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t12 + 8), t11);
        gm_t11mbkfrj7jag t13 = t12;
        uint8_t t14 = ((((uint8_t *)&t13))[0] != 0);
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_io_result_olh5erq(int64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_io_result_olh5erq F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        int64_t t3 = gm_l0;
        uint8_t t4 = (t3 >= ((int64_t)0LL));
        if (!t4) goto ie2;
        {
            int64_t t6 = gm_l0;
            uint64_t t7 = (uint64_t)t6;
            gm_thf5kr0is9u9i t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t8 + 8), t7);
            gm_thf5kr0is9u9i t9 = t8;
            uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        int64_t t11 = gm_l0;
        gm_toe5fmr3vvf4d t12 = gm_f_io_error_h43olo2(t11);
        gm_thf5kr0is9u9i t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        ((uint8_t *)&t13)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t13 + 8), t12);
        gm_thf5kr0is9u9i t14 = t13;
        uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_io_unit_ugl1g10(int64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_io_unit_ugl1g10 F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            int64_t t3 = gm_l0;
            gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
            gm_thf5kr0is9u9i t5 = t4;
            if (((((uint8_t *)&t5))[0] != 0)) {
                gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
                gm_ttln0d0hd70en t7;
                GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
                ((uint8_t *)&t7)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 4), t6);
                F.ret = t7;
                goto gm_fret;
            }
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
        }
    }
    {
        gm_b0_1 t9;
        gm_ttln0d0hd70en t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 8, 0);
        ((uint8_t *)&t10)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t10 + 4), t9);
        gm_ttln0d0hd70en t11 = t10;
        uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
        F.ret = t10;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_kill_mbomb45(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_kill_mbomb45 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    int64_t t2 = gm_proc_kill(t1);
    gm_ttln0d0hd70en t3 = gm_f_io_unit_ugl1g10(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_kind_from_3c6rbh2(int64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_from_3c6rbh2 F;
    int64_t gm_l0; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        int64_t t2 = gm_l0;
        int64_t t3 = t2;
        gm_t9qkh0rsooc8d t4;
        int64_t t7 = GM_LD(int64_t, ((uint8_t *)&t3));
        if (!(t7 == ((int64_t)0LL))) goto mn6;
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
        t4 = t8;
        goto me5;
mn6:;
        int64_t t10 = GM_LD(int64_t, ((uint8_t *)&t3));
        if (!(t10 == ((int64_t)1LL))) goto mn9;
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)1ULL));
        t4 = t11;
        goto me5;
mn9:;
        int64_t t13 = GM_LD(int64_t, ((uint8_t *)&t3));
        if (!(t13 == ((int64_t)2LL))) goto mn12;
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)2ULL));
        t4 = t14;
        goto me5;
mn12:;
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)3ULL));
        t4 = t16;
        goto me5;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_src2ah81i3aa8hm, 271, 12);
me5:;
        uint8_t t17 = 0;
        F.ret = t4;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_n7ma24t(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_make_dir_n7ma24t F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    int64_t t3 = gm_fs_make_dir((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t4ce0jd3tdro4 gm_f_now_bonkgq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0) {
    struct gm_fr_now_bonkgq9 F;
    gm_b0_1 gm_l0; /* self */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_clock_now();
    gm_t4ce0jd3tdro4 t2;
    GM_ST(uint64_t, ((uint8_t *)&t2 + 0), t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_peer_tsk9h0n(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_peer_tsk9h0n F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    gm_b16_8 t3 = t2;
    int64_t t4 = gm_net_peer(t1, (void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8));
    gm_thf5kr0is9u9i t5 = gm_f_io_result_olh5erq(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_72hbrkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_read_72hbrkd F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    gm_b16_8 t3 = t2;
    int64_t t4 = gm_fs_read(t1, (void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8));
    gm_thf5kr0is9u9i t5 = gm_f_io_result_olh5erq(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_jvbmtq1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_read_jvbmtq1 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    int64_t t3 = gm_sys_read((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_link_dr0564a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_read_link_dr0564a F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = gm_l2;
    gm_b16_8 t4 = t3;
    int64_t t5 = gm_fs_read_link((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), (void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8));
    gm_thf5kr0is9u9i t6 = gm_f_io_result_olh5erq(t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_recv_g45gd8b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_recv_g45gd8b F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_b16_8 gm_l2; /* buf */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    gm_b16_8 t3 = t2;
    int64_t t4 = gm_net_recv(t1, (void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8));
    gm_thf5kr0is9u9i t5 = gm_f_io_result_olh5erq(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_0dgs92k(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_remove_0dgs92k F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    int64_t t3 = gm_fs_remove((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_dir_66s86sk(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_remove_dir_66s86sk F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    int64_t t3 = gm_fs_remove_dir((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_rename_6qkr5pp(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_rename_6qkr5pp F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* from */
    gm_b16_8 gm_l2; /* to */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = gm_l2;
    gm_b16_8 t4 = t3;
    int64_t t5 = gm_fs_rename((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), (void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8));
    gm_ttln0d0hd70en t6 = gm_f_io_unit_ugl1g10(t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_send_39j26gc(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_send_39j26gc F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_b16_8 gm_l2; /* bytes */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    gm_b16_8 t3 = t2;
    int64_t t4 = gm_net_send(t1, (void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8));
    gm_thf5kr0is9u9i t5 = gm_f_io_result_olh5erq(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_mode_rr5vrev(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint32_t a2) {
    struct gm_fr_set_mode_rr5vrev F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    uint32_t gm_l2; /* mode */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    uint32_t t3 = gm_l2;
    int64_t t4 = gm_fs_set_mode((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), t3);
    gm_ttln0d0hd70en t5 = gm_f_io_unit_ugl1g10(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_modified_52hvf3d(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, int64_t a2) {
    struct gm_fr_set_modified_52hvf3d F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    int64_t gm_l2; /* ns */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    int64_t t3 = gm_l2;
    int64_t t4 = gm_fs_set_modified((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), t3);
    gm_ttln0d0hd70en t5 = gm_f_io_unit_ugl1g10(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_timeouts_dubfjtu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3) {
    struct gm_fr_set_timeouts_dubfjtu F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_t6ie8iajlnpt1 gm_l2; /* recv */
    gm_t6ie8iajlnpt1 gm_l3; /* send */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_t6ie8iajlnpt1 t2 = gm_l2;
    uint64_t t3 = gm_f_timeout_ns_khm4a6r(t2);
    gm_t6ie8iajlnpt1 t4 = gm_l3;
    uint64_t t5 = gm_f_timeout_ns_khm4a6r(t4);
    int64_t t6 = gm_net_set_timeouts(t1, t3, t5);
    gm_ttln0d0hd70en t7 = gm_f_io_unit_ugl1g10(t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_shutdown_96qqd59(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_shutdown_96qqd59 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* s */
    gm_t9qkh0rsooc8d gm_l2; /* how */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_t9qkh0rsooc8d t2 = gm_l2;
    gm_t9qkh0rsooc8d t3 = t2;
    uint8_t t4 = GM_LD(uint8_t, ((uint8_t *)&t3));
    uint32_t t5 = (uint32_t)t4;
    int64_t t6 = gm_net_shutdown(t1, t5);
    gm_ttln0d0hd70en t7 = gm_f_io_unit_ugl1g10(t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sleep_d1vga5f(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_t4ce0jd3tdro4 a1) {
    struct gm_fr_sleep_d1vga5f F;
    gm_b0_1 gm_l0; /* self */
    gm_t4ce0jd3tdro4 gm_l1; /* d */
    GM_STACK_CHECK(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
    gm_clock_sleep(t1);
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_symlink_6kqaken(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_symlink_6kqaken F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* target */
    gm_b16_8 gm_l2; /* path */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    gm_b16_8 t3 = gm_l2;
    gm_b16_8 t4 = t3;
    int64_t t5 = gm_fs_symlink((void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), (void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8));
    gm_ttln0d0hd70en t6 = gm_f_io_unit_ugl1g10(t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_sync_85b2nq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_sync_85b2nq9 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    int64_t t2 = gm_fs_sync(t1);
    gm_ttln0d0hd70en t3 = gm_f_io_unit_ugl1g10(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_io_mcjcl9m F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b0_1 t1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_output_eg72lom F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b0_1 t1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_timeout_ns_khm4a6r(gm_t6ie8iajlnpt1 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_timeout_ns_khm4a6r F;
    gm_t6ie8iajlnpt1 gm_l0; /* d */
    gm_t4ce0jd3tdro4 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        if (!((((uint8_t *)&gm_l0))[8] != 0)) goto ie2;
        gm_t4ce0jd3tdro4 t3 = GM_LD(gm_t4ce0jd3tdro4, ((uint8_t *)&gm_l0));
        gm_l1 = t3;
        {
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            uint64_t t6 = (t5 > ((uint64_t)1ULL) ? t5 : ((uint64_t)1ULL));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t8 = 0;
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_track_resize_3vjj8df(void * a0, void * a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_track_resize_3vjj8df F;
    void * gm_l0; /* data */
    void * gm_l1; /* p */
    uint64_t gm_l2; /* old */
    uint64_t gm_l3; /* new */
    uint64_t gm_l4; /* align */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    F.ret = ((uint8_t)0);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_truncate_47rrin9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, uint64_t a2) {
    struct gm_fr_truncate_47rrin9 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    uint64_t gm_l2; /* size */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = gm_l2;
    int64_t t3 = gm_fs_truncate(t1, t2);
    gm_ttln0d0hd70en t4 = gm_f_io_unit_ugl1g10(t3);
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_wall_fid7j1o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0) {
    struct gm_fr_wall_fid7j1o F;
    gm_b0_1 gm_l0; /* self */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1 = gm_clock_wall();
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_86jor9g(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_write_86jor9g F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    gm_b16_8 gm_l2; /* bytes */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    gm_b16_8 t3 = t2;
    int64_t t4 = gm_proc_write_in(t1, (void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8));
    gm_thf5kr0is9u9i t5 = gm_f_io_result_olh5erq(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_nkervm9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_write_nkervm9 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    gm_b16_8 gm_l2; /* bytes */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    int64_t t1 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
    gm_b16_8 t2 = gm_l2;
    gm_b16_8 t3 = t2;
    int64_t t4 = gm_fs_write(t1, (void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8));
    gm_thf5kr0is9u9i t5 = gm_f_io_result_olh5erq(t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_p0k32d5(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_write_p0k32d5 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* bytes */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    gm_sys_write(((int32_t)2LL), (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_tltks68(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_write_tltks68 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* bytes */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_b16_8 t2 = t1;
    gm_sys_write(((int32_t)1LL), (void *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8));
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_accept_lrhmdkt(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_accept_lrhmdkt F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* l */
    int64_t gm_l2; /* h */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        int64_t t2 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
        int64_t t3 = gm_net_accept(t2);
        gm_t11mbkfrj7jag t4 = gm_f_io_handle_ja5u8vm(t3);
        gm_t11mbkfrj7jag t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_t62ebbi2i5rkd t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        int64_t t8 = GM_LD(int64_t, (((uint8_t *)&t5) + 8));
        gm_l2 = t8;
    }
    int64_t t9 = gm_l2;
    gm_tm98ap4112hee t10;
    GM_ST(int64_t, ((uint8_t *)&t10 + 0), t9);
    gm_t62ebbi2i5rkd t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
    ((uint8_t *)&t11)[0] = 0;
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t11 + 8), t10);
    gm_t62ebbi2i5rkd t12 = t11;
    uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_canonical_7njb3e6(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_close_dir_8brh3n1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_close_in_fnkagkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_close_km5664u(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_close_listener_26qaf3o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_close_ujrct9b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
inline __attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_connect_09fd74a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint16_t a2) {
    struct gm_fr_connect_09fd74a F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* host */
    uint16_t gm_l2; /* port */
    int64_t gm_l3; /* h */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = t2;
        uint16_t t4 = gm_l2;
        int64_t t5 = gm_net_connect((void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8), t4);
        gm_t11mbkfrj7jag t6 = gm_f_io_handle_ja5u8vm(t5);
        gm_t11mbkfrj7jag t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_toe5fmr3vvf4d t8 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t7) + 8));
            gm_t62ebbi2i5rkd t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        int64_t t10 = GM_LD(int64_t, (((uint8_t *)&t7) + 8));
        gm_l3 = t10;
    }
    int64_t t11 = gm_l3;
    gm_tm98ap4112hee t12;
    GM_ST(int64_t, ((uint8_t *)&t12 + 0), t11);
    gm_t62ebbi2i5rkd t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
    ((uint8_t *)&t13)[0] = 0;
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t13 + 8), t12);
    gm_t62ebbi2i5rkd t14 = t13;
    uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_current_n0t6630(void *env, gm_node *cx, gm_node *inst, gm_t3mf58gv9kf3n a0);
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_cwd_5pl4j79(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fill_rj3q5to(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) void * gm_f_heap_alloc_3fj7sbi(void * a0, uint64_t a1, uint64_t a2);
extern __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
extern __attribute__((aligned(4))) void gm_f_heap_free_5m809t7(void * a0, void * a1, uint64_t a2, uint64_t a3);
extern __attribute__((aligned(4))) uint8_t gm_f_heap_resize_493jqcr(void * a0, void * a1, uint64_t a2, uint64_t a3, uint64_t a4);
inline __attribute__((aligned(4))) gm_toe5fmr3vvf4d gm_f_io_error_h43olo2(int64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_io_error_h43olo2 F;
    int64_t gm_l0; /* n */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        int64_t t3 = gm_l0;
        uint8_t t4 = (t3 <= ((int64_t)-1000LL));
        if (!t4) goto ie2;
        {
            int64_t t6 = gm_l0;
            int64_t r7; bool o8 = __builtin_sub_overflow(((int64_t)-1000LL), t6, &r7);
            if (o8) gm_trap("integer overflow in `-`", gm_src2ah81i3aa8hm, 230, 36);
            int64_t t9 = r7;
            int32_t t10 = (int32_t)t9;
            gm_toe5fmr3vvf4d t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)13ULL));
            GM_ST(int32_t, ((uint8_t *)&t11 + 4), t10);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        int64_t t13 = gm_l0;
        int64_t r14; bool o15 = __builtin_sub_overflow(((int64_t)-1LL), t13, &r14);
        if (o15) gm_trap("integer overflow in `-`", gm_src2ah81i3aa8hm, 231, 24);
        int64_t t16 = r14;
        int64_t t17 = t16;
        gm_toe5fmr3vvf4d t18;
        int64_t t21 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t21 == ((int64_t)0LL))) goto mn20;
        gm_toe5fmr3vvf4d t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        t18 = t22;
        goto me19;
mn20:;
        int64_t t24 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t24 == ((int64_t)1LL))) goto mn23;
        gm_toe5fmr3vvf4d t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)1ULL));
        t18 = t25;
        goto me19;
mn23:;
        int64_t t27 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t27 == ((int64_t)2LL))) goto mn26;
        gm_toe5fmr3vvf4d t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
        t18 = t28;
        goto me19;
mn26:;
        int64_t t30 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t30 == ((int64_t)3LL))) goto mn29;
        gm_toe5fmr3vvf4d t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)3ULL));
        t18 = t31;
        goto me19;
mn29:;
        int64_t t33 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t33 == ((int64_t)4LL))) goto mn32;
        gm_toe5fmr3vvf4d t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)4ULL));
        t18 = t34;
        goto me19;
mn32:;
        int64_t t36 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t36 == ((int64_t)5LL))) goto mn35;
        gm_toe5fmr3vvf4d t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)5ULL));
        t18 = t37;
        goto me19;
mn35:;
        int64_t t39 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t39 == ((int64_t)6LL))) goto mn38;
        gm_toe5fmr3vvf4d t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)6ULL));
        t18 = t40;
        goto me19;
mn38:;
        int64_t t42 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t42 == ((int64_t)7LL))) goto mn41;
        gm_toe5fmr3vvf4d t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)7ULL));
        t18 = t43;
        goto me19;
mn41:;
        int64_t t45 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t45 == ((int64_t)8LL))) goto mn44;
        gm_toe5fmr3vvf4d t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)8ULL));
        t18 = t46;
        goto me19;
mn44:;
        int64_t t48 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t48 == ((int64_t)9LL))) goto mn47;
        gm_toe5fmr3vvf4d t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)9ULL));
        t18 = t49;
        goto me19;
mn47:;
        int64_t t51 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t51 == ((int64_t)10LL))) goto mn50;
        gm_toe5fmr3vvf4d t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)10ULL));
        t18 = t52;
        goto me19;
mn50:;
        int64_t t54 = GM_LD(int64_t, ((uint8_t *)&t17));
        if (!(t54 == ((int64_t)11LL))) goto mn53;
        gm_toe5fmr3vvf4d t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)11ULL));
        t18 = t55;
        goto me19;
mn53:;
        gm_toe5fmr3vvf4d t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)12ULL));
        t18 = t57;
        goto me19;
mn56:;
        gm_trap("no match arm matched the scrutinee", gm_src2ah81i3aa8hm, 231, 18);
me19:;
        gm_l1 = t18;
    }
    {
        gm_toe5fmr3vvf4d t58 = gm_l1;
        uint8_t t59 = 0;
        F.ret = t58;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t11mbkfrj7jag gm_f_io_handle_ja5u8vm(int64_t a0);
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_io_result_olh5erq(int64_t a0);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_io_unit_ugl1g10(int64_t a0);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_kill_mbomb45(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_kind_from_3c6rbh2(int64_t a0);
inline __attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_listen_etnhveu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint16_t a2) {
    struct gm_fr_listen_etnhveu F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* host */
    uint16_t gm_l2; /* port */
    int64_t gm_l3; /* h */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = t2;
        uint16_t t4 = gm_l2;
        int64_t t5 = gm_net_listen((void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8), t4);
        gm_t11mbkfrj7jag t6 = gm_f_io_handle_ja5u8vm(t5);
        gm_t11mbkfrj7jag t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_toe5fmr3vvf4d t8 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t7) + 8));
            gm_t62ebbi2i5rkd t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        int64_t t10 = GM_LD(int64_t, (((uint8_t *)&t7) + 8));
        gm_l3 = t10;
    }
    int64_t t11 = gm_l3;
    gm_tm98ap4112hee t12;
    GM_ST(int64_t, ((uint8_t *)&t12 + 0), t11);
    gm_t62ebbi2i5rkd t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
    ((uint8_t *)&t13)[0] = 0;
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t13 + 8), t12);
    gm_t62ebbi2i5rkd t14 = t13;
    uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tvcqs9u9iqpct gm_f_local_port_472tpmj(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_local_port_472tpmj F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* l */
    uint64_t gm_l2; /* p */
    GM_STACK_CHECK(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        int64_t t2 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
        int64_t t3 = gm_net_local_port(t2);
        gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
        gm_thf5kr0is9u9i t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_tvcqs9u9iqpct t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 4), t6);
            F.ret = t7;
            goto gm_fret;
        }
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
        gm_l2 = t8;
    }
    uint64_t t9 = gm_l2;
    uint16_t t10 = (uint16_t)t9;
    gm_tvcqs9u9iqpct t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 8, 0);
    ((uint8_t *)&t11)[0] = 0;
    GM_ST(uint16_t, ((uint8_t *)&t11 + 4), t10);
    gm_tvcqs9u9iqpct t12 = t11;
    uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lstat_iuafs2d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_lstat_iuafs2d F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    gm_t767tucha9lsh gm_l2; /* out */
    GM_STACK_CHECK(&F, 406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_b16_8 t4 = t3;
            void * t5 = ((uint8_t *)&gm_l2);
            if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_src2ah81i3aa8hm, 311, 43);
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t5 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
            gm_b16_8 t7 = t6;
            int64_t t8 = gm_fs_lstat((void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8), (void *)GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t7) + 8));
            gm_thf5kr0is9u9i t9 = gm_f_io_result_olh5erq(t8);
            gm_thf5kr0is9u9i t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) {
                gm_toe5fmr3vvf4d t11 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t10) + 8));
                gm_tih6h4avv5nrb t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t12 + 32, 0);
                ((uint8_t *)&t12)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t12 + 8), t11);
                F.ret = t12;
                goto gm_fret;
            }
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&t10) + 8));
        }
    }
    gm_t5j14m8ru132p t14;
    memset((uint8_t *)&t14 + 9, 0, 7);
    memset((uint8_t *)&t14 + 28, 0, 4);
    uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
    if (t15 >= (uintptr_t)(4)) gm_trap_index(t15, 4, gm_src2ah81i3aa8hm, 312, 31);
    int64_t t16 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t15 * 8));
    uint64_t t17 = (uint64_t)t16;
    GM_ST(uint64_t, (uint8_t *)&t14 + 0, t17);
    uint64_t t18 = (uintptr_t)((uint64_t)1ULL);
    if (t18 >= (uintptr_t)(4)) gm_trap_index(t18, 4, gm_src2ah81i3aa8hm, 312, 66);
    int64_t t19 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t18 * 8));
    gm_t9qkh0rsooc8d t20 = gm_f_kind_from_3c6rbh2(t19);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t14 + 8, t20);
    uint64_t t21 = (uintptr_t)((uint64_t)2ULL);
    if (t21 >= (uintptr_t)(4)) gm_trap_index(t21, 4, gm_src2ah81i3aa8hm, 312, 85);
    int64_t t22 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t21 * 8));
    GM_ST(int64_t, (uint8_t *)&t14 + 16, t22);
    uint64_t t23 = (uintptr_t)((uint64_t)3ULL);
    if (t23 >= (uintptr_t)(4)) gm_trap_index(t23, 4, gm_src2ah81i3aa8hm, 312, 99);
    int64_t t24 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t23 * 8));
    uint32_t t25 = (uint32_t)t24;
    GM_ST(uint32_t, (uint8_t *)&t14 + 24, t25);
    gm_tih6h4avv5nrb t26;
    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
    ((uint8_t *)&t26)[0] = 0;
    GM_ST(gm_t5j14m8ru132p, ((uint8_t *)&t26 + 8), t14);
    gm_tih6h4avv5nrb t27 = t26;
    uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_n7ma24t(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_next_entry_cv8e316(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_next_entry_cv8e316 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* d */
    gm_b16_8 gm_l2; /* buf */
    int64_t gm_l3; /* r */
    GM_STACK_CHECK(&F, 412u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        int64_t t2 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t3 = gm_l2;
        gm_b16_8 t4 = t3;
        int64_t t5 = gm_fs_next_entry(t2, (void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8));
        gm_l3 = t5;
    }
    {
        {
            int64_t t7 = gm_l3;
            gm_thf5kr0is9u9i t8 = gm_f_io_result_olh5erq(t7);
            gm_thf5kr0is9u9i t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) {
                gm_toe5fmr3vvf4d t10 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t9) + 8));
                gm_tiuq7k5hm0kak t11;
                GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
                ((uint8_t *)&t11)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t11 + 8), t10);
                F.ret = t11;
                goto gm_fret;
            }
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&t9) + 8));
        }
    }
    {
        int64_t t14 = gm_l3;
        uint8_t t15 = ((t14 == ((int64_t)0LL)));
        if (!t15) goto ie13;
        {
            gm_t11t9d8s5diq9 t17;
            memset(&t17, 0, sizeof t17);
            gm_tiuq7k5hm0kak t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(gm_t11t9d8s5diq9, ((uint8_t *)&t18 + 8), t17);
            gm_tiuq7k5hm0kak t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    int64_t t21 = gm_l3;
    int64_t t22 = (int64_t)(t21 & ((int64_t)4294967295LL));
    uint64_t t23 = (uint64_t)t22;
    int64_t t24 = gm_l3;
    unsigned __int128 n25 = (unsigned __int128)((uint64_t)32ULL);
    if (n25 >= 64) gm_trap_shift((uint64_t)n25, "i64", gm_src2ah81i3aa8hm, 327, 78);
    int64_t t26 = (int64_t)(t24 >> (int)n25);
    gm_t9qkh0rsooc8d t27 = gm_f_kind_from_3c6rbh2(t26);
    gm_t617vr72bd7b3 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t23);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 8), t27);
    gm_t11t9d8s5diq9 t29;
    GM_ST(uint8_t, (uint8_t *)&t29 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t29 + 20, 0);
    GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t29), t28);
    ((uint8_t *)&t29)[16] = 1;
    gm_tiuq7k5hm0kak t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
    ((uint8_t *)&t30)[0] = 0;
    GM_ST(gm_t11t9d8s5diq9, ((uint8_t *)&t30 + 8), t29);
    gm_tiuq7k5hm0kak t31 = t30;
    uint8_t t32 = ((((uint8_t *)&t31))[0] != 0);
    F.ret = t30;
    goto gm_fret;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_t4ce0jd3tdro4 gm_f_now_bonkgq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0);
inline __attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_open_dir_c8tj9tn(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_open_dir_c8tj9tn F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    int64_t gm_l2; /* h */
    GM_STACK_CHECK(&F, 209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3 = t2;
        int64_t t4 = gm_fs_open_dir((void *)GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8));
        gm_t11mbkfrj7jag t5 = gm_f_io_handle_ja5u8vm(t4);
        gm_t11mbkfrj7jag t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t6) + 8));
            gm_t62ebbi2i5rkd t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        int64_t t9 = GM_LD(int64_t, (((uint8_t *)&t6) + 8));
        gm_l2 = t9;
    }
    int64_t t10 = gm_l2;
    gm_tm98ap4112hee t11;
    GM_ST(int64_t, ((uint8_t *)&t11 + 0), t10);
    gm_t62ebbi2i5rkd t12;
    GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
    ((uint8_t *)&t12)[0] = 0;
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t12 + 8), t11);
    gm_t62ebbi2i5rkd t13 = t12;
    uint8_t t14 = ((((uint8_t *)&t13))[0] != 0);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_open_ebv8fp2(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_t4q2b5ibsrdik a2) {
    struct gm_fr_open_ebv8fp2 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    gm_t4q2b5ibsrdik gm_l2; /* o */
    uint32_t gm_l3; /* flags */
    int64_t gm_l4; /* h */
    GM_STACK_CHECK(&F, 277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint32_t)0ULL);
    }
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (!t3) goto ie2;
        {
            uint32_t t5 = gm_l3;
            uint32_t t6 = (uint32_t)(t5 | ((uint32_t)1ULL));
            gm_l3 = t6;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 1));
        if (!t8) goto ie7;
        {
            uint32_t t10 = gm_l3;
            uint32_t t11 = (uint32_t)(t10 | ((uint32_t)2ULL));
            gm_l3 = t11;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 2));
        if (!t13) goto ie12;
        {
            uint32_t t15 = gm_l3;
            uint32_t t16 = (uint32_t)(t15 | ((uint32_t)4ULL));
            gm_l3 = t16;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 3));
        if (!t18) goto ie17;
        {
            uint32_t t20 = gm_l3;
            uint32_t t21 = (uint32_t)(t20 | ((uint32_t)8ULL));
            gm_l3 = t21;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 4));
        if (!t23) goto ie22;
        {
            uint32_t t25 = gm_l3;
            uint32_t t26 = (uint32_t)(t25 | ((uint32_t)16ULL));
            gm_l3 = t26;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 5));
        if (!t28) goto ie27;
        {
            uint32_t t30 = gm_l3;
            uint32_t t31 = (uint32_t)(t30 | ((uint32_t)32ULL));
            gm_l3 = t31;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_b16_8 t32 = gm_l1;
        gm_b16_8 t33 = t32;
        uint32_t t34 = gm_l3;
        int64_t t35 = gm_fs_open((void *)GM_LD(void *, ((uint8_t *)&t33)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8), t34);
        gm_t11mbkfrj7jag t36 = gm_f_io_handle_ja5u8vm(t35);
        gm_t11mbkfrj7jag t37 = t36;
        if (((((uint8_t *)&t37))[0] != 0)) {
            gm_toe5fmr3vvf4d t38 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t37) + 8));
            gm_t62ebbi2i5rkd t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            ((uint8_t *)&t39)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t39 + 8), t38);
            F.ret = t39;
            goto gm_fret;
        }
        int64_t t40 = GM_LD(int64_t, (((uint8_t *)&t37) + 8));
        gm_l4 = t40;
    }
    int64_t t41 = gm_l4;
    gm_tm98ap4112hee t42;
    GM_ST(int64_t, ((uint8_t *)&t42 + 0), t41);
    gm_t62ebbi2i5rkd t43;
    GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
    ((uint8_t *)&t43)[0] = 0;
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t43 + 8), t42);
    gm_t62ebbi2i5rkd t44 = t43;
    uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
    F.ret = t43;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_peer_tsk9h0n(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_72hbrkd(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_jvbmtq1(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_read_link_dr0564a(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_read_nba4chq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2) {
    struct gm_fr_read_nba4chq F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    gm_b16_8 gm_l2; /* buf */
    uint64_t gm_l3; /* n */
    gm_t9qkh0rsooc8d gm_l4; /* from */
    GM_STACK_CHECK(&F, 403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        int64_t t2 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t3 = gm_l2;
        gm_b16_8 t4 = t3;
        int64_t t5 = gm_proc_read_out(t2, (void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8));
        gm_thf5kr0is9u9i t6 = gm_f_io_result_olh5erq(t5);
        gm_thf5kr0is9u9i t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_toe5fmr3vvf4d t8 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t7) + 8));
            gm_tlbmih0s72in3 t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&t7) + 8));
        gm_l3 = t10;
    }
    {
        uint64_t t12 = gm_l3;
        uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
        if (!t13) goto ie11;
        {
            gm_thdrvp9q0u7b5 t15;
            memset(&t15, 0, sizeof t15);
            gm_tlbmih0s72in3 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            ((uint8_t *)&t16)[0] = 0;
            GM_ST(gm_thdrvp9q0u7b5, ((uint8_t *)&t16 + 8), t15);
            gm_tlbmih0s72in3 t17 = t16;
            uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
            F.ret = t16;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_t9qkh0rsooc8d t19;
        uint64_t t21 = gm_l3;
        unsigned __int128 n22 = (unsigned __int128)((uint64_t)32ULL);
        if (n22 >= 64) gm_trap_shift((uint64_t)n22, "usize", gm_src2ah81i3aa8hm, 390, 28);
        uint64_t t23 = (uint64_t)(t21 >> (int)n22);
        uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
        if (!t24) goto ie20;
        gm_t9qkh0rsooc8d t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t27 = t26;
        t19 = t27;
        goto ix20;
ie20:;
        gm_t9qkh0rsooc8d t29;
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)1ULL));
        gm_t9qkh0rsooc8d t30 = t29;
        t19 = t30;
ix20:;
        gm_l4 = t19;
    }
    gm_t9qkh0rsooc8d t31 = gm_l4;
    uint64_t t32 = gm_l3;
    uint64_t t33 = (uint64_t)(t32 & ((uint64_t)4294967295ULL));
    gm_te0mgnjdphs16 t34;
    GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 0), t31);
    GM_ST(uint64_t, ((uint8_t *)&t34 + 8), t33);
    gm_thdrvp9q0u7b5 t35;
    GM_ST(uint8_t, (uint8_t *)&t35 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t35 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t35 + 20, 0);
    GM_ST(gm_te0mgnjdphs16, ((uint8_t *)&t35), t34);
    ((uint8_t *)&t35)[16] = 1;
    gm_tlbmih0s72in3 t36;
    GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
    ((uint8_t *)&t36)[0] = 0;
    GM_ST(gm_thdrvp9q0u7b5, ((uint8_t *)&t36 + 8), t35);
    gm_tlbmih0s72in3 t37 = t36;
    uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
    F.ret = t36;
    goto gm_fret;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_recv_g45gd8b(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_0dgs92k(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_dir_66s86sk(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_rename_6qkr5pp(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_runes_5kk1v3e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_runes_5kk1v3e F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* b */
    uint64_t gm_l3; /* n */
    uint8_t gm_l4; /* lo */
    uint8_t gm_l5; /* hi */
    uint32_t gm_l6; /* c */
    uint64_t gm_l7; /* k */
    uint8_t gm_l8; /* d */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l1;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_src2ah81i3aa8hm, 527, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l2 = t11;
            }
            {
                uint8_t t13 = gm_l2;
                uint8_t t14 = (t13 < ((uint8_t)128ULL));
                if (!t14) goto ie12;
                {
                    uint8_t t16 = gm_l2;
                    uint32_t t17 = (uint32_t)t16;
                    gm_node *pn18 = gm_find(cx, 78927826);
                    if (!pn18) gm_trap("no handler installed for effect `Yield`", gm_src2ah81i3aa8hm, 529, 13);
                    gm_b0_1 t19;
                    t19 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint32_t))pn18->clauses[0])(pn18->env, pn18->hctx, pn18, t17);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
                {
                    uint64_t t20 = gm_l1;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    if (o22) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 530, 13);
                    uint64_t t23 = r21;
                    gm_l1 = t23;
                }
                {
                    goto lc2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                gm_l4 = ((uint8_t)128ULL);
            }
            {
                gm_l5 = ((uint8_t)191ULL);
            }
            {
                gm_l6 = ((uint32_t)0ULL);
            }
            {
                uint8_t t25 = gm_l2;
                uint8_t t26 = (t25 >= ((uint8_t)194ULL));
                if (!t26) goto ie24;
                uint8_t t27 = gm_l2;
                uint8_t t28 = (t27 <= ((uint8_t)223ULL));
                if (!t28) goto ie24;
                {
                    gm_l3 = ((uint64_t)1ULL);
                }
                {
                    uint8_t t30 = gm_l2;
                    uint8_t t31 = (uint8_t)(t30 & ((uint8_t)31ULL));
                    uint32_t t32 = (uint32_t)t31;
                    gm_l6 = t32;
                }
                goto ix24;
ie24:;
                uint8_t t34 = gm_l2;
                uint8_t t35 = (t34 >= ((uint8_t)224ULL));
                if (!t35) goto ie33;
                uint8_t t36 = gm_l2;
                uint8_t t37 = (t36 <= ((uint8_t)239ULL));
                if (!t37) goto ie33;
                {
                    gm_l3 = ((uint64_t)2ULL);
                }
                {
                    uint8_t t39 = gm_l2;
                    uint8_t t40 = (uint8_t)(t39 & ((uint8_t)15ULL));
                    uint32_t t41 = (uint32_t)t40;
                    gm_l6 = t41;
                }
                {
                    uint8_t t43 = gm_l2;
                    uint8_t t44 = ((t43 == ((uint8_t)224ULL)));
                    if (!t44) goto ie42;
                    {
                        gm_l4 = ((uint8_t)160ULL);
                    }
                    goto ix42;
ie42:;
ix42:;
                }
                {
                    uint8_t t47 = gm_l2;
                    uint8_t t48 = ((t47 == ((uint8_t)237ULL)));
                    if (!t48) goto ie46;
                    {
                        gm_l5 = ((uint8_t)159ULL);
                    }
                    goto ix46;
ie46:;
ix46:;
                }
                goto ix33;
ie33:;
                uint8_t t51 = gm_l2;
                uint8_t t52 = (t51 >= ((uint8_t)240ULL));
                if (!t52) goto ie50;
                uint8_t t53 = gm_l2;
                uint8_t t54 = (t53 <= ((uint8_t)244ULL));
                if (!t54) goto ie50;
                {
                    gm_l3 = ((uint64_t)3ULL);
                }
                {
                    uint8_t t56 = gm_l2;
                    uint8_t t57 = (uint8_t)(t56 & ((uint8_t)7ULL));
                    uint32_t t58 = (uint32_t)t57;
                    gm_l6 = t58;
                }
                {
                    uint8_t t60 = gm_l2;
                    uint8_t t61 = ((t60 == ((uint8_t)240ULL)));
                    if (!t61) goto ie59;
                    {
                        gm_l4 = ((uint8_t)144ULL);
                    }
                    goto ix59;
ie59:;
ix59:;
                }
                {
                    uint8_t t64 = gm_l2;
                    uint8_t t65 = ((t64 == ((uint8_t)244ULL)));
                    if (!t65) goto ie63;
                    {
                        gm_l5 = ((uint8_t)143ULL);
                    }
                    goto ix63;
ie63:;
ix63:;
                }
                goto ix50;
ie50:;
                {
                    gm_node *pn68 = gm_find(cx, 78927826);
                    if (!pn68) gm_trap("no handler installed for effect `Yield`", gm_src2ah81i3aa8hm, 552, 13);
                    gm_b0_1 t69;
                    t69 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint32_t))pn68->clauses[0])(pn68->env, pn68->hctx, pn68, ((uint32_t)65533ULL));
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
                {
                    uint64_t t70 = gm_l1;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    if (o72) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 553, 13);
                    uint64_t t73 = r71;
                    gm_l1 = t73;
                }
                {
                    goto lc2;
                }
ix50:;
ix33:;
ix24:;
            }
            {
                gm_l7 = ((uint64_t)1ULL);
            }
            {
                for (;;) {
                    uint64_t t75 = gm_l7;
                    uint64_t t76 = gm_l3;
                    uint8_t t77 = (t75 <= t76);
                    if (!t77) goto lb74;
                    {
                        uint64_t t81 = gm_l1;
                        uint64_t t82 = gm_l7;
                        uint64_t r83; bool o84 = __builtin_add_overflow(t81, t82, &r83);
                        if (o84) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 558, 16);
                        uint64_t t85 = r83;
                        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t87 = (t85 >= t86);
                        if (!t87) goto ie80;
                        {
                            goto lb74;
                        }
                        goto ix80;
ie80:;
ix80:;
                    }
                    {
                        uint64_t t89 = gm_l1;
                        uint64_t t90 = gm_l7;
                        uint64_t r91; bool o92 = __builtin_add_overflow(t89, t90, &r91);
                        if (o92) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 559, 20);
                        uint64_t t93 = r91;
                        uint64_t t94 = (uintptr_t)t93;
                        uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t94 >= (uintptr_t)(t95)) gm_trap_index(t94, t95, gm_src2ah81i3aa8hm, 559, 18);
                        uint8_t t96 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t94 * 1));
                        gm_l8 = t96;
                    }
                    {
                        uint8_t t98 = 0;
                        uint8_t t99 = gm_l8;
                        uint8_t t100 = gm_l4;
                        uint8_t t101 = (t99 < t100);
                        if (!t101) {
                            uint8_t t102 = gm_l8;
                            uint8_t t103 = gm_l5;
                            uint8_t t104 = (t102 > t103);
                            t98 = t104;
                        } else t98 = 1;
                        if (!t98) goto ie97;
                        {
                            goto lb74;
                        }
                        goto ix97;
ie97:;
ix97:;
                    }
                    {
                        uint32_t t106 = gm_l6;
                        unsigned __int128 n107 = (unsigned __int128)((uint64_t)6ULL);
                        if (n107 >= 32) gm_trap_shift((uint64_t)n107, "u32", gm_src2ah81i3aa8hm, 561, 18);
                        uint32_t r108 = (uint32_t)((uint32_t)t106 << (int)n107);
                        bool o109 = (uint32_t)(r108 >> (int)n107) != t106;
                        if (o109) gm_trap("integer overflow in `<<`", gm_src2ah81i3aa8hm, 561, 18);
                        uint32_t t110 = r108;
                        uint8_t t111 = gm_l8;
                        uint8_t t112 = (uint8_t)(t111 & ((uint8_t)63ULL));
                        uint32_t t113 = (uint32_t)t112;
                        uint32_t t114 = (uint32_t)(t110 | t113);
                        gm_l6 = t114;
                    }
                    {
                        gm_l4 = ((uint8_t)128ULL);
                    }
                    {
                        gm_l5 = ((uint8_t)191ULL);
                    }
                    {
                        uint64_t t115 = gm_l7;
                        uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                        if (o117) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 564, 13);
                        uint64_t t118 = r116;
                        gm_l7 = t118;
                    }
lc74:;
                }
lb74:;
            }
            {
                uint64_t t120 = gm_l7;
                uint64_t t121 = gm_l3;
                uint8_t t122 = (t120 > t121);
                if (!t122) goto ie119;
                {
                    uint32_t t124 = gm_l6;
                    gm_node *pn125 = gm_find(cx, 78927826);
                    if (!pn125) gm_trap("no handler installed for effect `Yield`", gm_src2ah81i3aa8hm, 567, 13);
                    gm_b0_1 t126;
                    t126 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint32_t))pn125->clauses[0])(pn125->env, pn125->hctx, pn125, t124);
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
                goto ix119;
ie119:;
                {
                    gm_node *pn128 = gm_find(cx, 78927826);
                    if (!pn128) gm_trap("no handler installed for effect `Yield`", gm_src2ah81i3aa8hm, 569, 13);
                    gm_b0_1 t129;
                    t129 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, uint32_t))pn128->clauses[0])(pn128->env, pn128->hctx, pn128, ((uint32_t)65533ULL));
                    if (gm_unw.kind) {
                        goto gm_ll6;
                    }
                }
ix119:;
            }
            {
                uint64_t t130 = gm_l1;
                uint64_t t131 = gm_l7;
                uint64_t r132; bool o133 = __builtin_add_overflow(t130, t131, &r132);
                if (o133) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 571, 9);
                uint64_t t134 = r132;
                gm_l1 = t134;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_runes_5kk1v3e_ssomsr2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_runes_5kk1v3e F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* b */
    uint64_t gm_l3; /* n */
    uint8_t gm_l4; /* lo */
    uint8_t gm_l5; /* hi */
    uint32_t gm_l6; /* c */
    uint64_t gm_l7; /* k */
    uint8_t gm_l8; /* d */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l1;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_src2ah81i3aa8hm, 527, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l2 = t11;
            }
            {
                uint8_t t13 = gm_l2;
                uint8_t t14 = (t13 < ((uint8_t)128ULL));
                if (!t14) goto ie12;
                {
                    uint8_t t16 = gm_l2;
                    uint32_t t17 = (uint32_t)t16;
                    gm_b0_1 t18 = gm_f_for_body_d80h595(cx->env, cx->hctx, cx, t17);
                }
                {
                    uint64_t t19 = gm_l1;
                    uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                    if (o21) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 530, 13);
                    uint64_t t22 = r20;
                    gm_l1 = t22;
                }
                {
                    goto lc2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                gm_l4 = ((uint8_t)128ULL);
            }
            {
                gm_l5 = ((uint8_t)191ULL);
            }
            {
                gm_l6 = ((uint32_t)0ULL);
            }
            {
                uint8_t t24 = gm_l2;
                uint8_t t25 = (t24 >= ((uint8_t)194ULL));
                if (!t25) goto ie23;
                uint8_t t26 = gm_l2;
                uint8_t t27 = (t26 <= ((uint8_t)223ULL));
                if (!t27) goto ie23;
                {
                    gm_l3 = ((uint64_t)1ULL);
                }
                {
                    uint8_t t29 = gm_l2;
                    uint8_t t30 = (uint8_t)(t29 & ((uint8_t)31ULL));
                    uint32_t t31 = (uint32_t)t30;
                    gm_l6 = t31;
                }
                goto ix23;
ie23:;
                uint8_t t33 = gm_l2;
                uint8_t t34 = (t33 >= ((uint8_t)224ULL));
                if (!t34) goto ie32;
                uint8_t t35 = gm_l2;
                uint8_t t36 = (t35 <= ((uint8_t)239ULL));
                if (!t36) goto ie32;
                {
                    gm_l3 = ((uint64_t)2ULL);
                }
                {
                    uint8_t t38 = gm_l2;
                    uint8_t t39 = (uint8_t)(t38 & ((uint8_t)15ULL));
                    uint32_t t40 = (uint32_t)t39;
                    gm_l6 = t40;
                }
                {
                    uint8_t t42 = gm_l2;
                    uint8_t t43 = ((t42 == ((uint8_t)224ULL)));
                    if (!t43) goto ie41;
                    {
                        gm_l4 = ((uint8_t)160ULL);
                    }
                    goto ix41;
ie41:;
ix41:;
                }
                {
                    uint8_t t46 = gm_l2;
                    uint8_t t47 = ((t46 == ((uint8_t)237ULL)));
                    if (!t47) goto ie45;
                    {
                        gm_l5 = ((uint8_t)159ULL);
                    }
                    goto ix45;
ie45:;
ix45:;
                }
                goto ix32;
ie32:;
                uint8_t t50 = gm_l2;
                uint8_t t51 = (t50 >= ((uint8_t)240ULL));
                if (!t51) goto ie49;
                uint8_t t52 = gm_l2;
                uint8_t t53 = (t52 <= ((uint8_t)244ULL));
                if (!t53) goto ie49;
                {
                    gm_l3 = ((uint64_t)3ULL);
                }
                {
                    uint8_t t55 = gm_l2;
                    uint8_t t56 = (uint8_t)(t55 & ((uint8_t)7ULL));
                    uint32_t t57 = (uint32_t)t56;
                    gm_l6 = t57;
                }
                {
                    uint8_t t59 = gm_l2;
                    uint8_t t60 = ((t59 == ((uint8_t)240ULL)));
                    if (!t60) goto ie58;
                    {
                        gm_l4 = ((uint8_t)144ULL);
                    }
                    goto ix58;
ie58:;
ix58:;
                }
                {
                    uint8_t t63 = gm_l2;
                    uint8_t t64 = ((t63 == ((uint8_t)244ULL)));
                    if (!t64) goto ie62;
                    {
                        gm_l5 = ((uint8_t)143ULL);
                    }
                    goto ix62;
ie62:;
ix62:;
                }
                goto ix49;
ie49:;
                {
                    gm_b0_1 t67 = gm_f_for_body_d80h595(cx->env, cx->hctx, cx, ((uint32_t)65533ULL));
                }
                {
                    uint64_t t68 = gm_l1;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    if (o70) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 553, 13);
                    uint64_t t71 = r69;
                    gm_l1 = t71;
                }
                {
                    goto lc2;
                }
ix49:;
ix32:;
ix23:;
            }
            {
                gm_l7 = ((uint64_t)1ULL);
            }
            {
                for (;;) {
                    uint64_t t73 = gm_l7;
                    uint64_t t74 = gm_l3;
                    uint8_t t75 = (t73 <= t74);
                    if (!t75) goto lb72;
                    {
                        uint64_t t79 = gm_l1;
                        uint64_t t80 = gm_l7;
                        uint64_t r81; bool o82 = __builtin_add_overflow(t79, t80, &r81);
                        if (o82) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 558, 16);
                        uint64_t t83 = r81;
                        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t85 = (t83 >= t84);
                        if (!t85) goto ie78;
                        {
                            goto lb72;
                        }
                        goto ix78;
ie78:;
ix78:;
                    }
                    {
                        uint64_t t87 = gm_l1;
                        uint64_t t88 = gm_l7;
                        uint64_t r89; bool o90 = __builtin_add_overflow(t87, t88, &r89);
                        if (o90) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 559, 20);
                        uint64_t t91 = r89;
                        uint64_t t92 = (uintptr_t)t91;
                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_src2ah81i3aa8hm, 559, 18);
                        uint8_t t94 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t92 * 1));
                        gm_l8 = t94;
                    }
                    {
                        uint8_t t96 = 0;
                        uint8_t t97 = gm_l8;
                        uint8_t t98 = gm_l4;
                        uint8_t t99 = (t97 < t98);
                        if (!t99) {
                            uint8_t t100 = gm_l8;
                            uint8_t t101 = gm_l5;
                            uint8_t t102 = (t100 > t101);
                            t96 = t102;
                        } else t96 = 1;
                        if (!t96) goto ie95;
                        {
                            goto lb72;
                        }
                        goto ix95;
ie95:;
ix95:;
                    }
                    {
                        uint32_t t104 = gm_l6;
                        unsigned __int128 n105 = (unsigned __int128)((uint64_t)6ULL);
                        if (n105 >= 32) gm_trap_shift((uint64_t)n105, "u32", gm_src2ah81i3aa8hm, 561, 18);
                        uint32_t r106 = (uint32_t)((uint32_t)t104 << (int)n105);
                        bool o107 = (uint32_t)(r106 >> (int)n105) != t104;
                        if (o107) gm_trap("integer overflow in `<<`", gm_src2ah81i3aa8hm, 561, 18);
                        uint32_t t108 = r106;
                        uint8_t t109 = gm_l8;
                        uint8_t t110 = (uint8_t)(t109 & ((uint8_t)63ULL));
                        uint32_t t111 = (uint32_t)t110;
                        uint32_t t112 = (uint32_t)(t108 | t111);
                        gm_l6 = t112;
                    }
                    {
                        gm_l4 = ((uint8_t)128ULL);
                    }
                    {
                        gm_l5 = ((uint8_t)191ULL);
                    }
                    {
                        uint64_t t113 = gm_l7;
                        uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                        if (o115) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 564, 13);
                        uint64_t t116 = r114;
                        gm_l7 = t116;
                    }
lc72:;
                }
lb72:;
            }
            {
                uint64_t t118 = gm_l7;
                uint64_t t119 = gm_l3;
                uint8_t t120 = (t118 > t119);
                if (!t120) goto ie117;
                {
                    uint32_t t122 = gm_l6;
                    gm_b0_1 t123 = gm_f_for_body_d80h595(cx->env, cx->hctx, cx, t122);
                }
                goto ix117;
ie117:;
                {
                    gm_b0_1 t125 = gm_f_for_body_d80h595(cx->env, cx->hctx, cx, ((uint32_t)65533ULL));
                }
ix117:;
            }
            {
                uint64_t t126 = gm_l1;
                uint64_t t127 = gm_l7;
                uint64_t r128; bool o129 = __builtin_add_overflow(t126, t127, &r128);
                if (o129) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 571, 9);
                uint64_t t130 = r128;
                gm_l1 = t130;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_seek_r2chmaj(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, int64_t a2, gm_t9qkh0rsooc8d a3) {
    struct gm_fr_seek_r2chmaj F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* f */
    int64_t gm_l2; /* offset */
    gm_t9qkh0rsooc8d gm_l3; /* from */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        int64_t t2 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
        int64_t t3 = gm_l2;
        gm_t9qkh0rsooc8d t4 = gm_l3;
        gm_t9qkh0rsooc8d t5 = t4;
        uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&t5));
        uint32_t t7 = (uint32_t)t6;
        int64_t t8 = gm_fs_seek(t2, t3, t7);
        gm_thf5kr0is9u9i t9 = gm_f_io_result_olh5erq(t8);
        gm_thf5kr0is9u9i t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) {
            gm_toe5fmr3vvf4d t11 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t10) + 8));
            gm_thf5kr0is9u9i t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t12 + 8), t11);
            F.ret = t12;
            goto gm_fret;
        }
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&t10) + 8));
        gm_l4 = t13;
    }
    uint64_t t14 = gm_l4;
    uint64_t t15 = (uint64_t)t14;
    gm_thf5kr0is9u9i t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    ((uint8_t *)&t16)[0] = 0;
    GM_ST(uint64_t, ((uint8_t *)&t16 + 8), t15);
    gm_thf5kr0is9u9i t17 = t16;
    uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
    F.ret = t16;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_send_39j26gc(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_mode_rr5vrev(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, uint32_t a2);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_modified_52hvf3d(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, int64_t a2);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_set_timeouts_dubfjtu(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_shutdown_96qqd59(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_sleep_d1vga5f(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_d42ov39(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_spawn_d42ov39 F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* argv */
    gm_b16_8 gm_l3; /* dir */
    gm_b16_8 gm_l4; /* program */
    int64_t gm_l5; /* h */
    GM_STACK_CHECK(&F, 583u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_toe5fmr3vvf4d t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)7ULL));
            gm_t62ebbi2i5rkd t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 8), t6);
            gm_t62ebbi2i5rkd t8 = t7;
            uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_th2lqbo77pvsc t10 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 8));
        gm_th2lqbo77pvsc t11 = t10;
        gm_b16_8 t12;
        if (((((uint8_t *)&t11))[16] != 0)) {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(0));
            t12 = t14;
        }
        gm_l3 = t12;
    }
    {
        gm_th2lqbo77pvsc t15 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 56));
        gm_th2lqbo77pvsc t16 = t15;
        gm_b16_8 t17;
        if (((((uint8_t *)&t16))[16] != 0)) {
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 0));
            t17 = t18;
        } else {
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(0));
            t17 = t19;
        }
        gm_l4 = t17;
    }
    {
        gm_b16_8 t20 = gm_l1;
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 32));
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = gm_l3;
        gm_b16_8 t25 = t24;
        gm_b16_8 t26 = gm_l4;
        gm_b16_8 t27 = t26;
        uint32_t t28 = gm_f_stdio_flags_p5ii4hd(&(*(gm_t32dckm4jfarl *)((uint8_t *)&F.l2)));
        int64_t t29 = gm_proc_launch((void *)GM_LD(void *, ((uint8_t *)&t21)), GM_LD(uintptr_t, ((uint8_t *)&t21) + 8), (void *)GM_LD(void *, ((uint8_t *)&t23)), GM_LD(uintptr_t, ((uint8_t *)&t23) + 8), (void *)GM_LD(void *, ((uint8_t *)&t25)), GM_LD(uintptr_t, ((uint8_t *)&t25) + 8), (void *)GM_LD(void *, ((uint8_t *)&t27)), GM_LD(uintptr_t, ((uint8_t *)&t27) + 8), t28);
        gm_t11mbkfrj7jag t30 = gm_f_io_handle_ja5u8vm(t29);
        gm_t11mbkfrj7jag t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) {
            gm_toe5fmr3vvf4d t32 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t31) + 8));
            gm_t62ebbi2i5rkd t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t33 + 8), t32);
            F.ret = t33;
            goto gm_fret;
        }
        int64_t t34 = GM_LD(int64_t, (((uint8_t *)&t31) + 8));
        gm_l5 = t34;
    }
    int64_t t35 = gm_l5;
    gm_tm98ap4112hee t36;
    GM_ST(int64_t, ((uint8_t *)&t36 + 0), t35);
    gm_t62ebbi2i5rkd t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
    ((uint8_t *)&t37)[0] = 0;
    GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t37 + 8), t36);
    gm_t62ebbi2i5rkd t38 = t37;
    uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
    F.ret = t37;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_stat_ak3228i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1) {
    struct gm_fr_stat_ak3228i F;
    gm_b0_1 gm_l0; /* self */
    gm_b16_8 gm_l1; /* path */
    gm_t767tucha9lsh gm_l2; /* out */
    GM_STACK_CHECK(&F, 406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_b16_8 t4 = t3;
            void * t5 = ((uint8_t *)&gm_l2);
            if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_src2ah81i3aa8hm, 306, 42);
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t5 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
            gm_b16_8 t7 = t6;
            int64_t t8 = gm_fs_stat((void *)GM_LD(void *, ((uint8_t *)&t4)), GM_LD(uintptr_t, ((uint8_t *)&t4) + 8), (void *)GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t7) + 8));
            gm_thf5kr0is9u9i t9 = gm_f_io_result_olh5erq(t8);
            gm_thf5kr0is9u9i t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) {
                gm_toe5fmr3vvf4d t11 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t10) + 8));
                gm_tih6h4avv5nrb t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t12 + 32, 0);
                ((uint8_t *)&t12)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t12 + 8), t11);
                F.ret = t12;
                goto gm_fret;
            }
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&t10) + 8));
        }
    }
    gm_t5j14m8ru132p t14;
    memset((uint8_t *)&t14 + 9, 0, 7);
    memset((uint8_t *)&t14 + 28, 0, 4);
    uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
    if (t15 >= (uintptr_t)(4)) gm_trap_index(t15, 4, gm_src2ah81i3aa8hm, 307, 31);
    int64_t t16 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t15 * 8));
    uint64_t t17 = (uint64_t)t16;
    GM_ST(uint64_t, (uint8_t *)&t14 + 0, t17);
    uint64_t t18 = (uintptr_t)((uint64_t)1ULL);
    if (t18 >= (uintptr_t)(4)) gm_trap_index(t18, 4, gm_src2ah81i3aa8hm, 307, 66);
    int64_t t19 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t18 * 8));
    gm_t9qkh0rsooc8d t20 = gm_f_kind_from_3c6rbh2(t19);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t14 + 8, t20);
    uint64_t t21 = (uintptr_t)((uint64_t)2ULL);
    if (t21 >= (uintptr_t)(4)) gm_trap_index(t21, 4, gm_src2ah81i3aa8hm, 307, 85);
    int64_t t22 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t21 * 8));
    GM_ST(int64_t, (uint8_t *)&t14 + 16, t22);
    uint64_t t23 = (uintptr_t)((uint64_t)3ULL);
    if (t23 >= (uintptr_t)(4)) gm_trap_index(t23, 4, gm_src2ah81i3aa8hm, 307, 99);
    int64_t t24 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + t23 * 8));
    uint32_t t25 = (uint32_t)t24;
    GM_ST(uint32_t, (uint8_t *)&t14 + 24, t25);
    gm_tih6h4avv5nrb t26;
    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
    ((uint8_t *)&t26)[0] = 0;
    GM_ST(gm_t5j14m8ru132p, ((uint8_t *)&t26 + 8), t14);
    gm_tih6h4avv5nrb t27 = t26;
    uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
    F.ret = t26;
    goto gm_fret;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_stdio_flags_p5ii4hd(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stdio_flags_p5ii4hd F;
    uint32_t gm_l1; /* flags */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9qkh0rsooc8d t2 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 0));
        gm_t9qkh0rsooc8d t3 = t2;
        uint8_t t4 = GM_LD(uint8_t, ((uint8_t *)&t3));
        uint32_t t5 = (uint32_t)t4;
        gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 1));
        gm_t9qkh0rsooc8d t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t7));
        uint32_t t9 = (uint32_t)t8;
        unsigned __int128 n10 = (unsigned __int128)((uint64_t)2ULL);
        if (n10 >= 32) gm_trap_shift((uint64_t)n10, "u32", gm_src2ah81i3aa8hm, 370, 42);
        uint32_t r11 = (uint32_t)((uint32_t)t9 << (int)n10);
        bool o12 = (uint32_t)(r11 >> (int)n10) != t9;
        if (o12) gm_trap("integer overflow in `<<`", gm_src2ah81i3aa8hm, 370, 42);
        uint32_t t13 = r11;
        uint32_t t14 = (uint32_t)(t5 | t13);
        gm_t9qkh0rsooc8d t15 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 2));
        gm_t9qkh0rsooc8d t16 = t15;
        uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t16));
        uint32_t t18 = (uint32_t)t17;
        unsigned __int128 n19 = (unsigned __int128)((uint64_t)4ULL);
        if (n19 >= 32) gm_trap_shift((uint64_t)n19, "u32", gm_src2ah81i3aa8hm, 370, 77);
        uint32_t r20 = (uint32_t)((uint32_t)t18 << (int)n19);
        bool o21 = (uint32_t)(r20 >> (int)n19) != t18;
        if (o21) gm_trap("integer overflow in `<<`", gm_src2ah81i3aa8hm, 370, 77);
        uint32_t t22 = r20;
        uint32_t t23 = (uint32_t)(t14 | t22);
        gm_l1 = t23;
    }
    {
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 48));
        if (!t25) goto ie24;
        {
            uint32_t t27 = gm_l1;
            uint32_t t28 = (uint32_t)(t27 | ((uint32_t)64ULL));
            gm_l1 = t28;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 49));
        if (!t30) goto ie29;
        {
            uint32_t t32 = gm_l1;
            uint32_t t33 = (uint32_t)(t32 | ((uint32_t)128ULL));
            gm_l1 = t33;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        uint8_t t35;
        if ((((((uint8_t *)a0) + 56))[16] != 0)) goto mn37;
        t35 = ((uint8_t)0);
        goto me36;
mn37:;
        t35 = ((uint8_t)1);
        goto me36;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_src2ah81i3aa8hm, 373, 8);
me36:;
        if (!t35) goto ie34;
        {
            uint32_t t40 = gm_l1;
            uint32_t t41 = (uint32_t)(t40 | ((uint32_t)256ULL));
            gm_l1 = t41;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        uint32_t t42 = gm_l1;
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint32_t gm_f_stdio_flags_p5ii4hd_c(gm_t32dckm4jfarl a0) { return gm_f_stdio_flags_p5ii4hd(&a0); }
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_symlink_6kqaken(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_sync_85b2nq9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_system_output_eg72lom(void);
inline __attribute__((aligned(4))) void gm_f_tag_78nb3u9(void * a0, gm_cs6mnftt383dnt a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tag_78nb3u9 F;
    void * gm_l0; /* raw */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l3; /* t */
    void * gm_l4; /* b */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t3mf58gv9kf3n, a1);
    {
    }
    {
        void * t3 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
        void * t4 = t3;
        void * t5 = (void *)((uint8_t *)gm_cf807b860br);
        void * t6 = t5;
        uint8_t t7 = ((t4 == t6));
        if (!t7) goto ie2;
        {
            void * t9 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            if ((uintptr_t)t9 % 8 != 0) gm_trap_misaligned((uintptr_t)t9, 8, gm_src2ah81i3aa8hm, 628, 14);
            void * t10 = t9;
            gm_l3 = t10;
        }
        {
            void * t12 = gm_l3;
            void * t13 = (void *)(((uint8_t *)t12) + 24);
            void * t14 = gm_l0;
            uint64_t t15 = (uintptr_t)t14;
            void * t16 = gm_f_get_ptr_o6r20s1(t13, t15);
            void * t17 = t16;
            if (!(GM_LD(void *, ((uint8_t *)&t17)) != 0)) goto ie11;
            void * t18 = GM_LD(void *, ((uint8_t *)&t17));
            gm_l4 = t18;
            {
                void * t20 = gm_l4;
                gm_tb40qn7hk0tl9 t21 = GM_LD(gm_tb40qn7hk0tl9, ((uint8_t *)a2));
                gm_th6bh7gc364t4 t22;
                GM_ST(uint8_t, (uint8_t *)&t22 + 41, 0);
                GM_ST(uint16_t, (uint8_t *)&t22 + 42, 0);
                GM_ST(uint32_t, (uint8_t *)&t22 + 44, 0);
                GM_ST(gm_tb40qn7hk0tl9, ((uint8_t *)&t22), t21);
                ((uint8_t *)&t22)[40] = 1;
                GM_ST(gm_th6bh7gc364t4, (((uint8_t *)t20) + 8), t22);
            }
            goto ix11;
ie11:;
ix11:;
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
__attribute__((aligned(4))) void gm_f_tag_78nb3u9_c(void * a0, gm_cs6mnftt383dnt a1, gm_tb40qn7hk0tl9 a2) { gm_f_tag_78nb3u9(a0, a1, &a2); }
extern __attribute__((aligned(4))) uint64_t gm_f_timeout_ns_khm4a6r(gm_t6ie8iajlnpt1 a0);
inline __attribute__((aligned(4))) void * gm_f_track_alloc_ef9b553(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_track_alloc_ef9b553 F;
    void * gm_l0; /* data */
    uint64_t gm_l1; /* size */
    uint64_t gm_l2; /* align */
    void * gm_l3; /* t */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 366u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_src2ah81i3aa8hm, 803, 10);
        void * t3 = t2;
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l3;
        void * t5 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = gm_l3;
        void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 0));
        uint64_t t9 = gm_l1;
        uint64_t t10 = gm_l2;
        void * t11 = t6;
        void * t12;
        t12 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t11))(t8, t9, t10);
        void * t13 = t12;
        void * t14;
        if ((GM_LD(void *, ((uint8_t *)&t13)) != 0)) {
            void * t15 = GM_LD(void *, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            uint8_t t16 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l4 = t14;
    }
    {
        void * t17 = gm_l3;
        void * t18 = gm_l3;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 0));
        uint64_t t20 = gm_l1;
        uint64_t r21; bool o22 = __builtin_add_overflow(t19, t20, &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_src2ah81i3aa8hm, 805, 5);
        uint64_t t23 = r21;
        GM_ST(uint64_t, (((uint8_t *)t17) + 0), t23);
    }
    {
        void * t24 = gm_l3;
        void * t25 = (void *)(((uint8_t *)t24) + 24);
        void * t26 = gm_l4;
        uint64_t t27 = (uintptr_t)t26;
        gm_ts5u7codfocvn t28;
        uint64_t t29 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t28 + 0, t29);
        gm_th6bh7gc364t4 t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(gm_th6bh7gc364t4, (uint8_t *)&t28 + 8, t30);
        gm_tftb1odc739t5 t31 = gm_f_put_6kg1l5a(t25, t27, &t28);
        gm_tftb1odc739t5 t32 = t31;
        gm_b0_1 t33;
        if (!((((uint8_t *)&t32))[0] != 0)) {
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            t33 = t34;
        } else {
            t33 = GM_ZERO(gm_b0_1);
        }
    }
    {
        void * t36 = gm_l4;
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_track_free_n3n5osq(void * a0, void * a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_track_free_n3n5osq F;
    void * gm_l0; /* data */
    void * gm_l1; /* p */
    uint64_t gm_l2; /* size */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* t */
    GM_STACK_CHECK(&F, 264u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        if ((uintptr_t)t2 % 8 != 0) gm_trap_misaligned((uintptr_t)t2, 8, gm_src2ah81i3aa8hm, 813, 10);
        void * t3 = t2;
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l4;
        void * t5 = gm_l4;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 0));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_src2ah81i3aa8hm, 814, 5);
        uint64_t t10 = r8;
        GM_ST(uint64_t, (((uint8_t *)t4) + 0), t10);
    }
    {
        {
            void * t12 = gm_l4;
            void * t13 = (void *)(((uint8_t *)t12) + 24);
            void * t14 = gm_l1;
            uint64_t t15 = (uintptr_t)t14;
            gm_tl9turq9ndgsc t16;
            gm_f_remove_f5r5ejv((void *)&t16, t13, t15);
        }
    }
    {
        void * t17 = gm_l4;
        void * t18 = GM_LD(void *, ((((uint8_t *)t17) + 8) + 8));
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 16));
        void * t20 = gm_l4;
        void * t21 = GM_LD(void *, ((((uint8_t *)t20) + 8) + 0));
        void * t22 = gm_l1;
        uint64_t t23 = gm_l2;
        uint64_t t24 = gm_l3;
        void * t25 = t19;
        gm_b0_1 t26;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t25))(t21, t22, t23, t24);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint8_t gm_f_track_resize_3vjj8df(void * a0, void * a1, uint64_t a2, uint64_t a3, uint64_t a4);
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_truncate_47rrin9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_tr6norheal11n gm_f_try_wait_qpdkkhq(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_try_wait_qpdkkhq F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    uint64_t gm_l2; /* n */
    gm_tsoiri44d0i6q gm_l3; /* status */
    GM_STACK_CHECK(&F, 348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        int64_t t2 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
        int64_t t3 = gm_proc_try_wait(t2);
        gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
        gm_thf5kr0is9u9i t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_tr6norheal11n t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 12, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 4), t6);
            F.ret = t7;
            goto gm_fret;
        }
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
        gm_l2 = t8;
    }
    {
        uint64_t t10 = gm_l2;
        unsigned __int128 n11 = (unsigned __int128)((uint64_t)33ULL);
        if (n11 >= 64) gm_trap_shift((uint64_t)n11, "usize", gm_src2ah81i3aa8hm, 400, 13);
        uint64_t t12 = (uint64_t)(t10 >> (int)n11);
        uint8_t t13 = (!(t12 == ((uint64_t)0ULL)));
        if (!t13) goto ie9;
        {
            gm_tqnlpelep1582 t15;
            memset(&t15, 0, sizeof t15);
            gm_tr6norheal11n t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            ((uint8_t *)&t16)[0] = 0;
            GM_ST(gm_tqnlpelep1582, ((uint8_t *)&t16 + 4), t15);
            gm_tr6norheal11n t17 = t16;
            uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
            F.ret = t16;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_tsoiri44d0i6q t19;
        uint64_t t21 = gm_l2;
        unsigned __int128 n22 = (unsigned __int128)((uint64_t)32ULL);
        if (n22 >= 64) gm_trap_shift((uint64_t)n22, "usize", gm_src2ah81i3aa8hm, 401, 34);
        uint64_t t23 = (uint64_t)(t21 >> (int)n22);
        uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
        if (!t24) goto ie20;
        uint64_t t26 = gm_l2;
        uint64_t t27 = (uint64_t)(t26 & ((uint64_t)4294967295ULL));
        uint32_t t28 = (uint32_t)t27;
        gm_tsoiri44d0i6q t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t29 + 4), t28);
        gm_tsoiri44d0i6q t30 = t29;
        t19 = t30;
        goto ix20;
ie20:;
        uint64_t t32 = gm_l2;
        uint64_t t33 = (uint64_t)(t32 & ((uint64_t)4294967295ULL));
        int32_t t34 = (int32_t)t33;
        gm_tsoiri44d0i6q t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)1ULL));
        GM_ST(int32_t, ((uint8_t *)&t35 + 4), t34);
        gm_tsoiri44d0i6q t36 = t35;
        t19 = t36;
ix20:;
        gm_l3 = t19;
    }
    gm_tsoiri44d0i6q t37 = gm_l3;
    gm_tqnlpelep1582 t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t38 + 10, 0);
    GM_ST(gm_tsoiri44d0i6q, ((uint8_t *)&t38), t37);
    ((uint8_t *)&t38)[8] = 1;
    gm_tr6norheal11n t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
    ((uint8_t *)&t39)[0] = 0;
    GM_ST(gm_tqnlpelep1582, ((uint8_t *)&t39 + 4), t38);
    gm_tr6norheal11n t40 = t39;
    uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
    F.ret = t39;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tlmjd3nndlf3r gm_f_wait_j3dkg61(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1) {
    struct gm_fr_wait_j3dkg61 F;
    gm_b0_1 gm_l0; /* self */
    gm_tm98ap4112hee gm_l1; /* c */
    uint64_t gm_l2; /* n */
    gm_tsoiri44d0i6q gm_l3; /* status */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        int64_t t2 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 0));
        int64_t t3 = gm_proc_wait(t2);
        gm_thf5kr0is9u9i t4 = gm_f_io_result_olh5erq(t3);
        gm_thf5kr0is9u9i t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_toe5fmr3vvf4d t6 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_tlmjd3nndlf3r t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 4), t6);
            F.ret = t7;
            goto gm_fret;
        }
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
        gm_l2 = t8;
    }
    {
        gm_tsoiri44d0i6q t9;
        uint64_t t11 = gm_l2;
        unsigned __int128 n12 = (unsigned __int128)((uint64_t)32ULL);
        if (n12 >= 64) gm_trap_shift((uint64_t)n12, "usize", gm_src2ah81i3aa8hm, 395, 34);
        uint64_t t13 = (uint64_t)(t11 >> (int)n12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie10;
        uint64_t t16 = gm_l2;
        uint64_t t17 = (uint64_t)(t16 & ((uint64_t)4294967295ULL));
        uint32_t t18 = (uint32_t)t17;
        gm_tsoiri44d0i6q t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t19 + 4), t18);
        gm_tsoiri44d0i6q t20 = t19;
        t9 = t20;
        goto ix10;
ie10:;
        uint64_t t22 = gm_l2;
        uint64_t t23 = (uint64_t)(t22 & ((uint64_t)4294967295ULL));
        int32_t t24 = (int32_t)t23;
        gm_tsoiri44d0i6q t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)1ULL));
        GM_ST(int32_t, ((uint8_t *)&t25 + 4), t24);
        gm_tsoiri44d0i6q t26 = t25;
        t9 = t26;
ix10:;
        gm_l3 = t9;
    }
    gm_tsoiri44d0i6q t27 = gm_l3;
    gm_tlmjd3nndlf3r t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
    ((uint8_t *)&t28)[0] = 0;
    GM_ST(gm_tsoiri44d0i6q, ((uint8_t *)&t28 + 4), t27);
    gm_tlmjd3nndlf3r t29 = t28;
    uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_wall_fid7j1o(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0);
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_86jor9g(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_thf5kr0is9u9i gm_f_write_nkervm9(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_tm98ap4112hee a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_write_p0k32d5(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_write_tltks68(void *env, gm_node *cx, gm_node *inst, gm_b0_1 a0, gm_b16_8 a1);

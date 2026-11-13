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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { void * f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; void * f2; } gm_tf361jk9mn3il;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_b16_8 f0; gm_t5hdnnftkup6m f1; uint32_t f2; uint32_t f3; } gm_td1bmbv6cf6nk;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_t5hdnnftkup6m f1; gm_b16_8 f2; uint8_t f3; uint8_t pe[7]; } gm_tdi3m8srnr9m6;
_Static_assert(sizeof(gm_tf361jk9mn3il) == 24 && _Alignof(gm_tf361jk9mn3il) == 8 && offsetof(gm_tf361jk9mn3il, f0) == 0 && offsetof(gm_tf361jk9mn3il, f1) == 8 && offsetof(gm_tf361jk9mn3il, f2) == 16, "layout of `Ctx`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_th2lqbo77pvsc f3; } gm_tl5cuhjfnc4cr;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
typedef struct { gm_t5hdnnftkup6m f0; gm_t5hdnnftkup6m f1; } gm_tmo31gpro8pgo;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t8vuock3jnv59 v1; } f1; } gm_t9l835tjp727s;
_Static_assert(sizeof(gm_td1bmbv6cf6nk) == 48 && _Alignof(gm_td1bmbv6cf6nk) == 8 && offsetof(gm_td1bmbv6cf6nk, f0) == 0 && offsetof(gm_td1bmbv6cf6nk, f1) == 16 && offsetof(gm_td1bmbv6cf6nk, f2) == 40 && offsetof(gm_td1bmbv6cf6nk, f3) == 44, "layout of `LocalName`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tdi3m8srnr9m6) == 64 && _Alignof(gm_tdi3m8srnr9m6) == 8 && offsetof(gm_tdi3m8srnr9m6, f0) == 0 && offsetof(gm_tdi3m8srnr9m6, f1) == 16 && offsetof(gm_tdi3m8srnr9m6, f2) == 40 && offsetof(gm_tdi3m8srnr9m6, f3) == 56, "layout of `Target`");
typedef struct { gm_tl5cuhjfnc4cr f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tfgiovingnnea;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tl5cuhjfnc4cr) == 64 && _Alignof(gm_tl5cuhjfnc4cr) == 8 && offsetof(gm_tl5cuhjfnc4cr, f0) == 0 && offsetof(gm_tl5cuhjfnc4cr, f1) == 16 && offsetof(gm_tl5cuhjfnc4cr, f2) == 24 && offsetof(gm_tl5cuhjfnc4cr, f3) == 40, "layout of `Item`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tdi3m8srnr9m6 v1; } f1; } gm_tmkrnkep1qbgl;
_Static_assert(sizeof(gm_tmo31gpro8pgo) == 48 && _Alignof(gm_tmo31gpro8pgo) == 8 && offsetof(gm_tmo31gpro8pgo, f0) == 0 && offsetof(gm_tmo31gpro8pgo, f1) == 24, "layout of `RefPair`");
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_t9l835tjp727s) == 32 && _Alignof(gm_t9l835tjp727s) == 8 && offsetof(gm_t9l835tjp727s, f0) == 0 && offsetof(gm_t9l835tjp727s, f1) == 8, "layout of `string!Value`");
_Static_assert(sizeof(gm_tfgiovingnnea) == 88 && _Alignof(gm_tfgiovingnnea) == 8 && offsetof(gm_tfgiovingnnea, f0) == 0 && offsetof(gm_tfgiovingnnea, f1) == 64 && offsetof(gm_tfgiovingnnea, f2) == 76 && offsetof(gm_tfgiovingnnea, f3) == 80, "layout of `LocalScope`");
_Static_assert(sizeof(gm_tmkrnkep1qbgl) == 72 && _Alignof(gm_tmkrnkep1qbgl) == 8 && offsetof(gm_tmkrnkep1qbgl, f0) == 0 && offsetof(gm_tmkrnkep1qbgl, f1) == 8, "layout of `string!Target`");
struct gm_fr_check_clashes_cqluvhl {
    gm_frame h;
    gm_td1bmbv6cf6nk l14; /* me */
    gm_td1bmbv6cf6nk l17; /* l */
    gm_td1bmbv6cf6nk l26; /* l */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_editable_56hen53 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* roots */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_new_name_31k8n9d {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_declaration_at_lquhblp {
    gm_frame h;
    gm_tmkrnkep1qbgl ret;
};
struct gm_fr_find_m559mi8 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* modules */
    gm_trqfvmjtt4sij l3; /* roots */
    gm_trqfvmjtt4sij l11; /* uses */
    gm_tmo31gpro8pgo l17; /* r */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_fn_o57h82n {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_local_jek4n0p {
    gm_frame h;
    gm_tfgiovingnnea l4; /* s */
    uint8_t ret;
};
struct gm_fr_is_word_byte_7lklilo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_line_of_n80f3n1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_location_in_v59u1gt {
    gm_frame h;
    gm_tin97tt4ud0s1 l5; /* o */
    gm_t3ph3kbiqt1ah ret;
};
struct gm_fr_narrow_5epj0dh {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_prepare_rename_i0odtgj {
    gm_frame h;
    gm_tdi3m8srnr9m6 l3; /* t */
    gm_tin97tt4ud0s1 l15; /* o */
    gm_t9l835tjp727s ret;
};
struct gm_fr_references_all_9v25v88 {
    gm_frame h;
    gm_tdi3m8srnr9m6 l5; /* t */
    gm_td7pkamr64l0c l6; /* found */
    gm_trqfvmjtt4sij l7; /* locs */
    gm_trqfvmjtt4sij l11; /* out */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_rename_4pp7bs8 {
    gm_frame h;
    gm_tdi3m8srnr9m6 l5; /* t */
    gm_tin97tt4ud0s1 l7; /* o */
    gm_td7pkamr64l0c l8; /* found */
    gm_trqfvmjtt4sij l10; /* by_file */
    gm_trqfvmjtt4sij l11; /* texts */
    gm_trqfvmjtt4sij l12; /* locs */
    gm_tin97tt4ud0s1 l30; /* changes */
    gm_trqfvmjtt4sij l40; /* edits */
    gm_tin97tt4ud0s1 l44; /* e */
    gm_tin97tt4ud0s1 l45; /* o */
    gm_t9l835tjp727s ret;
};
struct gm_fr_text_of_qs06tq9 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_check_clashes_cqluvhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_check_editable_56hen53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_check_new_name_31k8n9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_declaration_at_lquhblp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) uint64_t gm_f_dedup_5og6p3h(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_dedup_8ks2cpi(gm_b16_8 a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_definition_at_9jj9uuu(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void * gm_f_facts_s8nso10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_find_m559mi8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_o57h82n(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_2g3rqdg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7jgfnob(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ce9c14n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cebtc0e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_egbi75q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_guqevu2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h78t8d0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ics51qh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o1952f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s8ajp67(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t3f3kj5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t6rdn7r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_found_text_sajr2an(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_kfrlfsf(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_local_jek4n0p(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_word_byte_7lklilo(uint8_t a0);
__attribute__((aligned(4))) void gm_f_line_index_2necgtf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_line_of_n80f3n1(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_0hnol3g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_6u1pehv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ctsmjqd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_t5p1bue_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_u1fokev(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_loc_equal_h2ruinn(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_loc_key_n6bogvi(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_loc_less_gf4kba8(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_location_in_v59u1gt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_mentions_gartt1q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_module_dirs_72lhhi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_narrow_5epj0dh(gm_b16_8 a0, uint32_t a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_package_roots_uh3pgnt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_prepare_rename_i0odtgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0pgacsj_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1drnucl(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1prlsnl(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_5gfgdpl(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_drp4m1b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sjirrjh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_range_json_hlsolbo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_real_path_al8cv9r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_references_all_9v25v88(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_references_pd7nt1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_rename_4pp7bs8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_sort_by_m9qke1u(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_sort_by_ucn5r3c(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_store_q18c91o(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_text_in_sgvn4eo(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_text_of_qs06tq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_truncate_pphi06n(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_truncate_tgh982e(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern const uint8_t gm_s1n4l05blor5r[40];
extern const uint8_t gm_s4ss30tejbl47[32];
extern const uint8_t gm_s54jmbv61vfjv[12];
extern const uint8_t gm_s71u85v3jrj86[63];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sirpcjgkmlehr[8];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sn68oj251cu21[38];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srcn13n69hd0cht[];
extern const uint8_t gm_ssvcneuafnfds[8];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) void gm_f_check_clashes_cqluvhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_check_clashes_cqluvhl F;
    void * gm_l0; /* rq */
    void * gm_l1; /* t */
    void * gm_l2; /* found */
    gm_b16_8 gm_l3; /* new_name */
    gm_b16_8 gm_l4; /* old */
    gm_b16_8 gm_l5; /* home */
    void * gm_l6; /* module */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* m */
    void * gm_l10; /* m */
    gm_b16_8 gm_l11; /* names */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_t5hdnnftkup6m gm_l20; /* u */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    void * gm_l23; /* fm */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l27; /* line */
    gm_b16_8 gm_l28; /* body */
    GM_STACK_CHECK(&F, 2993u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 40));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 16) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_parent_lk0iero((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t7 = t6;
        gm_b16_8 t8;
        if (((((uint8_t *)&t7))[16] != 0)) {
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(0));
            t8 = t10;
        }
        gm_l5 = t8;
    }
    {
        gm_l6 = ((void *)0);
    }
    {
        {
            void * t12 = gm_l2;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 40) + 0));
            gm_l7 = t13;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l8;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l8;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    void * t23 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t21 * 8));
                    void * t24 = t23;
                    void * t27 = GM_LD(void *, ((uint8_t *)&t24));
                    gm_l9 = t27;
                    {
                        void * t30 = gm_l9;
                        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 8));
                        gm_b16_8 t32 = gm_l5;
                        gm_b16_8 t33 = t31;
                        gm_b16_8 t34 = t32;
                        uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                        if (!t35) goto ie29;
                        {
                            void * t37 = gm_l9;
                            gm_l6 = t37;
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 275, 5);
me25:;
                }
lc14:;
                {
                    uint64_t t39 = gm_l8;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l8 = t42;
                }
            }
lb14:;
        }
    }
    {
        void * t43 = gm_l6;
        void * t44 = t43;
        void * t45;
        if ((GM_LD(void *, ((uint8_t *)&t44)) != 0)) {
            void * t46 = GM_LD(void *, (((uint8_t *)&t44) + 0));
            t45 = t46;
        } else {
            void * t47 = gm_l0;
            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 8));
            gm_b16_8 t49 = gm_l5;
            void * t50 = gm_l0;
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)t50) + 32));
            void * t52 = gm_f_facts_s8nso10((void *)NULL, cx, NULL, t48, t49, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t45 = t52;
        }
        gm_l10 = t45;
    }
    {
        void * t54 = gm_l10;
        void * t55 = (void *)(((uint8_t *)t54) + 144);
        void * t56 = gm_l1;
        gm_b16_8 t57 = gm_f_loc_key_n6bogvi((void *)NULL, cx, NULL, &(*(gm_t5hdnnftkup6m *)(((uint8_t *)t56) + 16)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t58;
        gm_f_get_kfrlfsf((void *)&t58, t55, t57);
        gm_th2lqbo77pvsc t59 = t58;
        if (!((((uint8_t *)&t59))[16] != 0)) goto ie53;
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
        gm_l11 = t60;
        {
            gm_b16_8 t63 = gm_l3;
            gm_b16_8 t64 = gm_l11;
            gm_b16_8 t65 = t64;
            uint8_t t66 = 0;
            gm_b16_8 t67 = t63;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t65) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t65))) + i * 16, ((uint8_t *)&t67))) { t66 = 1; break; }
            uint8_t t68 = (uint8_t)(t66);
            if (!t68) goto ie62;
            {
                gm_tfeab279c16rr t70;
                gm_b16_8 t71 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t71);
                gm_b16_8 t72 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t70 + 16, t72);
                gm_b16_8 t73 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t70 + 32, t73);
                gm_tabb710d9mq2e t74;
                gm_f_format_ce9c14n((void *)&t74, (void *)NULL, cx, NULL, &t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t75 = t74;
                if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srcn13n69hd0cht, 281, 20);
                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
                gm_b16_8 t77 = t76;
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                gm_th2lqbo77pvsc t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t79), t78);
                ((uint8_t *)&t79)[16] = 1;
                uint8_t t80 = 0;
                F.ret = t79;
                goto gm_fret;
            }
            goto ix62;
ie62:;
ix62:;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        {
            void * t82 = gm_l10;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((((uint8_t *)t82) + 104) + 0));
            gm_l12 = t83;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t85 = gm_l13;
                uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t87 = (t85 < t86);
                if (!t87) goto lb84;
                {
                    uint64_t t90 = gm_l13;
                    uint64_t t91 = (uintptr_t)t90;
                    uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_td1bmbv6cf6nk t95 = GM_LD(gm_td1bmbv6cf6nk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t91 * 48));
                    F.l14 = t95;
                    {
                        void * t98 = (void *)(((uint8_t *)&F.l14) + 16);
                        void * t99 = gm_l1;
                        void * t100 = (void *)(((uint8_t *)t99) + 16);
                        uint8_t t101 = gm_f_loc_equal_h2ruinn(t98, t100);
                        uint8_t t102 = (uint8_t)!t101;
                        if (!t102) goto ie97;
                        {
                            goto lc84;
                        }
                        goto ix97;
ie97:;
ix97:;
                    }
                    {
                        void * t105 = gm_l10;
                        void * t106 = (void *)(((uint8_t *)t105) + 200);
                        gm_b16_8 t107 = gm_l3;
                        uint8_t t108 = gm_f_contains_57n7jqd(t106, t107);
                        if (!t108) goto ie104;
                        {
                            gm_tfeab279c16rr t110;
                            gm_b16_8 t111 = gm_l4;
                            GM_ST(gm_b16_8, (uint8_t *)&t110 + 0, t111);
                            gm_b16_8 t112 = gm_l3;
                            GM_ST(gm_b16_8, (uint8_t *)&t110 + 16, t112);
                            gm_b16_8 t113 = gm_l3;
                            GM_ST(gm_b16_8, (uint8_t *)&t110 + 32, t113);
                            gm_tabb710d9mq2e t114;
                            gm_f_format_t6rdn7r((void *)&t114, (void *)NULL, cx, NULL, &t110);
                            if (gm_unw.kind) {
                                goto gm_ll88;
                            }
                            gm_tabb710d9mq2e t115 = t114;
                            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 8, gm_srcn13n69hd0cht, 287, 20);
                            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
                            gm_b16_8 t117 = t116;
                            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                            gm_th2lqbo77pvsc t119;
                            GM_ST(uint8_t, (uint8_t *)&t119 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t119 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t119 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t119), t118);
                            ((uint8_t *)&t119)[16] = 1;
                            uint8_t t120 = 0;
                            F.ret = t119;
                            goto gm_fret;
                        }
                        goto ix104;
ie104:;
ix104:;
                    }
                    {
                        {
                            void * t122 = gm_l10;
                            gm_b16_8 t123 = GM_LD(gm_b16_8, ((((uint8_t *)t122) + 104) + 0));
                            gm_l15 = t123;
                        }
                        {
                            gm_l16 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t125 = gm_l16;
                                uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                uint8_t t127 = (t125 < t126);
                                if (!t127) goto lb124;
                                {
                                    uint64_t t130 = gm_l16;
                                    uint64_t t131 = (uintptr_t)t130;
                                    uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    gm_td1bmbv6cf6nk t135 = GM_LD(gm_td1bmbv6cf6nk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t131 * 48));
                                    F.l17 = t135;
                                    {
                                        gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                                        gm_b16_8 t139 = gm_l3;
                                        gm_b16_8 t140 = t138;
                                        gm_b16_8 t141 = t139;
                                        uint8_t t142 = (gm_eq0iqiubfm7e(((uint8_t *)&t140), ((uint8_t *)&t141)));
                                        if (!t142) goto ie137;
                                        void * t143 = (void *)(((uint8_t *)&F.l17) + 16);
                                        void * t144 = (void *)(((uint8_t *)&F.l14) + 16);
                                        uint8_t t145 = gm_f_loc_equal_h2ruinn(t143, t144);
                                        uint8_t t146 = (uint8_t)!t145;
                                        if (!t146) goto ie137;
                                        gm_b16_8 t147 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l17) + 16) + 0));
                                        gm_b16_8 t148 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l14) + 16) + 0));
                                        gm_b16_8 t149 = t147;
                                        gm_b16_8 t150 = t148;
                                        uint8_t t151 = (gm_eq0iqiubfm7e(((uint8_t *)&t149), ((uint8_t *)&t150)));
                                        if (!t151) goto ie137;
                                        uint32_t t152 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 40));
                                        uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 44));
                                        uint8_t t154 = (t152 <= t153);
                                        if (!t154) goto ie137;
                                        uint32_t t155 = GM_LD(uint32_t, (((uint8_t *)&F.l14) + 40));
                                        uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 44));
                                        uint8_t t157 = (t155 <= t156);
                                        if (!t157) goto ie137;
                                        {
                                            gm_tfeab279c16rr t159;
                                            gm_b16_8 t160 = gm_l4;
                                            GM_ST(gm_b16_8, (uint8_t *)&t159 + 0, t160);
                                            gm_b16_8 t161 = gm_l3;
                                            GM_ST(gm_b16_8, (uint8_t *)&t159 + 16, t161);
                                            gm_b16_8 t162 = gm_l3;
                                            GM_ST(gm_b16_8, (uint8_t *)&t159 + 32, t162);
                                            gm_tabb710d9mq2e t163;
                                            gm_f_format_ics51qh((void *)&t163, (void *)NULL, cx, NULL, &t159);
                                            if (gm_unw.kind) {
                                                goto gm_ll128;
                                            }
                                            gm_tabb710d9mq2e t164 = t163;
                                            if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 8, gm_srcn13n69hd0cht, 291, 24);
                                            gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
                                            gm_b16_8 t166 = t165;
                                            gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                                            gm_th2lqbo77pvsc t168;
                                            GM_ST(uint8_t, (uint8_t *)&t168 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t168 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t168 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t168), t167);
                                            ((uint8_t *)&t168)[16] = 1;
                                            uint8_t t169 = 0;
                                            F.ret = t168;
                                            goto gm_fret;
                                        }
                                        goto ix137;
ie137:;
ix137:;
                                    }
                                    goto me133;
mn134:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 289, 9);
me133:;
                                }
lc124:;
                                {
                                    uint64_t t171 = gm_l16;
                                    uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                                    uint64_t t174 = r172;
                                    gm_l16 = t174;
                                }
                            }
gm_ll128:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb124; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc124; }
                            }
                            goto gm_ll88;
lb124:;
                        }
                    }
                    {
                        goto lb84;
                    }
                    goto me93;
mn94:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 284, 5);
me93:;
                }
lc84:;
                {
                    uint64_t t176 = gm_l13;
                    uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                    uint64_t t179 = r177;
                    gm_l13 = t179;
                }
            }
gm_ll88:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb84; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc84; }
            }
            goto gm_lad;
lb84:;
        }
    }
    {
        {
            void * t181 = gm_l2;
            gm_b16_8 t182 = GM_LD(gm_b16_8, ((((uint8_t *)t181) + 0) + 0));
            gm_l18 = t182;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t184 = gm_l19;
                uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t186 = (t184 < t185);
                if (!t186) goto lb183;
                {
                    uint64_t t189 = gm_l19;
                    uint64_t t190 = (uintptr_t)t189;
                    uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_t5hdnnftkup6m t194 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t190 * 24));
                    gm_l20 = t194;
                    {
                        {
                            void * t197 = gm_l2;
                            gm_b16_8 t198 = GM_LD(gm_b16_8, ((((uint8_t *)t197) + 40) + 0));
                            gm_l21 = t198;
                        }
                        {
                            gm_l22 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t200 = gm_l22;
                                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                uint8_t t202 = (t200 < t201);
                                if (!t202) goto lb199;
                                {
                                    uint64_t t205 = gm_l22;
                                    uint64_t t206 = (uintptr_t)t205;
                                    uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                    void * t208 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t206 * 8));
                                    void * t209 = t208;
                                    void * t212 = GM_LD(void *, ((uint8_t *)&t209));
                                    gm_l23 = t212;
                                    {
                                        {
                                            void * t215 = gm_l23;
                                            gm_b16_8 t216 = GM_LD(gm_b16_8, ((((uint8_t *)t215) + 104) + 0));
                                            gm_l24 = t216;
                                        }
                                        {
                                            gm_l25 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t218 = gm_l25;
                                                uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                                uint8_t t220 = (t218 < t219);
                                                if (!t220) goto lb217;
                                                {
                                                    uint64_t t223 = gm_l25;
                                                    uint64_t t224 = (uintptr_t)t223;
                                                    uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                                    gm_td1bmbv6cf6nk t228 = GM_LD(gm_td1bmbv6cf6nk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t224 * 48));
                                                    F.l26 = t228;
                                                    {
                                                        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                                                        gm_b16_8 t232 = gm_l3;
                                                        gm_b16_8 t233 = t231;
                                                        gm_b16_8 t234 = t232;
                                                        uint8_t t235 = (gm_eq0iqiubfm7e(((uint8_t *)&t233), ((uint8_t *)&t234)));
                                                        if (!t235) goto ie230;
                                                        gm_b16_8 t236 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 16) + 0));
                                                        gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                                        gm_b16_8 t238 = t236;
                                                        gm_b16_8 t239 = t237;
                                                        uint8_t t240 = (gm_eq0iqiubfm7e(((uint8_t *)&t238), ((uint8_t *)&t239)));
                                                        if (!t240) goto ie230;
                                                        void * t241 = (void *)(((uint8_t *)&F.l26) + 16);
                                                        void * t242 = gm_l1;
                                                        void * t243 = (void *)(((uint8_t *)t242) + 16);
                                                        uint8_t t244 = gm_f_loc_equal_h2ruinn(t241, t243);
                                                        uint8_t t245 = (uint8_t)!t244;
                                                        if (!t245) goto ie230;
                                                        uint32_t t246 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 40));
                                                        uint32_t t247 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 16));
                                                        uint8_t t248 = (t246 <= t247);
                                                        if (!t248) goto ie230;
                                                        uint32_t t249 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 16));
                                                        uint32_t t250 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 44));
                                                        uint8_t t251 = (t249 <= t250);
                                                        if (!t251) goto ie230;
                                                        {
                                                            gm_l27 = ((uint64_t)0ULL);
                                                        }
                                                        {
                                                            void * t254 = gm_l23;
                                                            gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                                            gm_th2lqbo77pvsc t256;
                                                            gm_f_text_in_sgvn4eo((void *)&t256, t254, t255);
                                                            gm_th2lqbo77pvsc t257 = t256;
                                                            if (!((((uint8_t *)&t257))[16] != 0)) goto ie253;
                                                            gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                                                            gm_l28 = t258;
                                                            {
                                                                gm_b16_8 t260 = gm_l28;
                                                                uint32_t t261 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 16));
                                                                uint64_t t262 = gm_f_line_of_n80f3n1(t260, t261);
                                                                gm_l27 = t262;
                                                            }
                                                            goto ix253;
ie253:;
ix253:;
                                                        }
                                                        {
                                                            gm_tjftu5su6fh8e t263;
                                                            gm_b16_8 t264 = gm_l4;
                                                            GM_ST(gm_b16_8, (uint8_t *)&t263 + 0, t264);
                                                            gm_b16_8 t265 = gm_l3;
                                                            GM_ST(gm_b16_8, (uint8_t *)&t263 + 16, t265);
                                                            gm_b16_8 t266 = gm_l3;
                                                            GM_ST(gm_b16_8, (uint8_t *)&t263 + 32, t266);
                                                            gm_b16_8 t267 = gm_l4;
                                                            GM_ST(gm_b16_8, (uint8_t *)&t263 + 48, t267);
                                                            gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                                            GM_ST(gm_b16_8, (uint8_t *)&t263 + 64, t268);
                                                            uint64_t t269 = gm_l27;
                                                            GM_ST(uint64_t, (uint8_t *)&t263 + 80, t269);
                                                            gm_tabb710d9mq2e t270;
                                                            gm_f_format_t3f3kj5((void *)&t270, (void *)NULL, cx, NULL, &t263);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll221;
                                                            }
                                                            gm_tabb710d9mq2e t271 = t270;
                                                            if (((((uint8_t *)&t271))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t271) + 8, gm_srcn13n69hd0cht, 302, 28);
                                                            gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&t271) + 8));
                                                            gm_b16_8 t273 = t272;
                                                            gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&t273));
                                                            gm_th2lqbo77pvsc t275;
                                                            GM_ST(uint8_t, (uint8_t *)&t275 + 17, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t275 + 18, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t275 + 20, 0);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t275), t274);
                                                            ((uint8_t *)&t275)[16] = 1;
                                                            uint8_t t276 = 0;
                                                            F.ret = t275;
                                                            goto gm_fret;
                                                        }
                                                        goto ix230;
ie230:;
ix230:;
                                                    }
                                                    goto me226;
mn227:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 298, 13);
me226:;
                                                }
lc217:;
                                                {
                                                    uint64_t t278 = gm_l25;
                                                    uint64_t r279; bool o280 = __builtin_add_overflow(t278, ((uint64_t)1ULL), &r279);
                                                    uint64_t t281 = r279;
                                                    gm_l25 = t281;
                                                }
                                            }
gm_ll221:;
                                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb217; }
                                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc217; }
                                            }
                                            goto gm_ll203;
lb217:;
                                        }
                                    }
                                    goto me210;
mn211:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 297, 9);
me210:;
                                }
lc199:;
                                {
                                    uint64_t t283 = gm_l22;
                                    uint64_t r284; bool o285 = __builtin_add_overflow(t283, ((uint64_t)1ULL), &r284);
                                    uint64_t t286 = r284;
                                    gm_l22 = t286;
                                }
                            }
gm_ll203:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb199; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc199; }
                            }
                            goto gm_ll187;
lb199:;
                        }
                    }
                    goto me192;
mn193:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 296, 5);
me192:;
                }
lc183:;
                {
                    uint64_t t288 = gm_l19;
                    uint64_t r289; bool o290 = __builtin_add_overflow(t288, ((uint64_t)1ULL), &r289);
                    uint64_t t291 = r289;
                    gm_l19 = t291;
                }
            }
gm_ll187:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb183; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc183; }
            }
            goto gm_lad;
lb183:;
        }
    }
    {
        gm_th2lqbo77pvsc t292;
        memset(&t292, 0, sizeof t292);
        uint8_t t293 = 0;
        F.ret = t292;
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
inline __attribute__((aligned(4))) void gm_f_check_editable_56hen53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_check_editable_56hen53 F;
    void * gm_l0; /* rq */
    void * gm_l1; /* t */
    GM_STACK_CHECK(&F, 502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 16) + 0));
        gm_b16_8 t5 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t6 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t7) goto ie2;
        {
            void * t9 = gm_l1;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 40));
            gm_tnpifv20irnph t11;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
            gm_tabb710d9mq2e t12;
            gm_f_format_o1952f2((void *)&t12, (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcn13n69hd0cht, 46, 16);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_b16_8 t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_th2lqbo77pvsc t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[16] = 1;
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
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 16));
        gm_f_package_roots_uh3pgnt((void *)&F.l2, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t22 = gm_l1;
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)t22) + 16) + 0));
        uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_srcn13n69hd0cht, 49, 41);
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t24 * 16));
        uint8_t t27 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t23, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t28 = (uint8_t)!t27;
        if (!t28) goto ie21;
        {
            void * t30 = gm_l1;
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 40));
            gm_tnpifv20irnph t32;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
            gm_tabb710d9mq2e t33;
            gm_f_format_cebtc0e((void *)&t33, (void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcn13n69hd0cht, 50, 16);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
            gm_b16_8 t36 = t35;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_th2lqbo77pvsc t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[16] = 1;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_th2lqbo77pvsc t40;
        memset(&t40, 0, sizeof t40);
        uint8_t t41 = 0;
        F.ret = t40;
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
inline __attribute__((aligned(4))) void gm_f_check_new_name_31k8n9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_check_new_name_31k8n9d F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK(&F, 651u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        uint8_t t4 = gm_f_is_identifier_kvlcjjo(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            gm_b16_8 t7 = gm_l0;
            gm_tnpifv20irnph t8;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
            gm_tabb710d9mq2e t9;
            gm_f_format_egbi75q((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcn13n69hd0cht, 179, 46);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_th2lqbo77pvsc t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[16] = 1;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t17 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sfdbfrv46lihd, 1) == 0));
        if (!t17) goto ie16;
        {
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1n4l05blor5r));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(39));
            gm_th2lqbo77pvsc t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t20), t19);
            ((uint8_t *)&t20)[16] = 1;
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        gm_b16_8 t23 = gm_l0;
        void * t24 = ((uint8_t *)gm_cka4oohkshv);
        if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcn13n69hd0cht, 181, 16);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t24 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
        gm_b16_8 t26 = t25;
        uint8_t t27 = 0;
        gm_b16_8 t28 = t23;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t26) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t26))) + i * 16, ((uint8_t *)&t28))) { t27 = 1; break; }
        uint8_t t29 = (uint8_t)(t27);
        if (!t29) goto ie22;
        {
            gm_b16_8 t31 = gm_l0;
            gm_tnpifv20irnph t32;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
            gm_tabb710d9mq2e t33;
            gm_f_format_2g3rqdg((void *)&t33, (void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcn13n69hd0cht, 181, 50);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
            gm_b16_8 t36 = t35;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_th2lqbo77pvsc t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t38), t37);
            ((uint8_t *)&t38)[16] = 1;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        gm_b16_8 t41 = gm_l0;
        uint8_t t42 = gm_f_is_reserved_type_name_tuq4qsj(t41);
        if (!t42) goto ie40;
        {
            gm_b16_8 t44 = gm_l0;
            gm_tnpifv20irnph t45;
            GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t44);
            gm_tabb710d9mq2e t46;
            gm_f_format_s8ajp67((void *)&t46, (void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t47 = t46;
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srcn13n69hd0cht, 182, 50);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
            gm_b16_8 t49 = t48;
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_th2lqbo77pvsc t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t51), t50);
            ((uint8_t *)&t51)[16] = 1;
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        gm_th2lqbo77pvsc t53;
        memset(&t53, 0, sizeof t53);
        uint8_t t54 = 0;
        F.ret = t53;
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
__attribute__((aligned(4))) void gm_f_declaration_at_lquhblp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_declaration_at_lquhblp F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* t */
    gm_b16_8 gm_l5; /* name */
    gm_b16_8 gm_l6; /* path */
    gm_t5hdnnftkup6m gm_l7; /* loc */
    GM_STACK_CHECK(&F, 1377u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 120);
        uint32_t t5 = gm_l1;
        uint32_t t6 = gm_l2;
        gm_toceffb697p0u t7 = gm_f_definition_at_9jj9uuu(t4, t5, t6);
        gm_toceffb697p0u t8 = t7;
        gm_tl874t2atnlh3 t9;
        if (((((uint8_t *)&t8))[12] != 0)) {
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s4ss30tejbl47));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(31));
            gm_tmkrnkep1qbgl t12;
            memset(&t12, 0, sizeof t12);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 8), t11);
            gm_tmkrnkep1qbgl t13 = t12;
            uint8_t t14 = ((((uint8_t *)&t13))[0] != 0);
            F.ret = t12;
            goto gm_fret;
        }
        gm_l3 = t9;
    }
    {
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie15;
        {
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s71u85v3jrj86));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(62));
            gm_tmkrnkep1qbgl t21;
            memset(&t21, 0, sizeof t21);
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t21 + 8), t20);
            gm_tmkrnkep1qbgl t22 = t21;
            uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
            F.ret = t21;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        gm_b16_8 t27 = gm_f_source_text_7ru912k(t25, t26);
        gm_l4 = t27;
    }
    {
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(0));
        gm_l5 = t28;
    }
    {
        uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t31 = (uint64_t)t30;
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t33 = (t31 <= t32);
        if (!t33) goto ie29;
        gm_b16_8 t34 = gm_l4;
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint64_t t36 = (uint64_t)t35;
        uint8_t t37 = gm_f_is_char_boundary_h62fhaa(t34, t36);
        if (!t37) goto ie29;
        gm_b16_8 t38 = gm_l4;
        uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t40 = (uint64_t)t39;
        uint8_t t41 = gm_f_is_char_boundary_h62fhaa(t38, t40);
        if (!t41) goto ie29;
        {
            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t44 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            uint64_t t46 = (uint64_t)t45;
            uint64_t t47 = (uintptr_t)t46;
            uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t49 = (uint64_t)t48;
            uint64_t t50 = (uintptr_t)t49;
            if (t47 > t50 || t50 > (uintptr_t)t43) gm_trap_slice(t47, t50, t43, gm_srcn13n69hd0cht, 37, 121);
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t44 + t47 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50 - t47));
            gm_l5 = t51;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
        uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        gm_th2lqbo77pvsc t55;
        gm_f_real_path_al8cv9r((void *)&t55, (void *)NULL, cx, NULL, t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t56 = t55;
        gm_b16_8 t57;
        if (((((uint8_t *)&t56))[16] != 0)) {
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 0));
            t57 = t58;
        } else {
            gm_b16_8 t59 = gm_l5;
            gm_tnpifv20irnph t60;
            GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
            gm_tabb710d9mq2e t61;
            gm_f_format_h78t8d0((void *)&t61, (void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t62 = t61;
            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srcn13n69hd0cht, 38, 52);
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
            gm_b16_8 t64 = t63;
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_tmkrnkep1qbgl t66;
            memset(&t66, 0, sizeof t66);
            ((uint8_t *)&t66)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
            gm_tmkrnkep1qbgl t67 = t66;
            uint8_t t68 = ((((uint8_t *)&t67))[0] != 0);
            F.ret = t66;
            goto gm_fret;
        }
        gm_l6 = t57;
    }
    {
        gm_b16_8 t69 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&gm_l7 + 0, t69);
        uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        GM_ST(uint32_t, (uint8_t *)&gm_l7 + 16, t70);
        uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
        GM_ST(uint32_t, (uint8_t *)&gm_l7 + 20, t71);
    }
    {
        gm_tdi3m8srnr9m6 t72;
        memset((uint8_t *)&t72 + 12, 0, 4);
        memset((uint8_t *)&t72 + 57, 0, 7);
        gm_tl874t2atnlh3 t73 = gm_l3;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t72 + 0, t73);
        memcpy((uint8_t *)&t72 + 16, ((uint8_t *)&gm_l7), 24);
        gm_b16_8 t74 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 40, t74);
        void * t75 = gm_l0;
        gm_tl874t2atnlh3 t76 = gm_l3;
        uint8_t t77 = gm_f_is_local_jek4n0p(t75, GM_CONV(gm_cseahvg8v7p0vs, t76));
        GM_ST(uint8_t, (uint8_t *)&t72 + 56, t77);
        gm_tmkrnkep1qbgl t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
        ((uint8_t *)&t78)[0] = 0;
        GM_ST(gm_tdi3m8srnr9m6, ((uint8_t *)&t78 + 8), t72);
        gm_tmkrnkep1qbgl t79 = t78;
        uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
        F.ret = t78;
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
__attribute__((aligned(4))) void gm_f_find_m559mi8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_find_m559mi8 F;
    void * gm_l0; /* rq */
    void * gm_l1; /* t */
    uint8_t gm_l4; /* under */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* r */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* cand */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l14; /* m */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    GM_STACK_CHECK(&F, 1208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_ctsmjqd((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        void * t5 = gm_l0;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 16));
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l0;
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 32));
        void * t11 = gm_f_store_q18c91o((void *)NULL, cx, NULL, t4, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t12 = gm_f_push_1drnucl(t2, t11);
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcn13n69hd0cht, 59, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        void * t16 = gm_l1;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t16) + 56));
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie15;
        {
            void * t20 = gm_l0;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 16));
            gm_f_package_roots_uh3pgnt((void *)&F.l3, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l4 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_l5 = t23;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t25 = gm_l6;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t27 = (t25 < t26);
                    if (!t27) goto lb24;
                    {
                        uint64_t t30 = gm_l6;
                        uint64_t t31 = (uintptr_t)t30;
                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t31 * 16));
                        gm_l7 = t35;
                        {
                            void * t38 = gm_l1;
                            gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t38) + 16) + 0));
                            gm_b16_8 t40 = gm_l7;
                            uint8_t t41 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (!t41) goto ie37;
                            {
                                gm_l4 = ((uint8_t)1);
                            }
                            goto ix37;
ie37:;
ix37:;
                        }
                        goto me33;
mn34:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 63, 9);
me33:;
                    }
lc24:;
                    {
                        uint64_t t44 = gm_l6;
                        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                        uint64_t t47 = r45;
                        gm_l6 = t47;
                    }
                }
gm_ll28:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
                }
                goto gm_lad;
lb24:;
            }
        }
        {
            uint8_t t49 = gm_l4;
            if (!t49) goto ie48;
            {
                {
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                    gm_b16_8 t53 = t52;
                    gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                    void * t55 = gm_l0;
                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t55) + 32));
                    gm_trqfvmjtt4sij t57;
                    gm_f_module_dirs_72lhhi5((void *)&t57, (void *)NULL, cx, NULL, t54, t56);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_trqfvmjtt4sij t58 = t57;
                    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 0));
                    gm_l8 = t59;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t61 = gm_l9;
                        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t63 = (t61 < t62);
                        if (!t63) goto lb60;
                        {
                            uint64_t t66 = gm_l9;
                            uint64_t t67 = (uintptr_t)t66;
                            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t67 * 16));
                            gm_l10 = t71;
                            {
                                gm_b16_8 t74 = gm_l10;
                                void * t75 = gm_l0;
                                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)t75) + 16));
                                gm_b16_8 t77 = t74;
                                gm_b16_8 t78 = t76;
                                uint8_t t79 = (!gm_eq0iqiubfm7e(((uint8_t *)&t77), ((uint8_t *)&t78)));
                                if (!t79) goto ie73;
                                gm_b16_8 t80 = gm_l10;
                                void * t81 = gm_l1;
                                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)t81) + 40));
                                void * t83 = gm_l0;
                                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t83) + 32));
                                uint8_t t85 = gm_f_mentions_gartt1q((void *)NULL, cx, NULL, t80, t82, t84);
                                if (gm_unw.kind) {
                                    goto gm_ll64;
                                }
                                if (!t85) goto ie73;
                                {
                                    void * t87 = (void *)((uint8_t *)&F.l2);
                                    void * t88 = gm_l0;
                                    void * t89 = GM_LD(void *, (((uint8_t *)t88) + 8));
                                    gm_b16_8 t90 = gm_l10;
                                    void * t91 = gm_l0;
                                    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)t91) + 32));
                                    void * t93 = gm_f_facts_s8nso10((void *)NULL, cx, NULL, t89, t90, t92);
                                    if (gm_unw.kind) {
                                        goto gm_ll64;
                                    }
                                    gm_tftb1odc739t5 t94 = gm_f_push_1drnucl(t87, t93);
                                    gm_tftb1odc739t5 t95 = t94;
                                    if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srcn13n69hd0cht, 69, 21);
                                    gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                                }
                                goto ix73;
ie73:;
ix73:;
                            }
                            goto me69;
mn70:;
                            gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 67, 13);
me69:;
                        }
lc60:;
                        {
                            uint64_t t98 = gm_l9;
                            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                            uint64_t t101 = r99;
                            gm_l9 = t101;
                        }
                    }
gm_ll64:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
                    }
                    goto gm_lad;
lb60:;
                }
            }
            goto ix48;
ie48:;
ix48:;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_f_list_0hnol3g((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_l12 = t103;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t105 = gm_l13;
                uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t107 = (t105 < t106);
                if (!t107) goto lb104;
                {
                    uint64_t t110 = gm_l13;
                    uint64_t t111 = (uintptr_t)t110;
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    void * t113 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t111 * 8));
                    void * t114 = t113;
                    void * t117 = GM_LD(void *, ((uint8_t *)&t114));
                    gm_l14 = t117;
                    {
                        {
                            void * t120 = gm_l14;
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((((uint8_t *)t120) + 24) + 0));
                            gm_l15 = t121;
                        }
                        {
                            gm_l16 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t123 = gm_l16;
                                uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                uint8_t t125 = (t123 < t124);
                                if (!t125) goto lb122;
                                {
                                    uint64_t t128 = gm_l16;
                                    uint64_t t129 = (uintptr_t)t128;
                                    uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    gm_tmo31gpro8pgo t133 = GM_LD(gm_tmo31gpro8pgo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t129 * 48));
                                    F.l17 = t133;
                                    {
                                        void * t136 = (void *)(((uint8_t *)&F.l17) + 24);
                                        void * t137 = gm_l1;
                                        void * t138 = (void *)(((uint8_t *)t137) + 16);
                                        uint8_t t139 = gm_f_loc_equal_h2ruinn(t136, t138);
                                        if (!t139) goto ie135;
                                        {
                                            void * t141 = (void *)((uint8_t *)&F.l11);
                                            gm_tftb1odc739t5 t142 = gm_f_push_drp4m1b(t141, &(*(gm_t5hdnnftkup6m *)(((uint8_t *)&F.l17) + 0)));
                                            gm_tftb1odc739t5 t143 = t142;
                                            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcn13n69hd0cht, 77, 43);
                                            gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
                                        }
                                        goto ix135;
ie135:;
ix135:;
                                    }
                                    goto me131;
mn132:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 76, 9);
me131:;
                                }
lc122:;
                                {
                                    uint64_t t146 = gm_l16;
                                    uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                                    uint64_t t149 = r147;
                                    gm_l16 = t149;
                                }
                            }
lb122:;
                        }
                    }
                    goto me115;
mn116:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 75, 5);
me115:;
                }
lc104:;
                {
                    uint64_t t151 = gm_l13;
                    uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                    uint64_t t154 = r152;
                    gm_l13 = t154;
                }
            }
lb104:;
        }
    }
    {
        gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_closure t156 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_loc_less_gf4kba8)), NULL, cx});
        gm_f_sort_by_ucn5r3c(t155, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t157 = (void *)((uint8_t *)&F.l11);
        gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        uint64_t t159 = gm_f_dedup_5og6p3h(t158);
        gm_f_truncate_pphi06n(t157, t159);
    }
    {
        memcpy((uint8_t *)&(*(gm_td7pkamr64l0c *)gm_out) + 0, ((uint8_t *)&F.l11), 40);
        memcpy((uint8_t *)&(*(gm_td7pkamr64l0c *)gm_out) + 40, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_o57h82n(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_o57h82n F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 4));
    gm_tcc1rq4n1avb2 t5;
    GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t4);
    void * t6 = gm_l1;
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 0));
    void * t8 = gm_l1;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 4));
    gm_tcc1rq4n1avb2 t10;
    GM_ST(uint32_t, ((uint8_t *)&t10 + 0), t7);
    GM_ST(uint32_t, ((uint8_t *)&t10 + 4), t9);
    gm_tcc1rq4n1avb2 t11 = t5;
    gm_tcc1rq4n1avb2 t12 = t10;
    uint8_t t13 = (uint8_t)gm_lt8l669blqfv(((uint8_t *)&t11), ((uint8_t *)&t12));
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_local_jek4n0p(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_local_jek4n0p F;
    void * gm_l0; /* rq */
    gm_tl874t2atnlh3 gm_l1; /* def */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 120) + 136) + 0));
            gm_l2 = t5;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tfgiovingnnea t17 = GM_LD(gm_tfgiovingnnea, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 88));
                    F.l4 = t17;
                    {
                        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 64));
                        gm_tl874t2atnlh3 t21 = gm_l1;
                        gm_tl874t2atnlh3 t22 = t20;
                        gm_tl874t2atnlh3 t23 = t21;
                        uint8_t t24 = (gm_eqff85ac10o3(((uint8_t *)&t22), ((uint8_t *)&t23)));
                        if (!t24) goto ie19;
                        {
                            uint8_t t26 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 25, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
                }
            }
lb6:;
        }
    }
    {
        uint8_t t32 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_byte_7lklilo(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_word_byte_7lklilo F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)128ULL));
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_ident_cont_317fpdb(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_line_of_n80f3n1(gm_b16_8 a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_line_of_n80f3n1 F;
    gm_b16_8 gm_l0; /* t */
    uint32_t gm_l1; /* off */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 277u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint32_t t4 = gm_l1;
        uint64_t t5 = (uint64_t)t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t7 = (t5 > t6);
        if (!t7) {
            gm_b16_8 t8 = gm_l0;
            uint32_t t9 = gm_l1;
            uint64_t t10 = (uint64_t)t9;
            uint8_t t11 = gm_f_is_char_boundary_h62fhaa(t8, t10);
            uint8_t t12 = (uint8_t)!t11;
            t3 = t12;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t14 = 0;
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t17 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
            uint32_t t19 = gm_l1;
            uint64_t t20 = (uint64_t)t19;
            uint64_t t21 = (uintptr_t)t20;
            if (t18 > t21 || t21 > (uintptr_t)t16) gm_trap_slice(t18, t21, t16, gm_srcn13n69hd0cht, 189, 14);
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t17 + t18 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21 - t18));
            gm_l3 = t22;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t24 = gm_l4;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t26 = (t24 < t25);
                if (!t26) goto lb23;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t t30 = (uintptr_t)t29;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 1));
                    uint8_t t33 = t32;
                    uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t33));
                    gm_l5 = t36;
                    {
                        uint8_t t39 = gm_l5;
                        uint8_t t40 = ((t39 == ((uint8_t)10ULL)));
                        if (!t40) goto ie38;
                        {
                            uint64_t t42 = gm_l2;
                            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                            if (o44) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 190, 24);
                            uint64_t t45 = r43;
                            gm_l2 = t45;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 189, 5);
me34:;
                }
lc23:;
                {
                    uint64_t t47 = gm_l4;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                    uint64_t t50 = r48;
                    gm_l4 = t50;
                }
            }
lb23:;
        }
    }
    {
        uint64_t t51 = gm_l2;
        uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 192, 12);
        uint64_t t54 = r52;
        uint8_t t55 = 0;
        F.ret = t54;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_location_in_v59u1gt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_location_in_v59u1gt F;
    void * gm_l0; /* rq */
    void * gm_l1; /* found */
    void * gm_l2; /* loc */
    gm_b16_8 gm_l3; /* t */
    gm_t6j6smj7qoctu gm_l4; /* ix */
    GM_STACK_CHECK(&F, 515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = gm_l2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_text_of_qs06tq9((void *)&t6, (void *)NULL, cx, NULL, t2, t3, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t7 = t6;
        gm_b16_8 t8;
        if (((((uint8_t *)&t7))[16] != 0)) {
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_t3ph3kbiqt1ah t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l3 = t8;
    }
    {
        gm_b16_8 t12 = gm_l3;
        gm_f_line_index_2necgtf((void *)&gm_l4, (void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sbo52miqad506));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
        void * t15 = gm_l0;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 56));
        void * t17 = gm_l2;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 0));
        gm_b16_8 t19 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t16, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8vuock3jnv59 t20;
        gm_f_text_ls49dp6((void *)&t20, t19);
        gm_b0_1 t21 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t13, t14, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sl4ns6hobicht));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(5));
        void * t24 = (void *)((uint8_t *)&gm_l4);
        void * t25 = gm_l2;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 16));
        uint64_t t27 = (uint64_t)t26;
        void * t28 = gm_l2;
        uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)t28) + 20));
        uint64_t t30 = (uint64_t)t29;
        void * t31 = gm_l0;
        gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t31) + 48));
        gm_t8vuock3jnv59 t33;
        gm_f_range_json_hlsolbo((void *)&t33, (void *)NULL, cx, NULL, t24, t27, t30, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t34 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t22, t23, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t35 = (void *)((uint8_t *)&F.l5);
        gm_t8vuock3jnv59 t36;
        gm_f_value_pkuqal3((void *)&t36, t35);
        gm_t3ph3kbiqt1ah t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 28, 0);
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t37), t36);
        ((uint8_t *)&t37)[24] = 1;
        uint8_t t38 = 0;
        F.ret = t37;
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
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_narrow_5epj0dh(gm_b16_8 a0, uint32_t a1, uint32_t a2, gm_b16_8 a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_narrow_5epj0dh F;
    gm_b16_8 gm_l0; /* t */
    uint32_t gm_l1; /* lo */
    uint32_t gm_l2; /* hi */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* slice */
    gm_tf5srh4mp4b3i gm_l5; /* best */
    uint64_t gm_l6; /* from */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* at */
    uint64_t gm_l9; /* end */
    uint8_t gm_l10; /* before_ok */
    uint8_t gm_l11; /* after_ok */
    uint64_t gm_l12; /* at */
    GM_STACK_CHECK(&F, 889u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint32_t t6 = gm_l1;
        uint64_t t7 = (uint64_t)t6;
        uint32_t t8 = gm_l2;
        uint64_t t9 = (uint64_t)t8;
        uint8_t t10 = (t7 > t9);
        if (!t10) {
            uint32_t t11 = gm_l2;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t14 = (t12 > t13);
            t5 = t14;
        } else t5 = 1;
        if (!t5) {
            gm_b16_8 t15 = gm_l0;
            uint32_t t16 = gm_l1;
            uint64_t t17 = (uint64_t)t16;
            uint8_t t18 = gm_f_is_char_boundary_h62fhaa(t15, t17);
            uint8_t t19 = (uint8_t)!t18;
            t4 = t19;
        } else t4 = 1;
        if (!t4) {
            gm_b16_8 t20 = gm_l0;
            uint32_t t21 = gm_l2;
            uint64_t t22 = (uint64_t)t21;
            uint8_t t23 = gm_f_is_char_boundary_h62fhaa(t20, t22);
            uint8_t t24 = (uint8_t)!t23;
            t3 = t24;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_t736djc66po0l t26;
            memset(&t26, 0, sizeof t26);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint32_t t30 = gm_l1;
        uint64_t t31 = (uint64_t)t30;
        uint64_t t32 = (uintptr_t)t31;
        uint32_t t33 = gm_l2;
        uint64_t t34 = (uint64_t)t33;
        uint64_t t35 = (uintptr_t)t34;
        if (t32 > t35 || t35 > (uintptr_t)t28) gm_trap_slice(t32, t35, t28, gm_srcn13n69hd0cht, 129, 14);
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t29 + t32 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t32));
        gm_l4 = t36;
    }
    {
        gm_b16_8 t38 = gm_l4;
        gm_b16_8 t39 = gm_l3;
        gm_b16_8 t40 = t38;
        gm_b16_8 t41 = t39;
        uint8_t t42 = (gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
        if (!t42) goto ie37;
        {
            uint32_t t44 = gm_l1;
            uint32_t t45 = gm_l2;
            gm_tcc1rq4n1avb2 t46;
            GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t44);
            GM_ST(uint32_t, ((uint8_t *)&t46 + 4), t45);
            gm_t736djc66po0l t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 10, 0);
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t47), t46);
            ((uint8_t *)&t47)[8] = 1;
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        gm_tf5srh4mp4b3i t49;
        memset(&t49, 0, sizeof t49);
        gm_l5 = t49;
    }
    {
        gm_l6 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t51 = gm_l6;
            uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t53 = (t51 <= t52);
            if (!t53) goto lb50;
            {
                uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t57 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t58 = gm_l6;
                uint64_t t59 = (uintptr_t)t58;
                if (t59 > (uintptr_t)t56 || (uintptr_t)t56 > (uintptr_t)t56) gm_trap_slice(t59, (uintptr_t)t56, t56, gm_srcn13n69hd0cht, 134, 23);
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t57 + t59 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)((uintptr_t)t56 - t59));
                gm_b16_8 t61 = gm_l3;
                gm_tf5srh4mp4b3i t62 = gm_f_find_nab29co(t60, t61);
                gm_tf5srh4mp4b3i t63 = t62;
                uint64_t t64;
                if (((((uint8_t *)&t63))[8] != 0)) {
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&t63) + 0));
                    t64 = t65;
                } else {
                    goto lb50;
                }
                gm_l7 = t64;
            }
            {
                uint64_t t66 = gm_l6;
                uint64_t t67 = gm_l7;
                uint64_t r68; bool o69 = __builtin_add_overflow(t66, t67, &r68);
                if (o69) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 135, 15);
                uint64_t t70 = r68;
                gm_l8 = t70;
            }
            {
                uint64_t t71 = gm_l8;
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint64_t r73; bool o74 = __builtin_add_overflow(t71, t72, &r73);
                if (o74) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 136, 16);
                uint64_t t75 = r73;
                gm_l9 = t75;
            }
            {
                uint8_t t76 = 0;
                uint64_t t77 = gm_l8;
                uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
                if (!t78) {
                    uint64_t t79 = gm_l8;
                    uint64_t r80; bool o81 = __builtin_sub_overflow(t79, ((uint64_t)1ULL), &r80);
                    if (o81) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 137, 56);
                    uint64_t t82 = r80;
                    uint64_t t83 = (uintptr_t)t82;
                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srcn13n69hd0cht, 137, 50);
                    uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t83 * 1));
                    uint8_t t86 = gm_f_is_word_byte_7lklilo(t85);
                    uint8_t t87 = (uint8_t)!t86;
                    t76 = t87;
                } else t76 = 1;
                gm_l10 = t76;
            }
            {
                uint8_t t88 = 0;
                uint64_t t89 = gm_l9;
                uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t91 = (t89 >= t90);
                if (!t91) {
                    uint64_t t92 = gm_l9;
                    uint64_t t93 = (uintptr_t)t92;
                    uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcn13n69hd0cht, 138, 58);
                    uint8_t t95 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t93 * 1));
                    uint8_t t96 = gm_f_is_word_byte_7lklilo(t95);
                    uint8_t t97 = (uint8_t)!t96;
                    t88 = t97;
                } else t88 = 1;
                gm_l11 = t88;
            }
            {
                uint8_t t99 = gm_l10;
                if (!t99) goto ie98;
                uint8_t t100 = gm_l11;
                if (!t100) goto ie98;
                {
                    uint64_t t102 = gm_l8;
                    gm_tf5srh4mp4b3i t103;
                    GM_ST(uint8_t, (uint8_t *)&t103 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t103 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t103 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t103), t102);
                    ((uint8_t *)&t103)[8] = 1;
                    gm_l5 = t103;
                }
                goto ix98;
ie98:;
ix98:;
            }
            {
                uint64_t t104 = gm_l8;
                uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                if (o106) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 140, 16);
                uint64_t t107 = r105;
                gm_l6 = t107;
            }
lc50:;
        }
lb50:;
    }
    {
        gm_tf5srh4mp4b3i t108 = gm_l5;
        gm_tf5srh4mp4b3i t109 = t108;
        uint64_t t110;
        if (((((uint8_t *)&t109))[8] != 0)) {
            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&t109) + 0));
            t110 = t111;
        } else {
            gm_t736djc66po0l t112;
            memset(&t112, 0, sizeof t112);
            uint8_t t113 = 0;
            F.ret = t112;
            goto gm_fret;
        }
        gm_l12 = t110;
    }
    {
        uint32_t t114 = gm_l1;
        uint64_t t115 = gm_l12;
        uint32_t t116 = (uint32_t)t115;
        uint32_t r117; bool o118 = __builtin_add_overflow(t114, t116, &r117);
        if (o118) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 143, 17);
        uint32_t t119 = r117;
        uint32_t t120 = gm_l1;
        uint64_t t121 = gm_l12;
        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t r123; bool o124 = __builtin_add_overflow(t121, t122, &r123);
        if (o124) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 143, 47);
        uint64_t t125 = r123;
        uint32_t t126 = (uint32_t)t125;
        uint32_t r127; bool o128 = __builtin_add_overflow(t120, t126, &r127);
        if (o128) gm_trap("integer overflow in `+`", gm_srcn13n69hd0cht, 143, 41);
        uint32_t t129 = r127;
        gm_tcc1rq4n1avb2 t130;
        GM_ST(uint32_t, ((uint8_t *)&t130 + 0), t119);
        GM_ST(uint32_t, ((uint8_t *)&t130 + 4), t129);
        gm_t736djc66po0l t131;
        GM_ST(uint8_t, (uint8_t *)&t131 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t131 + 10, 0);
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t131), t130);
        ((uint8_t *)&t131)[8] = 1;
        uint8_t t132 = 0;
        F.ret = t131;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_prepare_rename_i0odtgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_prepare_rename_i0odtgj F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l4; /* msg */
    gm_b16_8 gm_l5; /* t_text */
    gm_toceffb697p0u gm_l6; /* best */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t2p74ki8sl4an gm_l9; /* r */
    gm_tl874t2atnlh3 gm_l10; /* b */
    gm_tl874t2atnlh3 gm_l11; /* b */
    gm_tl874t2atnlh3 gm_l12; /* span */
    gm_tcc1rq4n1avb2 gm_l13; /* r */
    gm_t6j6smj7qoctu gm_l14; /* ix */
    GM_STACK_CHECK(&F, 1611u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        uint32_t t4 = gm_l2;
        gm_tmkrnkep1qbgl t5;
        gm_f_declaration_at_lquhblp((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmkrnkep1qbgl t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
            gm_t9l835tjp727s t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t8 + 24, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tdi3m8srnr9m6 t9 = GM_LD(gm_tdi3m8srnr9m6, (((uint8_t *)&t6) + 8));
        F.l3 = t9;
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)((uint8_t *)&F.l3);
        gm_th2lqbo77pvsc t13;
        gm_f_check_editable_56hen53((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t14 = t13;
        if (!((((uint8_t *)&t14))[16] != 0)) goto ie10;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_l4 = t15;
        {
            gm_b16_8 t17 = gm_l4;
            gm_t9l835tjp727s t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
            gm_t9l835tjp727s t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        uint32_t t23 = gm_l1;
        gm_b16_8 t24 = gm_f_source_text_7ru912k(t22, t23);
        gm_l5 = t24;
    }
    {
        gm_toceffb697p0u t25;
        memset(&t25, 0, sizeof t25);
        gm_l6 = t25;
    }
    {
        {
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((((uint8_t *)t28) + 120) + 0) + 0));
            gm_l7 = t29;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t31 = gm_l8;
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t33 = (t31 < t32);
                if (!t33) goto lb30;
                {
                    uint64_t t36 = gm_l8;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t2p74ki8sl4an t41 = GM_LD(gm_t2p74ki8sl4an, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t37 * 24));
                    gm_l9 = t41;
                    {
                        uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)&gm_l9) + 0) + 0));
                        uint32_t t45 = gm_l1;
                        uint8_t t46 = ((t44 == t45));
                        if (!t46) goto ie43;
                        uint32_t t47 = GM_LD(uint32_t, ((((uint8_t *)&gm_l9) + 0) + 4));
                        uint32_t t48 = gm_l2;
                        uint8_t t49 = (t47 <= t48);
                        if (!t49) goto ie43;
                        uint32_t t50 = gm_l2;
                        uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)&gm_l9) + 0) + 8));
                        uint8_t t52 = (t50 <= t51);
                        if (!t52) goto ie43;
                        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 12));
                        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 0));
                        gm_tl874t2atnlh3 t55 = t53;
                        gm_tl874t2atnlh3 t56 = t54;
                        uint8_t t57 = (gm_eqff85ac10o3(((uint8_t *)&t55), ((uint8_t *)&t56)));
                        if (!t57) goto ie43;
                        {
                            if (!((((uint8_t *)&gm_l6))[12] != 0)) goto ie59;
                            gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l6));
                            gm_l10 = t60;
                            {
                                uint32_t t63 = GM_LD(uint32_t, ((((uint8_t *)&gm_l9) + 0) + 8));
                                uint32_t t64 = GM_LD(uint32_t, ((((uint8_t *)&gm_l9) + 0) + 4));
                                uint32_t r65; bool o66 = __builtin_sub_overflow(t63, t64, &r65);
                                if (o66) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 156, 20);
                                uint32_t t67 = r65;
                                uint32_t t68 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
                                uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                                uint32_t r70; bool o71 = __builtin_sub_overflow(t68, t69, &r70);
                                if (o71) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 156, 45);
                                uint32_t t72 = r70;
                                uint8_t t73 = (t67 >= t72);
                                if (!t73) goto ie62;
                                {
                                    goto lc30;
                                }
                                goto ix62;
ie62:;
ix62:;
                            }
                            goto ix59;
ie59:;
ix59:;
                        }
                        {
                            gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 0));
                            gm_toceffb697p0u t76;
                            GM_ST(uint8_t, (uint8_t *)&t76 + 13, 0);
                            GM_ST(uint16_t, (uint8_t *)&t76 + 14, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t76), t75);
                            ((uint8_t *)&t76)[12] = 1;
                            gm_l6 = t76;
                        }
                        goto ix43;
ie43:;
ix43:;
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 153, 5);
me39:;
                }
lc30:;
                {
                    uint64_t t78 = gm_l8;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l8 = t81;
                }
            }
lb30:;
        }
    }
    {
        uint32_t t83 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 0) + 0));
        uint32_t t84 = gm_l1;
        uint8_t t85 = ((t83 == t84));
        if (!t85) goto ie82;
        uint32_t t86 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 0) + 4));
        uint32_t t87 = gm_l2;
        uint8_t t88 = (t86 <= t87);
        if (!t88) goto ie82;
        uint32_t t89 = gm_l2;
        uint32_t t90 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 0) + 8));
        uint8_t t91 = (t89 <= t90);
        if (!t91) goto ie82;
        {
            if (!((((uint8_t *)&gm_l6))[12] != 0)) goto ie93;
            gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l6));
            gm_l11 = t94;
            {
                uint32_t t97 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 0) + 8));
                uint32_t t98 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 0) + 4));
                uint32_t r99; bool o100 = __builtin_sub_overflow(t97, t98, &r99);
                if (o100) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 163, 16);
                uint32_t t101 = r99;
                uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
                uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 4));
                uint32_t r104; bool o105 = __builtin_sub_overflow(t102, t103, &r104);
                if (o105) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 163, 40);
                uint32_t t106 = r104;
                uint8_t t107 = (t101 < t106);
                if (!t107) goto ie96;
                {
                    gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 0));
                    gm_toceffb697p0u t110;
                    GM_ST(uint8_t, (uint8_t *)&t110 + 13, 0);
                    GM_ST(uint16_t, (uint8_t *)&t110 + 14, 0);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t110), t109);
                    ((uint8_t *)&t110)[12] = 1;
                    gm_l6 = t110;
                }
                goto ix96;
ie96:;
ix96:;
            }
            goto ix93;
ie93:;
            {
                gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 0));
                gm_toceffb697p0u t113;
                GM_ST(uint8_t, (uint8_t *)&t113 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t113 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t113), t112);
                ((uint8_t *)&t113)[12] = 1;
                gm_l6 = t113;
            }
ix93:;
        }
        goto ix82;
ie82:;
ix82:;
    }
    {
        gm_toceffb697p0u t114 = gm_l6;
        gm_toceffb697p0u t115 = t114;
        gm_tl874t2atnlh3 t116;
        if (((((uint8_t *)&t115))[12] != 0)) {
            gm_tl874t2atnlh3 t117 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t115) + 0));
            t116 = t117;
        } else {
            gm_b16_8 t118;
            GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s4ss30tejbl47));
            GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(31));
            gm_t9l835tjp727s t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t119 + 24, 0);
            ((uint8_t *)&t119)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t119 + 8), t118);
            gm_t9l835tjp727s t120 = t119;
            uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
            F.ret = t119;
            goto gm_fret;
        }
        gm_l12 = t116;
    }
    {
        gm_b16_8 t122 = gm_l5;
        uint32_t t123 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
        uint32_t t124 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
        gm_t736djc66po0l t126 = gm_f_narrow_5epj0dh(t122, t123, t124, t125);
        gm_t736djc66po0l t127 = t126;
        gm_tcc1rq4n1avb2 t128;
        if (((((uint8_t *)&t127))[8] != 0)) {
            gm_tcc1rq4n1avb2 t129 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)&t127) + 0));
            t128 = t129;
        } else {
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sn68oj251cu21));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(37));
            gm_t9l835tjp727s t131;
            GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t131 + 24, 0);
            ((uint8_t *)&t131)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t131 + 8), t130);
            gm_t9l835tjp727s t132 = t131;
            uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
            F.ret = t131;
            goto gm_fret;
        }
        gm_l13 = t128;
    }
    {
        gm_b16_8 t134 = gm_l5;
        gm_f_line_index_2necgtf((void *)&gm_l14, (void *)NULL, cx, NULL, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t135 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t136;
        GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sl4ns6hobicht));
        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(5));
        void * t137 = (void *)((uint8_t *)&gm_l14);
        uint32_t t138 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 0));
        uint64_t t139 = (uint64_t)t138;
        uint32_t t140 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 4));
        uint64_t t141 = (uint64_t)t140;
        void * t142 = gm_l0;
        gm_t9qkh0rsooc8d t143 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t142) + 48));
        gm_t8vuock3jnv59 t144;
        gm_f_range_json_hlsolbo((void *)&t144, (void *)NULL, cx, NULL, t137, t139, t141, t143);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t145 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t135, t136, &t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t146 = (void *)((uint8_t *)&F.l15);
        gm_b16_8 t147;
        GM_ST(void *, (uint8_t *)&t147, (void *)(gm_s54jmbv61vfjv));
        GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(11));
        gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
        gm_t8vuock3jnv59 t149;
        gm_f_text_ls49dp6((void *)&t149, t148);
        gm_b0_1 t150 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t146, t147, &t149);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t151 = (void *)((uint8_t *)&F.l15);
        gm_t8vuock3jnv59 t152;
        gm_f_value_pkuqal3((void *)&t152, t151);
        gm_t9l835tjp727s t153;
        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
        ((uint8_t *)&t153)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t153 + 8), t152);
        gm_t9l835tjp727s t154 = t153;
        uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
        F.ret = t153;
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
__attribute__((aligned(4))) void gm_f_references_all_9v25v88(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3) {
    struct gm_fr_references_all_9v25v88 F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    uint8_t gm_l3; /* include_declaration */
    gm_tf361jk9mn3il gm_l4; /* cx */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t5hdnnftkup6m gm_l10; /* u */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_t8vuock3jnv59 gm_l15; /* l */
    GM_STACK_CHECK(&F, 882u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        uint32_t t4 = gm_l2;
        gm_tmkrnkep1qbgl t5;
        gm_f_declaration_at_lquhblp((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmkrnkep1qbgl t6 = t5;
        gm_tdi3m8srnr9m6 t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tdi3m8srnr9m6 t8 = GM_LD(gm_tdi3m8srnr9m6, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            {
                memset((uint8_t *)&gm_l4 + 9, 0, 7);
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
                GM_ST(void *, (uint8_t *)&gm_l4 + 0, t11);
                void * t12 = gm_l0;
                gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t12) + 48));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&gm_l4 + 8, t13);
                void * t14 = gm_l0;
                void * t15 = GM_LD(void *, (((uint8_t *)t14) + 56));
                GM_ST(void *, (uint8_t *)&gm_l4 + 16, t15);
            }
            void * t16 = (void *)((uint8_t *)&gm_l4);
            uint32_t t17 = gm_l1;
            uint32_t t18 = gm_l2;
            uint8_t t19 = gm_l3;
            gm_f_references_pd7nt1t((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t16, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        F.l5 = t7;
    }
    {
        void * t20 = gm_l0;
        void * t21 = (void *)((uint8_t *)&F.l5);
        gm_f_find_m559mi8((void *)&F.l6, (void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_0hnol3g((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t23 = gm_l3;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l7);
            gm_tftb1odc739t5 t26 = gm_f_push_drp4m1b(t25, &(*(gm_t5hdnnftkup6m *)(((uint8_t *)&F.l5) + 16)));
            gm_tftb1odc739t5 t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcn13n69hd0cht, 114, 30);
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        {
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 0) + 0));
            gm_l8 = t30;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t32 = gm_l9;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l9;
                    uint64_t t38 = (uintptr_t)t37;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_t5hdnnftkup6m t42 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t38 * 24));
                    gm_l10 = t42;
                    {
                        void * t44 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t45 = gm_f_push_drp4m1b(t44, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&gm_l10)));
                        gm_tftb1odc739t5 t46 = t45;
                        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcn13n69hd0cht, 115, 33);
                        gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                    }
                    goto me40;
mn41:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 115, 5);
me40:;
                }
lc31:;
                {
                    uint64_t t49 = gm_l9;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l9 = t52;
                }
            }
lb31:;
        }
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            gm_l12 = t54;
        }
        {
            for (;;) {
                uint64_t t56 = gm_l13;
                uint64_t t57 = gm_l12;
                uint8_t t58 = (t56 < t57);
                if (!t58) goto lb55;
                {
                    uint64_t t61 = gm_l13;
                    uint64_t t62 = t61;
                    uint64_t t65 = GM_LD(uint64_t, ((uint8_t *)&t62));
                    gm_l14 = t65;
                    {
                        void * t68 = gm_l0;
                        void * t69 = (void *)((uint8_t *)&F.l6);
                        uint64_t t70 = gm_l14;
                        uint64_t t71 = (uintptr_t)t70;
                        uint64_t t72 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                        if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_srcn13n69hd0cht, 118, 42);
                        void * t73 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t71 * 24);
                        gm_t3ph3kbiqt1ah t74;
                        gm_f_location_in_v59u1gt((void *)&t74, (void *)NULL, cx, NULL, t68, t69, t73);
                        if (gm_unw.kind) {
                            goto gm_ll59;
                        }
                        gm_t3ph3kbiqt1ah t75 = t74;
                        if (!((((uint8_t *)&t75))[24] != 0)) goto ie67;
                        gm_t8vuock3jnv59 t76 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&t75));
                        gm_l15 = t76;
                        {
                            void * t78 = (void *)((uint8_t *)&F.l11);
                            gm_tftb1odc739t5 t79 = gm_f_push_0pgacsj_3(t78, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l15)));
                            gm_tftb1odc739t5 t80 = t79;
                            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcn13n69hd0cht, 118, 59);
                            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                        }
                        goto ix67;
ie67:;
ix67:;
                    }
                    goto me63;
mn64:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 117, 5);
me63:;
                }
lc55:;
                {
                    uint64_t t83 = gm_l13;
                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                    uint64_t t86 = r84;
                    gm_l13 = t86;
                }
            }
gm_ll59:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb55; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc55; }
            }
            goto gm_lad;
lb55:;
        }
    }
    {
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t88 = t87;
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
        gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void gm_f_rename_4pp7bs8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3) {
    struct gm_fr_rename_4pp7bs8 F;
    void * gm_l0; /* rq */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l3; /* new_name */
    gm_b16_8 gm_l4; /* msg */
    gm_b16_8 gm_l6; /* msg */
    gm_b16_8 gm_l9; /* msg */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t5hdnnftkup6m gm_l15; /* u */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t5hdnnftkup6m gm_l18; /* loc */
    gm_th2lqbo77pvsc gm_l19; /* known */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t6j6smj7qoctu gm_l22; /* te */
    gm_b16_8 gm_l23; /* body */
    gm_b16_8 gm_l24; /* t_text */
    gm_tcc1rq4n1avb2 gm_l25; /* r */
    gm_tf5srh4mp4b3i gm_l26; /* idx */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* k */
    void * gm_l34; /* fe */
    gm_b16_8 gm_l35; /* body */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    gm_t6j6smj7qoctu gm_l38; /* te */
    gm_t6j6smj7qoctu gm_l39; /* ix */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l43; /* sp */
    GM_STACK_CHECK(&F, 4337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t3 = gm_l3;
        gm_th2lqbo77pvsc t4;
        gm_f_check_new_name_31k8n9d((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t5 = t4;
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l4 = t6;
        {
            gm_b16_8 t8 = gm_l4;
            gm_t9l835tjp727s t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            gm_t9l835tjp727s t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_l1;
        uint32_t t14 = gm_l2;
        gm_tmkrnkep1qbgl t15;
        gm_f_declaration_at_lquhblp((void *)&t15, (void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmkrnkep1qbgl t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_t9l835tjp727s t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_tdi3m8srnr9m6 t19 = GM_LD(gm_tdi3m8srnr9m6, (((uint8_t *)&t16) + 8));
        F.l5 = t19;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)((uint8_t *)&F.l5);
        gm_th2lqbo77pvsc t23;
        gm_f_check_editable_56hen53((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t24 = t23;
        if (!((((uint8_t *)&t24))[16] != 0)) goto ie20;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_l6 = t25;
        {
            gm_b16_8 t27 = gm_l6;
            gm_t9l835tjp727s t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t28 + 24, 0);
            ((uint8_t *)&t28)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 8), t27);
            gm_t9l835tjp727s t29 = t28;
            uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
            F.ret = t28;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_b16_8 t33 = gm_l3;
        gm_b16_8 t34 = t32;
        gm_b16_8 t35 = t33;
        uint8_t t36 = (gm_eq0iqiubfm7e(((uint8_t *)&t34), ((uint8_t *)&t35)));
        if (!t36) goto ie31;
        {
            gm_f_obj_aoa7fl7((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t38 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_ssvcneuafnfds));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(7));
            gm_tin97tt4ud0s1 t40;
            gm_f_obj_aoa7fl7((void *)&t40, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tin97tt4ud0s1 t41 = t40;
            void * t42 = (void *)((uint8_t *)&t41);
            gm_t8vuock3jnv59 t43;
            gm_f_value_pkuqal3((void *)&t43, t42);
            gm_b0_1 t44 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t38, t39, &t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t45 = (void *)((uint8_t *)&F.l7);
            gm_t8vuock3jnv59 t46;
            gm_f_value_pkuqal3((void *)&t46, t45);
            gm_t9l835tjp727s t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            ((uint8_t *)&t47)[0] = 0;
            GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t47 + 8), t46);
            gm_t9l835tjp727s t48 = t47;
            uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
            F.ret = t47;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t50 = gm_l0;
        void * t51 = (void *)((uint8_t *)&F.l5);
        gm_f_find_m559mi8((void *)&F.l8, (void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t53 = gm_l0;
        void * t54 = (void *)((uint8_t *)&F.l5);
        void * t55 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t56 = gm_l3;
        gm_th2lqbo77pvsc t57;
        gm_f_check_clashes_cqluvhl((void *)&t57, (void *)NULL, cx, NULL, t53, t54, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t58 = t57;
        if (!((((uint8_t *)&t58))[16] != 0)) goto ie52;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_l9 = t59;
        {
            gm_b16_8 t61 = gm_l9;
            gm_t9l835tjp727s t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t62 + 24, 0);
            ((uint8_t *)&t62)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 8), t61);
            gm_t9l835tjp727s t63 = t62;
            uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
            F.ret = t62;
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        gm_f_list_t5p1bue_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_u1fokev((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_0hnol3g((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = (void *)((uint8_t *)&F.l12);
        gm_tftb1odc739t5 t66 = gm_f_push_drp4m1b(t65, &(*(gm_t5hdnnftkup6m *)(((uint8_t *)&F.l5) + 16)));
        gm_tftb1odc739t5 t67 = t66;
        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srcn13n69hd0cht, 220, 5);
        gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
    }
    {
        {
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 0) + 0));
            gm_l13 = t70;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t72 = gm_l14;
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t74 = (t72 < t73);
                if (!t74) goto lb71;
                {
                    uint64_t t77 = gm_l14;
                    uint64_t t78 = (uintptr_t)t77;
                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_t5hdnnftkup6m t82 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t78 * 24));
                    gm_l15 = t82;
                    {
                        void * t84 = (void *)((uint8_t *)&F.l12);
                        gm_tftb1odc739t5 t85 = gm_f_push_drp4m1b(t84, &(*(gm_t5hdnnftkup6m *)((uint8_t *)&gm_l15)));
                        gm_tftb1odc739t5 t86 = t85;
                        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcn13n69hd0cht, 221, 33);
                        gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                    }
                    goto me80;
mn81:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 221, 5);
me80:;
                }
lc71:;
                {
                    uint64_t t89 = gm_l14;
                    uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                    uint64_t t92 = r90;
                    gm_l14 = t92;
                }
            }
lb71:;
        }
    }
    {
        {
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_l16 = t94;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t96 = gm_l17;
                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t98 = (t96 < t97);
                if (!t98) goto lb95;
                {
                    uint64_t t101 = gm_l17;
                    uint64_t t102 = (uintptr_t)t101;
                    uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_t5hdnnftkup6m t106 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t102 * 24));
                    gm_l18 = t106;
                    {
                        gm_th2lqbo77pvsc t108;
                        memset(&t108, 0, sizeof t108);
                        gm_l19 = t108;
                    }
                    {
                        {
                            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_l20 = t110;
                        }
                        {
                            gm_l21 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t112 = gm_l21;
                                uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                uint8_t t114 = (t112 < t113);
                                if (!t114) goto lb111;
                                {
                                    uint64_t t117 = gm_l21;
                                    uint64_t t118 = (uintptr_t)t117;
                                    uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    gm_t6j6smj7qoctu t122 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t118 * 32));
                                    gm_l22 = t122;
                                    {
                                        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 0));
                                        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                        gm_b16_8 t127 = t125;
                                        gm_b16_8 t128 = t126;
                                        uint8_t t129 = (gm_eq0iqiubfm7e(((uint8_t *)&t127), ((uint8_t *)&t128)));
                                        if (!t129) goto ie124;
                                        {
                                            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 16));
                                            gm_th2lqbo77pvsc t132;
                                            GM_ST(uint8_t, (uint8_t *)&t132 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t132 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t132 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t132), t131);
                                            ((uint8_t *)&t132)[16] = 1;
                                            gm_l19 = t132;
                                        }
                                        goto ix124;
ie124:;
ix124:;
                                    }
                                    goto me120;
mn121:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 224, 9);
me120:;
                                }
lc111:;
                                {
                                    uint64_t t134 = gm_l21;
                                    uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                                    uint64_t t137 = r135;
                                    gm_l21 = t137;
                                }
                            }
lb111:;
                        }
                    }
                    {
                        uint8_t t139;
                        if (((((uint8_t *)&gm_l19))[16] != 0)) goto mn141;
                        t139 = ((uint8_t)1);
                        goto me140;
mn141:;
                        t139 = ((uint8_t)0);
                        goto me140;
mn142:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 227, 12);
me140:;
                        if (!t139) goto ie138;
                        {
                            void * t144 = gm_l0;
                            void * t145 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                            gm_th2lqbo77pvsc t147;
                            gm_f_text_of_qs06tq9((void *)&t147, (void *)NULL, cx, NULL, t144, t145, t146);
                            if (gm_unw.kind) {
                                goto gm_ll99;
                            }
                            gm_th2lqbo77pvsc t148 = t147;
                            gm_b16_8 t149;
                            if (((((uint8_t *)&t148))[16] != 0)) {
                                gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&t148) + 0));
                                t149 = t150;
                            } else {
                                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                gm_tnpifv20irnph t152;
                                GM_ST(gm_b16_8, ((uint8_t *)&t152 + 0), t151);
                                gm_tabb710d9mq2e t153;
                                gm_f_format_guqevu2((void *)&t153, (void *)NULL, cx, NULL, t152);
                                if (gm_unw.kind) {
                                    goto gm_ll99;
                                }
                                gm_tabb710d9mq2e t154 = t153;
                                if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 8, gm_srcn13n69hd0cht, 228, 64);
                                gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&t154) + 8));
                                gm_b16_8 t156 = t155;
                                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                                gm_t9l835tjp727s t158;
                                GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t158 + 24, 0);
                                ((uint8_t *)&t158)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t158 + 8), t157);
                                gm_t9l835tjp727s t159 = t158;
                                uint8_t t160 = ((((uint8_t *)&t159))[0] != 0);
                                F.ret = t158;
                                goto gm_fret;
                            }
                            gm_l23 = t149;
                        }
                        {
                            void * t161 = (void *)((uint8_t *)&F.l11);
                            gm_t6j6smj7qoctu t162;
                            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t162 + 0, t163);
                            gm_b16_8 t164 = gm_l23;
                            GM_ST(gm_b16_8, (uint8_t *)&t162 + 16, t164);
                            gm_tftb1odc739t5 t165 = gm_f_push_5gfgdpl(t161, &t162);
                            gm_tftb1odc739t5 t166 = t165;
                            if (((((uint8_t *)&t166))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t166) + 1, gm_srcn13n69hd0cht, 229, 13);
                            gm_b0_1 t167 = GM_LD(gm_b0_1, (((uint8_t *)&t166) + 1));
                        }
                        {
                            gm_b16_8 t168 = gm_l23;
                            gm_th2lqbo77pvsc t169;
                            GM_ST(uint8_t, (uint8_t *)&t169 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t169 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t169 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t169), t168);
                            ((uint8_t *)&t169)[16] = 1;
                            gm_l19 = t169;
                        }
                        goto ix138;
ie138:;
ix138:;
                    }
                    {
                        gm_th2lqbo77pvsc t170 = gm_l19;
                        gm_th2lqbo77pvsc t171 = t170;
                        gm_b16_8 t172;
                        if (((((uint8_t *)&t171))[16] != 0)) {
                            gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&t171) + 0));
                            t172 = t173;
                        } else {
                            gm_b16_8 t174;
                            GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(0));
                            t172 = t174;
                        }
                        gm_l24 = t172;
                    }
                    {
                        gm_b16_8 t175 = gm_l24;
                        uint32_t t176 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                        uint32_t t177 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 20));
                        gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                        gm_t736djc66po0l t179 = gm_f_narrow_5epj0dh(t175, t176, t177, t178);
                        gm_t736djc66po0l t180 = t179;
                        gm_tcc1rq4n1avb2 t181;
                        if (((((uint8_t *)&t180))[8] != 0)) {
                            gm_tcc1rq4n1avb2 t182 = GM_LD(gm_tcc1rq4n1avb2, (((uint8_t *)&t180) + 0));
                            t181 = t182;
                        } else {
                            gm_tftksr4op114e t184;
                            gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                            GM_ST(gm_b16_8, (uint8_t *)&t184 + 0, t185);
                            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t184 + 16, t186);
                            gm_b16_8 t187 = gm_l24;
                            uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                            uint64_t t189 = gm_f_line_of_n80f3n1(t187, t188);
                            GM_ST(uint64_t, (uint8_t *)&t184 + 32, t189);
                            gm_tabb710d9mq2e t190;
                            gm_f_format_7jgfnob((void *)&t190, (void *)NULL, cx, NULL, &t184);
                            if (gm_unw.kind) {
                                goto gm_ll99;
                            }
                            gm_tabb710d9mq2e t191 = t190;
                            if (((((uint8_t *)&t191))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t191) + 8, gm_srcn13n69hd0cht, 234, 21);
                            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&t191) + 8));
                            gm_b16_8 t193 = t192;
                            gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&t193));
                            gm_t9l835tjp727s t195;
                            GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t195 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t195 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t195 + 24, 0);
                            ((uint8_t *)&t195)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t195 + 8), t194);
                            gm_t9l835tjp727s t196 = t195;
                            uint8_t t197 = ((((uint8_t *)&t196))[0] != 0);
                            F.ret = t195;
                            goto gm_fret;
                        }
                        gm_l25 = t181;
                    }
                    {
                        gm_tf5srh4mp4b3i t198;
                        memset(&t198, 0, sizeof t198);
                        gm_l26 = t198;
                    }
                    {
                        {
                            gm_l28 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t200 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                            gm_l27 = t200;
                        }
                        {
                            for (;;) {
                                uint64_t t202 = gm_l28;
                                uint64_t t203 = gm_l27;
                                uint8_t t204 = (t202 < t203);
                                if (!t204) goto lb201;
                                {
                                    uint64_t t207 = gm_l28;
                                    uint64_t t208 = t207;
                                    uint64_t t211 = GM_LD(uint64_t, ((uint8_t *)&t208));
                                    gm_l29 = t211;
                                    {
                                        uint64_t t214 = gm_l29;
                                        uint64_t t215 = (uintptr_t)t214;
                                        uint64_t t216 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                        if (t215 >= (uintptr_t)(t216)) gm_trap_index(t215, t216, gm_srcn13n69hd0cht, 238, 16);
                                        gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t215 * 56) + 0));
                                        gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                        gm_b16_8 t219 = t217;
                                        gm_b16_8 t220 = t218;
                                        uint8_t t221 = (gm_eq0iqiubfm7e(((uint8_t *)&t219), ((uint8_t *)&t220)));
                                        if (!t221) goto ie213;
                                        {
                                            uint64_t t223 = gm_l29;
                                            gm_tf5srh4mp4b3i t224;
                                            GM_ST(uint8_t, (uint8_t *)&t224 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t224 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t224 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t224), t223);
                                            ((uint8_t *)&t224)[8] = 1;
                                            gm_l26 = t224;
                                        }
                                        goto ix213;
ie213:;
ix213:;
                                    }
                                    goto me209;
mn210:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 237, 9);
me209:;
                                }
lc201:;
                                {
                                    uint64_t t226 = gm_l28;
                                    uint64_t r227; bool o228 = __builtin_add_overflow(t226, ((uint64_t)1ULL), &r227);
                                    uint64_t t229 = r227;
                                    gm_l28 = t229;
                                }
                            }
lb201:;
                        }
                    }
                    {
                        uint8_t t231;
                        if (((((uint8_t *)&gm_l26))[8] != 0)) goto mn233;
                        t231 = ((uint8_t)1);
                        goto me232;
mn233:;
                        t231 = ((uint8_t)0);
                        goto me232;
mn234:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 240, 12);
me232:;
                        if (!t231) goto ie230;
                        {
                            void * t236 = (void *)((uint8_t *)&F.l10);
                            gm_tl7hpf5nlilqq t237;
                            gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t237 + 0, t238);
                            gm_f_list_6u1pehv((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t237 + 16)), (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll99;
                            }
                            gm_tftb1odc739t5 t239 = gm_f_push_sjirrjh(t236, &t237);
                            gm_tftb1odc739t5 t240 = t239;
                            if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 1, gm_srcn13n69hd0cht, 241, 13);
                            gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 1));
                        }
                        {
                            uint64_t t242 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                            uint64_t r243; bool o244 = __builtin_sub_overflow(t242, ((uint64_t)1ULL), &r243);
                            if (o244) gm_trap("integer overflow in `-`", gm_srcn13n69hd0cht, 242, 19);
                            uint64_t t245 = r243;
                            gm_tf5srh4mp4b3i t246;
                            GM_ST(uint8_t, (uint8_t *)&t246 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t246 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t246 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t246), t245);
                            ((uint8_t *)&t246)[8] = 1;
                            gm_l26 = t246;
                        }
                        goto ix230;
ie230:;
ix230:;
                    }
                    {
                        gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                        gm_b16_8 t248 = t247;
                        gm_tf5srh4mp4b3i t249 = gm_l26;
                        gm_tf5srh4mp4b3i t250 = t249;
                        uint64_t t251;
                        if (((((uint8_t *)&t250))[8] != 0)) {
                            uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&t250) + 0));
                            t251 = t252;
                        } else {
                            t251 = ((uint64_t)0ULL);
                        }
                        uint64_t t253 = (uintptr_t)t251;
                        gm_b16_8 t254 = t248;
                        uint64_t t255 = GM_LD(uintptr_t, ((uint8_t *)&t254) + 8);
                        if (t253 >= (uintptr_t)(t255)) gm_trap_index(t253, t255, gm_srcn13n69hd0cht, 244, 9);
                        void * t256 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&t254)) + t253 * 56) + 16);
                        uint32_t t257 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 0));
                        uint32_t t258 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 4));
                        gm_tcc1rq4n1avb2 t259;
                        GM_ST(uint32_t, ((uint8_t *)&t259 + 0), t257);
                        GM_ST(uint32_t, ((uint8_t *)&t259 + 4), t258);
                        gm_tftb1odc739t5 t260 = gm_f_push_1prlsnl(t256, GM_CONV(gm_cs9ufftihtbpo2, t259));
                        gm_tftb1odc739t5 t261 = t260;
                        if (((((uint8_t *)&t261))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t261) + 1, gm_srcn13n69hd0cht, 244, 9);
                        gm_b0_1 t262 = GM_LD(gm_b0_1, (((uint8_t *)&t261) + 1));
                    }
                    goto me104;
mn105:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 222, 5);
me104:;
                }
lc95:;
                {
                    uint64_t t264 = gm_l17;
                    uint64_t r265; bool o266 = __builtin_add_overflow(t264, ((uint64_t)1ULL), &r265);
                    uint64_t t267 = r265;
                    gm_l17 = t267;
                }
            }
gm_ll99:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb95; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc95; }
            }
            goto gm_lad;
lb95:;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l32 = ((uint64_t)0ULL);
        }
        {
            uint64_t t269 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
            gm_l31 = t269;
        }
        {
            for (;;) {
                uint64_t t271 = gm_l32;
                uint64_t t272 = gm_l31;
                uint8_t t273 = (t271 < t272);
                if (!t273) goto lb270;
                {
                    uint64_t t276 = gm_l32;
                    uint64_t t277 = t276;
                    uint64_t t280 = GM_LD(uint64_t, ((uint8_t *)&t277));
                    gm_l33 = t280;
                    {
                        uint64_t t282 = gm_l33;
                        uint64_t t283 = (uintptr_t)t282;
                        uint64_t t284 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                        if (t283 >= (uintptr_t)(t284)) gm_trap_index(t283, t284, gm_srcn13n69hd0cht, 248, 16);
                        void * t285 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t283 * 56);
                        gm_l34 = t285;
                    }
                    {
                        void * t286 = gm_l34;
                        gm_b16_8 t287 = GM_LD(gm_b16_8, ((((uint8_t *)t286) + 16) + 0));
                        gm_closure t288 = ((gm_closure){GM_PTR_TAG(gm_f_fn_o57h82n), NULL, cx});
                        gm_f_sort_by_m9qke1u(t287, t288);
                        if (gm_unw.kind) {
                            goto gm_ll274;
                        }
                    }
                    {
                        void * t289 = gm_l34;
                        void * t290 = (void *)(((uint8_t *)t289) + 16);
                        void * t291 = gm_l34;
                        gm_b16_8 t292 = GM_LD(gm_b16_8, ((((uint8_t *)t291) + 16) + 0));
                        uint64_t t293 = gm_f_dedup_8ks2cpi(t292);
                        gm_f_truncate_tgh982e(t290, t293);
                    }
                    {
                        gm_b16_8 t294;
                        GM_ST(void *, (uint8_t *)&t294, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t294 + 8, (uintptr_t)(0));
                        gm_l35 = t294;
                    }
                    {
                        {
                            gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_l36 = t296;
                        }
                        {
                            gm_l37 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t298 = gm_l37;
                                uint64_t t299 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                uint8_t t300 = (t298 < t299);
                                if (!t300) goto lb297;
                                {
                                    uint64_t t303 = gm_l37;
                                    uint64_t t304 = (uintptr_t)t303;
                                    uint64_t t305 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                    gm_t6j6smj7qoctu t308 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t304 * 32));
                                    gm_l38 = t308;
                                    {
                                        gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 0));
                                        void * t312 = gm_l34;
                                        gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)t312) + 0));
                                        gm_b16_8 t314 = t311;
                                        gm_b16_8 t315 = t313;
                                        uint8_t t316 = (gm_eq0iqiubfm7e(((uint8_t *)&t314), ((uint8_t *)&t315)));
                                        if (!t316) goto ie310;
                                        {
                                            gm_b16_8 t318 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 16));
                                            gm_l35 = t318;
                                        }
                                        goto ix310;
ie310:;
ix310:;
                                    }
                                    goto me306;
mn307:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 252, 9);
me306:;
                                }
lc297:;
                                {
                                    uint64_t t320 = gm_l37;
                                    uint64_t r321; bool o322 = __builtin_add_overflow(t320, ((uint64_t)1ULL), &r321);
                                    uint64_t t323 = r321;
                                    gm_l37 = t323;
                                }
                            }
lb297:;
                        }
                    }
                    {
                        gm_b16_8 t324 = gm_l35;
                        gm_f_line_index_2necgtf((void *)&gm_l39, (void *)NULL, cx, NULL, t324);
                        if (gm_unw.kind) {
                            goto gm_ll274;
                        }
                    }
                    {
                        gm_f_list_0202sdh_3((void *)&F.l40, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll274;
                        }
                    }
                    {
                        {
                            void * t326 = gm_l34;
                            gm_b16_8 t327 = GM_LD(gm_b16_8, ((((uint8_t *)t326) + 16) + 0));
                            gm_l41 = t327;
                        }
                        {
                            gm_l42 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t329 = gm_l42;
                                uint64_t t330 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                uint8_t t331 = (t329 < t330);
                                if (!t331) goto lb328;
                                {
                                    uint64_t t334 = gm_l42;
                                    uint64_t t335 = (uintptr_t)t334;
                                    uint64_t t336 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                    gm_tcc1rq4n1avb2 t339 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t335 * 8));
                                    gm_l43 = t339;
                                    {
                                        gm_f_obj_aoa7fl7((void *)&F.l44, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll332;
                                        }
                                    }
                                    {
                                        void * t341 = (void *)((uint8_t *)&F.l44);
                                        gm_b16_8 t342;
                                        GM_ST(void *, (uint8_t *)&t342, (void *)(gm_sl4ns6hobicht));
                                        GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(5));
                                        void * t343 = (void *)((uint8_t *)&gm_l39);
                                        uint32_t t344 = GM_LD(uint32_t, (((uint8_t *)&gm_l43) + 0));
                                        uint64_t t345 = (uint64_t)t344;
                                        uint32_t t346 = GM_LD(uint32_t, (((uint8_t *)&gm_l43) + 4));
                                        uint64_t t347 = (uint64_t)t346;
                                        void * t348 = gm_l0;
                                        gm_t9qkh0rsooc8d t349 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t348) + 48));
                                        gm_t8vuock3jnv59 t350;
                                        gm_f_range_json_hlsolbo((void *)&t350, (void *)NULL, cx, NULL, t343, t345, t347, t349);
                                        if (gm_unw.kind) {
                                            goto gm_ll332;
                                        }
                                        gm_b0_1 t351 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t341, t342, &t350);
                                        if (gm_unw.kind) {
                                            goto gm_ll332;
                                        }
                                    }
                                    {
                                        void * t352 = (void *)((uint8_t *)&F.l44);
                                        gm_b16_8 t353;
                                        GM_ST(void *, (uint8_t *)&t353, (void *)(gm_sirpcjgkmlehr));
                                        GM_ST(uintptr_t, (uint8_t *)&t353 + 8, (uintptr_t)(7));
                                        gm_b16_8 t354 = gm_l3;
                                        gm_t8vuock3jnv59 t355;
                                        gm_f_text_ls49dp6((void *)&t355, t354);
                                        gm_b0_1 t356 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t352, t353, &t355);
                                        if (gm_unw.kind) {
                                            goto gm_ll332;
                                        }
                                    }
                                    {
                                        void * t357 = (void *)((uint8_t *)&F.l40);
                                        void * t358 = (void *)((uint8_t *)&F.l44);
                                        gm_t8vuock3jnv59 t359;
                                        gm_f_value_pkuqal3((void *)&t359, t358);
                                        gm_tftb1odc739t5 t360 = gm_f_push_0pgacsj_3(t357, &t359);
                                        gm_tftb1odc739t5 t361 = t360;
                                        if (((((uint8_t *)&t361))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t361) + 1, gm_srcn13n69hd0cht, 261, 13);
                                        gm_b0_1 t362 = GM_LD(gm_b0_1, (((uint8_t *)&t361) + 1));
                                    }
                                    goto me337;
mn338:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 257, 9);
me337:;
                                }
lc328:;
                                {
                                    uint64_t t364 = gm_l42;
                                    uint64_t r365; bool o366 = __builtin_add_overflow(t364, ((uint64_t)1ULL), &r365);
                                    uint64_t t367 = r365;
                                    gm_l42 = t367;
                                }
                            }
gm_ll332:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb328; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc328; }
                            }
                            goto gm_ll274;
lb328:;
                        }
                    }
                    {
                        void * t368 = (void *)((uint8_t *)&F.l30);
                        void * t369 = gm_l0;
                        void * t370 = GM_LD(void *, (((uint8_t *)t369) + 56));
                        void * t371 = gm_l34;
                        gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)t371) + 0));
                        gm_b16_8 t373 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t370, t372);
                        if (gm_unw.kind) {
                            goto gm_ll274;
                        }
                        gm_b16_8 t374 = GM_LD(gm_b16_8, (((uint8_t *)&F.l40) + 0));
                        gm_b16_8 t375 = t374;
                        gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&t375));
                        gm_t8vuock3jnv59 t377;
                        gm_f_array_k32ana3((void *)&t377, (void *)NULL, cx, NULL, t376);
                        if (gm_unw.kind) {
                            goto gm_ll274;
                        }
                        gm_b0_1 t378 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t368, t373, &t377);
                        if (gm_unw.kind) {
                            goto gm_ll274;
                        }
                    }
                    goto me278;
mn279:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 247, 5);
me278:;
                }
lc270:;
                {
                    uint64_t t380 = gm_l32;
                    uint64_t r381; bool o382 = __builtin_add_overflow(t380, ((uint64_t)1ULL), &r381);
                    uint64_t t383 = r381;
                    gm_l32 = t383;
                }
            }
gm_ll274:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb270; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc270; }
            }
            goto gm_lad;
lb270:;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t384 = (void *)((uint8_t *)&F.l45);
        gm_b16_8 t385;
        GM_ST(void *, (uint8_t *)&t385, (void *)(gm_ssvcneuafnfds));
        GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(7));
        void * t386 = (void *)((uint8_t *)&F.l30);
        gm_t8vuock3jnv59 t387;
        gm_f_value_pkuqal3((void *)&t387, t386);
        gm_b0_1 t388 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t384, t385, &t387);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t389 = (void *)((uint8_t *)&F.l45);
        gm_t8vuock3jnv59 t390;
        gm_f_value_pkuqal3((void *)&t390, t389);
        gm_t9l835tjp727s t391;
        GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t391 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t391 + 4, 0);
        ((uint8_t *)&t391)[0] = 0;
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t391 + 8), t390);
        gm_t9l835tjp727s t392 = t391;
        uint8_t t393 = ((((uint8_t *)&t392))[0] != 0);
        F.ret = t391;
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
inline __attribute__((aligned(4))) void gm_f_text_of_qs06tq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_text_of_qs06tq9 F;
    void * gm_l0; /* rq */
    void * gm_l1; /* found */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* t */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* o */
    gm_b16_8 gm_l7; /* bytes */
    GM_STACK_CHECK(&F, 662u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_th2lqbo77pvsc t5;
        gm_f_found_text_sajr2an((void *)&t5, t3, t4);
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_b16_8 t9 = gm_l3;
            gm_th2lqbo77pvsc t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t10), t9);
            ((uint8_t *)&t10)[16] = 1;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t13 = gm_l0;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 32));
            gm_l4 = t14;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l5;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l5;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t6j6smj7qoctu t26 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t22 * 32));
                    gm_l6 = t26;
                    {
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                        gm_b16_8 t30 = gm_l2;
                        gm_b16_8 t31 = t29;
                        gm_b16_8 t32 = t30;
                        uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
                        if (!t33) goto ie28;
                        {
                            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                            gm_th2lqbo77pvsc t36;
                            GM_ST(uint8_t, (uint8_t *)&t36 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t36 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t36 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t36), t35);
                            ((uint8_t *)&t36)[16] = 1;
                            uint8_t t37 = 0;
                            F.ret = t36;
                            goto gm_fret;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn13n69hd0cht, 88, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t39 = gm_l5;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l5 = t42;
                }
            }
lb15:;
        }
    }
    {
        gm_b16_8 t43 = gm_l2;
        gm_tgvh15qshf643 t44;
        gm_f_read_all_r9rh9qd((void *)&t44, (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t45 = t44;
        gm_b16_8 t46;
        if (!((((uint8_t *)&t45))[0] != 0)) {
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
            t46 = t47;
        } else {
            gm_th2lqbo77pvsc t48;
            memset(&t48, 0, sizeof t48);
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        gm_l7 = t46;
    }
    {
        gm_b16_8 t50 = gm_l7;
        gm_b16_8 t51 = t50;
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        gm_th2lqbo77pvsc t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t53), t52);
        ((uint8_t *)&t53)[16] = 1;
        uint8_t t54 = 0;
        F.ret = t53;
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

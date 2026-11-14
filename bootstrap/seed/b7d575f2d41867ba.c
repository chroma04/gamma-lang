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
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_t586m754bse2v;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_td7tfovebirdn;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tgat3d8836orb;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; } gm_th6ngf7aenlq3;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_ti71ll5knnvij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { gm_t3mf58gv9kf3n a[2]; } gm_tmmlu9bk8dch4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; } gm_tn96msocmj81h;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9805qtonbsag v0; } f1; } gm_tnrltbc61i6v1;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; uint32_t f6; uint8_t pe[4]; } gm_tobu7tlrkigeq;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t586m754bse2v) == 120 && _Alignof(gm_t586m754bse2v) == 8 && offsetof(gm_t586m754bse2v, f0) == 0 && offsetof(gm_t586m754bse2v, f1) == 16 && offsetof(gm_t586m754bse2v, f2) == 32 && offsetof(gm_t586m754bse2v, f3) == 40 && offsetof(gm_t586m754bse2v, f4) == 56 && offsetof(gm_t586m754bse2v, f5) == 72 && offsetof(gm_t586m754bse2v, f6) == 88 && offsetof(gm_t586m754bse2v, f7) == 104, "layout of `([]u8, string, u32, []u8, []u8, []u8, []u8, []u8)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_th2lqbo77pvsc v1; } f1; } gm_t6j4d6ei14c2t;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thognv9odov03 v0; gm_tiilmfd16v3fc v1; } f1; } gm_tbf8u1b86cgmi;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td7tfovebirdn) == 40 && _Alignof(gm_td7tfovebirdn) == 8 && offsetof(gm_td7tfovebirdn, f0) == 0 && offsetof(gm_td7tfovebirdn, f1) == 16 && offsetof(gm_td7tfovebirdn, f2) == 32, "layout of `([]u8, string, u32)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgat3d8836orb) == 40 && _Alignof(gm_tgat3d8836orb) == 8 && offsetof(gm_tgat3d8836orb, f0) == 0 && offsetof(gm_tgat3d8836orb, f1) == 4 && offsetof(gm_tgat3d8836orb, f2) == 8 && offsetof(gm_tgat3d8836orb, f3) == 24, "layout of `(u32, u32, string, string)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th6ngf7aenlq3) == 128 && _Alignof(gm_th6ngf7aenlq3) == 8 && offsetof(gm_th6ngf7aenlq3, f0) == 0 && offsetof(gm_th6ngf7aenlq3, f1) == 16 && offsetof(gm_th6ngf7aenlq3, f2) == 24 && offsetof(gm_th6ngf7aenlq3, f3) == 40 && offsetof(gm_th6ngf7aenlq3, f4) == 56 && offsetof(gm_th6ngf7aenlq3, f5) == 72 && offsetof(gm_th6ngf7aenlq3, f6) == 80 && offsetof(gm_th6ngf7aenlq3, f7) == 96 && offsetof(gm_th6ngf7aenlq3, f8) == 112, "layout of `([]u8, u32, string, string, string, u32, []u8, string, string)`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_ti71ll5knnvij) == 72 && _Alignof(gm_ti71ll5knnvij) == 8 && offsetof(gm_ti71ll5knnvij, f0) == 0 && offsetof(gm_ti71ll5knnvij, f1) == 16 && offsetof(gm_ti71ll5knnvij, f2) == 32 && offsetof(gm_ti71ll5knnvij, f3) == 40 && offsetof(gm_ti71ll5knnvij, f4) == 56, "layout of `(string, []u8, usize, []u8, string)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tmmlu9bk8dch4) == 32 && _Alignof(gm_tmmlu9bk8dch4) == 8 && offsetof(gm_tmmlu9bk8dch4, a) == 0, "layout of `[2](^Expr, ^Expr)`");
_Static_assert(sizeof(gm_tn96msocmj81h) == 136 && _Alignof(gm_tn96msocmj81h) == 8 && offsetof(gm_tn96msocmj81h, f0) == 0 && offsetof(gm_tn96msocmj81h, f1) == 16 && offsetof(gm_tn96msocmj81h, f2) == 32 && offsetof(gm_tn96msocmj81h, f3) == 48 && offsetof(gm_tn96msocmj81h, f4) == 64 && offsetof(gm_tn96msocmj81h, f5) == 72 && offsetof(gm_tn96msocmj81h, f6) == 88 && offsetof(gm_tn96msocmj81h, f7) == 104 && offsetof(gm_tn96msocmj81h, f8) == 120, "layout of `(string, string, string, string, u64, string, string, string, string)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tnrltbc61i6v1) == 12 && _Alignof(gm_tnrltbc61i6v1) == 4 && offsetof(gm_tnrltbc61i6v1, f0) == 0 && offsetof(gm_tnrltbc61i6v1, f1) == 4, "layout of `Observed`");
_Static_assert(sizeof(gm_tobu7tlrkigeq) == 88 && _Alignof(gm_tobu7tlrkigeq) == 8 && offsetof(gm_tobu7tlrkigeq, f0) == 0 && offsetof(gm_tobu7tlrkigeq, f1) == 16 && offsetof(gm_tobu7tlrkigeq, f2) == 24 && offsetof(gm_tobu7tlrkigeq, f3) == 40 && offsetof(gm_tobu7tlrkigeq, f4) == 56 && offsetof(gm_tobu7tlrkigeq, f5) == 64 && offsetof(gm_tobu7tlrkigeq, f6) == 80, "layout of `([]u8, u32, string, string, u64, string, u32)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { void * f0; gm_b16_8 f1; gm_tnrltbc61i6v1 f2; uint8_t pe[4]; } gm_ts0eitcfeteg8;
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t6j4d6ei14c2t) == 40 && _Alignof(gm_t6j4d6ei14c2t) == 8 && offsetof(gm_t6j4d6ei14c2t, f0) == 0 && offsetof(gm_t6j4d6ei14c2t, f1) == 8, "layout of `Unsup!?string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbf8u1b86cgmi) == 48 && _Alignof(gm_tbf8u1b86cgmi) == 8 && offsetof(gm_tbf8u1b86cgmi, f0) == 0 && offsetof(gm_tbf8u1b86cgmi, f1) == 8, "layout of `ScopeKind`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_tbf8u1b86cgmi f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint64_t f3; } gm_tehgl6lkqgvt0;
typedef struct { gm_ts0eitcfeteg8 f0; uint8_t f1; uint8_t pe[7]; } gm_tjefu848ssvm9;
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_ts0eitcfeteg8) == 40 && _Alignof(gm_ts0eitcfeteg8) == 8 && offsetof(gm_ts0eitcfeteg8, f0) == 0 && offsetof(gm_ts0eitcfeteg8, f1) == 8 && offsetof(gm_ts0eitcfeteg8, f2) == 24, "layout of `DestCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_tehgl6lkqgvt0 f0; uint8_t f1; uint8_t pe[7]; } gm_t1g28mkc78m0c;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tehgl6lkqgvt0) == 104 && _Alignof(gm_tehgl6lkqgvt0) == 8 && offsetof(gm_tehgl6lkqgvt0, f0) == 0 && offsetof(gm_tehgl6lkqgvt0, f1) == 48 && offsetof(gm_tehgl6lkqgvt0, f2) == 56 && offsetof(gm_tehgl6lkqgvt0, f3) == 96, "layout of `Scope`");
_Static_assert(sizeof(gm_tjefu848ssvm9) == 48 && _Alignof(gm_tjefu848ssvm9) == 8 && offsetof(gm_tjefu848ssvm9, f0) == 0 && offsetof(gm_tjefu848ssvm9, f1) == 40, "layout of `?DestCall`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t1g28mkc78m0c) == 112 && _Alignof(gm_t1g28mkc78m0c) == 8 && offsetof(gm_t1g28mkc78m0c, f0) == 0 && offsetof(gm_t1g28mkc78m0c, f1) == 104, "layout of `?Scope`");
struct gm_fr_adt_def_ni54he1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_arg_may_overlap_u7aort7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_call_closure_mha9p1e {
    gm_frame h;
    gm_tddtjfggdat0d l6; /* sig */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_fn_q122g0r {
    gm_frame h;
    gm_trqfvmjtt4sij l15; /* tys */
    gm_trqfvmjtt4sij l22; /* args */
    gm_trqfvmjtt4sij l28; /* rest */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_indirect_khk02ee {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* ps */
    gm_trqfvmjtt4sij l18; /* pargs */
    gm_b16_8 ret;
};
struct gm_fr_call_inner_i2bbshq {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_trqfvmjtt4sij l8; /* vals */
    gm_trqfvmjtt4sij l9; /* tys */
    gm_tddtjfggdat0d l29; /* sig */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_nk38pfa {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_ts0eitcfeteg8 l6; /* dc */
    gm_t5o0tpv5jka2l l7; /* r */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_result_vg14o5p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_call_site_8n74to1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_callee_borrows_mk6en5r {
    gm_frame h;
    gm_tsth9qu1dv0g3 l1; /* callee */
    uint8_t ret;
};
struct gm_fr_callee_name_4k9m9s6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_calls_emit_plain_euobq8f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_calls_native_set_iq1d80t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_conv_attr_m0b89pc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_equal_hcuag9d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_format_stub_53r45gt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_ptr_type_f9efs3c {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* ps */
    gm_t9bmh80edou9c l5; /* <temporary array> */
    gm_b16_8 ret;
};
struct gm_fr_fn_sink_stub_5ipff9h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_gamma_arg_ldsagr2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_gamma_args_jb6l3ik {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_gamma_value_rgbfhcc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_handler_decl_m66mgr8 {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_lit_b02boff {
    gm_frame h;
    void * ret;
};
struct gm_fr_in_place_root_417o9i7 {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_710oev4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_u9c0aqb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_native_op_r81a4at {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_out_call_uj833o2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_out_stub_ils4a69 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_perform_t77ghfg {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_piece_stub_m8mb7rc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_plain_args_o7acjm5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_plain_value_e380k1u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_g2usrvd {
    gm_frame h;
    gm_tehgl6lkqgvt0 l2; /* x */
    gm_t1g28mkc78m0c ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_type_plain_sanetd4 {
    gm_frame h;
    gm_tddtjfggdat0d l2; /* sig */
    uint8_t ret;
};
struct gm_fr_result_var_rn01cvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_str_equal_at_mshane1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_stub_perform_m3dbb7g {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_target_ret_q7qjs0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_try_expr_ln1m7b8 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_variant_equal_bgud9j0 {
    gm_frame h;
    gm_tckdobunaulee l16; /* variant */
    gm_t6j4d6ei14c2t ret;
};
struct gm_fr_with_expr_l1uk88k {
    gm_frame h;
    gm_tehgl6lkqgvt0 l38; /* scope */
    gm_t5o0tpv5jka2l l40; /* r */
    gm_tehgl6lkqgvt0 l41; /* scope2 */
    gm_t5o0tpv5jka2l ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_arg_may_overlap_u7aort7(void * a0, gm_tnrltbc61i6v1 a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) void gm_f_call_fn_q122g0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_call_indirect_khk02ee(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, uint8_t a7, uint8_t a8);
__attribute__((aligned(4))) void gm_f_call_inner_i2bbshq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, const void * a4, gm_tnrltbc61i6v1 a5);
__attribute__((aligned(4))) void gm_f_call_nk38pfa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_call_result_vg14o5p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_call_site_8n74to1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) uint8_t gm_f_callee_borrows_mk6en5r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_calls_emit_plain_euobq8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_const_arg_8kiicv6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) void gm_f_ctl_perform_be6h6eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_decl_table_l7u1k0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_direct_perform_vc33l18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_ptr_type_f9efs3c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_format_01h4tbj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0mlfk3c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0mp0khb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_18tu4v6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2d30afk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_35to3im(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_37aah2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3o89hhj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3s8s1g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_570tujl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5p0qm94(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6m84e0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7cklfe7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7tqfnpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8lfevbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9g4m5fi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_b2nifvt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dn5pqnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_drnt54k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eir8aii(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g55p0hj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g9ssmu3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gjujk09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h6uc0qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ht2r43m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_itcjtai(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jp1qc7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l0bpsnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lc73epv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mgf81l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_mjg3n5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n1vqu08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nivudut(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p19pl6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qj6rkil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_r9dvh2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rslrdtj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ru16275(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s4f2nfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sjgrpqb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tcvndjh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_field_nj8m22n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_frame_pure_cbdra2q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_arg_ldsagr2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_args_jb6l3ik(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_table_2kdtlit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_in_place_root_417o9i7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ctl_oclllo3(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_temp_rrllgdr(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_0v7c424(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_18bk901(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3fol08t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4ku5dms(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_98ea3qh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_arl1b28(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_bg1q0dr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ch11let(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_d2o1cif(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gg3n7rd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_i03em5a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_i5ai2tu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_icst3ps(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_lot5c36(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_m4ia8qg(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_o1esdvv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qg7idri(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ubknr7g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_res_riv19po(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_may_unwind_irl2ktm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_native_op_r81a4at(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, const void * a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_nested_spec_po688kb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_no_writes_p1jjjfj(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_present_joptd3k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_passed_spec_rvmvr8h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_plain_args_o7acjm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pcqb56b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_queue_spec_khngsmv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_during_build_ng10fmi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_is_err_11p8qlp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_scrutinee_addr_3fvmbme(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_handler_table_8h552cp(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_specializable_7or9fsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_t9805qtonbsag a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_str_equal_at_mshane1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stub_perform_m3dbb7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7);
__attribute__((aligned(4))) uint8_t gm_f_takes_fn_n239ee5(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_try_expr_ln1m7b8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_variant_equal_bgud9j0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_with_expr_l1uk88k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5);
__attribute__((aligned(4))) void gm_f_with_on_fl0pjo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, void * a5, void * a6);
__attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_502hm86(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6o17mnv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fp9dg1h(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_mv65o15(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_scdecr6(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vp4v8ut(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0aqhubr4upni[7];
extern const uint8_t gm_s1k3lfare8sei[4];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s3jl3afv8nlvv[4];
extern const uint8_t gm_s3vqae7u5ljtm[14];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s6cju4qiijn6t[23];
extern const uint8_t gm_s8o6lm1cn4eo5[3];
extern const uint8_t gm_s9go9vvkpcjpd[48];
extern const uint8_t gm_s9u3cqhrfmcru[13];
extern const uint8_t gm_sb6j3egm9oljk[5];
extern const uint8_t gm_sbp38o7rrfqgd[19];
extern const uint8_t gm_sc4r4l9k6f577[3];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_siihmb4jo3dsn[125];
extern const uint8_t gm_sjr00kolufse9[19];
extern const uint8_t gm_sk8csn7us23m3[9];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_sl8of2tnl835f[6];
extern const uint8_t gm_slq58vfha8k9h[2];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_sono5n5lrjd7h[10];
extern const uint8_t gm_sp5b2hbslij9l[5];
extern const uint8_t gm_spsh7mv5lihcc[34];
extern const uint8_t gm_sqi7tb51umq69[13];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcmeaag4q4qrjl[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_st6to3kuud2ge[46];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_su2c9m69n1vjb[31];
extern const uint8_t gm_svubtddeidu1d[2];
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
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_def_ni54he1 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    void * t5 = gm_f_adt_of_gej99vn(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)6ULL)));
    if (t5) {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint64_t t8 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (t8 > ((uint64_t)16ULL));
        t1 = t9;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_callee_name_4k9m9s6 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_b16_8 t4 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t5 = 0;
        F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_calls_native_set_iq1d80t F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* res */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* r */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)a1))[16] != 0)) goto ie2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l3 = t3;
        {
            void * t5 = gm_l0;
            gm_t6j6smj7qoctu t6;
            gm_b16_8 t7 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
            gm_b16_8 t8 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
            gm_b0_1 t9 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t5, &t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_unwind_2hji4bl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 296));
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_smr5ddngcjuao));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(18));
        gm_b0_1 t10 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l0;
        gm_b0_1 t12 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t11);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conv_attr_m0b89pc F;
    uint8_t gm_l0; /* win64 */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t3 = gm_l0;
    if (!t3) goto ie2;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_skdpq4t6o94o2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(24));
    gm_b16_8 t6 = t5;
    t1 = t6;
    goto ix2;
ie2:;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
    gm_b16_8 t9 = t8;
    t1 = t9;
ix2:;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 480, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 360));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 481, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 360), t13);
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 232));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_decl_m66mgr8 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 544) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 132, 83);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 544) + 0)) + t3 * 64);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_lit_b02boff F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 504) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 129, 76);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 504) + 0)) + t3 * 40);
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)5ULL)));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (!t4) {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_tabb710d9mq2e t6;
    gm_f_format_8n626pi((void *)&t6, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcht195tfrfuqo, 80, 91);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_b16_8 t9 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t1, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t9;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_label_710oev4 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 184);
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tftb1odc739t5 t6 = gm_f_write_f1cbljc(t3, t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcmok80ho8hh01, 535, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 184);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcmok80ho8hh01, 464, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 184);
        gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcmok80ho8hh01, 465, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loc_u9c0aqb F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 72));
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t2, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_out_call_uj833o2 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_tfeab279c16rr t6;
        gm_b16_8 t7 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
        gm_b16_8 t8 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
        gm_b16_8 t9 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 32, t9);
        gm_b0_1 t10 = gm_f_linef_bg1q0dr((void *)NULL, cx, NULL, t5, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t11 = gm_l4;
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) goto ie2;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sqtc9fffut4s2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    gm_b16_8 t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = t12;
    t1 = t13;
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_plain_cty_q9lhlci F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_mirror_lcjegsf((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            gm_b16_8 t8 = gm_l2;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13 = 0;
        F.ret = t12;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_g2usrvd F;
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
            gm_t1g28mkc78m0c t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
        gm_tehgl6lkqgvt0 t17 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 104));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 104)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 105, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 106, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 108, 0);
        ((uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out))[104] = 1;
        memmove((uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 0, t29, 104);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uint64_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_result_var_rn01cvq F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 152))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 152));
        gm_l2 = t4;
        {
            void * t6 = gm_l0;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t6) + 152), t7);
        }
        {
            gm_b16_8 t8 = gm_l2;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_b16_8 t12 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13 = 0;
        F.ret = t12;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_target_ret_q7qjs0g F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* f */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_tommf4j9upomc t4;
        gm_f_frame_up_g8486eu((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t5 = t4;
        gm_l2 = GM_LD(uint32_t, (uint8_t *)&t5 + 16);
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint32_t t9 = gm_l2;
        void * t10 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 64));
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t1, t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t6);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t7);
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
inline __attribute__((aligned(4))) uint8_t gm_f_arg_may_overlap_u7aort7(void * a0, gm_tnrltbc61i6v1 a1, uint8_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arg_may_overlap_u7aort7 F;
    void * gm_l0; /* a */
    gm_tnrltbc61i6v1 gm_l1; /* obs */
    uint8_t gm_l2; /* borrows */
    gm_t9805qtonbsag gm_l3; /* root */
    uint32_t gm_l4; /* l */
    uint32_t gm_l5; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l2;
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma10; default: goto ma11; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        uint8_t t15 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn13:;
ma9:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t17 != ((uint8_t)1ULL)) goto mn16;
        void * t18 = gm_l0;
        uint8_t t19;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
        if (t22 != ((uint8_t)15ULL)) goto mn21;
        t19 = ((uint8_t)0);
        goto me20;
mn21:;
        t19 = ((uint8_t)1);
        goto me20;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 132, 24);
me20:;
        uint8_t t24 = 0;
        F.ret = t19;
        goto gm_fret;
        goto me7;
mn16:;
ma10:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t26 != ((uint8_t)2ULL)) goto mn25;
        gm_t9805qtonbsag t27 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 4));
        gm_l3 = t27;
        {
            void * t30 = gm_l0;
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
            if (t31 != ((uint8_t)15ULL)) goto ie29;
            uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)t30) + 8) + 8));
            gm_l4 = t32;
            {
                gm_t9805qtonbsag t34 = gm_l3;
                uint32_t t35;
                if (((((uint8_t *)&t34))[4] != 0)) {
                    uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&t34) + 0));
                    t35 = t36;
                } else {
                    uint8_t t37 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                gm_l5 = t35;
            }
            {
                uint32_t t38 = gm_l5;
                uint32_t t39 = gm_l4;
                uint8_t t40 = ((t38 == t39));
                uint8_t t41 = 0;
                F.ret = t40;
                goto gm_fret;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            uint8_t t42 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me7;
mn25:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 130, 5);
me7:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_call_closure_mha9p1e F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* cv */
    gm_t88rcucg1b7uk gm_l2; /* cty */
    gm_b16_8 gm_l3; /* vals */
    gm_b16_8 gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* ccx */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_tiilmfd16v3fc gm_l11; /* eff */
    uint32_t gm_l12; /* id */
    gm_b16_8 gm_l13; /* n */
    gm_b16_8 gm_l14; /* cx */
    gm_b16_8 gm_l15; /* r */
    GM_STACK_CHECK(&F, 1194u);
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
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_ghvav22((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)15ULL)) goto ie2;
        F.l6 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t7) + 8));
        {
            void * t10 = gm_l0;
            gm_t88rcucg1b7uk t11 = gm_l2;
            gm_b16_8 t12 = gm_l1;
            gm_b16_8 t13 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t13;
        }
        {
            void * t14 = gm_l0;
            uint32_t t15 = gm_f_fresh_5kokjmg(t14);
            gm_t88rcucg1b7uk t16;
            GM_ST(uint32_t, ((uint8_t *)&t16 + 0), t15);
            gm_tabb710d9mq2e t17;
            gm_f_format_qr5c6un((void *)&t17, (void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcmeaag4q4qrjl, 305, 12);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_l8 = t18;
        }
        {
            void * t19 = gm_l0;
            gm_t6j6smj7qoctu t20;
            gm_b16_8 t21 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
            gm_b16_8 t22 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t22);
            gm_b0_1 t23 = gm_f_linef_lot5c36((void *)NULL, cx, NULL, t19, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t25 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 24) + 0));
                gm_l9 = t25;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t27 = gm_l10;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t29 = (t27 < t28);
                    if (!t29) goto lb26;
                    {
                        uint64_t t32 = gm_l10;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tiilmfd16v3fc t37 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t33 * 24));
                        gm_l11 = t37;
                        {
                            void * t39 = gm_l0;
                            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
                            void * t41 = (void *)((uint8_t *)&gm_l11);
                            uint32_t t42 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t40, t41);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                            gm_l12 = t42;
                        }
                        {
                            void * t43 = gm_l0;
                            uint32_t t44 = gm_f_fresh_5kokjmg(t43);
                            gm_t88rcucg1b7uk t45;
                            GM_ST(uint32_t, ((uint8_t *)&t45 + 0), t44);
                            gm_tabb710d9mq2e t46;
                            gm_f_format_mgf81l5((void *)&t46, (void *)NULL, cx, NULL, t45);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcmeaag4q4qrjl, 309, 14);
                            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
                            gm_l13 = t47;
                        }
                        {
                            void * t48 = gm_l0;
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 280));
                            gm_l14 = t49;
                        }
                        {
                            void * t50 = gm_l0;
                            gm_t586m754bse2v t51;
                            memset((uint8_t *)&t51 + 36, 0, 4);
                            gm_b16_8 t52 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
                            gm_b16_8 t53 = gm_l14;
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t53);
                            uint32_t t54 = gm_l12;
                            GM_ST(uint32_t, (uint8_t *)&t51 + 32, t54);
                            gm_b16_8 t55 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 40, t55);
                            gm_b16_8 t56 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 56, t56);
                            gm_b16_8 t57 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 72, t57);
                            gm_b16_8 t58 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 88, t58);
                            gm_b16_8 t59 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 104, t59);
                            gm_b0_1 t60 = gm_f_linef_ubknr7g((void *)NULL, cx, NULL, t50, &t51);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                        }
                        goto me35;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 307, 5);
me35:;
                    }
lc26:;
                    {
                        uint64_t t62 = gm_l10;
                        uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                        uint64_t t65 = r63;
                        gm_l10 = t65;
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
            void * t66 = gm_l0;
            gm_b16_8 t67 = gm_l7;
            gm_tnpifv20irnph t68;
            GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
            gm_tabb710d9mq2e t69;
            gm_f_format_l0bpsnf((void *)&t69, (void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcmeaag4q4qrjl, 313, 27);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_b16_8 t72 = gm_l7;
            gm_tnpifv20irnph t73;
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
            gm_tabb710d9mq2e t74;
            gm_f_format_eir8aii((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srcmeaag4q4qrjl, 313, 54);
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            gm_b16_8 t77 = gm_l8;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_b16_8 t79 = gm_l3;
            gm_b16_8 t80 = gm_l4;
            gm_t88rcucg1b7uk t81 = gm_l5;
            gm_b16_8 t82 = gm_f_call_indirect_khk02ee((void *)NULL, cx, NULL, t66, t71, t76, t78, t79, t80, t81, ((uint8_t)1), ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t82;
        }
        {
            void * t83 = gm_l0;
            gm_b0_1 t84 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t85 = gm_l15;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t85);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            gm_trap("reached `@unreachable()`", gm_srcmeaag4q4qrjl, 303, 40);
        }
ix2:;
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
__attribute__((aligned(4))) void gm_f_call_fn_q122g0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3) {
    struct gm_fr_call_fn_q122g0r F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* f */
    gm_b16_8 gm_l3; /* vals */
    void * gm_l4; /* def */
    gm_b16_8 gm_l5; /* env */
    uint32_t gm_l6; /* p */
    uint32_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* l */
    gm_b16_8 gm_l10; /* up */
    uint32_t gm_l11; /* fid */
    gm_b16_8 gm_l12; /* name */
    gm_tbl8il92l97ut gm_l13; /* sc */
    uint32_t gm_l14; /* s */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l18; /* p */
    gm_b16_8 gm_l19; /* r */
    uint8_t gm_l20; /* foreign_ */
    uint64_t gm_l21; /* n */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    gm_t88rcucg1b7uk gm_l26; /* t */
    gm_b16_8 gm_l27; /* call */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l31; /* a */
    gm_b16_8 gm_l32; /* c2 */
    gm_b16_8 gm_l33; /* cx */
    gm_b16_8 gm_l34; /* args */
    gm_b16_8 gm_l35; /* call */
    GM_STACK_CHECK(&F, 2689u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l2;
        gm_b0_1 t5 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint32_t t9 = gm_l2;
        void * t10 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t10;
    }
    {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s04qus0luoga5));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
        gm_l5 = t11;
    }
    {
        void * t13 = gm_l4;
        if (!(((((uint8_t *)t13) + 88))[4] != 0)) goto ie12;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 88));
        gm_l6 = t14;
        {
            {
                gm_l8 = ((uint32_t)0ULL);
            }
            {
                gm_l7 = ((uint32_t)64ULL);
            }
            {
                for (;;) {
                    uint32_t t18 = gm_l8;
                    uint32_t t19 = gm_l7;
                    uint8_t t20 = (t18 < t19);
                    if (!t20) goto lb17;
                    {
                        uint32_t t23 = gm_l8;
                        uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&t23));
                        gm_l9 = t26;
                        {
                            void * t28 = gm_l0;
                            uint32_t t29 = gm_l9;
                            gm_tommf4j9upomc t30;
                            gm_f_frame_up_g8486eu((void *)&t30, (void *)NULL, cx, NULL, t28, t29);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_tommf4j9upomc t31 = t30;
                            gm_l10 = GM_LD(gm_b16_8, (uint8_t *)&t31 + 0);
                            gm_l11 = GM_LD(uint32_t, (uint8_t *)&t31 + 16);
                        }
                        {
                            uint32_t t33 = gm_l11;
                            uint32_t t34 = gm_l6;
                            uint8_t t35 = ((t33 == t34));
                            if (!t35) goto ie32;
                            {
                                gm_b16_8 t37 = gm_l10;
                                gm_l5 = t37;
                            }
                            {
                                goto lb17;
                            }
                            goto ix32;
ie32:;
ix32:;
                        }
                        goto me24;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 238, 9);
me24:;
                    }
lc17:;
                    {
                        uint32_t t39 = gm_l8;
                        uint32_t r40; bool o41 = __builtin_add_overflow(t39, ((uint32_t)1ULL), &r40);
                        uint32_t t42 = r40;
                        gm_l8 = t42;
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
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(0));
        gm_l12 = t43;
    }
    {
        void * t45 = gm_l0;
        if (!(((((uint8_t *)t45) + 40))[16] != 0)) goto ie44;
        gm_tbl8il92l97ut t46 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)t45) + 40));
        gm_l13 = t46;
        {
            void * t49 = GM_LD(void *, (((uint8_t *)&gm_l13) + 0));
            void * t50 = gm_l1;
            uint8_t t51 = ((t49 == t50));
            if (!t51) goto ie48;
            {
                void * t53 = gm_l0;
                gm_t5lf9i3n1r3co t54;
                memset(&t54, 0, sizeof t54);
                GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t53) + 40), t54);
            }
            {
                void * t55 = gm_l0;
                void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
                uint32_t t57 = gm_l2;
                uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t59 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t56, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t59;
            }
            goto ix48;
ie48:;
ix48:;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
        uint8_t t62 = ((t61 == ((uint64_t)0ULL)));
        if (!t62) goto ie60;
        {
            void * t65 = gm_l0;
            uint32_t t66 = gm_l2;
            gm_t9805qtonbsag t67 = gm_f_passed_spec_rvmvr8h((void *)NULL, cx, NULL, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t67))[4] != 0)) goto ie64;
            uint32_t t68 = GM_LD(uint32_t, ((uint8_t *)&t67));
            gm_l14 = t68;
            {
                void * t70 = gm_l0;
                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
                uint32_t t72 = gm_l2;
                uint32_t t73 = gm_l14;
                gm_b16_8 t74 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t71, t72, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t74;
            }
            goto ix64;
ie64:;
            {
                void * t76 = gm_l0;
                void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                uint32_t t78 = gm_l2;
                gm_b16_8 t79 = gm_f_callee_name_4k9m9s6((void *)NULL, cx, NULL, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t79;
            }
ix64:;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        gm_f_list_nof7vfj((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t81 = gm_l4;
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)t81) + 32));
            gm_l16 = t82;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t84 = gm_l17;
                uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t86 = (t84 < t85);
                if (!t86) goto lb83;
                {
                    uint64_t t89 = gm_l17;
                    uint64_t t90 = (uintptr_t)t89;
                    uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint32_t t92 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t90 * 4));
                    uint32_t t95 = GM_LD(uint32_t, ((uint8_t *)&t92));
                    gm_l18 = t95;
                    {
                        void * t97 = (void *)((uint8_t *)&F.l15);
                        uint32_t t98 = gm_l18;
                        uint64_t t99 = (uint64_t)t98;
                        uint64_t t100 = (uintptr_t)t99;
                        void * t101 = gm_l4;
                        uint64_t t102 = GM_LD(uintptr_t, (((uint8_t *)t101) + 48) + 8);
                        if (t100 >= (uintptr_t)(t102)) gm_trap_index(t100, t102, gm_srcmeaag4q4qrjl, 257, 36);
                        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t101) + 48)) + t100 * 40) + 16));
                        gm_tftb1odc739t5 t104 = gm_f_push_k3l9nic(t97, GM_CONV(gm_csguankko4lmhd, t103));
                        if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcmeaag4q4qrjl, 257, 27);
                        gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                    }
                    goto me93;
mn94:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 257, 5);
me93:;
                }
lc83:;
                {
                    uint64_t t107 = gm_l17;
                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                    uint64_t t110 = r108;
                    gm_l17 = t110;
                }
            }
lb83:;
        }
    }
    {
        gm_b16_8 t111;
        GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(0));
        gm_l19 = t111;
    }
    {
        void * t113 = gm_l0;
        void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
        uint32_t t115 = gm_l2;
        uint8_t t116 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t114, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t116) goto ie112;
        {
            void * t118 = gm_l4;
            uint8_t t119;
            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t118) + 96) + 0));
            if (t122 != ((uint8_t)3ULL)) goto mn121;
            t119 = ((uint8_t)1);
            goto me120;
mn121:;
            t119 = ((uint8_t)0);
            goto me120;
mn123:;
            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 261, 21);
me120:;
            gm_l20 = t119;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t126 = (t124 < t125 ? t124 : t125);
            gm_l21 = t126;
        }
        {
            gm_f_list_epvducg((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                uint64_t t128 = gm_l21;
                gm_l23 = t128;
            }
            {
                for (;;) {
                    uint64_t t130 = gm_l24;
                    uint64_t t131 = gm_l23;
                    uint8_t t132 = (t130 < t131);
                    if (!t132) goto lb129;
                    {
                        uint64_t t135 = gm_l24;
                        uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&t135));
                        gm_l25 = t138;
                        {
                            uint64_t t140 = gm_l25;
                            uint64_t t141 = (uintptr_t)t140;
                            uint64_t t142 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                            if (t141 >= (uintptr_t)(t142)) gm_trap_index(t141, t142, gm_srcmeaag4q4qrjl, 265, 18);
                            gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t141 * 4));
                            gm_l26 = t143;
                        }
                        {
                            uint8_t t145 = gm_l20;
                            uint8_t t146 = (uint8_t)!t145;
                            if (!t146) goto ie144;
                            void * t147 = gm_l0;
                            void * t148 = GM_LD(void *, (((uint8_t *)t147) + 0));
                            void * t149 = (void *)(((uint8_t *)t148) + 8);
                            gm_t88rcucg1b7uk t150 = gm_l26;
                            uint8_t t151 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t149, t150);
                            if (gm_unw.kind) {
                                goto gm_ll133;
                            }
                            if (!t151) goto ie144;
                            {
                                void * t153 = (void *)((uint8_t *)&F.l22);
                                void * t154 = gm_l0;
                                gm_t88rcucg1b7uk t155 = gm_l26;
                                uint64_t t156 = gm_l25;
                                uint64_t t157 = (uintptr_t)t156;
                                uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t157 >= (uintptr_t)(t158)) gm_trap_index(t157, t158, gm_srcmeaag4q4qrjl, 268, 43);
                                gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t157 * 16));
                                gm_b16_8 t160 = gm_f_gamma_arg_ldsagr2((void *)NULL, cx, NULL, t154, t155, t159);
                                if (gm_unw.kind) {
                                    goto gm_ll133;
                                }
                                gm_tftb1odc739t5 t161 = gm_f_push_3ef746b(t153, t160);
                                if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srcmeaag4q4qrjl, 268, 17);
                                gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                            }
                            goto ix144;
ie144:;
                            {
                                void * t164 = (void *)((uint8_t *)&F.l22);
                                void * t165 = gm_l0;
                                gm_t88rcucg1b7uk t166 = gm_l26;
                                uint64_t t167 = gm_l25;
                                uint64_t t168 = (uintptr_t)t167;
                                uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t168 >= (uintptr_t)(t169)) gm_trap_index(t168, t169, gm_srcmeaag4q4qrjl, 270, 45);
                                gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t168 * 16));
                                gm_b16_8 t171 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t165, t166, t170);
                                if (gm_unw.kind) {
                                    goto gm_ll133;
                                }
                                gm_tftb1odc739t5 t172 = gm_f_push_3ef746b(t164, t171);
                                if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_srcmeaag4q4qrjl, 270, 17);
                                gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
                            }
ix144:;
                        }
                        goto me136;
mn137:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 264, 9);
me136:;
                    }
lc129:;
                    {
                        uint64_t t175 = gm_l24;
                        uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                        uint64_t t178 = r176;
                        gm_l24 = t178;
                    }
                }
gm_ll133:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb129; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc129; }
                }
                goto gm_lad;
lb129:;
            }
        }
        {
            void * t179 = (void *)((uint8_t *)&F.l22);
            uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t181 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t182 = gm_l21;
            uint64_t t183 = (uintptr_t)t182;
            if (t183 > (uintptr_t)t180 || (uintptr_t)t180 > (uintptr_t)t180) gm_trap_slice(t183, (uintptr_t)t180, t180, gm_srcmeaag4q4qrjl, 273, 21);
            gm_b16_8 t184;
            GM_ST(void *, (uint8_t *)&t184, (void *)(((uint8_t *)t181 + t183 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)((uintptr_t)t180 - t183));
            gm_tftb1odc739t5 t185 = gm_f_extend_vogsd72(t179, t184);
            if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 1, gm_srcmeaag4q4qrjl, 273, 9);
            gm_b0_1 t186 = GM_LD(gm_b0_1, (((uint8_t *)&t185) + 1));
        }
        {
            gm_t6j6smj7qoctu t187;
            gm_b16_8 t188 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t187 + 0, t188);
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
            gm_b16_8 t191;
            GM_ST(void *, (uint8_t *)&t191, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t192;
            gm_f_join_sfknljb((void *)&t192, (void *)NULL, cx, NULL, t190, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 8, gm_srcmeaag4q4qrjl, 274, 51);
            gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&t192) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t187 + 16, t193);
            gm_tabb710d9mq2e t194;
            gm_f_format_99gc605((void *)&t194, (void *)NULL, cx, NULL, &t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 8, gm_srcmeaag4q4qrjl, 274, 24);
            gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&t194) + 8));
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
            gm_l27 = t196;
        }
        {
            uint8_t t198 = gm_l20;
            uint8_t t199 = (uint8_t)!t198;
            if (!t199) goto ie197;
            void * t200 = gm_l0;
            void * t201 = GM_LD(void *, (((uint8_t *)t200) + 0));
            void * t202 = (void *)(((uint8_t *)t201) + 8);
            void * t203 = gm_l4;
            gm_t88rcucg1b7uk t204 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t203) + 64));
            uint8_t t205 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t202, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t205) goto ie197;
            {
                gm_f_list_uvfu1k1((void *)&F.l28, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                    gm_l29 = t208;
                }
                {
                    gm_l30 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t210 = gm_l30;
                        uint64_t t211 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        uint8_t t212 = (t210 < t211);
                        if (!t212) goto lb209;
                        {
                            uint64_t t215 = gm_l30;
                            uint64_t t216 = (uintptr_t)t215;
                            uint64_t t217 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t216 * 16));
                            gm_l31 = t220;
                            {
                                void * t222 = (void *)((uint8_t *)&F.l28);
                                gm_b16_8 t223 = gm_l31;
                                gm_tnpifv20irnph t224;
                                GM_ST(gm_b16_8, ((uint8_t *)&t224 + 0), t223);
                                gm_tftb1odc739t5 t225 = gm_f_write_fp9dg1h(t222, t224);
                                if (((((uint8_t *)&t225))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t225) + 1, gm_srcmeaag4q4qrjl, 277, 35);
                                gm_b0_1 t226 = GM_LD(gm_b0_1, (((uint8_t *)&t225) + 1));
                            }
                            goto me218;
mn219:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 277, 13);
me218:;
                        }
lc209:;
                        {
                            uint64_t t228 = gm_l30;
                            uint64_t r229; bool o230 = __builtin_add_overflow(t228, ((uint64_t)1ULL), &r229);
                            uint64_t t231 = r229;
                            gm_l30 = t231;
                        }
                    }
lb209:;
                }
            }
            {
                void * t232 = gm_l0;
                void * t233 = gm_l4;
                gm_t88rcucg1b7uk t234 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t233) + 64));
                gm_b16_8 t235 = gm_l12;
                gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                gm_b16_8 t238 = gm_f_out_call_uj833o2((void *)NULL, cx, NULL, t232, t234, t235, t237);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t238;
            }
            goto ix197;
ie197:;
            void * t240 = gm_l0;
            void * t241 = GM_LD(void *, (((uint8_t *)t240) + 0));
            void * t242 = (void *)(((uint8_t *)t241) + 8);
            void * t243 = gm_l4;
            gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t243) + 64));
            uint8_t t245 = gm_f_is_void_vs2outo(t242, GM_CONV(gm_csguankko4lmhd, t244));
            if (!t245) goto ie239;
            {
                void * t247 = gm_l0;
                gm_b16_8 t248 = gm_l27;
                gm_tnpifv20irnph t249;
                GM_ST(gm_b16_8, ((uint8_t *)&t249 + 0), t248);
                gm_b0_1 t250 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t247, t249);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t251 = gm_l0;
                void * t252 = gm_l1;
                gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t252) + 0));
                gm_b16_8 t254 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t251, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t254;
            }
            goto ix239;
ie239:;
            {
                gm_b16_8 t256;
                uint8_t t258 = gm_l20;
                uint8_t t259 = (uint8_t)!t258;
                if (!t259) goto ie257;
                void * t260 = gm_l0;
                void * t261 = GM_LD(void *, (((uint8_t *)t260) + 0));
                void * t262 = (void *)(((uint8_t *)t261) + 8);
                void * t263 = gm_l4;
                gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t263) + 64));
                uint8_t t265 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t262, t264);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t265) goto ie257;
                gm_b16_8 t267 = gm_l27;
                gm_b16_8 t268 = t267;
                t256 = t268;
                goto ix257;
ie257:;
                void * t270 = gm_l0;
                void * t271 = gm_l4;
                gm_t88rcucg1b7uk t272 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t271) + 64));
                gm_b16_8 t273 = gm_l27;
                gm_b16_8 t274 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t270, t272, t273);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t275 = t274;
                t256 = t275;
ix257:;
                gm_l32 = t256;
            }
            {
                void * t276 = gm_l0;
                void * t277 = gm_l1;
                gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t277) + 0));
                gm_b16_8 t279 = gm_l32;
                gm_b16_8 t280 = gm_f_call_result_vg14o5p((void *)NULL, cx, NULL, t276, t278, t279);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t280;
            }
ix239:;
ix197:;
        }
        goto ix112;
ie112:;
        {
            void * t282 = gm_l0;
            gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)t282) + 280));
            gm_l33 = t283;
        }
        {
            void * t284 = gm_l0;
            gm_b16_8 t285 = gm_l3;
            gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
            gm_b16_8 t288 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t284, t285, t287);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t288;
        }
        {
            void * t290 = gm_l0;
            void * t291 = GM_LD(void *, (((uint8_t *)t290) + 0));
            void * t292 = (void *)(((uint8_t *)t291) + 8);
            void * t293 = gm_l4;
            gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t293) + 64));
            uint8_t t295 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t292, t294);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t295) goto ie289;
            {
                void * t297 = gm_l0;
                void * t298 = gm_l4;
                gm_t88rcucg1b7uk t299 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t298) + 64));
                gm_b16_8 t300 = gm_l12;
                gm_tfeab279c16rr t301;
                gm_b16_8 t302 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t301 + 0, t302);
                gm_b16_8 t303 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t301 + 16, t303);
                gm_b16_8 t304 = gm_l34;
                GM_ST(gm_b16_8, (uint8_t *)&t301 + 32, t304);
                gm_tabb710d9mq2e t305;
                gm_f_format_0mp0khb((void *)&t305, (void *)NULL, cx, NULL, &t301);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t305))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t305) + 8, gm_srcmeaag4q4qrjl, 290, 44);
                gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&t305) + 8));
                gm_b16_8 t307 = GM_LD(gm_b16_8, ((uint8_t *)&t306));
                gm_b16_8 t308 = gm_f_out_call_uj833o2((void *)NULL, cx, NULL, t297, t299, t300, t307);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t308;
            }
            goto ix289;
ie289:;
            {
                gm_t0uuhaljsf410 t310;
                gm_b16_8 t311 = gm_l12;
                GM_ST(gm_b16_8, (uint8_t *)&t310 + 0, t311);
                gm_b16_8 t312 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t310 + 16, t312);
                gm_b16_8 t313 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t310 + 32, t313);
                gm_b16_8 t314 = gm_l34;
                GM_ST(gm_b16_8, (uint8_t *)&t310 + 48, t314);
                gm_tabb710d9mq2e t315;
                gm_f_format_570tujl((void *)&t315, (void *)NULL, cx, NULL, &t310);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 8, gm_srcmeaag4q4qrjl, 292, 21);
                gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&t315) + 8));
                gm_l35 = t316;
            }
            {
                void * t317 = gm_l0;
                void * t318 = gm_l1;
                gm_t88rcucg1b7uk t319 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t318) + 0));
                gm_b16_8 t320 = gm_l35;
                gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&t320));
                gm_b16_8 t322 = gm_f_call_result_vg14o5p((void *)NULL, cx, NULL, t317, t319, t321);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t322;
            }
ix289:;
        }
ix112:;
    }
    {
        void * t324 = gm_l0;
        void * t325 = GM_LD(void *, (((uint8_t *)t324) + 0));
        uint32_t t326 = gm_l2;
        uint8_t t327 = gm_f_may_unwind_irl2ktm((void *)NULL, cx, NULL, t325, t326);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t327) goto ie323;
        {
            void * t329 = gm_l0;
            gm_b0_1 t330 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t329);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix323;
ie323:;
ix323:;
    }
    {
        gm_b16_8 t331 = gm_l19;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t331);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_call_indirect_khk02ee(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, uint8_t a7, uint8_t a8) {
    struct gm_fr_call_indirect_khk02ee F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* env */
    gm_b16_8 gm_l3; /* cx */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    uint8_t gm_l7; /* hidden */
    uint8_t gm_l8; /* win64 */
    gm_b16_8 gm_l9; /* plain_ret */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t88rcucg1b7uk gm_l13; /* p */
    gm_b16_8 gm_l14; /* plain_params */
    gm_b16_8 gm_l15; /* plain_ty */
    gm_b16_8 gm_l16; /* p */
    gm_th2lqbo77pvsc gm_l17; /* res */
    gm_b16_8 gm_l19; /* plain_call */
    gm_b16_8 gm_l20; /* fp */
    gm_b16_8 gm_l21; /* args */
    gm_b16_8 gm_l22; /* out */
    gm_b16_8 gm_l23; /* r */
    gm_b16_8 gm_l24; /* hidden_call */
    gm_th2lqbo77pvsc gm_l25; /* rv */
    gm_b16_8 gm_l26; /* r */
    GM_STACK_CHECK(&F, 2506u);
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
    gm_l7 = a7;
    gm_l8 = a8;
    {
        gm_b16_8 t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l6;
        uint8_t t8 = gm_f_is_void_vs2outo(t6, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) goto ie3;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
        gm_b16_8 t11 = t10;
        t2 = t11;
        goto ix3;
ie3:;
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l6;
        gm_b16_8 t17 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t18 = t17;
        t2 = t18;
ix3:;
        gm_l9 = t2;
    }
    {
        gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t20 = gm_l5;
            gm_l11 = t20;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t22 = gm_l12;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l12;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t28 * 4));
                    gm_l13 = t32;
                    {
                        void * t34 = (void *)((uint8_t *)&F.l10);
                        void * t35 = gm_l0;
                        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                        void * t37 = (void *)(((uint8_t *)t36) + 8);
                        gm_t88rcucg1b7uk t38 = gm_l13;
                        gm_b16_8 t39 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_ll25;
                        }
                        gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t34, t39);
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srcmeaag4q4qrjl, 343, 20);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 343, 5);
me30:;
                }
lc21:;
                {
                    uint64_t t43 = gm_l12;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l12 = t46;
                }
            }
gm_ll25:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb21; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc21; }
            }
            goto gm_lad;
lb21:;
        }
    }
    {
        gm_b16_8 t47;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
        uint8_t t50 = ((t49 == ((uint64_t)0ULL)));
        if (!t50) goto ie48;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(4));
        gm_b16_8 t53 = t52;
        t47 = t53;
        goto ix48;
ie48:;
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t58;
        gm_f_join_sfknljb((void *)&t58, (void *)NULL, cx, NULL, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srcmeaag4q4qrjl, 344, 67);
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
        gm_b16_8 t60 = t59;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        t47 = t61;
ix48:;
        gm_l14 = t47;
    }
    {
        gm_tfeab279c16rr t62;
        gm_b16_8 t63 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
        uint8_t t64 = gm_l8;
        gm_b16_8 t65 = gm_f_conv_attr_m0b89pc(t64);
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t65);
        gm_b16_8 t66 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 32, t66);
        gm_tabb710d9mq2e t67;
        gm_f_format_7tqfnpi((void *)&t67, (void *)NULL, cx, NULL, &t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srcmeaag4q4qrjl, 345, 17);
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
        gm_l15 = t68;
    }
    {
        void * t69 = gm_l0;
        void * t70 = gm_l0;
        void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
        void * t74 = (void *)(((uint8_t *)t73) + 8);
        gm_t53at80pfdus3 t75;
        memset(&t75, 0, sizeof t75);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t76 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t74, &t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t77 = gm_l1;
        gm_b16_8 t78 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t69, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t78;
    }
    {
        gm_th2lqbo77pvsc t79;
        memset(&t79, 0, sizeof t79);
        gm_l17 = t79;
    }
    {
        void * t81 = gm_l0;
        void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
        void * t83 = (void *)(((uint8_t *)t82) + 8);
        gm_t88rcucg1b7uk t84 = gm_l6;
        uint8_t t85 = gm_f_is_never_in5ftma(t83, GM_CONV(gm_csguankko4lmhd, t84));
        uint8_t t86 = (uint8_t)!t85;
        if (!t86) goto ie80;
        {
            void * t88 = gm_l0;
            gm_t88rcucg1b7uk t89 = gm_l6;
            gm_b16_8 t90 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)) + 0, t90);
        }
        goto ix80;
ie80:;
ix80:;
    }
    {
        void * t91 = gm_l0;
        gm_b16_8 t92 = gm_l4;
        gm_b16_8 t93 = gm_l5;
        gm_f_plain_args_o7acjm5((void *)&F.l18, (void *)NULL, cx, NULL, t91, t92, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tfeab279c16rr t94;
        gm_b16_8 t95 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t95);
        gm_b16_8 t96 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 16, t96);
        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
        gm_b16_8 t99;
        GM_ST(void *, (uint8_t *)&t99, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t100;
        gm_f_join_sfknljb((void *)&t100, (void *)NULL, cx, NULL, t98, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_srcmeaag4q4qrjl, 350, 80);
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 32, t101);
        gm_tabb710d9mq2e t102;
        gm_f_format_35to3im((void *)&t102, (void *)NULL, cx, NULL, &t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_srcmeaag4q4qrjl, 350, 26);
        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
        gm_l19 = t104;
    }
    {
        void * t105 = gm_l0;
        gm_t88rcucg1b7uk t106 = gm_l6;
        gm_b16_8 t107 = gm_l19;
        gm_b16_8 t108 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t105, t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t108;
    }
    {
        uint8_t t110 = gm_l7;
        if (!t110) goto ie109;
        {
            void * t112 = gm_l0;
            gm_t88rcucg1b7uk t113 = gm_l6;
            gm_b16_8 t114 = gm_l5;
            gm_b16_8 t115 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t112, t113, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t115;
        }
        {
            void * t116 = gm_l0;
            gm_b16_8 t117 = gm_l4;
            gm_b16_8 t118 = gm_l5;
            gm_b16_8 t119 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t116, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t119;
        }
        {
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(0));
            gm_l22 = t120;
        }
        {
            if (!((((uint8_t *)&gm_l17))[16] != 0)) goto ie121;
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
            gm_l23 = t122;
            {
                void * t125 = gm_l0;
                void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
                void * t127 = (void *)(((uint8_t *)t126) + 8);
                gm_t88rcucg1b7uk t128 = gm_l6;
                uint8_t t129 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t127, t128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t129) goto ie124;
                {
                    gm_b16_8 t131 = gm_l23;
                    gm_tnpifv20irnph t132;
                    GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t131);
                    gm_tabb710d9mq2e t133;
                    gm_f_format_itcjtai((void *)&t133, (void *)NULL, cx, NULL, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 8, gm_srcmeaag4q4qrjl, 357, 44);
                    gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 8));
                    gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                    gm_l22 = t135;
                }
                goto ix124;
ie124:;
ix124:;
            }
            goto ix121;
ie121:;
ix121:;
        }
        {
            gm_tif3elqk01bdl t136;
            gm_b16_8 t137 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t136 + 0, t137);
            gm_b16_8 t138 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t136 + 16, t138);
            gm_b16_8 t139 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t136 + 32, t139);
            gm_b16_8 t140 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t136 + 48, t140);
            gm_b16_8 t141 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t136 + 64, t141);
            gm_b16_8 t142 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t136 + 80, t142);
            gm_tabb710d9mq2e t143;
            gm_f_format_lc73epv((void *)&t143, (void *)NULL, cx, NULL, &t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_srcmeaag4q4qrjl, 359, 24);
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
            gm_l24 = t144;
        }
        {
            void * t145 = gm_l0;
            gm_b16_8 t146 = gm_l16;
            gm_tnpifv20irnph t147;
            GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t146);
            gm_b0_1 t148 = gm_f_linef_98ea3qh((void *)NULL, cx, NULL, t145, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t149 = gm_l0;
            void * t150 = gm_l0;
            uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)t150) + 224));
            uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
            if (o153) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 361, 9);
            uint64_t t154 = r152;
            GM_ST(uint64_t, (((uint8_t *)t149) + 224), t154);
        }
        {
            void * t155 = gm_l0;
            gm_t88rcucg1b7uk t156 = gm_l6;
            gm_b16_8 t157 = gm_l19;
            gm_b0_1 t158 = gm_f_calls_emit_plain_euobq8f((void *)NULL, cx, NULL, t155, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)), t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t159 = gm_l0;
            void * t160 = gm_l0;
            uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)t160) + 224));
            uint64_t r162; bool o163 = __builtin_sub_overflow(t161, ((uint64_t)1ULL), &r162);
            if (o163) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 363, 9);
            uint64_t t164 = r162;
            GM_ST(uint64_t, (((uint8_t *)t159) + 224), t164);
        }
        {
            void * t165 = gm_l0;
            gm_b16_8 t166;
            GM_ST(void *, (uint8_t *)&t166, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(8));
            gm_b0_1 t167 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t165, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t168 = gm_l0;
            void * t169 = gm_l0;
            uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)t169) + 224));
            uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
            if (o172) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 365, 9);
            uint64_t t173 = r171;
            GM_ST(uint64_t, (((uint8_t *)t168) + 224), t173);
        }
        {
            gm_th2lqbo77pvsc t174 = gm_l17;
            gm_l25 = t174;
        }
        {
            uint8_t t176;
            if (((((uint8_t *)&gm_l25))[16] != 0)) goto mn178;
            t176 = ((uint8_t)0);
            goto me177;
mn178:;
            t176 = ((uint8_t)1);
            goto me177;
mn179:;
            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 367, 12);
me177:;
            if (!t176) goto ie175;
            uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
            uint8_t t181 = ((t180 == ((uint64_t)0ULL)));
            if (!t181) goto ie175;
            {
                void * t183 = gm_l0;
                gm_t6j6smj7qoctu t184;
                gm_th2lqbo77pvsc t185 = gm_l25;
                if (!((((uint8_t *)&t185))[16] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 368, 34);
                gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                GM_ST(gm_b16_8, (uint8_t *)&t184 + 0, t186);
                gm_b16_8 t187 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t184 + 16, t187);
                gm_b0_1 t188 = gm_f_linef_i5ai2tu((void *)NULL, cx, NULL, t183, &t184);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix175;
ie175:;
            {
                void * t190 = gm_l0;
                gm_b16_8 t191 = gm_l24;
                gm_tnpifv20irnph t192;
                GM_ST(gm_b16_8, ((uint8_t *)&t192 + 0), t191);
                gm_b0_1 t193 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t190, t192);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix175:;
        }
        {
            void * t194 = gm_l0;
            void * t195 = gm_l0;
            uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)t195) + 224));
            uint64_t r197; bool o198 = __builtin_sub_overflow(t196, ((uint64_t)1ULL), &r197);
            if (o198) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 372, 9);
            uint64_t t199 = r197;
            GM_ST(uint64_t, (((uint8_t *)t194) + 224), t199);
        }
        {
            void * t200 = gm_l0;
            gm_b16_8 t201;
            GM_ST(void *, (uint8_t *)&t201, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(1));
            gm_b0_1 t202 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t200, t201);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix109;
ie109:;
        {
            void * t204 = gm_l0;
            gm_t88rcucg1b7uk t205 = gm_l6;
            gm_b16_8 t206 = gm_l19;
            gm_b0_1 t207 = gm_f_calls_emit_plain_euobq8f((void *)NULL, cx, NULL, t204, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)), t205, t206);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix109:;
    }
    {
        if (!((((uint8_t *)&gm_l17))[16] != 0)) goto ie208;
        gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l26 = t209;
        {
            void * t212 = gm_l0;
            void * t213 = GM_LD(void *, (((uint8_t *)t212) + 0));
            void * t214 = (void *)(((uint8_t *)t213) + 8);
            gm_t88rcucg1b7uk t215 = gm_l6;
            uint8_t t216 = gm_f_is_unit_17uennd(t214, GM_CONV(gm_csguankko4lmhd, t215));
            uint8_t t217 = (uint8_t)!t216;
            if (!t217) goto ie211;
            {
                gm_b16_8 t219 = gm_l26;
                uint8_t t220 = 0;
                F.ret = t219;
                goto gm_fret;
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
        void * t221 = gm_l0;
        gm_t88rcucg1b7uk t222 = gm_l6;
        gm_b16_8 t223 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t221, t222);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t224 = 0;
        F.ret = t223;
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
__attribute__((aligned(4))) void gm_f_call_inner_i2bbshq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, const void * a4, gm_tnrltbc61i6v1 a5) {
    struct gm_fr_call_inner_i2bbshq F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l3; /* args */
    gm_th2lqbo77pvsc gm_l4; /* dest */
    gm_tnrltbc61i6v1 gm_l5; /* obs */
    gm_th2lqbo77pvsc gm_l6; /* cv */
    void * gm_l7; /* c */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l13; /* a */
    uint8_t gm_l14; /* later_pure */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* j */
    uint8_t gm_l18; /* borrows */
    void * gm_l19; /* root */
    uint8_t gm_l20; /* overlaps */
    void * gm_l21; /* r */
    gm_th2lqbo77pvsc gm_l22; /* k */
    gm_b16_8 gm_l23; /* v */
    gm_b16_8 gm_l24; /* kc */
    gm_b16_8 gm_l25; /* addr */
    uint32_t gm_l26; /* f */
    void * gm_l27; /* c */
    gm_b16_8 gm_l28; /* cvv */
    uint8_t gm_l30; /* effectful */
    gm_b16_8 gm_l31; /* cx */
    gm_b16_8 gm_l32; /* r */
    gm_tiilmfd16v3fc gm_l33; /* eff */
    uint32_t gm_l34; /* op */
    GM_STACK_CHECK(&F, 2466u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    gm_l5 = a5;
    {
        gm_th2lqbo77pvsc t2;
        memset(&t2, 0, sizeof t2);
        gm_l6 = t2;
    }
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t4 != ((uint8_t)2ULL)) goto ie3;
        void * t5 = GM_LD(void *, (((uint8_t *)a2) + 8));
        gm_l7 = t5;
        {
            void * t7 = gm_l0;
            void * t8 = gm_l7;
            gm_t5o0tpv5jka2l t9;
            gm_f_expr_porbc9t((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t9))[0] != 0)) {
                gm_t5o0tpv5jka2l t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t10 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t9) + 8)));
                F.ret = t10;
                goto gm_fret;
            }
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 0, t11);
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l10 = t13;
        }
        {
            for (;;) {
                uint64_t t15 = gm_l11;
                uint64_t t16 = gm_l10;
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l11;
                    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                    gm_l12 = t23;
                    {
                        uint64_t t25 = gm_l12;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcmeaag4q4qrjl, 30, 15);
                        void * t28 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t26 * 88);
                        gm_l13 = t28;
                    }
                    {
                        gm_l14 = ((uint8_t)1);
                    }
                    {
                        {
                            uint64_t t30 = gm_l12;
                            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                            if (o32) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 34, 19);
                            uint64_t t33 = r31;
                            gm_l16 = t33;
                        }
                        {
                            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            gm_l15 = t34;
                        }
                        {
                            for (;;) {
                                uint64_t t36 = gm_l16;
                                uint64_t t37 = gm_l15;
                                uint8_t t38 = (t36 < t37);
                                if (!t38) goto lb35;
                                {
                                    uint64_t t41 = gm_l16;
                                    uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&t41));
                                    gm_l17 = t44;
                                    {
                                        uint64_t t47 = gm_l17;
                                        uint64_t t48 = (uintptr_t)t47;
                                        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                        if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srcmeaag4q4qrjl, 35, 31);
                                        void * t50 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t48 * 88);
                                        uint8_t t51 = gm_f_no_writes_p1jjjfj(t50);
                                        uint8_t t52 = (uint8_t)!t51;
                                        if (!t52) goto ie46;
                                        {
                                            gm_l14 = ((uint8_t)0);
                                        }
                                        goto ix46;
ie46:;
ix46:;
                                    }
                                    goto me42;
mn43:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 34, 9);
me42:;
                                }
lc35:;
                                {
                                    uint64_t t55 = gm_l16;
                                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                                    uint64_t t58 = r56;
                                    gm_l16 = t58;
                                }
                            }
lb35:;
                        }
                    }
                    {
                        uint8_t t59 = 0;
                        uint8_t t60;
                        if (((((uint8_t *)&gm_l4))[16] != 0)) goto mn62;
                        t60 = ((uint8_t)0);
                        goto me61;
mn62:;
                        t60 = ((uint8_t)1);
                        goto me61;
mn63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 39, 20);
me61:;
                        if (t60) {
                            void * t64 = gm_l0;
                            uint64_t t65 = gm_l12;
                            uint8_t t66 = gm_f_callee_borrows_mk6en5r((void *)NULL, cx, NULL, t64, &(*(gm_tsth9qu1dv0g3 *)((uint8_t *)a2)), t65);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            t59 = t66;
                        } else t59 = 0;
                        gm_l18 = t59;
                    }
                    {
                        void * t67 = gm_l0;
                        void * t68 = gm_l13;
                        void * t69 = gm_f_in_place_root_417o9i7((void *)NULL, cx, NULL, t67, t68);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_l19 = t69;
                    }
                    {
                        gm_l20 = ((uint8_t)1);
                    }
                    {
                        void * t71 = gm_l19;
                        if (!(GM_LD(void *, ((uint8_t *)&t71)) != 0)) goto ie70;
                        void * t72 = GM_LD(void *, ((uint8_t *)&t71));
                        gm_l21 = t72;
                        {
                            void * t74 = gm_l21;
                            gm_tnrltbc61i6v1 t75 = gm_l5;
                            uint8_t t76 = gm_l18;
                            uint8_t t77 = gm_f_arg_may_overlap_u7aort7(t74, t75, t76);
                            gm_l20 = t77;
                        }
                        goto ix70;
ie70:;
ix70:;
                    }
                    {
                        gm_th2lqbo77pvsc t78;
                        memset(&t78, 0, sizeof t78);
                        gm_l22 = t78;
                    }
                    {
                        void * t80 = gm_l0;
                        void * t81 = GM_LD(void *, (((uint8_t *)t80) + 0));
                        void * t82 = (void *)(((uint8_t *)t81) + 8);
                        void * t83 = gm_l13;
                        gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t83) + 0));
                        uint8_t t85 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t82, t84);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        if (!t85) goto ie79;
                        {
                            void * t87 = gm_l0;
                            void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
                            void * t89 = gm_l13;
                            gm_f_const_arg_8kiicv6((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l22)), (void *)NULL, cx, NULL, t88, t89);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                        }
                        goto ix79;
ie79:;
ix79:;
                    }
                    {
                        gm_b16_8 t90;
                        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(0));
                        gm_l23 = t90;
                    }
                    {
                        if (!((((uint8_t *)&gm_l22))[16] != 0)) goto ie91;
                        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                        gm_l24 = t92;
                        {
                            gm_t6j6smj7qoctu t94;
                            void * t95 = gm_l0;
                            void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
                            void * t97 = (void *)(((uint8_t *)t96) + 8);
                            void * t98 = gm_l13;
                            gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t98) + 0));
                            gm_b16_8 t100 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t97, t99);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t100);
                            gm_b16_8 t101 = gm_l24;
                            GM_ST(gm_b16_8, (uint8_t *)&t94 + 16, t101);
                            gm_tabb710d9mq2e t102;
                            gm_f_format_8lfevbr((void *)&t102, (void *)NULL, cx, NULL, &t94);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_srcmeaag4q4qrjl, 48, 17);
                            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
                            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                            gm_l23 = t104;
                        }
                        goto ix91;
ie91:;
                        void * t106 = gm_l0;
                        void * t107 = GM_LD(void *, (((uint8_t *)t106) + 0));
                        void * t108 = (void *)(((uint8_t *)t107) + 8);
                        void * t109 = gm_l13;
                        gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t109) + 0));
                        uint8_t t111 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t108, t110);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        if (!t111) goto ie105;
                        void * t112 = gm_l19;
                        uint8_t t113;
                        if ((GM_LD(void *, ((uint8_t *)&t112)) != 0)) goto mn115;
                        t113 = ((uint8_t)0);
                        goto me114;
mn115:;
                        t113 = ((uint8_t)1);
                        goto me114;
mn116:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 49, 44);
me114:;
                        if (!t113) goto ie105;
                        uint8_t t117 = gm_l14;
                        if (!t117) goto ie105;
                        uint8_t t118 = gm_l20;
                        uint8_t t119 = (uint8_t)!t118;
                        if (!t119) goto ie105;
                        {
                            void * t121 = gm_l0;
                            void * t122 = gm_l13;
                            gm_t5o0tpv5jka2l t123;
                            gm_f_place_addr_7ji15ot((void *)&t123, (void *)NULL, cx, NULL, t121, t122);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            if (((((uint8_t *)&t123))[0] != 0)) {
                                gm_t5o0tpv5jka2l t124;
                                GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                                ((uint8_t *)&t124)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t124 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t123) + 8)));
                                F.ret = t124;
                                goto gm_fret;
                            }
                            gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
                            gm_l25 = t125;
                        }
                        {
                            gm_t6j6smj7qoctu t126;
                            void * t127 = gm_l0;
                            void * t128 = GM_LD(void *, (((uint8_t *)t127) + 0));
                            void * t129 = (void *)(((uint8_t *)t128) + 8);
                            void * t130 = gm_l13;
                            gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t130) + 0));
                            gm_b16_8 t132 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t129, t131);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t132);
                            gm_b16_8 t133 = gm_l25;
                            GM_ST(gm_b16_8, (uint8_t *)&t126 + 16, t133);
                            gm_tabb710d9mq2e t134;
                            gm_f_format_8lfevbr((void *)&t134, (void *)NULL, cx, NULL, &t126);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_srcmeaag4q4qrjl, 51, 17);
                            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                            gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                            gm_l23 = t136;
                        }
                        goto ix105;
ie105:;
                        {
                            void * t138 = gm_l0;
                            void * t139 = gm_l13;
                            gm_t5o0tpv5jka2l t140;
                            gm_f_expr_porbc9t((void *)&t140, (void *)NULL, cx, NULL, t138, t139);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            if (((((uint8_t *)&t140))[0] != 0)) {
                                gm_t5o0tpv5jka2l t141;
                                GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
                                ((uint8_t *)&t141)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t141 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t140) + 8)));
                                F.ret = t141;
                                goto gm_fret;
                            }
                            gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
                            gm_l23 = t142;
                        }
ix105:;
ix91:;
                    }
                    {
                        void * t143 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t144 = gm_l23;
                        gm_tftb1odc739t5 t145 = gm_f_push_3ef746b(t143, t144);
                        if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srcmeaag4q4qrjl, 55, 9);
                        gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
                    }
                    {
                        void * t147 = (void *)((uint8_t *)&F.l9);
                        void * t148 = gm_l13;
                        gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t148) + 0));
                        gm_tftb1odc739t5 t150 = gm_f_push_k3l9nic(t147, GM_CONV(gm_csguankko4lmhd, t149));
                        if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_srcmeaag4q4qrjl, 56, 9);
                        gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 29, 5);
me21:;
                }
lc14:;
                {
                    uint64_t t153 = gm_l11;
                    uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                    uint64_t t156 = r154;
                    gm_l11 = t156;
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
        void * t157 = gm_l0;
        void * t158 = gm_l1;
        gm_tl874t2atnlh3 t159 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t158) + 72));
        gm_b0_1 t160 = gm_f_call_site_8n74to1((void *)NULL, cx, NULL, t157, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t161 = gm_l0;
        gm_th2lqbo77pvsc t162 = gm_l4;
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t161) + 152), t162);
    }
    {
        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t168) { case ((uint8_t)0ULL): goto ma164; case ((uint8_t)1ULL): goto ma166; case ((uint8_t)2ULL): goto ma165; default: goto ma167; }
ma164:;
        uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t170 != ((uint8_t)0ULL)) goto mn169;
        uint32_t t171 = GM_LD(uint32_t, (((uint8_t *)a2) + 8));
        gm_l26 = t171;
        void * t172 = gm_l0;
        void * t173 = gm_l1;
        uint32_t t174 = gm_l26;
        gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
        gm_f_call_fn_q122g0r((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t172, t173, t174, t176);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me163;
mn169:;
ma165:;
        uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t178 != ((uint8_t)2ULL)) goto mn177;
        void * t179 = GM_LD(void *, (((uint8_t *)a2) + 8));
        gm_l27 = t179;
        {
            gm_th2lqbo77pvsc t181 = gm_l6;
            if (!((((uint8_t *)&t181))[16] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 64, 20);
            gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
            gm_l28 = t182;
        }
        {
            void * t183 = gm_l0;
            void * t184 = GM_LD(void *, (((uint8_t *)t183) + 0));
            void * t185 = (void *)(((uint8_t *)t184) + 8);
            void * t186 = gm_l27;
            gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t186) + 0));
            gm_t53at80pfdus3 t188;
            gm_f_kind_ghvav22((void *)&t188, t185, GM_CONV(gm_csguankko4lmhd, t187));
            uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&t188) + 0));
            switch (t194) { case ((uint8_t)0ULL): goto ma192; case ((uint8_t)1ULL): goto ma192; case ((uint8_t)2ULL): goto ma192; case ((uint8_t)3ULL): goto ma192; case ((uint8_t)4ULL): goto ma192; case ((uint8_t)5ULL): goto ma192; case ((uint8_t)6ULL): goto ma192; case ((uint8_t)7ULL): goto ma192; case ((uint8_t)8ULL): goto ma192; case ((uint8_t)9ULL): goto ma192; case ((uint8_t)10ULL): goto ma192; case ((uint8_t)11ULL): goto ma192; case ((uint8_t)12ULL): goto ma192; case ((uint8_t)13ULL): goto ma192; case ((uint8_t)14ULL): goto ma192; case ((uint8_t)15ULL): goto ma190; case ((uint8_t)16ULL): goto ma191; case ((uint8_t)17ULL): goto ma192; case ((uint8_t)18ULL): goto ma192; case ((uint8_t)19ULL): goto ma192; case ((uint8_t)20ULL): goto ma192; case ((uint8_t)21ULL): goto ma192; case ((uint8_t)22ULL): goto ma192; case ((uint8_t)23ULL): goto ma192; case ((uint8_t)24ULL): goto ma192; case ((uint8_t)25ULL): goto ma192; case ((uint8_t)26ULL): goto ma192; default: goto ma192; }
ma190:;
            uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&t188) + 0));
            if (t196 != ((uint8_t)15ULL)) goto mn195;
            void * t197 = gm_l0;
            gm_b16_8 t198 = gm_l28;
            void * t199 = gm_l27;
            gm_t88rcucg1b7uk t200 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t199) + 0));
            gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
            void * t205 = gm_l1;
            gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t205) + 0));
            gm_f_call_closure_mha9p1e((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t197, t198, t200, t202, t204, t206);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me189;
mn195:;
ma191:;
            uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&t188) + 0));
            if (t208 != ((uint8_t)16ULL)) goto mn207;
            F.l29 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t188) + 8));
            {
                void * t210 = gm_l0;
                void * t211 = GM_LD(void *, (((uint8_t *)t210) + 0));
                void * t212 = (void *)(((uint8_t *)&F.l29) + 24);
                uint8_t t213 = gm_f_within_sys_2lc2ugq(t211, t212);
                uint8_t t214 = (uint8_t)!t213;
                gm_l30 = t214;
            }
            {
                void * t215 = gm_l0;
                gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)t215) + 280));
                gm_l31 = t216;
            }
            {
                void * t217 = gm_l0;
                gm_b16_8 t218 = gm_l28;
                gm_b16_8 t219;
                GM_ST(void *, (uint8_t *)&t219, (void *)(gm_s04qus0luoga5));
                GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(4));
                gm_b16_8 t220 = gm_l31;
                gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
                gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&t223));
                void * t225 = gm_l1;
                gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t225) + 0));
                uint8_t t227 = gm_l30;
                uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&F.l29) + 40));
                gm_b16_8 t229 = gm_f_call_indirect_khk02ee((void *)NULL, cx, NULL, t217, t218, t219, t220, t222, t224, t226, t227, t228);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t229;
            }
            {
                uint8_t t231 = 0;
                uint8_t t232 = gm_l30;
                if (!t232) {
                    void * t233 = gm_l0;
                    void * t234 = GM_LD(void *, (((uint8_t *)t233) + 0));
                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                    gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                    uint8_t t237 = gm_f_takes_fn_n239ee5(t234, t236);
                    t231 = t237;
                } else t231 = 1;
                if (!t231) goto ie230;
                {
                    void * t239 = gm_l0;
                    gm_b0_1 t240 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix230;
ie230:;
ix230:;
            }
            {
                gm_b16_8 t241 = gm_l32;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t241);
                goto gm_fdone;
            }
            goto me189;
mn207:;
ma192:;
            void * t243 = gm_l1;
            gm_tl874t2atnlh3 t244 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t243) + 72));
            void * t245 = gm_l0;
            void * t246 = GM_LD(void *, (((uint8_t *)t245) + 0));
            void * t247 = (void *)(((uint8_t *)t246) + 8);
            void * t248 = gm_l27;
            gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t248) + 0));
            gm_b16_8 t250 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t247, t249);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t251;
            GM_ST(gm_b16_8, ((uint8_t *)&t251 + 0), t250);
            gm_tabb710d9mq2e t252;
            gm_f_format_0mlfk3c((void *)&t252, (void *)NULL, cx, NULL, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t252))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t252) + 8, gm_srcmeaag4q4qrjl, 75, 50);
            gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&t252) + 8));
            gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
            gm_t08olq97rscn3 t255;
            gm_f_unsupported_531q51r((void *)&t255, (void *)NULL, cx, NULL, t244, t254);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t255);
            goto gm_fdone;
            goto me189;
mn242:;
ma193:;
            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 65, 13);
me189:;
        }
        goto me163;
mn177:;
ma166:;
        uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t257 != ((uint8_t)1ULL)) goto mn256;
        gm_tiilmfd16v3fc t258 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)a2) + 8) + 0));
        gm_l33 = t258;
        uint32_t t259 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 24));
        gm_l34 = t259;
        void * t260 = gm_l0;
        void * t261 = gm_l1;
        void * t262 = (void *)((uint8_t *)&gm_l33);
        uint32_t t263 = gm_l34;
        gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
        gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&t266));
        void * t268 = gm_l1;
        gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t268) + 0));
        gm_f_perform_t77ghfg((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t260, t261, t262, t263, t265, t267, t269);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me163;
mn256:;
ma167:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 61, 5);
me163:;
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
inline __attribute__((aligned(4))) void gm_f_call_nk38pfa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_call_nk38pfa F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l3; /* args */
    gm_th2lqbo77pvsc gm_l4; /* dest */
    gm_tnrltbc61i6v1 gm_l5; /* obs */
    GM_STACK_CHECK(&F, 329u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        gm_th2lqbo77pvsc t2;
        memset(&t2, 0, sizeof t2);
        gm_l4 = t2;
    }
    {
        gm_tnrltbc61i6v1 t3;
        GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
        gm_l5 = t3;
    }
    {
        void * t5 = gm_l0;
        if (!(((((uint8_t *)t5) + 104))[40] != 0)) goto ie4;
        gm_ts0eitcfeteg8 t6 = GM_LD(gm_ts0eitcfeteg8, (((uint8_t *)t5) + 104));
        F.l6 = t6;
        {
            void * t9 = GM_LD(void *, (((uint8_t *)&F.l6) + 0));
            void * t10 = gm_l1;
            uint8_t t11 = ((t9 == t10));
            if (!t11) goto ie8;
            {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 0, t13);
            }
            {
                gm_tnrltbc61i6v1 t14 = GM_LD(gm_tnrltbc61i6v1, (((uint8_t *)&F.l6) + 24));
                gm_l5 = t14;
            }
            {
                void * t15 = gm_l0;
                gm_tjefu848ssvm9 t16;
                memset(&t16, 0, sizeof t16);
                GM_ST(gm_tjefu848ssvm9, (((uint8_t *)t15) + 104), t16);
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l1;
        gm_b16_8 t19 = gm_l3;
        gm_tnrltbc61i6v1 t20 = gm_l5;
        gm_f_call_inner_i2bbshq((void *)&F.l7, (void *)NULL, cx, NULL, t17, t18, &(*(gm_tsth9qu1dv0g3 *)((uint8_t *)a2)), t19, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l0;
        gm_th2lqbo77pvsc t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t21) + 152), t22);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l7), 40);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_call_result_vg14o5p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_call_result_vg14o5p F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* call */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = gm_l2;
            gm_tnpifv20irnph t11;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
            gm_b0_1 t12 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t9, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t13 = gm_l0;
            gm_t88rcucg1b7uk t14 = gm_l1;
            gm_b16_8 t15 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_b16_8 t19 = gm_l2;
        gm_b16_8 t20 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = 0;
        F.ret = t20;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_call_site_8n74to1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_call_site_8n74to1 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* f */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    gm_b16_8 gm_l4; /* file */
    GM_STACK_CHECK(&F, 270u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 1));
        if (!t5) goto ie2;
        gm_tl874t2atnlh3 t6 = gm_l1;
        gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint8_t t8 = (!gm_eqff85ac10o3(((uint8_t *)&t6), ((uint8_t *)&t7)));
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            void * t17 = gm_f_file_o15fqku(t15, t16);
            gm_l2 = t17;
        }
        {
            void * t18 = gm_l2;
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_tcc1rq4n1avb2 t20 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t18, t19));
            gm_l3 = t20;
        }
        {
            void * t21 = gm_l2;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
            gm_b16_8 t23 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t23;
        }
        {
            void * t24 = gm_l0;
            gm_t5hdnnftkup6m t25;
            gm_b16_8 t26 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
            uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            GM_ST(uint32_t, (uint8_t *)&t25 + 16, t27);
            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            GM_ST(uint32_t, (uint8_t *)&t25 + 20, t28);
            gm_b0_1 t29 = gm_f_linef_4ku5dms((void *)NULL, cx, NULL, t24, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) uint8_t gm_f_callee_borrows_mk6en5r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2) {
    struct gm_fr_callee_borrows_mk6en5r F;
    void * gm_l0; /* fg */
    uint64_t gm_l2; /* i */
    uint32_t gm_l3; /* f */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; default: goto ma6; }
ma3:;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
        gm_l3 = t10;
        {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            uint32_t t14 = gm_l3;
            gm_b16_8 t15 = gm_f_read_during_build_ng10fmi((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t15;
        }
        {
            uint8_t t16 = 0;
            uint64_t t17 = gm_l2;
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t19 = (t17 < t18);
            if (t19) {
                uint64_t t20 = gm_l2;
                uint64_t t21 = (uintptr_t)t20;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcmeaag4q4qrjl, 88, 34);
                uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t21 * 1));
                t16 = t23;
            } else t16 = 0;
            uint8_t t24 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto me2;
mn8:;
ma4:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t26 != ((uint8_t)2ULL)) goto mn25;
        uint8_t t27 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me2;
mn25:;
ma5:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t29 != ((uint8_t)1ULL)) goto mn28;
        uint8_t t30 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me2;
mn28:;
ma6:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 85, 5);
me2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_emit_plain_euobq8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_calls_emit_plain_euobq8f F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* res */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_b16_8 gm_l3; /* plain_call */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l4 = t3;
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l2;
            uint8_t t10 = gm_f_is_unit_17uennd(t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t11 = (uint8_t)!t10;
            if (!t11) goto ie5;
            {
                void * t13 = gm_l0;
                gm_t6j6smj7qoctu t14;
                gm_b16_8 t15 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
                gm_b16_8 t16 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t16);
                gm_b0_1 t17 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t13, &t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t18;
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l0;
        gm_b16_8 t21 = gm_l3;
        gm_tnpifv20irnph t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
        gm_b0_1 t23 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t20, t22);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_equal_hcuag9d F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* a */
    gm_b16_8 gm_l3; /* b */
    gm_b16_8 gm_l4; /* f */
    gm_b16_8 gm_l5; /* pa */
    gm_b16_8 gm_l6; /* pb */
    GM_STACK_CHECK(&F, 607u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t5v8n6qrn4rh4 t7;
        gm_f_repr_53quqki((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&t7) + 0) == ((uint8_t)6ULL)));
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            void * t12 = (void *)(((uint8_t *)t11) + 1968);
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            gm_t88rcucg1b7uk t16 = gm_l1;
            gm_b16_8 t17 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t12, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t17;
        }
        {
            void * t18 = gm_l0;
            gm_t88rcucg1b7uk t19 = gm_l1;
            gm_b16_8 t20 = gm_l2;
            gm_b16_8 t21 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t18, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t21;
        }
        {
            void * t22 = gm_l0;
            gm_t88rcucg1b7uk t23 = gm_l1;
            gm_b16_8 t24 = gm_l3;
            gm_b16_8 t25 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t25;
        }
        {
            gm_tfeab279c16rr t26;
            gm_b16_8 t27 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
            gm_b16_8 t28 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t28);
            gm_b16_8 t29 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t26 + 32, t29);
            gm_tabb710d9mq2e t30;
            gm_f_format_ht2r43m((void *)&t30, (void *)NULL, cx, NULL, &t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcmeaag4q4qrjl, 694, 16);
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t34;
        gm_b16_8 t35 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t35);
        gm_b16_8 t36 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t34 + 16, t36);
        gm_tabb710d9mq2e t37;
        gm_f_format_6m84e0r((void *)&t37, (void *)NULL, cx, NULL, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcmeaag4q4qrjl, 696, 12);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        uint8_t t40 = 0;
        F.ret = t39;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fn_format_stub_53r45gt F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* sink */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* f */
    gm_b16_8 gm_l4; /* f */
    gm_b16_8 gm_l5; /* head */
    GM_STACK_CHECK(&F, 600u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sk8csn7us23m3));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
        gm_l2 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 1216);
        gm_b16_8 t7 = gm_l2;
        gm_th2lqbo77pvsc t8;
        gm_f_get_e3joqcc((void *)&t8, t6, t7);
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie3;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_l3 = t9;
        {
            gm_b16_8 t11 = gm_l3;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 1272);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
        gm_b16_8 t17 = gm_l2;
        gm_b16_8 t18 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = (void *)(((uint8_t *)t20) + 1216);
        gm_b16_8 t22 = gm_l2;
        gm_b16_8 t23 = gm_l4;
        gm_tftb1odc739t5 t24 = gm_f_put_7shfu9u(t21, t22, t23);
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcmeaag4q4qrjl, 492, 5);
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
    }
    {
        gm_b16_8 t26 = gm_l4;
        gm_tnpifv20irnph t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_n1vqu08((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcmeaag4q4qrjl, 493, 13);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_l5 = t29;
    }
    {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        void * t32 = (void *)((((uint8_t *)t31) + 1968) + 0);
        gm_b16_8 t33 = gm_l5;
        gm_tnpifv20irnph t34;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
        gm_tftb1odc739t5 t35 = gm_f_write_vp4v8ut(t32, t34);
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srcmeaag4q4qrjl, 494, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    {
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        void * t39 = (void *)((((uint8_t *)t38) + 1968) + 40);
        gm_t6j6smj7qoctu t40;
        gm_b16_8 t41 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t40 + 0, t41);
        gm_b16_8 t42 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t40 + 16, t42);
        gm_tftb1odc739t5 t43 = gm_f_write_6o17mnv(t39, &t40);
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcmeaag4q4qrjl, 495, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        gm_b16_8 t45 = gm_l4;
        uint8_t t46 = 0;
        F.ret = t45;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_ptr_type_f9efs3c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_fn_ptr_type_f9efs3c F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* params */
    gm_b16_8 gm_l3; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* p */
    GM_STACK_CHECK(&F, 657u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(0));
        gm_l3 = t2;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint8_t t8 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t8) goto ie3;
        {
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
            gm_l3 = t10;
        }
        {
            void * t11 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s44ek1gecbajs));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(6));
            gm_tftb1odc739t5 t13 = gm_f_push_3ef746b(t11, t12);
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcmeaag4q4qrjl, 160, 9);
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix3;
ie3:;
        {
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            void * t18 = (void *)(((uint8_t *)t17) + 8);
            gm_t88rcucg1b7uk t19 = gm_l1;
            gm_b16_8 t20 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t20;
        }
ix3:;
    }
    {
    }
    {
        void * t21 = (void *)((uint8_t *)&F.l4);
        {
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s44ek1gecbajs));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(6));
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sono5n5lrjd7h));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(9));
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sono5n5lrjd7h));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l5)) + 0, t23);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l5)) + 16, t24);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l5)) + 32, t25);
        }
        void * t26 = ((uint8_t *)&F.l5);
        if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcmeaag4q4qrjl, 164, 15);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t26 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
        gm_b16_8 t28 = t27;
        gm_tftb1odc739t5 t29 = gm_f_extend_vogsd72(t21, t28);
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcmeaag4q4qrjl, 164, 5);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
    }
    {
        {
            gm_b16_8 t32 = gm_l2;
            gm_l6 = t32;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t34 = gm_l7;
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t36 = (t34 < t35);
                if (!t36) goto lb33;
                {
                    uint64_t t39 = gm_l7;
                    uint64_t t40 = (uintptr_t)t39;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t40 * 4));
                    gm_l8 = t44;
                    {
                        void * t46 = (void *)((uint8_t *)&F.l4);
                        void * t47 = gm_l0;
                        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                        void * t49 = (void *)(((uint8_t *)t48) + 8);
                        gm_t88rcucg1b7uk t50 = gm_l8;
                        gm_b16_8 t51 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t49, t50);
                        if (gm_unw.kind) {
                            goto gm_ll37;
                        }
                        gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t46, t51);
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcmeaag4q4qrjl, 165, 23);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                    }
                    goto me42;
mn43:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 165, 5);
me42:;
                }
lc33:;
                {
                    uint64_t t55 = gm_l7;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l7 = t58;
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
        gm_t6j6smj7qoctu t59;
        gm_b16_8 t60 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t64;
        gm_f_join_sfknljb((void *)&t64, (void *)NULL, cx, NULL, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_srcmeaag4q4qrjl, 166, 40);
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 16, t65);
        gm_tabb710d9mq2e t66;
        gm_f_format_p19pl6n((void *)&t66, (void *)NULL, cx, NULL, &t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srcmeaag4q4qrjl, 166, 12);
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        uint8_t t69 = 0;
        F.ret = t68;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_fn_sink_stub_5ipff9h F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* str_ty */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* f */
    gm_b16_8 gm_l4; /* f */
    gm_t88rcucg1b7uk gm_l5; /* unit_ty */
    gm_tnqk2qt7j42o9 gm_l6; /* tys */
    gm_b16_8 gm_l7; /* head */
    gm_b16_8 gm_l8; /* pt */
    gm_b16_8 gm_l9; /* plain */
    gm_b16_8 gm_l10; /* hidden */
    GM_STACK_CHECK(&F, 1096u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0aqhubr4upni));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
        gm_l2 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 1216);
        gm_b16_8 t7 = gm_l2;
        gm_th2lqbo77pvsc t8;
        gm_f_get_e3joqcc((void *)&t8, t6, t7);
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie3;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_l3 = t9;
        {
            gm_b16_8 t11 = gm_l3;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 1272);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
        gm_b16_8 t17 = gm_l2;
        gm_b16_8 t18 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = (void *)(((uint8_t *)t20) + 1216);
        gm_b16_8 t22 = gm_l2;
        gm_b16_8 t23 = gm_l4;
        gm_tftb1odc739t5 t24 = gm_f_put_7shfu9u(t21, t22, t23);
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcmeaag4q4qrjl, 472, 5);
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
    }
    {
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28 = (void *)(((uint8_t *)t27) + 8);
        gm_t88rcucg1b7uk t29 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t29;
    }
    {
        gm_t88rcucg1b7uk t30 = gm_l1;
        gm_tnqk2qt7j42o9 t31;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t31 + 0), t30);
        gm_l6 = t31;
    }
    {
        gm_b16_8 t32 = gm_l4;
        gm_tnpifv20irnph t33;
        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_01h4tbj((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcmeaag4q4qrjl, 475, 13);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_l7 = t35;
    }
    {
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = (void *)((((uint8_t *)t37) + 1968) + 0);
        gm_b16_8 t39 = gm_l7;
        gm_tnpifv20irnph t40;
        GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
        gm_tftb1odc739t5 t41 = gm_f_write_vp4v8ut(t38, t40);
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcmeaag4q4qrjl, 476, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_t88rcucg1b7uk t46 = gm_l1;
        gm_b16_8 t47 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t47;
    }
    {
        gm_t6j6smj7qoctu t48;
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
        void * t51 = (void *)(((uint8_t *)t50) + 8);
        gm_t88rcucg1b7uk t52 = gm_l1;
        gm_b16_8 t53 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t53);
        void * t54 = gm_l0;
        gm_t88rcucg1b7uk t55 = gm_l1;
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_so78814tnt5g5));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
        gm_b16_8 t57 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t54, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t48 + 16, t57);
        gm_tabb710d9mq2e t58;
        gm_f_format_drnt54k((void *)&t58, (void *)NULL, cx, NULL, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srcmeaag4q4qrjl, 478, 14);
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
        gm_l9 = t59;
    }
    {
        void * t60 = gm_l0;
        gm_t88rcucg1b7uk t61 = gm_l5;
        void * t62 = ((uint8_t *)&gm_l6);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcmeaag4q4qrjl, 479, 82);
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t62 + (uintptr_t)0 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
        gm_b16_8 t65 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t60, t61, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t66;
        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
        gm_tabb710d9mq2e t67;
        gm_f_format_5p0qm94((void *)&t67, (void *)NULL, cx, NULL, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srcmeaag4q4qrjl, 479, 15);
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
        gm_l10 = t68;
    }
    {
        void * t69 = gm_l0;
        void * t70 = GM_LD(void *, (((uint8_t *)t69) + 0));
        void * t71 = (void *)((((uint8_t *)t70) + 1968) + 40);
        gm_ti71ll5knnvij t72;
        gm_b16_8 t73 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 0, t73);
        gm_b16_8 t74 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 16, t74);
        void * t75 = gm_l0;
        uint64_t t76 = gm_f_ptr_bytes_eocneng(t75);
        GM_ST(uint64_t, (uint8_t *)&t72 + 32, t76);
        gm_b16_8 t77 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 40, t77);
        gm_b16_8 t78 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 56, t78);
        gm_tftb1odc739t5 t79 = gm_f_write_502hm86(t71, &t72);
        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_srcmeaag4q4qrjl, 480, 5);
        gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
    }
    {
        gm_b16_8 t81 = gm_l4;
        uint8_t t82 = 0;
        F.ret = t81;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_arg_ldsagr2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_gamma_arg_ldsagr2 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 371u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = (uint8_t)!t7;
        if (!t8) goto ie2;
        {
            gm_b16_8 t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t12;
        uint8_t t14 = 0;
        gm_b16_8 t15 = gm_l2;
        uint8_t t16 = gm_f_is_temp_rrllgdr(t15);
        if (!t16) {
            gm_b16_8 t17 = gm_l2;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_somhu4vltiil1));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            uint8_t t19 = gm_f_starts_with_6vvt01e(t17, t18);
            t14 = t19;
        } else t14 = 1;
        if (!t14) goto ie13;
        gm_b16_8 t21 = gm_l2;
        gm_b16_8 t22 = t21;
        t12 = t22;
        goto ix13;
ie13:;
        void * t24 = gm_l0;
        gm_t88rcucg1b7uk t25 = gm_l1;
        gm_b16_8 t26 = gm_l2;
        gm_b16_8 t27 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t28 = t27;
        t12 = t28;
ix13:;
        gm_l3 = t12;
    }
    {
        gm_b16_8 t29 = gm_l3;
        gm_tnpifv20irnph t30;
        GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
        gm_tabb710d9mq2e t31;
        gm_f_format_h6b2ofo((void *)&t31, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcmeaag4q4qrjl, 194, 12);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        uint8_t t34 = 0;
        F.ret = t33;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_args_jb6l3ik(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_gamma_args_jb6l3ik F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* vals */
    gm_b16_8 gm_l2; /* tys */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 320u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t5 = (t3 < t4 ? t3 : t4);
            gm_l4 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = gm_l4;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        void * t18 = gm_l0;
                        uint64_t t19 = gm_l6;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcmeaag4q4qrjl, 201, 72);
                        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 4));
                        uint64_t t23 = gm_l6;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_srcmeaag4q4qrjl, 201, 80);
                        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t24 * 16));
                        gm_b16_8 t27 = gm_f_gamma_arg_ldsagr2((void *)NULL, cx, NULL, t18, t22, t26);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tnpifv20irnph t28;
                        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
                        gm_tftb1odc739t5 t29 = gm_f_write_fp9dg1h(t17, t28);
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcmeaag4q4qrjl, 201, 41);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 201, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t32 = gm_l5;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l5 = t35;
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
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        uint8_t t38 = 0;
        F.ret = t37;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_gamma_value_rgbfhcc F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    GM_STACK_CHECK(&F, 279u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_th2lqbo77pvsc t7;
        gm_f_mirror_lcjegsf((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8;
        if (((((uint8_t *)&t7))[16] != 0)) goto mn10;
        t8 = ((uint8_t)0);
        goto me9;
mn10:;
        t8 = ((uint8_t)1);
        goto me9;
mn11:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 177, 8);
me9:;
        if (!t8) goto ie2;
        {
            gm_t6j6smj7qoctu t13;
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_b16_8 t18 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t13 + 0, t18);
            gm_b16_8 t19 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t13 + 16, t19);
            gm_tabb710d9mq2e t20;
            gm_f_format_3s8s1g6((void *)&t20, (void *)NULL, cx, NULL, &t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcmeaag4q4qrjl, 177, 47);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t24 = gm_l2;
        uint8_t t25 = 0;
        F.ret = t24;
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
__attribute__((aligned(4))) void * gm_f_in_place_root_417o9i7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_in_place_root_417o9i7 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* a */
    void * gm_l2; /* e */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    void * gm_l5; /* index */
    GM_STACK_CHECK(&F, 333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l2;
                uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; case ((uint8_t)51ULL): goto ma11; case ((uint8_t)52ULL): goto ma11; case ((uint8_t)53ULL): goto ma11; case ((uint8_t)54ULL): goto ma11; case ((uint8_t)55ULL): goto ma11; case ((uint8_t)56ULL): goto ma11; case ((uint8_t)57ULL): goto ma11; case ((uint8_t)58ULL): goto ma11; default: goto ma11; }
ma8:;
                uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t17 != ((uint8_t)15ULL)) goto pa16;
                goto po15;
pa16:;
                uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t19 != ((uint8_t)16ULL)) goto pa18;
                goto po15;
pa18:;
                uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t21 != ((uint8_t)18ULL)) goto pa20;
                goto po15;
pa20:;
                goto mn14;
po15:;
                void * t22 = gm_l2;
                uint8_t t23 = 0;
                F.ret = t22;
                goto gm_fret;
                goto me7;
mn14:;
ma9:;
                uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t25 != ((uint8_t)19ULL)) goto mn24;
                void * t26 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 0));
                gm_l3 = t26;
                {
                    void * t29 = gm_l0;
                    void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                    void * t31 = (void *)(((uint8_t *)t30) + 8);
                    void * t32 = gm_l3;
                    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
                    gm_t57euh612ligr t34;
                    gm_f_bits_layout_23rect9((void *)&t34, (void *)NULL, cx, NULL, t31, t33);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    uint8_t t35;
                    if (((((uint8_t *)&t34))[24] != 0)) goto mn37;
                    t35 = ((uint8_t)0);
                    goto me36;
mn37:;
                    t35 = ((uint8_t)1);
                    goto me36;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 106, 20);
me36:;
                    if (!t35) goto ie28;
                    {
                        uint8_t t40 = 0;
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    goto ix28;
ie28:;
ix28:;
                }
                {
                    void * t41 = gm_l3;
                    gm_l2 = t41;
                }
                goto me7;
mn24:;
ma10:;
                uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t43 != ((uint8_t)20ULL)) goto mn42;
                void * t44 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 0));
                gm_l4 = t44;
                void * t45 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 8));
                gm_l5 = t45;
                {
                    void * t48 = gm_l5;
                    uint8_t t49 = gm_f_no_writes_p1jjjfj(t48);
                    uint8_t t50 = (uint8_t)!t49;
                    if (!t50) goto ie47;
                    {
                        uint8_t t52 = 0;
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    goto ix47;
ie47:;
ix47:;
                }
                {
                    void * t53 = gm_l0;
                    void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
                    void * t55 = (void *)(((uint8_t *)t54) + 8);
                    void * t56 = gm_l4;
                    gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 0));
                    gm_t53at80pfdus3 t58;
                    gm_f_kind_ghvav22((void *)&t58, t55, GM_CONV(gm_csguankko4lmhd, t57));
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                    switch (t64) { case ((uint8_t)0ULL): goto ma62; case ((uint8_t)1ULL): goto ma62; case ((uint8_t)2ULL): goto ma62; case ((uint8_t)3ULL): goto ma62; case ((uint8_t)4ULL): goto ma62; case ((uint8_t)5ULL): goto ma62; case ((uint8_t)6ULL): goto ma62; case ((uint8_t)7ULL): goto ma62; case ((uint8_t)8ULL): goto ma62; case ((uint8_t)9ULL): goto ma61; case ((uint8_t)10ULL): goto ma61; case ((uint8_t)11ULL): goto ma60; case ((uint8_t)12ULL): goto ma62; case ((uint8_t)13ULL): goto ma62; case ((uint8_t)14ULL): goto ma62; case ((uint8_t)15ULL): goto ma62; case ((uint8_t)16ULL): goto ma62; case ((uint8_t)17ULL): goto ma62; case ((uint8_t)18ULL): goto ma62; case ((uint8_t)19ULL): goto ma62; case ((uint8_t)20ULL): goto ma62; case ((uint8_t)21ULL): goto ma62; case ((uint8_t)22ULL): goto ma62; case ((uint8_t)23ULL): goto ma62; case ((uint8_t)24ULL): goto ma62; case ((uint8_t)25ULL): goto ma62; case ((uint8_t)26ULL): goto ma62; default: goto ma62; }
ma60:;
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                    if (t66 != ((uint8_t)11ULL)) goto mn65;
                    {
                        void * t68 = gm_l4;
                        gm_l2 = t68;
                    }
                    goto me59;
mn65:;
ma61:;
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                    if (t72 != ((uint8_t)10ULL)) goto pa71;
                    goto po70;
pa71:;
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                    if (t74 != ((uint8_t)9ULL)) goto pa73;
                    goto po70;
pa73:;
                    goto mn69;
po70:;
                    {
                        void * t77 = gm_l4;
                        uint8_t t78 = gm_f_no_writes_p1jjjfj(t77);
                        if (!t78) goto ie76;
                        {
                            void * t80 = gm_l2;
                            uint8_t t81 = 0;
                            F.ret = t80;
                            goto gm_fret;
                        }
                        goto ix76;
ie76:;
ix76:;
                    }
                    {
                        uint8_t t82 = 0;
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    goto me59;
mn69:;
ma62:;
                    uint8_t t84 = 0;
                    F.ret = ((void *)0);
                    goto gm_fret;
                    goto me59;
mn83:;
ma63:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 111, 17);
me59:;
                }
                goto me7;
mn42:;
ma11:;
                uint8_t t86 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
                goto me7;
mn85:;
ma12:;
                gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 103, 9);
me7:;
            }
lc3:;
        }
gm_ll4:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
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
__attribute__((aligned(4))) void gm_f_native_op_r81a4at(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, const void * a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_native_op_r81a4at F;
    void * gm_l0; /* fg */
    void * gm_l1; /* effect_ */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* vals */
    gm_th2lqbo77pvsc gm_l4; /* res */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tk8ebsa8coi4p gm_l6; /* amb */
    uint64_t gm_l7; /* p */
    gm_b16_8 gm_l8; /* f */
    gm_b16_8 gm_l9; /* u */
    gm_b16_8 gm_l10; /* s */
    gm_b16_8 gm_l11; /* s */
    gm_b16_8 gm_l12; /* some */
    gm_b16_8 gm_l13; /* none */
    GM_STACK_CHECK(&F, 1325u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    gm_l5 = a5;
    {
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
            gm_tk8ebsa8coi4p t6 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t5) + 48));
            gm_l6 = t6;
        }
        {
            void * t7 = gm_l0;
            uint64_t t8 = gm_f_ptr_bytes_eocneng(t7);
            gm_l7 = t8;
        }
        {
            uint8_t t10 = 0;
            gm_t9805qtonbsag t11 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 0));
            void * t12 = gm_l1;
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 0));
            gm_t9805qtonbsag t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[4] = 1;
            uint8_t t15 = (gm_eqkricceou4u(((uint8_t *)&t11), ((uint8_t *)&t14)));
            if (!t15) {
                gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 8));
                void * t17 = gm_l1;
                uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 0));
                gm_t9805qtonbsag t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t19), t18);
                ((uint8_t *)&t19)[4] = 1;
                uint8_t t20 = (gm_eqkricceou4u(((uint8_t *)&t16), ((uint8_t *)&t19)));
                t10 = t20;
            } else t10 = 1;
            if (!t10) goto ie9;
            {
                gm_b16_8 t22;
                gm_t9805qtonbsag t24 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 0));
                void * t25 = gm_l1;
                uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 0));
                gm_t9805qtonbsag t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t27), t26);
                ((uint8_t *)&t27)[4] = 1;
                uint8_t t28 = (gm_eqkricceou4u(((uint8_t *)&t24), ((uint8_t *)&t27)));
                if (!t28) goto ie23;
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s9u3cqhrfmcru));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(12));
                gm_b16_8 t31 = t30;
                t22 = t31;
                goto ix23;
ie23:;
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sqi7tb51umq69));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(12));
                gm_b16_8 t34 = t33;
                t22 = t34;
ix23:;
                gm_l8 = t22;
            }
            {
                void * t35 = gm_l0;
                gm_tttl9nsdo1iu6 t36;
                gm_b16_8 t37 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t37);
                uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcmeaag4q4qrjl, 555, 87);
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t38 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t40);
                uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcmeaag4q4qrjl, 555, 96);
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t41 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 32, t43);
                uint64_t t44 = gm_l7;
                GM_ST(uint64_t, (uint8_t *)&t36 + 48, t44);
                gm_b0_1 t45 = gm_f_linef_0v7c424((void *)NULL, cx, NULL, t35, &t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t46 = gm_l0;
                gm_b16_8 t47 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t47;
            }
            {
                void * t48 = gm_l0;
                gm_b16_8 t49 = gm_l9;
                gm_b0_1 t50 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t48, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix9;
ie9:;
            {
                uint32_t t52 = gm_l2;
                uint32_t t55 = GM_LD(uint32_t, ((uint8_t *)&t52));
                if (!(t55 == ((uint32_t)0ULL))) goto mn54;
                {
                    void * t57 = gm_l0;
                    gm_b16_8 t58;
                    GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s9go9vvkpcjpd));
                    GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(47));
                    gm_b0_1 t59 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t57, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t60 = gm_l0;
                    gm_t88rcucg1b7uk t61 = gm_l5;
                    gm_b16_8 t62;
                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sp5b2hbslij9l));
                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(4));
                    gm_b16_8 t63;
                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sb6j3egm9oljk));
                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(4));
                    gm_b16_8 t64 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t60, t61, t62, t63);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t64;
                }
                {
                    void * t65 = gm_l0;
                    gm_b16_8 t66 = gm_l10;
                    gm_b0_1 t67 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t65, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto me53;
mn54:;
                uint32_t t69 = GM_LD(uint32_t, ((uint8_t *)&t52));
                if (!(t69 == ((uint32_t)1ULL))) goto mn68;
                {
                    void * t71 = gm_l0;
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_su2c9m69n1vjb));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(30));
                    gm_b0_1 t73 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t71, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t74 = gm_l0;
                    gm_tftksr4op114e t75;
                    uint64_t t76 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcmeaag4q4qrjl, 567, 123);
                    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t76 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t75 + 0, t78);
                    uint64_t t79 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_srcmeaag4q4qrjl, 567, 132);
                    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t79 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t75 + 16, t81);
                    uint64_t t82 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t75 + 32, t82);
                    gm_b0_1 t83 = gm_f_linef_3fol08t((void *)NULL, cx, NULL, t74, &t75);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t84 = gm_l0;
                    void * t85 = gm_l0;
                    void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                    void * t87 = (void *)(((uint8_t *)t86) + 8);
                    gm_t88rcucg1b7uk t88 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t89;
                    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sc4r4l9k6f577));
                    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(2));
                    gm_b16_8 t90;
                    GM_ST(void *, (uint8_t *)&t90, (void *)(gm_s3jl3afv8nlvv));
                    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(3));
                    gm_b16_8 t91 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t84, t88, t89, t90);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t91;
                }
                {
                    void * t92 = gm_l0;
                    gm_t88rcucg1b7uk t93 = gm_l5;
                    gm_b16_8 t94 = gm_l11;
                    gm_b16_8 t95 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t92, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t95;
                }
                {
                    void * t96 = gm_l0;
                    gm_t88rcucg1b7uk t97 = gm_l5;
                    gm_b16_8 t98 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t96, t97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t98;
                }
                {
                    void * t99 = gm_l0;
                    gm_th2lqbo77pvsc t100 = gm_l4;
                    gm_t6j6smj7qoctu t101;
                    gm_b16_8 t102 = gm_l12;
                    GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
                    gm_b16_8 t103 = gm_l13;
                    GM_ST(gm_b16_8, (uint8_t *)&t101 + 16, t103);
                    gm_tabb710d9mq2e t104;
                    gm_f_format_g55p0hj((void *)&t104, (void *)NULL, cx, NULL, &t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_srcmeaag4q4qrjl, 571, 42);
                    gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
                    gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                    gm_b0_1 t107 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t99, &t100, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto me53;
mn68:;
                {
                    void * t110 = gm_l0;
                    uint64_t t111 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_srcmeaag4q4qrjl, 574, 56);
                    gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t111 * 16));
                    gm_tnpifv20irnph t114;
                    GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t113);
                    gm_b0_1 t115 = gm_f_linef_i03em5a((void *)NULL, cx, NULL, t110, t114);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t116 = gm_l0;
                    gm_t9qkh0rsooc8d t117;
                    GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)3ULL));
                    gm_b16_8 t118;
                    GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s04qus0luoga5));
                    GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(4));
                    gm_b16_8 t119;
                    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sicgmpca63jln));
                    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(1));
                    gm_b16_8 t120;
                    GM_ST(void *, (uint8_t *)&t120, (void *)(gm_svubtddeidu1d));
                    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(1));
                    gm_b0_1 t121 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t116, t117, t118, t119, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto me53;
mn108:;
                gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 559, 9);
me53:;
            }
ix9:;
        }
    }
    gm_b0_1 t122;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t122);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_out_stub_ils4a69 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* effect_ */
    uint32_t gm_l2; /* id */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tk8ebsa8coi4p gm_l6; /* amb */
    gm_b16_8 gm_l7; /* writer */
    gm_b16_8 gm_l8; /* fp */
    gm_b16_8 gm_l9; /* pt */
    uint64_t gm_l10; /* p */
    gm_b16_8 gm_l11; /* name */
    gm_b16_8 gm_l12; /* key */
    gm_b16_8 gm_l13; /* f */
    gm_b16_8 gm_l14; /* f */
    gm_b16_8 gm_l15; /* sig */
    GM_STACK_CHECK(&F, 1506u);
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
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_tk8ebsa8coi4p t5 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t4) + 48));
        gm_l6 = t5;
    }
    {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(0));
        gm_l7 = t6;
    }
    {
        gm_t9805qtonbsag t8 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 0));
        void * t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 0));
        gm_t9805qtonbsag t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[4] = 1;
        uint8_t t12 = (gm_eqkricceou4u(((uint8_t *)&t8), ((uint8_t *)&t11)));
        if (!t12) goto ie7;
        {
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s9u3cqhrfmcru));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(12));
            gm_l7 = t14;
        }
        goto ix7;
ie7:;
        gm_t9805qtonbsag t16 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 8));
        void * t17 = gm_l1;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 0));
        gm_t9805qtonbsag t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t19), t18);
        ((uint8_t *)&t19)[4] = 1;
        uint8_t t20 = (gm_eqkricceou4u(((uint8_t *)&t16), ((uint8_t *)&t19)));
        if (!t20) goto ie15;
        {
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sqi7tb51umq69));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(12));
            gm_l7 = t22;
        }
        goto ix15;
ie15:;
        {
            gm_th2lqbo77pvsc t24;
            memset(&t24, 0, sizeof t24);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
ix15:;
ix7:;
    }
    {
        uint8_t t27 = 0;
        uint8_t t28 = 0;
        uint8_t t29 = 0;
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        void * t32 = (void *)(((uint8_t *)t31) + 8);
        gm_t88rcucg1b7uk t33 = gm_l5;
        uint8_t t34 = gm_f_is_unit_17uennd(t32, GM_CONV(gm_csguankko4lmhd, t33));
        uint8_t t35 = (uint8_t)!t34;
        if (!t35) {
            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t37 = (!(t36 == ((uint64_t)1ULL)));
            t29 = t37;
        } else t29 = 1;
        if (!t29) {
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcmeaag4q4qrjl, 448, 71);
            gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t41 * 4));
            uint8_t t44 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t40, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t28 = t44;
        } else t28 = 1;
        if (!t28) {
            void * t45 = gm_l0;
            void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
            void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
            void * t48 = gm_l1;
            uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)t48) + 0));
            uint32_t t50 = gm_l3;
            uint8_t t51 = gm_f_is_ctl_oclllo3(t47, t49, t50);
            t27 = t51;
        } else t27 = 1;
        if (!t27) goto ie26;
        {
            gm_th2lqbo77pvsc t53;
            memset(&t53, 0, sizeof t53);
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t55 = gm_l0;
        gm_t88rcucg1b7uk t56 = gm_l5;
        gm_b16_8 t57 = gm_l4;
        gm_b16_8 t58 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t55, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t58;
    }
    {
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
        void * t61 = (void *)(((uint8_t *)t60) + 8);
        uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcmeaag4q4qrjl, 452, 29);
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t62 * 4));
        gm_b16_8 t65 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t61, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t65;
    }
    {
        void * t66 = gm_l0;
        uint64_t t67 = gm_f_ptr_bytes_eocneng(t66);
        gm_l10 = t67;
    }
    {
        void * t68 = gm_l0;
        void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
        void * t70 = GM_LD(void *, (((uint8_t *)t69) + 0));
        void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
        void * t72 = (void *)(((uint8_t *)t71) + 8);
        void * t73 = gm_l1;
        uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)t73) + 0));
        gm_b16_8 t75 = gm_f_effect_name_prpp0ip(t72, t74);
        gm_l11 = t75;
    }
    {
        gm_tgat3d8836orb t76;
        uint32_t t77 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t76 + 0, t77);
        uint32_t t78 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t76 + 4, t78);
        gm_b16_8 t79 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t76 + 8, t79);
        gm_b16_8 t80 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t76 + 24, t80);
        gm_tabb710d9mq2e t81;
        gm_f_format_nivudut((void *)&t81, (void *)NULL, cx, NULL, &t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srcmeaag4q4qrjl, 455, 12);
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
        gm_l12 = t82;
    }
    {
        void * t84 = gm_l0;
        void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
        void * t86 = (void *)(((uint8_t *)t85) + 1216);
        gm_b16_8 t87 = gm_l12;
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        gm_th2lqbo77pvsc t89;
        gm_f_get_e3joqcc((void *)&t89, t86, t88);
        if (!((((uint8_t *)&t89))[16] != 0)) goto ie83;
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
        gm_l13 = t90;
        {
            gm_b16_8 t92 = gm_l13;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t92);
            goto gm_fdone;
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        void * t93 = gm_l0;
        void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
        void * t95 = (void *)(((uint8_t *)t94) + 1272);
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(5));
        gm_b16_8 t97 = gm_l12;
        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
        gm_b16_8 t99 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t95, t96, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t99;
    }
    {
        void * t100 = gm_l0;
        void * t101 = GM_LD(void *, (((uint8_t *)t100) + 0));
        void * t102 = (void *)(((uint8_t *)t101) + 1216);
        gm_b16_8 t103 = gm_l12;
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
        gm_b16_8 t105 = gm_l14;
        gm_tftb1odc739t5 t106 = gm_f_put_7shfu9u(t102, t104, t105);
        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srcmeaag4q4qrjl, 458, 5);
        gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
    }
    {
        gm_t6j6smj7qoctu t108;
        gm_b16_8 t109 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 0, t109);
        gm_b16_8 t110 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t110);
        gm_tabb710d9mq2e t111;
        gm_f_format_3o89hhj((void *)&t111, (void *)NULL, cx, NULL, &t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcmeaag4q4qrjl, 459, 12);
        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
        gm_l15 = t112;
    }
    {
        void * t113 = gm_l0;
        void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
        void * t115 = (void *)((((uint8_t *)t114) + 1968) + 0);
        gm_b16_8 t116 = gm_l15;
        gm_tnpifv20irnph t117;
        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 0), t116);
        gm_tftb1odc739t5 t118 = gm_f_write_vp4v8ut(t115, t117);
        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srcmeaag4q4qrjl, 460, 5);
        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
    }
    {
        void * t120 = gm_l0;
        void * t121 = GM_LD(void *, (((uint8_t *)t120) + 0));
        void * t122 = (void *)((((uint8_t *)t121) + 1968) + 40);
        gm_tobu7tlrkigeq t123;
        memset((uint8_t *)&t123 + 20, 0, 4);
        memset((uint8_t *)&t123 + 84, 0, 4);
        gm_b16_8 t124 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t123 + 0, t124);
        uint32_t t125 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t123 + 16, t125);
        gm_b16_8 t126 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t123 + 24, t126);
        gm_b16_8 t127 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t123 + 40, t127);
        uint64_t t128 = gm_l10;
        GM_ST(uint64_t, (uint8_t *)&t123 + 56, t128);
        gm_b16_8 t129 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t123 + 64, t129);
        uint32_t t130 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t123 + 80, t130);
        gm_tftb1odc739t5 t131 = gm_f_write_mv65o15(t122, &t123);
        if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 1, gm_srcmeaag4q4qrjl, 461, 5);
        gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 1));
    }
    {
        gm_b16_8 t133 = gm_l14;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t133);
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
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6) {
    struct gm_fr_perform_t77ghfg F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* effect_ */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l8; /* r */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l10; /* n */
    gm_b16_8 gm_l11; /* cx */
    gm_b16_8 gm_l12; /* loc */
    gm_b16_8 gm_l13; /* name */
    gm_th2lqbo77pvsc gm_l14; /* res */
    gm_b16_8 gm_l15; /* fp */
    gm_b16_8 gm_l16; /* args */
    gm_b16_8 gm_l17; /* out */
    gm_b16_8 gm_l18; /* r */
    gm_b16_8 gm_l19; /* clause_call */
    gm_tk8ebsa8coi4p gm_l20; /* amb */
    uint8_t gm_l21; /* native */
    gm_th2lqbo77pvsc gm_l22; /* rv */
    gm_b16_8 gm_l23; /* r */
    GM_STACK_CHECK(&F, 2426u);
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
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = gm_l2;
        uint32_t t5 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t5;
    }
    {
        void * t7 = gm_l0;
        uint32_t t8 = gm_l7;
        void * t9 = gm_l2;
        uint32_t t10 = gm_l3;
        gm_b16_8 t11 = gm_l4;
        gm_b16_8 t12 = gm_l5;
        gm_t88rcucg1b7uk t13 = gm_l6;
        gm_th2lqbo77pvsc t14;
        gm_f_direct_perform_vc33l18((void *)&t14, (void *)NULL, cx, NULL, t7, t8, t9, t10, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t14))[16] != 0)) goto ie6;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_l8 = t15;
        {
            gm_b16_8 t17 = gm_l8;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t17);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l1;
        void * t21 = gm_l2;
        uint32_t t22 = gm_l7;
        uint32_t t23 = gm_l3;
        gm_b16_8 t24 = gm_l4;
        gm_b16_8 t25 = gm_l5;
        gm_t88rcucg1b7uk t26 = gm_l6;
        gm_th2lqbo77pvsc t27;
        gm_f_stub_perform_m3dbb7g((void *)&t27, (void *)NULL, cx, NULL, t19, t20, t21, t22, t23, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t27))[16] != 0)) goto ie18;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_l9 = t28;
        {
            gm_b16_8 t30 = gm_l9;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t30);
            goto gm_fdone;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t31 = gm_l0;
        uint32_t t32 = gm_f_fresh_5kokjmg(t31);
        gm_t88rcucg1b7uk t33;
        GM_ST(uint32_t, ((uint8_t *)&t33 + 0), t32);
        gm_tabb710d9mq2e t34;
        gm_f_format_r9dvh2e((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcmeaag4q4qrjl, 392, 10);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_l10 = t35;
    }
    {
        void * t36 = gm_l0;
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t36) + 280));
        gm_l11 = t37;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l1;
        gm_b16_8 t40 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t40;
    }
    {
        void * t41 = gm_l0;
        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        void * t46 = gm_l2;
        uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)t46) + 0));
        gm_b16_8 t48 = gm_f_effect_name_prpp0ip(t45, t47);
        gm_l13 = t48;
    }
    {
        void * t49 = gm_l0;
        gm_td7tfovebirdn t50;
        memset((uint8_t *)&t50 + 36, 0, 4);
        gm_b16_8 t51 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 0, t51);
        gm_b16_8 t52 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 16, t52);
        uint32_t t53 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t50 + 32, t53);
        gm_b0_1 t54 = gm_f_linef_gg3n7rd((void *)NULL, cx, NULL, t49, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t55 = gm_l0;
        gm_tfeab279c16rr t56;
        gm_b16_8 t57 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t57);
        gm_b16_8 t58 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t56 + 16, t58);
        gm_b16_8 t59 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t56 + 32, t59);
        gm_b0_1 t60 = gm_f_linef_qg7idri((void *)NULL, cx, NULL, t55, &t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t62 = gm_l0;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
        void * t65 = gm_l2;
        uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)t65) + 0));
        uint32_t t67 = gm_l3;
        uint8_t t68 = gm_f_is_ctl_oclllo3(t64, t66, t67);
        if (!t68) goto ie61;
        {
            void * t70 = gm_l0;
            gm_b16_8 t71 = gm_l10;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            uint32_t t73 = gm_l3;
            gm_b16_8 t74 = gm_l4;
            gm_b16_8 t75 = gm_l5;
            gm_t88rcucg1b7uk t76 = gm_l6;
            gm_f_ctl_perform_be6h6eo((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t70, t72, t73, t74, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        gm_th2lqbo77pvsc t77;
        memset(&t77, 0, sizeof t77);
        gm_l14 = t77;
    }
    {
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
        void * t81 = (void *)(((uint8_t *)t80) + 8);
        gm_t88rcucg1b7uk t82 = gm_l6;
        uint8_t t83 = gm_f_is_never_in5ftma(t81, GM_CONV(gm_csguankko4lmhd, t82));
        uint8_t t84 = (uint8_t)!t83;
        if (!t84) goto ie78;
        {
            void * t86 = gm_l0;
            gm_t88rcucg1b7uk t87 = gm_l6;
            gm_b16_8 t88 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)) + 0, t88);
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        void * t89 = gm_l0;
        gm_t88rcucg1b7uk t90 = gm_l6;
        gm_b16_8 t91 = gm_l5;
        gm_b16_8 t92 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t89, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t92;
    }
    {
        void * t93 = gm_l0;
        gm_b16_8 t94 = gm_l4;
        gm_b16_8 t95 = gm_l5;
        gm_b16_8 t96 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t93, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t96;
    }
    {
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(0));
        gm_l17 = t97;
    }
    {
        if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie98;
        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l18 = t99;
        {
            void * t102 = gm_l0;
            void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
            void * t104 = (void *)(((uint8_t *)t103) + 8);
            gm_t88rcucg1b7uk t105 = gm_l6;
            uint8_t t106 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t106) goto ie101;
            {
                gm_b16_8 t108 = gm_l18;
                gm_tnpifv20irnph t109;
                GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                gm_tabb710d9mq2e t110;
                gm_f_format_itcjtai((void *)&t110, (void *)NULL, cx, NULL, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 8, gm_srcmeaag4q4qrjl, 405, 40);
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&t110) + 8));
                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                gm_l17 = t112;
            }
            goto ix101;
ie101:;
ix101:;
        }
        goto ix98;
ie98:;
ix98:;
    }
    {
        gm_t586m754bse2v t113;
        memset((uint8_t *)&t113 + 36, 0, 4);
        gm_b16_8 t114 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t114);
        gm_b16_8 t115 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t115);
        uint32_t t116 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t113 + 32, t116);
        gm_b16_8 t117 = gm_l17;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 40, t117);
        gm_b16_8 t118 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 56, t118);
        gm_b16_8 t119 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 72, t119);
        gm_b16_8 t120 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 88, t120);
        gm_b16_8 t121 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t113 + 104, t121);
        gm_tabb710d9mq2e t122;
        gm_f_format_18tu4v6((void *)&t122, (void *)NULL, cx, NULL, &t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 8, gm_srcmeaag4q4qrjl, 407, 20);
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&t122) + 8));
        gm_l19 = t123;
    }
    {
        void * t124 = gm_l0;
        void * t125 = GM_LD(void *, (((uint8_t *)t124) + 0));
        void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
        gm_tk8ebsa8coi4p t127 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t126) + 48));
        gm_l20 = t127;
    }
    {
        uint8_t t128 = 0;
        uint8_t t129 = 0;
        gm_t9805qtonbsag t130 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 0));
        void * t131 = gm_l2;
        uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)t131) + 0));
        gm_t9805qtonbsag t133;
        GM_ST(uint8_t, (uint8_t *)&t133 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t133 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t133), t132);
        ((uint8_t *)&t133)[4] = 1;
        uint8_t t134 = (gm_eqkricceou4u(((uint8_t *)&t130), ((uint8_t *)&t133)));
        if (!t134) {
            gm_t9805qtonbsag t135 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 8));
            void * t136 = gm_l2;
            uint32_t t137 = GM_LD(uint32_t, (((uint8_t *)t136) + 0));
            gm_t9805qtonbsag t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t138), t137);
            ((uint8_t *)&t138)[4] = 1;
            uint8_t t139 = (gm_eqkricceou4u(((uint8_t *)&t135), ((uint8_t *)&t138)));
            t129 = t139;
        } else t129 = 1;
        if (!t129) {
            gm_t9805qtonbsag t140 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 16));
            void * t141 = gm_l2;
            uint32_t t142 = GM_LD(uint32_t, (((uint8_t *)t141) + 0));
            gm_t9805qtonbsag t143;
            GM_ST(uint8_t, (uint8_t *)&t143 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t143 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t143), t142);
            ((uint8_t *)&t143)[4] = 1;
            uint8_t t144 = (gm_eqkricceou4u(((uint8_t *)&t140), ((uint8_t *)&t143)));
            t128 = t144;
        } else t128 = 1;
        gm_l21 = t128;
    }
    {
        uint8_t t146 = gm_l21;
        if (!t146) goto ie145;
        {
            void * t148 = gm_l0;
            gm_b16_8 t149 = gm_l10;
            gm_tnpifv20irnph t150;
            GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t149);
            gm_b0_1 t151 = gm_f_linef_ch11let((void *)NULL, cx, NULL, t148, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t152 = gm_l0;
            void * t153 = gm_l0;
            uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)t153) + 224));
            uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
            if (o156) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 412, 9);
            uint64_t t157 = r155;
            GM_ST(uint64_t, (((uint8_t *)t152) + 224), t157);
        }
        {
            void * t158 = gm_l0;
            void * t159 = gm_l2;
            uint32_t t160 = gm_l3;
            gm_b16_8 t161 = gm_l4;
            gm_t88rcucg1b7uk t162 = gm_l6;
            gm_tqgtd5gtgj65s t163;
            gm_f_native_op_r81a4at((void *)&t163, (void *)NULL, cx, NULL, t158, t159, t160, t161, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)), t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t163))[0] != 0)) {
                gm_t5o0tpv5jka2l t164;
                GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
                ((uint8_t *)&t164)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t164 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t163) + 8)));
                F.ret = t164;
                goto gm_fret;
            }
            gm_b0_1 t165 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 8));
        }
        {
            void * t166 = gm_l0;
            void * t167 = gm_l0;
            uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)t167) + 224));
            uint64_t r169; bool o170 = __builtin_sub_overflow(t168, ((uint64_t)1ULL), &r169);
            if (o170) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 414, 9);
            uint64_t t171 = r169;
            GM_ST(uint64_t, (((uint8_t *)t166) + 224), t171);
        }
        {
            void * t172 = gm_l0;
            gm_b16_8 t173;
            GM_ST(void *, (uint8_t *)&t173, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(8));
            gm_b0_1 t174 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t175 = gm_l0;
            void * t176 = gm_l0;
            uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)t176) + 224));
            uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
            if (o179) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 416, 9);
            uint64_t t180 = r178;
            GM_ST(uint64_t, (((uint8_t *)t175) + 224), t180);
        }
        goto ix145;
ie145:;
ix145:;
    }
    {
        gm_th2lqbo77pvsc t181 = gm_l14;
        gm_l22 = t181;
    }
    {
        uint8_t t183;
        if (((((uint8_t *)&gm_l22))[16] != 0)) goto mn185;
        t183 = ((uint8_t)0);
        goto me184;
mn185:;
        t183 = ((uint8_t)1);
        goto me184;
mn186:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 419, 8);
me184:;
        if (!t183) goto ie182;
        uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
        uint8_t t188 = ((t187 == ((uint64_t)0ULL)));
        if (!t188) goto ie182;
        {
            void * t190 = gm_l0;
            gm_t6j6smj7qoctu t191;
            gm_th2lqbo77pvsc t192 = gm_l22;
            if (!((((uint8_t *)&t192))[16] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 420, 30);
            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
            GM_ST(gm_b16_8, (uint8_t *)&t191 + 0, t193);
            gm_b16_8 t194 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t191 + 16, t194);
            gm_b0_1 t195 = gm_f_linef_i5ai2tu((void *)NULL, cx, NULL, t190, &t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix182;
ie182:;
        {
            void * t197 = gm_l0;
            gm_b16_8 t198 = gm_l19;
            gm_tnpifv20irnph t199;
            GM_ST(gm_b16_8, ((uint8_t *)&t199 + 0), t198);
            gm_b0_1 t200 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t197, t199);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix182:;
    }
    {
        uint8_t t202 = gm_l21;
        if (!t202) goto ie201;
        {
            void * t204 = gm_l0;
            void * t205 = gm_l0;
            uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)t205) + 224));
            uint64_t r207; bool o208 = __builtin_sub_overflow(t206, ((uint64_t)1ULL), &r207);
            if (o208) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 425, 9);
            uint64_t t209 = r207;
            GM_ST(uint64_t, (((uint8_t *)t204) + 224), t209);
        }
        {
            void * t210 = gm_l0;
            gm_b16_8 t211;
            GM_ST(void *, (uint8_t *)&t211, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(1));
            gm_b0_1 t212 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix201;
ie201:;
ix201:;
    }
    {
        void * t213 = gm_l0;
        gm_b0_1 t214 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t213);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie215;
        gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l23 = t216;
        {
            gm_b16_8 t218 = gm_l23;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t218);
            goto gm_fdone;
        }
        goto ix215;
ie215:;
ix215:;
    }
    {
        void * t219 = gm_l0;
        gm_t88rcucg1b7uk t220 = gm_l6;
        gm_b16_8 t221 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t219, t220);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t221);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_piece_stub_m8mb7rc F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* core */
    uint8_t gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* str_ty */
    gm_b16_8 gm_l4; /* kind_text */
    gm_b16_8 gm_l5; /* key */
    gm_b16_8 gm_l6; /* f */
    gm_b16_8 gm_l7; /* f */
    gm_b16_8 gm_l8; /* pt */
    uint64_t gm_l9; /* p */
    gm_b16_8 gm_l10; /* loc */
    gm_b16_8 gm_l11; /* head */
    gm_b16_8 gm_l12; /* pre */
    gm_b16_8 gm_l13; /* data */
    gm_b16_8 gm_l14; /* len */
    gm_b16_8 gm_l15; /* call */
    gm_b16_8 gm_l16; /* post */
    GM_STACK_CHECK(&F, 1591u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2;
        uint8_t t4 = gm_l2;
        uint8_t t5 = ((t4 == ((uint8_t)115ULL)));
        if (!t5) goto ie3;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s03pmf867dcsr));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        gm_b16_8 t8 = t7;
        t2 = t8;
        goto ix3;
ie3:;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_slq58vfha8k9h));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
        gm_b16_8 t11 = t10;
        t2 = t11;
ix3:;
        gm_l4 = t2;
    }
    {
        gm_t6j6smj7qoctu t12;
        gm_b16_8 t13 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 0, t13);
        gm_b16_8 t14 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 16, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_jp1qc7o((void *)&t15, (void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srcmeaag4q4qrjl, 504, 12);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_l5 = t16;
    }
    {
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = (void *)(((uint8_t *)t19) + 1216);
        gm_b16_8 t21 = gm_l5;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_th2lqbo77pvsc t23;
        gm_f_get_e3joqcc((void *)&t23, t20, t22);
        if (!((((uint8_t *)&t23))[16] != 0)) goto ie17;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_l6 = t24;
        {
            gm_b16_8 t26 = gm_l6;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        void * t30 = (void *)(((uint8_t *)t29) + 1272);
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(5));
        gm_b16_8 t32 = gm_l5;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_b16_8 t34 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t30, t31, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t34;
    }
    {
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
        void * t37 = (void *)(((uint8_t *)t36) + 1216);
        gm_b16_8 t38 = gm_l5;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_b16_8 t40 = gm_l7;
        gm_tftb1odc739t5 t41 = gm_f_put_7shfu9u(t37, t39, t40);
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcmeaag4q4qrjl, 507, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_t88rcucg1b7uk t46 = gm_l3;
        gm_b16_8 t47 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t47;
    }
    {
        void * t48 = gm_l0;
        uint64_t t49 = gm_f_ptr_bytes_eocneng(t48);
        gm_l9 = t49;
    }
    {
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_st6to3kuud2ge));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(45));
        gm_l10 = t50;
    }
    {
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(0));
        gm_l11 = t51;
    }
    {
        uint8_t t53 = gm_l2;
        uint8_t t54 = ((t53 == ((uint8_t)115ULL)));
        if (!t54) goto ie52;
        {
            gm_t6j6smj7qoctu t56;
            gm_b16_8 t57 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t57);
            gm_b16_8 t58 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 16, t58);
            gm_tabb710d9mq2e t59;
            gm_f_format_ru16275((void *)&t59, (void *)NULL, cx, NULL, &t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_srcmeaag4q4qrjl, 513, 16);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            gm_l11 = t61;
        }
        goto ix52;
ie52:;
        {
            gm_t6j6smj7qoctu t63;
            gm_b16_8 t64 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t64);
            gm_b16_8 t65 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 16, t65);
            gm_tabb710d9mq2e t66;
            gm_f_format_37aah2s((void *)&t66, (void *)NULL, cx, NULL, &t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srcmeaag4q4qrjl, 515, 16);
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
            gm_l11 = t68;
        }
ix52:;
    }
    {
        void * t69 = gm_l0;
        void * t70 = GM_LD(void *, (((uint8_t *)t69) + 0));
        void * t71 = (void *)((((uint8_t *)t70) + 1968) + 0);
        gm_b16_8 t72 = gm_l11;
        gm_tnpifv20irnph t73;
        GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
        gm_tftb1odc739t5 t74 = gm_f_write_scdecr6(t71, t73);
        if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcmeaag4q4qrjl, 517, 5);
        gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
    }
    {
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(0));
        gm_l12 = t76;
    }
    {
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_stafskn6bv24f));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
        gm_l13 = t77;
    }
    {
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_smnfu5n5pum1l));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(1));
        gm_l14 = t78;
    }
    {
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(0));
        gm_l15 = t79;
    }
    {
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(0));
        gm_l16 = t80;
    }
    {
        uint8_t t82 = gm_l2;
        uint8_t t83 = (!(t82 == ((uint8_t)115ULL)));
        if (!t83) goto ie81;
        {
            gm_b16_8 t85;
            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_siihmb4jo3dsn));
            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(124));
            gm_l12 = t85;
        }
        {
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s8o6lm1cn4eo5));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(2));
            gm_l13 = t86;
        }
        {
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s579fb7l5l6df));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(2));
            gm_l14 = t87;
        }
        {
            gm_b16_8 t88;
            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s3vqae7u5ljtm));
            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(13));
            gm_l15 = t88;
        }
        {
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_spsh7mv5lihcc));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(33));
            gm_l16 = t89;
        }
        goto ix81;
ie81:;
ix81:;
    }
    {
        void * t90 = gm_l0;
        void * t91 = GM_LD(void *, (((uint8_t *)t90) + 0));
        void * t92 = (void *)((((uint8_t *)t91) + 1968) + 40);
        gm_tn96msocmj81h t93;
        gm_b16_8 t94 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 0, t94);
        gm_b16_8 t95 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 16, t95);
        gm_b16_8 t96 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 32, t96);
        gm_b16_8 t97 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 48, t97);
        uint64_t t98 = gm_l9;
        GM_ST(uint64_t, (uint8_t *)&t93 + 64, t98);
        gm_b16_8 t99 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 72, t99);
        gm_b16_8 t100 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 88, t100);
        gm_b16_8 t101 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 104, t101);
        gm_b16_8 t102 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 120, t102);
        gm_tftb1odc739t5 t103 = gm_f_write_otf2npe(t92, &t93);
        if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srcmeaag4q4qrjl, 531, 5);
        gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
    }
    {
        gm_b16_8 t105 = gm_l7;
        uint8_t t106 = 0;
        F.ret = t105;
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
inline __attribute__((aligned(4))) void gm_f_plain_args_o7acjm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_plain_args_o7acjm5 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* vals */
    gm_b16_8 gm_l2; /* tys */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 239u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t5 = (t3 < t4 ? t3 : t4);
            gm_l4 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = gm_l4;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        void * t18 = gm_l0;
                        uint64_t t19 = gm_l6;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcmeaag4q4qrjl, 184, 65);
                        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 4));
                        uint64_t t23 = gm_l6;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_srcmeaag4q4qrjl, 184, 73);
                        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t24 * 16));
                        gm_b16_8 t27 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t18, t22, t26);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t28 = gm_f_push_3ef746b(t17, t27);
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcmeaag4q4qrjl, 184, 41);
                        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 184, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t31 = gm_l5;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l5 = t34;
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
        memmove(gm_out, ((uint8_t *)&F.l3), 40);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_plain_value_e380k1u F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* m */
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_th2lqbo77pvsc t7;
        gm_f_mirror_lcjegsf((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            gm_t6j6smj7qoctu t10;
            gm_b16_8 t11 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
            gm_b16_8 t12 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t12);
            gm_tabb710d9mq2e t13;
            gm_f_format_3s8s1g6((void *)&t13, (void *)NULL, cx, NULL, &t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcmeaag4q4qrjl, 171, 40);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t17 = gm_l2;
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
inline __attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_type_plain_sanetd4 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l3; /* inner */
    GM_STACK_CHECK(&F, 150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_ghvav22((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t14 != ((uint8_t)16ULL)) goto mn13;
        F.l2 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t6) + 8));
        void * t15 = gm_l0;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
        void * t17 = (void *)(((uint8_t *)&F.l2) + 24);
        uint8_t t18 = gm_f_within_sys_2lc2ugq(t16, t17);
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
        goto me7;
mn13:;
ma9:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t21 != ((uint8_t)13ULL)) goto mn20;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l3 = t22;
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l3;
        uint8_t t25 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
        goto me7;
mn20:;
ma10:;
        uint8_t t28 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn27:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 321, 5);
me7:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_str_equal_at_mshane1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_str_equal_at_mshane1 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    gm_b16_8 gm_l2; /* text */
    gm_b16_8 gm_l3; /* len */
    GM_STACK_CHECK(&F, 443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tgbgti82mf879 t2;
        gm_b16_8 t3 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
        void * t4 = gm_l0;
        uint64_t t5 = gm_f_ptr_bytes_eocneng(t4);
        GM_ST(uint64_t, (uint8_t *)&t2 + 16, t5);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        GM_ST(uint64_t, (uint8_t *)&t2 + 24, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_b2nifvt((void *)&t7, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcmeaag4q4qrjl, 756, 12);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l3 = t8;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
        if (!t11) goto ie9;
        {
            gm_b16_8 t13 = gm_l3;
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_tttl9nsdo1iu6 t16;
        gm_b16_8 t17 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t17);
        gm_b16_8 t18 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t18);
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l2;
        gm_b16_8 t22 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 32, t22);
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        GM_ST(uint64_t, (uint8_t *)&t16 + 48, t23);
        gm_tabb710d9mq2e t24;
        gm_f_format_7cklfe7((void *)&t24, (void *)NULL, cx, NULL, &t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcmeaag4q4qrjl, 758, 12);
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
        uint8_t t27 = 0;
        F.ret = t26;
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
inline __attribute__((aligned(4))) void gm_f_stub_perform_m3dbb7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7) {
    struct gm_fr_stub_perform_m3dbb7g F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* effect_ */
    uint32_t gm_l3; /* id */
    uint32_t gm_l4; /* op */
    gm_b16_8 gm_l5; /* vals */
    gm_b16_8 gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_b16_8 gm_l8; /* f */
    gm_b16_8 gm_l9; /* cx */
    gm_b16_8 gm_l10; /* loc */
    GM_STACK_CHECK(&F, 485u);
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
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        uint32_t t4 = gm_l3;
        uint32_t t5 = gm_l4;
        gm_b16_8 t6 = gm_l6;
        gm_t88rcucg1b7uk t7 = gm_l7;
        gm_th2lqbo77pvsc t8;
        gm_f_out_stub_ils4a69((void *)&t8, (void *)NULL, cx, NULL, t2, t3, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_th2lqbo77pvsc t11;
            memset(&t11, 0, sizeof t11);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l8 = t9;
    }
    {
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 280));
        gm_l9 = t14;
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l1;
        gm_b16_8 t17 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_t0uuhaljsf410 t19;
        gm_b16_8 t20 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
        gm_b16_8 t21 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t21);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srcmeaag4q4qrjl, 540, 40);
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t22 * 16));
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 32, t24);
        gm_b16_8 t25 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 48, t25);
        gm_b0_1 t26 = gm_f_linef_icst3ps((void *)NULL, cx, NULL, t18, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t27 = gm_l0;
        gm_b0_1 t28 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = gm_l0;
        gm_t88rcucg1b7uk t30 = gm_l7;
        gm_b16_8 t31 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t31);
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
__attribute__((aligned(4))) void gm_f_try_expr_ln1m7b8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4) {
    struct gm_fr_try_expr_ln1m7b8 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    gm_t9805qtonbsag gm_l3; /* lift */
    uint32_t gm_l4; /* levels */
    gm_b16_8 gm_l5; /* v */
    gm_b16_8 gm_l6; /* a */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_b16_8 gm_l8; /* slot */
    gm_b16_8 gm_l9; /* fr */
    gm_b16_8 gm_l10; /* present */
    gm_b16_8 gm_l11; /* none */
    gm_t88rcucg1b7uk gm_l12; /* err */
    gm_b16_8 gm_l13; /* bad */
    uint64_t gm_l14; /* off */
    gm_t88rcucg1b7uk gm_l15; /* rerr */
    gm_b16_8 gm_l16; /* ev */
    uint32_t gm_l17; /* i */
    gm_b16_8 gm_l18; /* r */
    GM_STACK_CHECK(&F, 1990u);
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
        void * t3 = gm_l2;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_t5o0tpv5jka2l t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t5 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t4) + 8)));
            F.ret = t5;
            goto gm_fret;
        }
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l2;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + 0));
        gm_b16_8 t10 = gm_l5;
        gm_b16_8 t11 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t7, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = gm_f_target_ret_q7qjs0g((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t14;
    }
    {
        void * t15 = gm_l0;
        uint32_t t16 = gm_l4;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s1k3lfare8sei));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
        gm_b16_8 t18 = gm_f_frame_field_nj8m22n((void *)NULL, cx, NULL, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t18;
    }
    {
        void * t19 = gm_l0;
        uint32_t t20 = gm_l4;
        gm_tommf4j9upomc t21;
        gm_f_frame_up_g8486eu((void *)&t21, (void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t22 = t21;
        gm_l9 = GM_LD(gm_b16_8, (uint8_t *)&t22 + 0);
    }
    {
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = (void *)(((uint8_t *)t24) + 8);
        void * t26 = gm_l2;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
        gm_t53at80pfdus3 t28;
        gm_f_kind_ghvav22((void *)&t28, t25, GM_CONV(gm_csguankko4lmhd, t27));
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        switch (t34) { case ((uint8_t)0ULL): goto ma32; case ((uint8_t)1ULL): goto ma32; case ((uint8_t)2ULL): goto ma32; case ((uint8_t)3ULL): goto ma32; case ((uint8_t)4ULL): goto ma32; case ((uint8_t)5ULL): goto ma32; case ((uint8_t)6ULL): goto ma32; case ((uint8_t)7ULL): goto ma32; case ((uint8_t)8ULL): goto ma32; case ((uint8_t)9ULL): goto ma32; case ((uint8_t)10ULL): goto ma32; case ((uint8_t)11ULL): goto ma32; case ((uint8_t)12ULL): goto ma32; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma32; case ((uint8_t)16ULL): goto ma32; case ((uint8_t)17ULL): goto ma32; case ((uint8_t)18ULL): goto ma32; case ((uint8_t)19ULL): goto ma32; case ((uint8_t)20ULL): goto ma32; case ((uint8_t)21ULL): goto ma32; case ((uint8_t)22ULL): goto ma32; case ((uint8_t)23ULL): goto ma32; case ((uint8_t)24ULL): goto ma32; case ((uint8_t)25ULL): goto ma32; case ((uint8_t)26ULL): goto ma32; default: goto ma32; }
ma30:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t36 != ((uint8_t)13ULL)) goto mn35;
        {
            void * t38 = gm_l0;
            void * t39 = gm_l2;
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
            gm_b16_8 t41 = gm_l6;
            gm_b16_8 t42 = gm_f_opt_present_joptd3k((void *)NULL, cx, NULL, t38, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t42;
        }
        {
            void * t43 = gm_l0;
            gm_b16_8 t44 = gm_l10;
            gm_tnpifv20irnph t45;
            GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t44);
            gm_b0_1 t46 = gm_f_linef_o1esdvv((void *)NULL, cx, NULL, t43, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t47 = gm_l0;
            void * t48 = gm_l0;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 224));
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            if (o51) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 657, 13);
            uint64_t t52 = r50;
            GM_ST(uint64_t, (((uint8_t *)t47) + 224), t52);
        }
        {
            void * t53 = gm_l0;
            gm_t88rcucg1b7uk t54 = gm_l7;
            gm_b16_8 t55 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t55;
        }
        {
            void * t56 = gm_l0;
            gm_t6j6smj7qoctu t57;
            gm_b16_8 t58 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t58);
            gm_b16_8 t59 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t59);
            gm_b0_1 t60 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t56, &t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t61 = gm_l0;
            gm_t9qkh0rsooc8d t62;
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
            gm_b16_8 t63 = gm_l9;
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(1));
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
            gm_b0_1 t66 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t61, t62, t63, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t67 = gm_l0;
            void * t68 = gm_l0;
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)t68) + 224));
            uint64_t r70; bool o71 = __builtin_sub_overflow(t69, ((uint64_t)1ULL), &r70);
            if (o71) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 661, 13);
            uint64_t t72 = r70;
            GM_ST(uint64_t, (((uint8_t *)t67) + 224), t72);
        }
        {
            void * t73 = gm_l0;
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
            gm_b0_1 t75 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t76 = gm_l0;
            void * t77 = gm_l1;
            gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t77) + 0));
            gm_b16_8 t79 = gm_l6;
            gm_b16_8 t80 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t76, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t80);
            goto gm_fdone;
        }
        goto me29;
mn35:;
ma31:;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t82 != ((uint8_t)14ULL)) goto mn81;
        gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t28) + 8) + 0));
        gm_l12 = t83;
        {
            void * t85 = gm_l0;
            void * t86 = gm_l2;
            gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + 0));
            gm_b16_8 t88 = gm_l6;
            gm_b16_8 t89 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t85, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t89;
        }
        {
            uint64_t t90 = (uintptr_t)((uint64_t)1ULL);
            void * t91 = gm_l0;
            void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
            void * t93 = (void *)(((uint8_t *)t92) + 8);
            void * t94 = gm_l2;
            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + 0));
            gm_b16_8 t96 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t93, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&t96) + 8);
            if (t90 >= (uintptr_t)(t97)) gm_trap_index(t90, t97, gm_srcmeaag4q4qrjl, 667, 20);
            uint64_t t98 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t96)) + t90 * 8));
            gm_l14 = t98;
        }
        {
            void * t99 = gm_l0;
            gm_b16_8 t100 = gm_l13;
            gm_tnpifv20irnph t101;
            GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t100);
            gm_b0_1 t102 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t99, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t103 = gm_l0;
            void * t104 = gm_l0;
            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)t104) + 224));
            uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
            if (o107) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 669, 13);
            uint64_t t108 = r106;
            GM_ST(uint64_t, (((uint8_t *)t103) + 224), t108);
        }
        {
            void * t110 = gm_l0;
            void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
            void * t112 = (void *)(((uint8_t *)t111) + 8);
            gm_t88rcucg1b7uk t113 = gm_l7;
            gm_t53at80pfdus3 t114;
            gm_f_kind_ghvav22((void *)&t114, t112, GM_CONV(gm_csguankko4lmhd, t113));
            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t114) + 0));
            if (t115 != ((uint8_t)14ULL)) goto ie109;
            gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t114) + 8) + 0));
            gm_l15 = t116;
            {
                void * t118 = gm_l0;
                gm_t88rcucg1b7uk t119 = gm_l12;
                gm_t3877pk7lv748 t120;
                gm_b16_8 t121 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
                uint64_t t122 = gm_l14;
                GM_ST(uint64_t, (uint8_t *)&t120 + 16, t122);
                gm_tabb710d9mq2e t123;
                gm_f_format_gjujk09((void *)&t123, (void *)NULL, cx, NULL, &t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_srcmeaag4q4qrjl, 671, 32);
                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
                gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                gm_b16_8 t126 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t118, t119, t125);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t126;
            }
            {
                if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie127;
                uint32_t t128 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                gm_l17 = t128;
                {
                    void * t130 = gm_l0;
                    gm_t88rcucg1b7uk t131 = gm_l15;
                    uint32_t t132 = gm_l17;
                    uint64_t t133 = (uint64_t)t132;
                    gm_b16_8 t134 = gm_l16;
                    gm_th2lqbo77pvsc t135;
                    GM_ST(uint8_t, (uint8_t *)&t135 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t135 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t135 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t135), t134);
                    ((uint8_t *)&t135)[16] = 1;
                    gm_b16_8 t136 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t130, t131, t133, &t135);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t136;
                }
                goto ix127;
ie127:;
ix127:;
            }
            {
                void * t137 = gm_l0;
                gm_t88rcucg1b7uk t138 = gm_l7;
                gm_t88rcucg1b7uk t139 = gm_l15;
                gm_b16_8 t140 = gm_l16;
                gm_b16_8 t141 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t137, t138, ((uint8_t)1), t139, t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t141;
            }
            {
                void * t142 = gm_l0;
                gm_t6j6smj7qoctu t143;
                gm_b16_8 t144 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t143 + 0, t144);
                gm_b16_8 t145 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t143 + 16, t145);
                gm_b0_1 t146 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t142, &t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t147 = gm_l0;
                gm_t9qkh0rsooc8d t148;
                GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)0ULL));
                gm_b16_8 t149 = gm_l9;
                gm_b16_8 t150;
                GM_ST(void *, (uint8_t *)&t150, (void *)(gm_sicgmpca63jln));
                GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(1));
                gm_b16_8 t151;
                GM_ST(void *, (uint8_t *)&t151, (void *)(gm_svubtddeidu1d));
                GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(1));
                gm_b0_1 t152 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t147, t148, t149, t150, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t153 = gm_l0;
                void * t154 = gm_l0;
                uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)t154) + 224));
                uint64_t r156; bool o157 = __builtin_sub_overflow(t155, ((uint64_t)1ULL), &r156);
                if (o157) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 676, 13);
                uint64_t t158 = r156;
                GM_ST(uint64_t, (((uint8_t *)t153) + 224), t158);
            }
            {
                void * t159 = gm_l0;
                gm_b16_8 t160;
                GM_ST(void *, (uint8_t *)&t160, (void *)(gm_s2cuflhqkkuaq));
                GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(1));
                gm_b0_1 t161 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t159, t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t162 = gm_l0;
                void * t163 = gm_l1;
                gm_t88rcucg1b7uk t164 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t163) + 0));
                gm_t3877pk7lv748 t165;
                gm_b16_8 t166 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t165 + 0, t166);
                uint64_t t167 = gm_l14;
                GM_ST(uint64_t, (uint8_t *)&t165 + 16, t167);
                gm_tabb710d9mq2e t168;
                gm_f_format_gjujk09((void *)&t168, (void *)NULL, cx, NULL, &t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 8, gm_srcmeaag4q4qrjl, 678, 34);
                gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
                gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
                gm_b16_8 t171 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t162, t164, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t171);
                goto gm_fdone;
            }
            goto ix109;
ie109:;
            {
                void * t173 = gm_l1;
                gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t173) + 72));
                gm_b16_8 t175;
                GM_ST(void *, (uint8_t *)&t175, (void *)(gm_s6cju4qiijn6t));
                GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(22));
                gm_t08olq97rscn3 t176;
                gm_f_unsupported_531q51r((void *)&t176, (void *)NULL, cx, NULL, t174, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t176);
                goto gm_fdone;
            }
ix109:;
        }
        goto me29;
mn81:;
ma32:;
        void * t178 = gm_l1;
        gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 72));
        gm_b16_8 t180;
        GM_ST(void *, (uint8_t *)&t180, (void *)(gm_sjr00kolufse9));
        GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(18));
        gm_t08olq97rscn3 t181;
        gm_f_unsupported_531q51r((void *)&t181, (void *)NULL, cx, NULL, t179, t180);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t181);
        goto gm_fdone;
        goto me29;
mn177:;
ma33:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 653, 5);
me29:;
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
__attribute__((aligned(4))) void gm_f_variant_equal_bgud9j0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_variant_equal_bgud9j0 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* lhs */
    void * gm_l2; /* rhs */
    gm_tmmlu9bk8dch4 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* x */
    void * gm_l7; /* y */
    gm_b16_8 gm_l8; /* bytes */
    void * gm_l9; /* x */
    gm_ttggq7ng4kp3p gm_l10; /* vx */
    gm_ttggq7ng4kp3p gm_l11; /* v */
    gm_ttggq7ng4kp3p gm_l12; /* v */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_t88rcucg1b7uk gm_l14; /* tag */
    gm_b16_8 gm_l15; /* variants */
    void * gm_l17; /* p */
    uint8_t gm_l18; /* in_place */
    gm_b16_8 gm_l19; /* addr */
    gm_th2lqbo77pvsc gm_l20; /* payload */
    void * gm_l21; /* p */
    void * gm_l22; /* p */
    gm_b16_8 gm_l23; /* offs */
    gm_b16_8 gm_l24; /* tc */
    gm_b16_8 gm_l25; /* tv */
    gm_tuglhjjmj2g58 gm_l26; /* ii */
    gm_b16_8 gm_l27; /* c */
    gm_b16_8 gm_l28; /* pv */
    gm_t88rcucg1b7uk gm_l29; /* pt */
    gm_b16_8 gm_l30; /* pa */
    gm_b16_8 gm_l31; /* pe */
    gm_b16_8 gm_l32; /* f */
    gm_b16_8 gm_l33; /* ld */
    gm_tuglhjjmj2g58 gm_l34; /* ii */
    GM_STACK_CHECK(&F, 3669u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            {
                void * t4 = gm_l1;
                void * t5 = gm_l2;
                gm_t3mf58gv9kf3n t6;
                GM_ST(void *, ((uint8_t *)&t6 + 0), t4);
                GM_ST(void *, ((uint8_t *)&t6 + 8), t5);
                GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&gm_l3 + 0, t6);
                void * t7 = gm_l2;
                void * t8 = gm_l1;
                gm_t3mf58gv9kf3n t9;
                GM_ST(void *, ((uint8_t *)&t9 + 0), t7);
                GM_ST(void *, ((uint8_t *)&t9 + 8), t8);
                GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&gm_l3 + 16, t9);
            }
            void * t10 = ((uint8_t *)&gm_l3);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcmeaag4q4qrjl, 704, 5);
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t10 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t12 = t11;
            gm_l4 = t12;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l5;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l5;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    void * t24 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t20 * 16) + 0));
                    gm_l6 = t24;
                    void * t25 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t20 * 16) + 8));
                    gm_l7 = t25;
                    {
                        void * t28 = gm_l7;
                        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
                        if (t29 != ((uint8_t)5ULL)) goto ie27;
                        gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)t28) + 8) + 8));
                        gm_l8 = t30;
                        void * t31 = gm_l0;
                        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                        void * t33 = (void *)(((uint8_t *)t32) + 8);
                        void * t34 = gm_l6;
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
                        gm_t53at80pfdus3 t36;
                        gm_f_kind_ghvav22((void *)&t36, t33, GM_CONV(gm_csguankko4lmhd, t35));
                        uint8_t t37;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
                        if (t40 != ((uint8_t)10ULL)) goto mn39;
                        t37 = ((uint8_t)1);
                        goto me38;
mn39:;
                        t37 = ((uint8_t)0);
                        goto me38;
mn41:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 705, 38);
me38:;
                        if (!t37) goto ie27;
                        {
                            void * t43 = gm_l0;
                            void * t44 = gm_l0;
                            void * t45 = gm_l6;
                            gm_t5o0tpv5jka2l t46;
                            gm_f_scrutinee_addr_3fvmbme((void *)&t46, (void *)NULL, cx, NULL, t44, t45, ((uint8_t)1));
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t46))[0] != 0)) {
                                gm_t6j4d6ei14c2t t47;
                                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                                ((uint8_t *)&t47)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t47 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t46) + 8)));
                                F.ret = t47;
                                goto gm_fret;
                            }
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
                            gm_b16_8 t49 = gm_l8;
                            gm_b16_8 t50 = gm_f_str_equal_at_mshane1((void *)NULL, cx, NULL, t43, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_th2lqbo77pvsc t51;
                            GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t51), t50);
                            ((uint8_t *)&t51)[16] = 1;
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
                            ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t51);
                            goto gm_fdone;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 704, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t53 = gm_l5;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l5 = t56;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
    }
    {
        void * t57 = gm_l1;
        gm_l9 = t57;
    }
    {
        gm_ttggq7ng4kp3p t58;
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 0), ((uint32_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t58 + 8), ((void *)0));
        gm_l10 = t58;
    }
    {
        void * t60 = gm_l2;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 8) + 0));
        if (t61 != ((uint8_t)38ULL)) goto ie59;
        gm_ttggq7ng4kp3p t62 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t60) + 8) + 8));
        gm_l11 = t62;
        {
            gm_ttggq7ng4kp3p t64 = gm_l11;
            gm_l10 = t64;
        }
        goto ix59;
ie59:;
        void * t66 = gm_l1;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t66) + 8) + 0));
        if (t67 != ((uint8_t)38ULL)) goto ie65;
        gm_ttggq7ng4kp3p t68 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t66) + 8) + 8));
        gm_l12 = t68;
        {
            void * t70 = gm_l2;
            gm_l9 = t70;
        }
        {
            gm_ttggq7ng4kp3p t71 = gm_l12;
            gm_l10 = t71;
        }
        goto ix65;
ie65:;
        {
            gm_th2lqbo77pvsc t73;
            memset(&t73, 0, sizeof t73);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t73);
            goto gm_fdone;
        }
ix65:;
ix59:;
    }
    {
        void * t74 = gm_l9;
        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t74) + 0));
        gm_l13 = t75;
    }
    {
        void * t77 = gm_l0;
        void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
        void * t79 = (void *)(((uint8_t *)t78) + 8);
        gm_t88rcucg1b7uk t80 = gm_l13;
        gm_t5v8n6qrn4rh4 t81;
        gm_f_repr_53quqki((void *)&t81, (void *)NULL, cx, NULL, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t82 = (!(GM_LD(uint8_t, ((uint8_t *)&t81) + 0) == ((uint8_t)6ULL)));
        if (!t82) goto ie76;
        {
            gm_th2lqbo77pvsc t84;
            memset(&t84, 0, sizeof t84);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t84);
            goto gm_fdone;
        }
        goto ix76;
ie76:;
ix76:;
    }
    {
        void * t86 = gm_l0;
        gm_t88rcucg1b7uk t87 = gm_l13;
        void * t88 = gm_f_adt_def_ni54he1(t86, GM_CONV(gm_csguankko4lmhd, t87));
        uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t88) + 24) + 0));
        if (t89 != ((uint8_t)1ULL)) goto ie85;
        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t88) + 24) + 8) + 0));
        gm_l14 = t90;
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((((uint8_t *)t88) + 24) + 8) + 8));
        gm_l15 = t91;
        {
            uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
            uint64_t t94 = (uint64_t)t93;
            uint64_t t95 = (uintptr_t)t94;
            uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcmeaag4q4qrjl, 722, 16);
            gm_tckdobunaulee t97 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t95 * 72));
            F.l16 = t97;
        }
        {
            uint8_t t98;
            void * t100 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
            if (!(GM_LD(void *, ((uint8_t *)&t100)) != 0)) goto ie99;
            void * t101 = GM_LD(void *, ((uint8_t *)&t100));
            gm_l17 = t101;
            void * t103 = gm_l0;
            void * t104 = gm_l17;
            uint8_t t105 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t106 = t105;
            t98 = t106;
            goto ix99;
ie99:;
            uint8_t t108 = ((uint8_t)1);
            t98 = t108;
ix99:;
            gm_l18 = t98;
        }
        {
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(0));
            gm_l19 = t109;
        }
        {
            gm_th2lqbo77pvsc t110;
            memset(&t110, 0, sizeof t110);
            gm_l20 = t110;
        }
        {
            void * t112 = gm_l9;
            void * t113 = gm_l1;
            uint8_t t114 = ((t112 == t113));
            if (!t114) goto ie111;
            {
                void * t116 = gm_l0;
                void * t117 = gm_l9;
                uint8_t t118 = gm_l18;
                gm_t5o0tpv5jka2l t119;
                gm_f_scrutinee_addr_3fvmbme((void *)&t119, (void *)NULL, cx, NULL, t116, t117, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t119))[0] != 0)) {
                    gm_t6j4d6ei14c2t t120;
                    GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                    ((uint8_t *)&t120)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t120 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t119) + 8)));
                    F.ret = t120;
                    goto gm_fret;
                }
                gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
                gm_l19 = t121;
            }
            {
                void * t123 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                if (!(GM_LD(void *, ((uint8_t *)&t123)) != 0)) goto ie122;
                void * t124 = GM_LD(void *, ((uint8_t *)&t123));
                gm_l21 = t124;
                {
                    void * t126 = gm_l0;
                    void * t127 = gm_l21;
                    gm_t5o0tpv5jka2l t128;
                    gm_f_expr_porbc9t((void *)&t128, (void *)NULL, cx, NULL, t126, t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t128))[0] != 0)) {
                        gm_t6j4d6ei14c2t t129;
                        GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                        ((uint8_t *)&t129)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t129 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t128) + 8)));
                        F.ret = t129;
                        goto gm_fret;
                    }
                    gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 0, t130);
                }
                goto ix122;
ie122:;
ix122:;
            }
            goto ix111;
ie111:;
            {
                void * t133 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                if (!(GM_LD(void *, ((uint8_t *)&t133)) != 0)) goto ie132;
                void * t134 = GM_LD(void *, ((uint8_t *)&t133));
                gm_l22 = t134;
                {
                    void * t136 = gm_l0;
                    void * t137 = gm_l22;
                    gm_t5o0tpv5jka2l t138;
                    gm_f_expr_porbc9t((void *)&t138, (void *)NULL, cx, NULL, t136, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t138))[0] != 0)) {
                        gm_t6j4d6ei14c2t t139;
                        GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                        ((uint8_t *)&t139)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t139 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t138) + 8)));
                        F.ret = t139;
                        goto gm_fret;
                    }
                    gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t138) + 8));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l20)) + 0, t140);
                }
                goto ix132;
ie132:;
ix132:;
            }
            {
                void * t141 = gm_l0;
                void * t142 = gm_l9;
                gm_t5o0tpv5jka2l t143;
                gm_f_scrutinee_addr_3fvmbme((void *)&t143, (void *)NULL, cx, NULL, t141, t142, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t143))[0] != 0)) {
                    gm_t6j4d6ei14c2t t144;
                    GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                    ((uint8_t *)&t144)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t144 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t143) + 8)));
                    F.ret = t144;
                    goto gm_fret;
                }
                gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
                gm_l19 = t145;
            }
ix111:;
        }
        {
            void * t146 = gm_l0;
            void * t147 = GM_LD(void *, (((uint8_t *)t146) + 0));
            void * t148 = (void *)(((uint8_t *)t147) + 8);
            gm_t88rcucg1b7uk t149 = gm_l13;
            gm_b16_8 t150 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t150;
        }
        {
            void * t151 = gm_l0;
            void * t152 = GM_LD(void *, (((uint8_t *)t151) + 0));
            void * t153 = (void *)(((uint8_t *)t152) + 8);
            gm_t88rcucg1b7uk t154 = gm_l14;
            gm_b16_8 t155 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t155;
        }
        {
            gm_tftksr4op114e t156;
            gm_b16_8 t157 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t156 + 0, t157);
            gm_b16_8 t158 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t156 + 16, t158);
            uint64_t t159 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t159 >= (uintptr_t)(t160)) gm_trap_index(t159, t160, gm_srcmeaag4q4qrjl, 736, 61);
            uint64_t t161 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t159 * 8));
            GM_ST(uint64_t, (uint8_t *)&t156 + 32, t161);
            gm_tabb710d9mq2e t162;
            gm_f_format_qj6rkil((void *)&t162, (void *)NULL, cx, NULL, &t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 8, gm_srcmeaag4q4qrjl, 736, 18);
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
            gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
            gm_l25 = t164;
        }
        {
            void * t166 = gm_l0;
            gm_t88rcucg1b7uk t167 = gm_l14;
            gm_tqcc6dmlne11b t168;
            gm_f_int_info_2tcdf1n((void *)&t168, (void *)NULL, cx, NULL, t166, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t168))[16] != 0)) goto ie165;
            gm_tuglhjjmj2g58 t169 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t168));
            gm_l26 = t169;
            {
                gm_tuglhjjmj2g58 t171 = gm_l26;
                gm_b16_8 t172 = gm_l25;
                gm_b16_8 t173 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t171, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t173;
            }
            goto ix165;
ie165:;
ix165:;
        }
        {
            gm_t6j6smj7qoctu t174;
            gm_b16_8 t175 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t174 + 0, t175);
            void * t176 = gm_l0;
            gm_t88rcucg1b7uk t177 = gm_l14;
            gm_b16_8 t178 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t176, t177, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l16) + 24)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t174 + 16, t178);
            gm_tabb710d9mq2e t179;
            gm_f_format_6m84e0r((void *)&t179, (void *)NULL, cx, NULL, &t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 8, gm_srcmeaag4q4qrjl, 738, 10);
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&t179) + 8));
            gm_l27 = t180;
        }
        {
            gm_th2lqbo77pvsc t181 = gm_l20;
            gm_b16_8 t182;
            if (((((uint8_t *)&t181))[16] != 0)) {
                gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 0));
                t182 = t183;
            } else {
                gm_b16_8 t184 = gm_l27;
                gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
                gm_th2lqbo77pvsc t186;
                GM_ST(uint8_t, (uint8_t *)&t186 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t186 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t186 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t186), t185);
                ((uint8_t *)&t186)[16] = 1;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t186);
                goto gm_fdone;
            }
            gm_l28 = t182;
        }
        {
            gm_tkn42u7na3t81 t187 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l16) + 16));
            gm_t88rcucg1b7uk t188;
            if (((((uint8_t *)&t187))[4] != 0)) {
                gm_t88rcucg1b7uk t189 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t187) + 0));
                t188 = t189;
            } else {
                gm_b16_8 t190 = gm_l27;
                gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
                gm_th2lqbo77pvsc t192;
                GM_ST(uint8_t, (uint8_t *)&t192 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t192 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t192 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t192), t191);
                ((uint8_t *)&t192)[16] = 1;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
                GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t192);
                goto gm_fdone;
            }
            gm_l29 = t188;
        }
        {
            gm_t3877pk7lv748 t193;
            gm_b16_8 t194 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t193 + 0, t194);
            uint64_t t195 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t195 >= (uintptr_t)(t196)) gm_trap_index(t195, t196, gm_srcmeaag4q4qrjl, 741, 41);
            uint64_t t197 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t195 * 8));
            GM_ST(uint64_t, (uint8_t *)&t193 + 16, t197);
            gm_tabb710d9mq2e t198;
            gm_f_format_gjujk09((void *)&t198, (void *)NULL, cx, NULL, &t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_srcmeaag4q4qrjl, 741, 11);
            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
            gm_l30 = t199;
        }
        {
            gm_b16_8 t200;
            GM_ST(void *, (uint8_t *)&t200, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(0));
            gm_l31 = t200;
        }
        {
            void * t202 = gm_l0;
            void * t203 = GM_LD(void *, (((uint8_t *)t202) + 0));
            void * t204 = (void *)(((uint8_t *)t203) + 8);
            gm_t88rcucg1b7uk t205 = gm_l29;
            gm_t5v8n6qrn4rh4 t206;
            gm_f_repr_53quqki((void *)&t206, (void *)NULL, cx, NULL, t204, t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t207 = ((GM_LD(uint8_t, ((uint8_t *)&t206) + 0) == ((uint8_t)6ULL)));
            if (!t207) goto ie201;
            {
                void * t209 = gm_l0;
                void * t210 = GM_LD(void *, (((uint8_t *)t209) + 0));
                void * t211 = (void *)(((uint8_t *)t210) + 1968);
                void * t212 = gm_l0;
                void * t213 = GM_LD(void *, (((uint8_t *)t212) + 0));
                void * t214 = (void *)(((uint8_t *)t213) + 8);
                gm_t88rcucg1b7uk t215 = gm_l29;
                gm_b16_8 t216 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t211, t214, t215);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t216;
            }
            {
                gm_tfeab279c16rr t217;
                gm_b16_8 t218 = gm_l32;
                GM_ST(gm_b16_8, (uint8_t *)&t217 + 0, t218);
                gm_b16_8 t219 = gm_l30;
                GM_ST(gm_b16_8, (uint8_t *)&t217 + 16, t219);
                void * t220 = gm_l0;
                gm_t88rcucg1b7uk t221 = gm_l29;
                gm_b16_8 t222 = gm_l28;
                gm_b16_8 t223 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t220, t221, t222);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t217 + 32, t223);
                gm_tabb710d9mq2e t224;
                gm_f_format_h6uc0qg((void *)&t224, (void *)NULL, cx, NULL, &t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_srcmeaag4q4qrjl, 745, 14);
                gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
                gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                gm_l31 = t226;
            }
            goto ix201;
ie201:;
            {
                gm_t6j6smj7qoctu t228;
                void * t229 = gm_l0;
                void * t230 = GM_LD(void *, (((uint8_t *)t229) + 0));
                void * t231 = (void *)(((uint8_t *)t230) + 8);
                gm_t88rcucg1b7uk t232 = gm_l29;
                gm_b16_8 t233 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t231, t232);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t233);
                gm_b16_8 t234 = gm_l30;
                GM_ST(gm_b16_8, (uint8_t *)&t228 + 16, t234);
                gm_tabb710d9mq2e t235;
                gm_f_format_g9ssmu3((void *)&t235, (void *)NULL, cx, NULL, &t228);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 8, gm_srcmeaag4q4qrjl, 747, 22);
                gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&t235) + 8));
                gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                gm_l33 = t237;
            }
            {
                void * t239 = gm_l0;
                gm_t88rcucg1b7uk t240 = gm_l29;
                gm_tqcc6dmlne11b t241;
                gm_f_int_info_2tcdf1n((void *)&t241, (void *)NULL, cx, NULL, t239, t240);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t241))[16] != 0)) goto ie238;
                gm_tuglhjjmj2g58 t242 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t241));
                gm_l34 = t242;
                {
                    gm_tuglhjjmj2g58 t244 = gm_l34;
                    gm_b16_8 t245 = gm_l33;
                    gm_b16_8 t246 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t244, t245);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t246;
                }
                goto ix238;
ie238:;
ix238:;
            }
            {
                gm_b16_8 t247;
                void * t249 = gm_l0;
                void * t250 = GM_LD(void *, (((uint8_t *)t249) + 0));
                void * t251 = (void *)(((uint8_t *)t250) + 8);
                gm_t88rcucg1b7uk t252 = gm_l29;
                gm_t5v8n6qrn4rh4 t253;
                gm_f_repr_53quqki((void *)&t253, (void *)NULL, cx, NULL, t251, t252);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t254 = ((GM_LD(uint8_t, ((uint8_t *)&t253) + 0) == ((uint8_t)2ULL)));
                if (!t254) goto ie248;
                gm_t6j6smj7qoctu t256;
                gm_b16_8 t257 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t256 + 0, t257);
                gm_b16_8 t258 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t256 + 16, t258);
                gm_tabb710d9mq2e t259;
                gm_f_format_mjg3n5a((void *)&t259, (void *)NULL, cx, NULL, &t256);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 8, gm_srcmeaag4q4qrjl, 749, 45);
                gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&t259) + 8));
                gm_b16_8 t261 = t260;
                gm_b16_8 t262 = GM_LD(gm_b16_8, ((uint8_t *)&t261));
                t247 = t262;
                goto ix248;
ie248:;
                gm_t6j6smj7qoctu t264;
                gm_b16_8 t265 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t264 + 0, t265);
                gm_b16_8 t266 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t264 + 16, t266);
                gm_tabb710d9mq2e t267;
                gm_f_format_6m84e0r((void *)&t267, (void *)NULL, cx, NULL, &t264);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 8, gm_srcmeaag4q4qrjl, 749, 102);
                gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
                gm_b16_8 t269 = t268;
                gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
                t247 = t270;
ix248:;
                gm_l31 = t247;
            }
ix201:;
        }
        {
            gm_t6j6smj7qoctu t271;
            gm_b16_8 t272 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t271 + 0, t272);
            gm_b16_8 t273 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t271 + 16, t273);
            gm_tabb710d9mq2e t274;
            gm_f_format_sjgrpqb((void *)&t274, (void *)NULL, cx, NULL, &t271);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t274))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t274) + 8, gm_srcmeaag4q4qrjl, 751, 12);
            gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&t274) + 8));
            gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
            gm_th2lqbo77pvsc t277;
            GM_ST(uint8_t, (uint8_t *)&t277 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t277 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t277 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t277), t276);
            ((uint8_t *)&t277)[16] = 1;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t277);
            goto gm_fdone;
        }
        goto ix85;
ie85:;
        {
            gm_th2lqbo77pvsc t279;
            memset(&t279, 0, sizeof t279);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out))[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_t6j4d6ei14c2t *)gm_out) + 8, t279);
            goto gm_fdone;
        }
ix85:;
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
__attribute__((aligned(4))) void gm_f_with_expr_l1uk88k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5) {
    struct gm_fr_with_expr_l1uk88k F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* label */
    gm_b16_8 gm_l3; /* handlers */
    void * gm_l4; /* on_ */
    void * gm_l5; /* body */
    gm_b16_8 gm_l6; /* cur */
    gm_b16_8 gm_l7; /* result_slot */
    gm_b16_8 gm_l8; /* fw */
    gm_b16_8 gm_l9; /* w */
    gm_b16_8 gm_l10; /* fw_ref */
    gm_t9805qtonbsag gm_l11; /* nested */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t4lnf71mruqpt gm_l14; /* h */
    void * gm_l15; /* x */
    gm_b16_8 gm_l16; /* hv */
    uint32_t gm_l17; /* hid */
    gm_b16_8 gm_l18; /* table */
    uint32_t gm_l19; /* ns */
    gm_tiilmfd16v3fc gm_l20; /* eref */
    uint32_t gm_l21; /* id */
    gm_b16_8 gm_l22; /* n */
    gm_b16_8 gm_l23; /* decls */
    void * gm_l24; /* value */
    gm_b16_8 gm_l25; /* v */
    gm_b16_8 gm_l26; /* s */
    gm_b16_8 gm_l27; /* outer */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint32_t gm_l30; /* d */
    gm_b16_8 gm_l31; /* table */
    gm_tiilmfd16v3fc gm_l32; /* eref */
    uint32_t gm_l33; /* id */
    gm_b16_8 gm_l34; /* n */
    void * gm_l35; /* stack */
    gm_b16_8 gm_l36; /* saved */
    gm_b16_8 gm_l37; /* end */
    gm_t5lf9i3n1r3co gm_l39; /* saved_spec */
    gm_b16_8 gm_l42; /* v */
    GM_STACK_CHECK(&F, 3866u);
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
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 280));
        gm_l6 = t3;
    }
    {
        uint32_t t4 = gm_l2;
        gm_t88rcucg1b7uk t5;
        GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_9g4m5fi((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcmeaag4q4qrjl, 587, 20);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l7 = t7;
    }
    {
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        gm_l8 = t8;
    }
    {
        void * t10 = gm_l4;
        uint8_t t11;
        if ((GM_LD(void *, ((uint8_t *)&t10)) != 0)) goto mn13;
        t11 = ((uint8_t)0);
        goto me12;
mn13:;
        t11 = ((uint8_t)1);
        goto me12;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 590, 8);
me12:;
        if (!t11) goto ie9;
        {
            void * t16 = gm_l0;
            uint32_t t17 = gm_f_fresh_5kokjmg(t16);
            gm_t88rcucg1b7uk t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t17);
            gm_tabb710d9mq2e t19;
            gm_f_format_2d30afk((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcmeaag4q4qrjl, 591, 14);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
            gm_l9 = t20;
        }
        {
            void * t21 = gm_l0;
            gm_tfeab279c16rr t22;
            gm_b16_8 t23 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t22 + 0, t23);
            gm_b16_8 t24 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t22 + 16, t24);
            gm_b16_8 t25 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t22 + 32, t25);
            gm_b0_1 t26 = gm_f_linef_18bk901((void *)NULL, cx, NULL, t21, &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t27 = gm_l9;
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            gm_l8 = t28;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b16_8 t29;
        void * t31 = gm_l4;
        uint8_t t32;
        if ((GM_LD(void *, ((uint8_t *)&t31)) != 0)) goto mn34;
        t32 = ((uint8_t)0);
        goto me33;
mn34:;
        t32 = ((uint8_t)1);
        goto me33;
mn35:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 595, 25);
me33:;
        if (!t32) goto ie30;
        gm_b16_8 t37 = gm_l8;
        gm_tnpifv20irnph t38;
        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
        gm_tabb710d9mq2e t39;
        gm_f_format_h6b2ofo((void *)&t39, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcmeaag4q4qrjl, 595, 43);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
        gm_b16_8 t41 = t40;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        t29 = t42;
        goto ix30;
ie30:;
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s04qus0luoga5));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
        gm_b16_8 t45 = t44;
        t29 = t45;
ix30:;
        gm_l10 = t29;
    }
    {
        void * t46 = gm_l0;
        gm_b16_8 t47 = gm_l3;
        void * t48 = gm_l4;
        gm_t9805qtonbsag t49 = gm_f_nested_spec_po688kb((void *)NULL, cx, NULL, t46, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t49;
    }
    {
        {
            gm_b16_8 t51 = gm_l3;
            gm_l12 = t51;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t53 = gm_l13;
                uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t55 = (t53 < t54);
                if (!t55) goto lb52;
                {
                    uint64_t t58 = gm_l13;
                    uint64_t t59 = (uintptr_t)t58;
                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_t4lnf71mruqpt t63 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t59 * 32));
                    gm_l14 = t63;
                    {
                        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                        if (t67 != ((uint8_t)0ULL)) goto mn66;
                        void * t68 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                        gm_l15 = t68;
                        {
                            void * t70 = gm_l0;
                            void * t71 = gm_l15;
                            gm_t5o0tpv5jka2l t72;
                            gm_f_expr_porbc9t((void *)&t72, (void *)NULL, cx, NULL, t70, t71);
                            if (gm_unw.kind) {
                                goto gm_ll56;
                            }
                            if (((((uint8_t *)&t72))[0] != 0)) {
                                gm_t5o0tpv5jka2l t73;
                                GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                                ((uint8_t *)&t73)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t73 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t72) + 8)));
                                F.ret = t73;
                                goto gm_fret;
                            }
                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                            gm_l16 = t74;
                        }
                        {
                            void * t76 = gm_l0;
                            void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                            void * t78 = (void *)(((uint8_t *)t77) + 8);
                            void * t79 = gm_l15;
                            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t79) + 0));
                            gm_t53at80pfdus3 t81;
                            gm_f_kind_ghvav22((void *)&t81, t78, GM_CONV(gm_csguankko4lmhd, t80));
                            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t81) + 0));
                            if (t82 != ((uint8_t)25ULL)) goto ie75;
                            uint32_t t83 = GM_LD(uint32_t, (((uint8_t *)&t81) + 8));
                            gm_l17 = t83;
                            {
                                gm_b16_8 t85;
                                GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(0));
                                gm_l18 = t85;
                            }
                            {
                                if (!((((uint8_t *)&gm_l11))[4] != 0)) goto ie86;
                                uint32_t t87 = GM_LD(uint32_t, ((uint8_t *)&gm_l11));
                                gm_l19 = t87;
                                {
                                    void * t89 = gm_l0;
                                    void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
                                    uint32_t t91 = gm_l17;
                                    uint32_t t92 = gm_l19;
                                    gm_b16_8 t93 = gm_f_spec_handler_table_8h552cp((void *)NULL, cx, NULL, t90, t91, t92);
                                    if (gm_unw.kind) {
                                        goto gm_ll56;
                                    }
                                    gm_l18 = t93;
                                }
                                goto ix86;
ie86:;
                                {
                                    void * t95 = gm_l0;
                                    void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
                                    uint32_t t97 = gm_l17;
                                    gm_b16_8 t98 = gm_f_handler_table_2kdtlit((void *)NULL, cx, NULL, t96, t97);
                                    if (gm_unw.kind) {
                                        goto gm_ll56;
                                    }
                                    gm_l18 = t98;
                                }
ix86:;
                            }
                            {
                                void * t99 = gm_l0;
                                void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
                                void * t101 = GM_LD(void *, (((uint8_t *)t100) + 0));
                                uint32_t t102 = gm_l17;
                                void * t103 = gm_f_handler_lit_b02boff(t101, t102);
                                gm_tiilmfd16v3fc t104 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t103) + 0));
                                gm_l20 = t104;
                            }
                            {
                                void * t105 = gm_l0;
                                void * t106 = GM_LD(void *, (((uint8_t *)t105) + 0));
                                void * t107 = (void *)((uint8_t *)&gm_l20);
                                uint32_t t108 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t106, t107);
                                if (gm_unw.kind) {
                                    goto gm_ll56;
                                }
                                gm_l21 = t108;
                            }
                            {
                                void * t109 = gm_l0;
                                uint32_t t110 = gm_f_fresh_5kokjmg(t109);
                                gm_t88rcucg1b7uk t111;
                                GM_ST(uint32_t, ((uint8_t *)&t111 + 0), t110);
                                gm_tabb710d9mq2e t112;
                                gm_f_format_tcvndjh((void *)&t112, (void *)NULL, cx, NULL, t111);
                                if (gm_unw.kind) {
                                    goto gm_ll56;
                                }
                                if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srcmeaag4q4qrjl, 606, 22);
                                gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
                                gm_l22 = t113;
                            }
                            {
                                void * t114 = gm_l0;
                                gm_th6ngf7aenlq3 t115;
                                memset((uint8_t *)&t115 + 20, 0, 4);
                                memset((uint8_t *)&t115 + 76, 0, 4);
                                gm_b16_8 t116 = gm_l22;
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 0, t116);
                                uint32_t t117 = gm_l21;
                                GM_ST(uint32_t, (uint8_t *)&t115 + 16, t117);
                                gm_b16_8 t118 = gm_l18;
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 24, t118);
                                gm_b16_8 t119 = gm_l16;
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 40, t119);
                                gm_b16_8 t120 = gm_l16;
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 56, t120);
                                uint32_t t121 = gm_l2;
                                GM_ST(uint32_t, (uint8_t *)&t115 + 72, t121);
                                gm_b16_8 t122 = gm_l7;
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 80, t122);
                                gm_b16_8 t123 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 96, t123);
                                gm_b16_8 t124 = gm_l6;
                                GM_ST(gm_b16_8, (uint8_t *)&t115 + 112, t124);
                                gm_b0_1 t125 = gm_f_linef_d2o1cif((void *)NULL, cx, NULL, t114, &t115);
                                if (gm_unw.kind) {
                                    goto gm_ll56;
                                }
                            }
                            {
                                gm_b16_8 t126 = gm_l22;
                                gm_tnpifv20irnph t127;
                                GM_ST(gm_b16_8, ((uint8_t *)&t127 + 0), t126);
                                gm_tabb710d9mq2e t128;
                                gm_f_format_s4f2nfi((void *)&t128, (void *)NULL, cx, NULL, t127);
                                if (gm_unw.kind) {
                                    goto gm_ll56;
                                }
                                if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_srcmeaag4q4qrjl, 608, 23);
                                gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
                                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                                gm_l6 = t130;
                            }
                            goto ix75;
ie75:;
                            {
                                void * t132 = gm_l15;
                                gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 72));
                                gm_b16_8 t134;
                                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sbp38o7rrfqgd));
                                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(18));
                                gm_t08olq97rscn3 t135;
                                gm_f_unsupported_531q51r((void *)&t135, (void *)NULL, cx, NULL, t133, t134);
                                if (gm_unw.kind) {
                                    goto gm_ll56;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t135);
                                goto gm_fdone;
                            }
ix75:;
                        }
                        goto me65;
mn66:;
                        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                        if (t137 != ((uint8_t)1ULL)) goto mn136;
                        gm_b16_8 t138 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l14) + 8) + 0));
                        gm_l23 = t138;
                        void * t139 = GM_LD(void *, ((((uint8_t *)&gm_l14) + 8) + 16));
                        gm_l24 = t139;
                        {
                            void * t141 = gm_l0;
                            void * t142 = gm_l24;
                            gm_t5o0tpv5jka2l t143;
                            gm_f_expr_porbc9t((void *)&t143, (void *)NULL, cx, NULL, t141, t142);
                            if (gm_unw.kind) {
                                goto gm_ll56;
                            }
                            if (((((uint8_t *)&t143))[0] != 0)) {
                                gm_t5o0tpv5jka2l t144;
                                GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                                ((uint8_t *)&t144)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t144 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t143) + 8)));
                                F.ret = t144;
                                goto gm_fret;
                            }
                            gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
                            gm_l25 = t145;
                        }
                        {
                            void * t146 = gm_l0;
                            void * t147 = gm_l24;
                            gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t147) + 0));
                            gm_b16_8 t149 = gm_l25;
                            gm_b16_8 t150 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t146, t148, t149);
                            if (gm_unw.kind) {
                                goto gm_ll56;
                            }
                            gm_l26 = t150;
                        }
                        {
                            gm_b16_8 t151 = gm_l6;
                            gm_l27 = t151;
                        }
                        {
                            {
                                gm_b16_8 t153 = gm_l23;
                                gm_l28 = t153;
                            }
                            {
                                gm_l29 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t155 = gm_l29;
                                    uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                    uint8_t t157 = (t155 < t156);
                                    if (!t157) goto lb154;
                                    {
                                        uint64_t t160 = gm_l29;
                                        uint64_t t161 = (uintptr_t)t160;
                                        uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                        uint32_t t163 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t161 * 4));
                                        uint32_t t166 = GM_LD(uint32_t, ((uint8_t *)&t163));
                                        gm_l30 = t166;
                                        {
                                            void * t168 = gm_l0;
                                            void * t169 = GM_LD(void *, (((uint8_t *)t168) + 0));
                                            uint32_t t170 = gm_l30;
                                            gm_t5o0tpv5jka2l t171;
                                            gm_f_decl_table_l7u1k0g((void *)&t171, (void *)NULL, cx, NULL, t169, t170);
                                            if (gm_unw.kind) {
                                                goto gm_ll158;
                                            }
                                            if (((((uint8_t *)&t171))[0] != 0)) {
                                                gm_t5o0tpv5jka2l t172;
                                                GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
                                                ((uint8_t *)&t172)[0] = 1;
                                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t172 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t171) + 8)));
                                                F.ret = t172;
                                                goto gm_fret;
                                            }
                                            gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&t171) + 8));
                                            gm_l31 = t173;
                                        }
                                        {
                                            void * t174 = gm_l0;
                                            void * t175 = GM_LD(void *, (((uint8_t *)t174) + 0));
                                            void * t176 = GM_LD(void *, (((uint8_t *)t175) + 0));
                                            uint32_t t177 = gm_l30;
                                            void * t178 = gm_f_handler_decl_m66mgr8(t176, t177);
                                            gm_tiilmfd16v3fc t179 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t178) + 0));
                                            gm_l32 = t179;
                                        }
                                        {
                                            void * t180 = gm_l0;
                                            void * t181 = GM_LD(void *, (((uint8_t *)t180) + 0));
                                            void * t182 = (void *)((uint8_t *)&gm_l32);
                                            uint32_t t183 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t181, t182);
                                            if (gm_unw.kind) {
                                                goto gm_ll158;
                                            }
                                            gm_l33 = t183;
                                        }
                                        {
                                            void * t184 = gm_l0;
                                            uint32_t t185 = gm_f_fresh_5kokjmg(t184);
                                            gm_t88rcucg1b7uk t186;
                                            GM_ST(uint32_t, ((uint8_t *)&t186 + 0), t185);
                                            gm_tabb710d9mq2e t187;
                                            gm_f_format_tcvndjh((void *)&t187, (void *)NULL, cx, NULL, t186);
                                            if (gm_unw.kind) {
                                                goto gm_ll158;
                                            }
                                            if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 8, gm_srcmeaag4q4qrjl, 618, 26);
                                            gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&t187) + 8));
                                            gm_l34 = t188;
                                        }
                                        {
                                            void * t189 = gm_l0;
                                            gm_th6ngf7aenlq3 t190;
                                            memset((uint8_t *)&t190 + 20, 0, 4);
                                            memset((uint8_t *)&t190 + 76, 0, 4);
                                            gm_b16_8 t191 = gm_l34;
                                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 0, t191);
                                            uint32_t t192 = gm_l33;
                                            GM_ST(uint32_t, (uint8_t *)&t190 + 16, t192);
                                            gm_b16_8 t193 = gm_l31;
                                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 24, t193);
                                            gm_b16_8 t194 = gm_l26;
                                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 40, t194);
                                            gm_b16_8 t195 = gm_l27;
                                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 56, t195);
                                            uint32_t t196 = gm_l2;
                                            GM_ST(uint32_t, (uint8_t *)&t190 + 72, t196);
                                            gm_b16_8 t197 = gm_l7;
                                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 80, t197);
                                            gm_b16_8 t198 = gm_l10;
                                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 96, t198);
                                            gm_b16_8 t199 = gm_l6;
                                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 112, t199);
                                            gm_b0_1 t200 = gm_f_linef_arl1b28((void *)NULL, cx, NULL, t189, &t190);
                                            if (gm_unw.kind) {
                                                goto gm_ll158;
                                            }
                                        }
                                        {
                                            gm_b16_8 t201 = gm_l34;
                                            gm_tnpifv20irnph t202;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t202 + 0), t201);
                                            gm_tabb710d9mq2e t203;
                                            gm_f_format_s4f2nfi((void *)&t203, (void *)NULL, cx, NULL, t202);
                                            if (gm_unw.kind) {
                                                goto gm_ll158;
                                            }
                                            if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 8, gm_srcmeaag4q4qrjl, 620, 27);
                                            gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&t203) + 8));
                                            gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                                            gm_l6 = t205;
                                        }
                                        goto me164;
mn165:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 614, 17);
me164:;
                                    }
lc154:;
                                    {
                                        uint64_t t207 = gm_l29;
                                        uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                                        uint64_t t210 = r208;
                                        gm_l29 = t210;
                                    }
                                }
gm_ll158:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb154; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc154; }
                                }
                                goto gm_ll56;
lb154:;
                            }
                        }
                        goto me65;
mn136:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 598, 9);
me65:;
                    }
                    goto me61;
mn62:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 597, 5);
me61:;
                }
lc52:;
                {
                    uint64_t t212 = gm_l13;
                    uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
                    uint64_t t215 = r213;
                    gm_l13 = t215;
                }
            }
gm_ll56:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb52; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc52; }
            }
            goto gm_lad;
lb52:;
        }
    }
    {
        void * t217 = gm_l4;
        if (!(GM_LD(void *, ((uint8_t *)&t217)) != 0)) goto ie216;
        void * t218 = GM_LD(void *, ((uint8_t *)&t217));
        gm_l35 = t218;
        {
            void * t220 = gm_l0;
            void * t221 = gm_l1;
            uint32_t t222 = gm_l2;
            gm_b16_8 t223 = gm_l8;
            gm_b16_8 t224 = gm_l6;
            void * t225 = gm_l35;
            void * t226 = gm_l5;
            gm_f_with_on_fl0pjo9((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t220, t221, t222, t223, t224, t225, t226);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix216;
ie216:;
ix216:;
    }
    {
        void * t227 = gm_l0;
        gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)t227) + 280));
        gm_l36 = t228;
    }
    {
        void * t229 = gm_l0;
        gm_b16_8 t230 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t229) + 280), t230);
    }
    {
        void * t231 = gm_l0;
        uint32_t t232 = gm_f_fresh_5kokjmg(t231);
        gm_t88rcucg1b7uk t233;
        GM_ST(uint32_t, ((uint8_t *)&t233 + 0), t232);
        gm_tabb710d9mq2e t234;
        gm_f_format_dn5pqnq((void *)&t234, (void *)NULL, cx, NULL, t233);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_srcmeaag4q4qrjl, 628, 12);
        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
        gm_l37 = t235;
    }
    {
        void * t236 = gm_l0;
        uint32_t t237 = gm_l2;
        gm_b16_8 t238 = gm_l37;
        gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
        gm_f_with_scope_o670qbc((void *)&F.l38, (void *)NULL, cx, NULL, t236, t237, t239);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t240 = gm_l0;
        void * t241 = (void *)(((uint8_t *)t240) + 240);
        gm_tftb1odc739t5 t242 = gm_f_push_pcqb56b(t241, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l38)));
        if (((((uint8_t *)&t242))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t242) + 1, gm_srcmeaag4q4qrjl, 630, 5);
        gm_b0_1 t243 = GM_LD(gm_b0_1, (((uint8_t *)&t242) + 1));
    }
    {
        void * t244 = gm_l0;
        gm_t5lf9i3n1r3co t245 = GM_LD(gm_t5lf9i3n1r3co, (((uint8_t *)t244) + 40));
        gm_l39 = t245;
    }
    {
        void * t246 = gm_l0;
        uint8_t *t247 = (((uint8_t *)t246) + 40);
        void * t248 = gm_l0;
        gm_b16_8 t249 = gm_l3;
        void * t250 = gm_l5;
        gm_t9805qtonbsag t251 = gm_l11;
        gm_f_specializable_7or9fsf((void *)&(*(gm_t5lf9i3n1r3co *)t247), (void *)NULL, cx, NULL, t248, t249, t250, t251);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t252 = gm_l0;
        void * t253 = gm_l5;
        gm_f_expr_porbc9t((void *)&F.l40, (void *)NULL, cx, NULL, t252, t253);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t254 = gm_l0;
        gm_t5lf9i3n1r3co t255 = gm_l39;
        GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t254) + 40), t255);
    }
    {
        void * t256 = gm_l0;
        void * t257 = (void *)(((uint8_t *)t256) + 240);
        gm_t1g28mkc78m0c t258;
        gm_f_pop_g2usrvd((void *)&t258, t257);
        if (!((((uint8_t *)&t258))[104] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 635, 15);
        F.l41 = (*(gm_tehgl6lkqgvt0 *)(((uint8_t *)&t258) + 0));
    }
    {
        void * t259 = gm_l0;
        gm_b16_8 t260 = gm_l36;
        GM_ST(gm_b16_8, (((uint8_t *)t259) + 280), t260);
    }
    {
        gm_t5o0tpv5jka2l t261 = F.l40;
        if (((((uint8_t *)&t261))[0] != 0)) {
            gm_t5o0tpv5jka2l t262;
            GM_ST(uint8_t, (uint8_t *)&t262 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t262 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t262 + 4, 0);
            ((uint8_t *)&t262)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t262 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t261) + 8)));
            F.ret = t262;
            goto gm_fret;
        }
        gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&t261) + 8));
        gm_l42 = t263;
    }
    {
        void * t265 = gm_l0;
        void * t266 = GM_LD(void *, (((uint8_t *)t265) + 0));
        void * t267 = (void *)(((uint8_t *)t266) + 8);
        void * t268 = gm_l5;
        gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t268) + 0));
        uint8_t t270 = gm_f_is_never_in5ftma(t267, GM_CONV(gm_csguankko4lmhd, t269));
        uint8_t t271 = (uint8_t)!t270;
        if (!t271) goto ie264;
        void * t272 = gm_l0;
        void * t273 = GM_LD(void *, (((uint8_t *)t272) + 0));
        void * t274 = (void *)(((uint8_t *)t273) + 8);
        void * t275 = gm_l1;
        gm_t88rcucg1b7uk t276 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t275) + 0));
        uint8_t t277 = gm_f_is_never_in5ftma(t274, GM_CONV(gm_csguankko4lmhd, t276));
        uint8_t t278 = (uint8_t)!t277;
        if (!t278) goto ie264;
        {
            void * t280 = gm_l0;
            gm_tiilmfd16v3fc t281;
            memset((uint8_t *)&t281 + 4, 0, 4);
            uint32_t t282 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t281 + 0, t282);
            gm_b16_8 t283 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t281 + 8, t283);
            gm_b0_1 t284 = gm_f_linef_m4ia8qg((void *)NULL, cx, NULL, t280, &t281);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix264;
ie264:;
ix264:;
    }
    {
        void * t285 = gm_l0;
        gm_b0_1 t286 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t285, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l41)), ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t287 = gm_l0;
        gm_b16_8 t288 = gm_l37;
        gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&t288));
        gm_b0_1 t290 = gm_f_label_710oev4((void *)NULL, cx, NULL, t287, t289);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t292 = gm_l0;
        void * t293 = GM_LD(void *, (((uint8_t *)t292) + 0));
        void * t294 = (void *)(((uint8_t *)t293) + 8);
        void * t295 = gm_l1;
        gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t295) + 0));
        uint8_t t297 = gm_f_is_never_in5ftma(t294, GM_CONV(gm_csguankko4lmhd, t296));
        if (!t297) goto ie291;
        {
            void * t299 = gm_l0;
            void * t300 = gm_l1;
            gm_t88rcucg1b7uk t301 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t300) + 0));
            gm_b16_8 t302 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t299, t301);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t302);
            goto gm_fdone;
        }
        goto ix291;
ie291:;
ix291:;
    }
    {
        void * t303 = gm_l0;
        void * t304 = gm_l1;
        gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t304) + 0));
        uint32_t t306 = gm_l2;
        gm_t88rcucg1b7uk t307;
        GM_ST(uint32_t, ((uint8_t *)&t307 + 0), t306);
        gm_tabb710d9mq2e t308;
        gm_f_format_rslrdtj((void *)&t308, (void *)NULL, cx, NULL, t307);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 8, gm_srcmeaag4q4qrjl, 644, 25);
        gm_b16_8 t309 = GM_LD(gm_b16_8, (((uint8_t *)&t308) + 8));
        gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
        gm_b16_8 t311 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t303, t305, t310);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t311);
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

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
typedef struct gm_cskfp28qopl236 { uint32_t f0[8]; uint8_t f1[64]; uint64_t f2; uint64_t f3; } gm_cskfp28qopl236;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { uint32_t a[8]; } gm_tg8e8rhc3qio9;
typedef struct { uint32_t a[64]; } gm_tldv4vabljrvm;
typedef struct { uint8_t a[64]; } gm_tsn02dhhitgs4;
_Static_assert(sizeof(gm_cskfp28qopl236) == 112 && _Alignof(gm_cskfp28qopl236) == 8 && offsetof(gm_cskfp28qopl236, f0) == 0 && offsetof(gm_cskfp28qopl236, f1) == 32 && offsetof(gm_cskfp28qopl236, f2) == 96 && offsetof(gm_cskfp28qopl236, f3) == 104, "layout of `Sha256`");
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
_Static_assert(sizeof(gm_tg8e8rhc3qio9) == 32 && _Alignof(gm_tg8e8rhc3qio9) == 4 && offsetof(gm_tg8e8rhc3qio9, a) == 0, "layout of `[8]u32`");
_Static_assert(sizeof(gm_tldv4vabljrvm) == 256 && _Alignof(gm_tldv4vabljrvm) == 4 && offsetof(gm_tldv4vabljrvm, a) == 0, "layout of `[64]u32`");
typedef struct { gm_tg8e8rhc3qio9 f0; gm_tsn02dhhitgs4 f1; uint64_t f2; uint64_t f3; } gm_tq14qvifgdvpm;
_Static_assert(sizeof(gm_tsn02dhhitgs4) == 64 && _Alignof(gm_tsn02dhhitgs4) == 1 && offsetof(gm_tsn02dhhitgs4, a) == 0, "layout of `[64]u8`");
_Static_assert(sizeof(gm_tq14qvifgdvpm) == 112 && _Alignof(gm_tq14qvifgdvpm) == 8 && offsetof(gm_tq14qvifgdvpm, f0) == 0 && offsetof(gm_tq14qvifgdvpm, f1) == 32 && offsetof(gm_tq14qvifgdvpm, f2) == 96 && offsetof(gm_tq14qvifgdvpm, f3) == 104, "layout of `Sha256`");
struct gm_fr_compress_t585fuj {
    gm_frame h;
    gm_tldv4vabljrvm l2; /* w */
    gm_b0_1 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_final_rg5ko3p {
    gm_frame h;
    gm_tq14qvifgdvpm l1; /* t */
    gm_t7mlrlibdq1ja ret;
};
struct gm_fr_init_s32ien0 {
    gm_frame h;
    gm_tq14qvifgdvpm ret;
};
struct gm_fr_sum_ugu720i {
    gm_frame h;
    gm_tq14qvifgdvpm l1; /* s */
    gm_t7mlrlibdq1ja ret;
};
struct gm_fr_update_pn3fq6a {
    gm_frame h;
    gm_b0_1 ret;
};
__attribute__((aligned(4))) void gm_f_compress_t585fuj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_final_rg5ko3p(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t7mlrlibdq1ja gm_f_final_rg5ko3p_c(void * a0);
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out);
__attribute__((aligned(4))) gm_cskfp28qopl236 gm_f_init_s32ien0_c(void);
inline __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t7mlrlibdq1ja gm_f_sum_ugu720i_c(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_update_pn3fq6a(void * a0, gm_b16_8 a1);
extern uint8_t gm_csu7k86qqgd[256] __attribute__((aligned(4)));
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcintcpduj3u2g[];
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_s32ien0 F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tg8e8rhc3qio9 t2;
        GM_ST(uint32_t, (uint8_t *)&t2 + 0, ((uint32_t)1779033703ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 4, ((uint32_t)3144134277ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 8, ((uint32_t)1013904242ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 12, ((uint32_t)2773480762ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 16, ((uint32_t)1359893119ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 20, ((uint32_t)2600822924ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 24, ((uint32_t)528734635ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 28, ((uint32_t)1541459225ULL));
        gm_tsn02dhhitgs4 t3;
        for (uint64_t i = 0; i < 64; i++) GM_ST(uint8_t, (uint8_t *)&t3 + i * 1, ((uint8_t)0ULL));
        GM_ST(gm_tg8e8rhc3qio9, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 0, t2);
        GM_ST(gm_tsn02dhhitgs4, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 32, t3);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 96, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 104, ((uint64_t)0ULL));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sum_ugu720i F;
    gm_b16_8 gm_l0; /* data */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_init_s32ien0((void *)&F.l1);
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = gm_l0;
        gm_f_update_pn3fq6a(t2, t3);
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_f_final_rg5ko3p((void *)&(*(gm_t7mlrlibdq1ja *)gm_out), t4);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_compress_t585fuj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_compress_t585fuj F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* hi */
    uint32_t gm_l7; /* lo */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint32_t gm_l11; /* x */
    uint32_t gm_l12; /* y */
    uint32_t gm_l13; /* s0 */
    uint32_t gm_l14; /* s1 */
    uint32_t gm_l15; /* a */
    uint32_t gm_l16; /* bb */
    uint32_t gm_l17; /* c */
    uint32_t gm_l18; /* d */
    uint32_t gm_l19; /* e */
    uint32_t gm_l20; /* f */
    uint32_t gm_l21; /* g */
    uint32_t gm_l22; /* hh */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    uint32_t gm_l26; /* s1 */
    uint32_t gm_l27; /* ch */
    uint32_t gm_l28; /* t1 */
    uint32_t gm_l29; /* s0 */
    uint32_t gm_l30; /* maj */
    uint32_t gm_l31; /* t2 */
    void * gm_l32; /* <tmp> */
    void * gm_l33; /* <tmp> */
    void * gm_l34; /* <tmp> */
    void * gm_l35; /* <tmp> */
    void * gm_l36; /* <tmp> */
    void * gm_l37; /* <tmp> */
    void * gm_l38; /* <tmp> */
    void * gm_l39; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 1703u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            gm_l3 = ((uint64_t)16ULL);
        }
        {
            for (;;) {
                uint64_t t4 = gm_l4;
                uint64_t t5 = gm_l3;
                uint8_t t6 = (t4 < t5);
                if (!t6) goto lb3;
                {
                    uint64_t t9 = gm_l4;
                    uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)&t9));
                    gm_l5 = t12;
                    {
                        uint64_t t14 = gm_l5;
                        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)4ULL), t14, &r15);
                        if (o16) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 53, 18);
                        uint64_t t17 = r15;
                        uint64_t t18 = (uintptr_t)t17;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srcintcpduj3u2g, 53, 16);
                        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 1));
                        uint32_t t21 = (uint32_t)t20;
                        unsigned __int128 n22 = (unsigned __int128)((uint64_t)24ULL);
                        if (n22 >= 32) gm_trap_shift((uint64_t)n22, "u32", gm_srcintcpduj3u2g, 53, 16);
                        uint32_t r23 = (uint32_t)((uint32_t)t21 << (int)n22);
                        bool o24 = (uint32_t)(r23 >> (int)n22) != t21;
                        if (o24) gm_trap("integer overflow in `<<`", gm_srcintcpduj3u2g, 53, 16);
                        uint32_t t25 = r23;
                        uint64_t t26 = gm_l5;
                        uint64_t r27; bool o28 = __builtin_mul_overflow(((uint64_t)4ULL), t26, &r27);
                        if (o28) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 53, 48);
                        uint64_t t29 = r27;
                        uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                        if (o31) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 53, 48);
                        uint64_t t32 = r30;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcintcpduj3u2g, 53, 46);
                        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t33 * 1));
                        uint32_t t36 = (uint32_t)t35;
                        unsigned __int128 n37 = (unsigned __int128)((uint64_t)16ULL);
                        if (n37 >= 32) gm_trap_shift((uint64_t)n37, "u32", gm_srcintcpduj3u2g, 53, 46);
                        uint32_t r38 = (uint32_t)((uint32_t)t36 << (int)n37);
                        bool o39 = (uint32_t)(r38 >> (int)n37) != t36;
                        if (o39) gm_trap("integer overflow in `<<`", gm_srcintcpduj3u2g, 53, 46);
                        uint32_t t40 = r38;
                        uint32_t t41 = (uint32_t)(t25 | t40);
                        gm_l6 = t41;
                    }
                    {
                        uint64_t t42 = gm_l5;
                        uint64_t r43; bool o44 = __builtin_mul_overflow(((uint64_t)4ULL), t42, &r43);
                        if (o44) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 54, 18);
                        uint64_t t45 = r43;
                        uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)2ULL), &r46);
                        if (o47) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 54, 18);
                        uint64_t t48 = r46;
                        uint64_t t49 = (uintptr_t)t48;
                        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srcintcpduj3u2g, 54, 16);
                        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t49 * 1));
                        uint32_t t52 = (uint32_t)t51;
                        unsigned __int128 n53 = (unsigned __int128)((uint64_t)8ULL);
                        if (n53 >= 32) gm_trap_shift((uint64_t)n53, "u32", gm_srcintcpduj3u2g, 54, 16);
                        uint32_t r54 = (uint32_t)((uint32_t)t52 << (int)n53);
                        bool o55 = (uint32_t)(r54 >> (int)n53) != t52;
                        if (o55) gm_trap("integer overflow in `<<`", gm_srcintcpduj3u2g, 54, 16);
                        uint32_t t56 = r54;
                        uint64_t t57 = gm_l5;
                        uint64_t r58; bool o59 = __builtin_mul_overflow(((uint64_t)4ULL), t57, &r58);
                        if (o59) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 54, 50);
                        uint64_t t60 = r58;
                        uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)3ULL), &r61);
                        if (o62) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 54, 50);
                        uint64_t t63 = r61;
                        uint64_t t64 = (uintptr_t)t63;
                        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcintcpduj3u2g, 54, 48);
                        uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t64 * 1));
                        uint32_t t67 = (uint32_t)t66;
                        uint32_t t68 = (uint32_t)(t56 | t67);
                        gm_l7 = t68;
                    }
                    {
                        uint64_t t69 = gm_l5;
                        uint64_t t70 = (uintptr_t)t69;
                        if (t70 >= (uintptr_t)(64)) gm_trap_index(t70, 64, gm_srcintcpduj3u2g, 55, 9);
                        uint32_t t71 = gm_l6;
                        uint32_t t72 = gm_l7;
                        uint32_t t73 = (uint32_t)(t71 | t72);
                        GM_ST(uint32_t, (((uint8_t *)&F.l2) + t70 * 4), t73);
                    }
                    goto me10;
mn11:;
                    gm_trap("no match arm matched the scrutinee", gm_srcintcpduj3u2g, 52, 5);
me10:;
                }
lc3:;
                {
                    uint64_t t75 = gm_l4;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l4 = t78;
                }
            }
lb3:;
        }
    }
    {
        {
            gm_l9 = ((uint64_t)16ULL);
        }
        {
            gm_l8 = ((uint64_t)64ULL);
        }
        {
            for (;;) {
                uint64_t t81 = gm_l9;
                uint64_t t82 = gm_l8;
                uint8_t t83 = (t81 < t82);
                if (!t83) goto lb80;
                {
                    uint64_t t86 = gm_l9;
                    uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&t86));
                    gm_l10 = t89;
                    {
                        uint64_t t91 = gm_l10;
                        uint64_t r92; bool o93 = __builtin_sub_overflow(t91, ((uint64_t)15ULL), &r92);
                        if (o93) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 58, 16);
                        uint64_t t94 = r92;
                        uint64_t t95 = (uintptr_t)t94;
                        if (t95 >= (uintptr_t)(64)) gm_trap_index(t95, 64, gm_srcintcpduj3u2g, 58, 14);
                        uint32_t t96 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + t95 * 4));
                        gm_l11 = t96;
                    }
                    {
                        uint64_t t97 = gm_l10;
                        uint64_t r98; bool o99 = __builtin_sub_overflow(t97, ((uint64_t)2ULL), &r98);
                        if (o99) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 59, 16);
                        uint64_t t100 = r98;
                        uint64_t t101 = (uintptr_t)t100;
                        if (t101 >= (uintptr_t)(64)) gm_trap_index(t101, 64, gm_srcintcpduj3u2g, 59, 14);
                        uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + t101 * 4));
                        gm_l12 = t102;
                    }
                    {
                        uint32_t t103 = gm_l11;
                        uint32_t t104 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t103), (32 - (uint64_t)((uint32_t)7ULL) % 32), 32);
                        uint32_t t105 = t104;
                        uint32_t t106 = gm_l11;
                        uint32_t t107 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t106), (32 - (uint64_t)((uint32_t)18ULL) % 32), 32);
                        uint32_t t108 = t107;
                        uint32_t t109 = (uint32_t)(t105 ^ t108);
                        uint32_t t110 = gm_l11;
                        unsigned __int128 n111 = (unsigned __int128)((uint64_t)3ULL);
                        if (n111 >= 32) gm_trap_shift((uint64_t)n111, "u32", gm_srcintcpduj3u2g, 60, 45);
                        uint32_t t112 = (uint32_t)(t110 >> (int)n111);
                        uint32_t t113 = (uint32_t)(t109 ^ t112);
                        gm_l13 = t113;
                    }
                    {
                        uint32_t t114 = gm_l12;
                        uint32_t t115 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t114), (32 - (uint64_t)((uint32_t)17ULL) % 32), 32);
                        uint32_t t116 = t115;
                        uint32_t t117 = gm_l12;
                        uint32_t t118 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t117), (32 - (uint64_t)((uint32_t)19ULL) % 32), 32);
                        uint32_t t119 = t118;
                        uint32_t t120 = (uint32_t)(t116 ^ t119);
                        uint32_t t121 = gm_l12;
                        unsigned __int128 n122 = (unsigned __int128)((uint64_t)10ULL);
                        if (n122 >= 32) gm_trap_shift((uint64_t)n122, "u32", gm_srcintcpduj3u2g, 61, 46);
                        uint32_t t123 = (uint32_t)(t121 >> (int)n122);
                        uint32_t t124 = (uint32_t)(t120 ^ t123);
                        gm_l14 = t124;
                    }
                    {
                        uint64_t t125 = gm_l10;
                        uint64_t t126 = (uintptr_t)t125;
                        if (t126 >= (uintptr_t)(64)) gm_trap_index(t126, 64, gm_srcintcpduj3u2g, 62, 9);
                        uint64_t t127 = gm_l10;
                        uint64_t r128; bool o129 = __builtin_sub_overflow(t127, ((uint64_t)16ULL), &r128);
                        if (o129) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 62, 18);
                        uint64_t t130 = r128;
                        uint64_t t131 = (uintptr_t)t130;
                        if (t131 >= (uintptr_t)(64)) gm_trap_index(t131, 64, gm_srcintcpduj3u2g, 62, 16);
                        uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + t131 * 4));
                        uint32_t t133 = gm_l13;
                        uint32_t r134; bool o135 = __builtin_add_overflow(t132, t133, &r134);
                        uint32_t t136 = r134;
                        uint64_t t137 = gm_l10;
                        uint64_t r138; bool o139 = __builtin_sub_overflow(t137, ((uint64_t)7ULL), &r138);
                        if (o139) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 62, 37);
                        uint64_t t140 = r138;
                        uint64_t t141 = (uintptr_t)t140;
                        if (t141 >= (uintptr_t)(64)) gm_trap_index(t141, 64, gm_srcintcpduj3u2g, 62, 35);
                        uint32_t t142 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + t141 * 4));
                        uint32_t r143; bool o144 = __builtin_add_overflow(t136, t142, &r143);
                        uint32_t t145 = r143;
                        uint32_t t146 = gm_l14;
                        uint32_t r147; bool o148 = __builtin_add_overflow(t145, t146, &r147);
                        uint32_t t149 = r147;
                        GM_ST(uint32_t, (((uint8_t *)&F.l2) + t126 * 4), t149);
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_srcintcpduj3u2g, 57, 5);
me87:;
                }
lc80:;
                {
                    uint64_t t151 = gm_l9;
                    uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                    uint64_t t154 = r152;
                    gm_l9 = t154;
                }
            }
lb80:;
        }
    }
    {
        uint64_t t155 = (uintptr_t)((uint64_t)0ULL);
        void * t156 = gm_l0;
        if (t155 >= (uintptr_t)(8)) gm_trap_index(t155, 8, gm_srcintcpduj3u2g, 64, 10);
        uint32_t t157 = GM_LD(uint32_t, (((uint8_t *)t156) + t155 * 4));
        gm_l15 = t157;
    }
    {
        uint64_t t158 = (uintptr_t)((uint64_t)1ULL);
        void * t159 = gm_l0;
        if (t158 >= (uintptr_t)(8)) gm_trap_index(t158, 8, gm_srcintcpduj3u2g, 65, 11);
        uint32_t t160 = GM_LD(uint32_t, (((uint8_t *)t159) + t158 * 4));
        gm_l16 = t160;
    }
    {
        uint64_t t161 = (uintptr_t)((uint64_t)2ULL);
        void * t162 = gm_l0;
        if (t161 >= (uintptr_t)(8)) gm_trap_index(t161, 8, gm_srcintcpduj3u2g, 66, 10);
        uint32_t t163 = GM_LD(uint32_t, (((uint8_t *)t162) + t161 * 4));
        gm_l17 = t163;
    }
    {
        uint64_t t164 = (uintptr_t)((uint64_t)3ULL);
        void * t165 = gm_l0;
        if (t164 >= (uintptr_t)(8)) gm_trap_index(t164, 8, gm_srcintcpduj3u2g, 67, 10);
        uint32_t t166 = GM_LD(uint32_t, (((uint8_t *)t165) + t164 * 4));
        gm_l18 = t166;
    }
    {
        uint64_t t167 = (uintptr_t)((uint64_t)4ULL);
        void * t168 = gm_l0;
        if (t167 >= (uintptr_t)(8)) gm_trap_index(t167, 8, gm_srcintcpduj3u2g, 68, 10);
        uint32_t t169 = GM_LD(uint32_t, (((uint8_t *)t168) + t167 * 4));
        gm_l19 = t169;
    }
    {
        uint64_t t170 = (uintptr_t)((uint64_t)5ULL);
        void * t171 = gm_l0;
        if (t170 >= (uintptr_t)(8)) gm_trap_index(t170, 8, gm_srcintcpduj3u2g, 69, 10);
        uint32_t t172 = GM_LD(uint32_t, (((uint8_t *)t171) + t170 * 4));
        gm_l20 = t172;
    }
    {
        uint64_t t173 = (uintptr_t)((uint64_t)6ULL);
        void * t174 = gm_l0;
        if (t173 >= (uintptr_t)(8)) gm_trap_index(t173, 8, gm_srcintcpduj3u2g, 70, 10);
        uint32_t t175 = GM_LD(uint32_t, (((uint8_t *)t174) + t173 * 4));
        gm_l21 = t175;
    }
    {
        uint64_t t176 = (uintptr_t)((uint64_t)7ULL);
        void * t177 = gm_l0;
        if (t176 >= (uintptr_t)(8)) gm_trap_index(t176, 8, gm_srcintcpduj3u2g, 71, 11);
        uint32_t t178 = GM_LD(uint32_t, (((uint8_t *)t177) + t176 * 4));
        gm_l22 = t178;
    }
    {
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            gm_l23 = ((uint64_t)64ULL);
        }
        {
            for (;;) {
                uint64_t t181 = gm_l24;
                uint64_t t182 = gm_l23;
                uint8_t t183 = (t181 < t182);
                if (!t183) goto lb180;
                {
                    uint64_t t186 = gm_l24;
                    uint64_t t189 = GM_LD(uint64_t, ((uint8_t *)&t186));
                    gm_l25 = t189;
                    {
                        uint32_t t191 = gm_l19;
                        uint32_t t192 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t191), (32 - (uint64_t)((uint32_t)6ULL) % 32), 32);
                        uint32_t t193 = t192;
                        uint32_t t194 = gm_l19;
                        uint32_t t195 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t194), (32 - (uint64_t)((uint32_t)11ULL) % 32), 32);
                        uint32_t t196 = t195;
                        uint32_t t197 = (uint32_t)(t193 ^ t196);
                        uint32_t t198 = gm_l19;
                        uint32_t t199 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t198), (32 - (uint64_t)((uint32_t)25ULL) % 32), 32);
                        uint32_t t200 = t199;
                        uint32_t t201 = (uint32_t)(t197 ^ t200);
                        gm_l26 = t201;
                    }
                    {
                        uint32_t t202 = gm_l19;
                        uint32_t t203 = gm_l20;
                        uint32_t t204 = (uint32_t)(t202 & t203);
                        uint32_t t205 = gm_l19;
                        uint32_t t206 = (uint32_t)~t205;
                        uint32_t t207 = gm_l21;
                        uint32_t t208 = (uint32_t)(t206 & t207);
                        uint32_t t209 = (uint32_t)(t204 ^ t208);
                        gm_l27 = t209;
                    }
                    {
                        uint32_t t210 = gm_l22;
                        uint32_t t211 = gm_l26;
                        uint32_t r212; bool o213 = __builtin_add_overflow(t210, t211, &r212);
                        uint32_t t214 = r212;
                        uint32_t t215 = gm_l27;
                        uint32_t r216; bool o217 = __builtin_add_overflow(t214, t215, &r216);
                        uint32_t t218 = r216;
                        uint64_t t219 = gm_l25;
                        uint64_t t220 = (uintptr_t)t219;
                        if (t220 >= (uintptr_t)(64)) gm_trap_index(t220, 64, gm_srcintcpduj3u2g, 75, 33);
                        uint32_t t221 = GM_LD(uint32_t, (((uint8_t *)gm_csu7k86qqgd) + t220 * 4));
                        uint32_t r222; bool o223 = __builtin_add_overflow(t218, t221, &r222);
                        uint32_t t224 = r222;
                        uint64_t t225 = gm_l25;
                        uint64_t t226 = (uintptr_t)t225;
                        if (t226 >= (uintptr_t)(64)) gm_trap_index(t226, 64, gm_srcintcpduj3u2g, 75, 41);
                        uint32_t t227 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + t226 * 4));
                        uint32_t r228; bool o229 = __builtin_add_overflow(t224, t227, &r228);
                        uint32_t t230 = r228;
                        gm_l28 = t230;
                    }
                    {
                        uint32_t t231 = gm_l15;
                        uint32_t t232 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t231), (32 - (uint64_t)((uint32_t)2ULL) % 32), 32);
                        uint32_t t233 = t232;
                        uint32_t t234 = gm_l15;
                        uint32_t t235 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t234), (32 - (uint64_t)((uint32_t)13ULL) % 32), 32);
                        uint32_t t236 = t235;
                        uint32_t t237 = (uint32_t)(t233 ^ t236);
                        uint32_t t238 = gm_l15;
                        uint32_t t239 = (uint32_t)gm_rotl((unsigned __int128)(uint32_t)(t238), (32 - (uint64_t)((uint32_t)22ULL) % 32), 32);
                        uint32_t t240 = t239;
                        uint32_t t241 = (uint32_t)(t237 ^ t240);
                        gm_l29 = t241;
                    }
                    {
                        uint32_t t242 = gm_l15;
                        uint32_t t243 = gm_l16;
                        uint32_t t244 = (uint32_t)(t242 & t243);
                        uint32_t t245 = gm_l15;
                        uint32_t t246 = gm_l17;
                        uint32_t t247 = (uint32_t)(t245 & t246);
                        uint32_t t248 = (uint32_t)(t244 ^ t247);
                        uint32_t t249 = gm_l16;
                        uint32_t t250 = gm_l17;
                        uint32_t t251 = (uint32_t)(t249 & t250);
                        uint32_t t252 = (uint32_t)(t248 ^ t251);
                        gm_l30 = t252;
                    }
                    {
                        uint32_t t253 = gm_l29;
                        uint32_t t254 = gm_l30;
                        uint32_t r255; bool o256 = __builtin_add_overflow(t253, t254, &r255);
                        uint32_t t257 = r255;
                        gm_l31 = t257;
                    }
                    {
                        uint32_t t258 = gm_l21;
                        gm_l22 = t258;
                    }
                    {
                        uint32_t t259 = gm_l20;
                        gm_l21 = t259;
                    }
                    {
                        uint32_t t260 = gm_l19;
                        gm_l20 = t260;
                    }
                    {
                        uint32_t t261 = gm_l18;
                        uint32_t t262 = gm_l28;
                        uint32_t r263; bool o264 = __builtin_add_overflow(t261, t262, &r263);
                        uint32_t t265 = r263;
                        gm_l19 = t265;
                    }
                    {
                        uint32_t t266 = gm_l17;
                        gm_l18 = t266;
                    }
                    {
                        uint32_t t267 = gm_l16;
                        gm_l17 = t267;
                    }
                    {
                        uint32_t t268 = gm_l15;
                        gm_l16 = t268;
                    }
                    {
                        uint32_t t269 = gm_l28;
                        uint32_t t270 = gm_l31;
                        uint32_t r271; bool o272 = __builtin_add_overflow(t269, t270, &r271);
                        uint32_t t273 = r271;
                        gm_l15 = t273;
                    }
                    goto me187;
mn188:;
                    gm_trap("no match arm matched the scrutinee", gm_srcintcpduj3u2g, 72, 5);
me187:;
                }
lc180:;
                {
                    uint64_t t275 = gm_l24;
                    uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                    uint64_t t278 = r276;
                    gm_l24 = t278;
                }
            }
lb180:;
        }
    }
    {
        {
            uint64_t t280 = (uintptr_t)((uint64_t)0ULL);
            void * t281 = gm_l0;
            if (t280 >= (uintptr_t)(8)) gm_trap_index(t280, 8, gm_srcintcpduj3u2g, 88, 5);
            void * t282 = (void *)(((uint8_t *)t281) + t280 * 4);
            gm_l32 = t282;
        }
        {
            void * t283 = gm_l32;
            void * t284 = gm_l32;
            uint32_t t285 = GM_LD(uint32_t, ((uint8_t *)t284));
            uint32_t t286 = gm_l15;
            uint32_t r287; bool o288 = __builtin_add_overflow(t285, t286, &r287);
            uint32_t t289 = r287;
            GM_ST(uint32_t, ((uint8_t *)t283), t289);
        }
    }
    {
        {
            uint64_t t291 = (uintptr_t)((uint64_t)1ULL);
            void * t292 = gm_l0;
            if (t291 >= (uintptr_t)(8)) gm_trap_index(t291, 8, gm_srcintcpduj3u2g, 89, 5);
            void * t293 = (void *)(((uint8_t *)t292) + t291 * 4);
            gm_l33 = t293;
        }
        {
            void * t294 = gm_l33;
            void * t295 = gm_l33;
            uint32_t t296 = GM_LD(uint32_t, ((uint8_t *)t295));
            uint32_t t297 = gm_l16;
            uint32_t r298; bool o299 = __builtin_add_overflow(t296, t297, &r298);
            uint32_t t300 = r298;
            GM_ST(uint32_t, ((uint8_t *)t294), t300);
        }
    }
    {
        {
            uint64_t t302 = (uintptr_t)((uint64_t)2ULL);
            void * t303 = gm_l0;
            if (t302 >= (uintptr_t)(8)) gm_trap_index(t302, 8, gm_srcintcpduj3u2g, 90, 5);
            void * t304 = (void *)(((uint8_t *)t303) + t302 * 4);
            gm_l34 = t304;
        }
        {
            void * t305 = gm_l34;
            void * t306 = gm_l34;
            uint32_t t307 = GM_LD(uint32_t, ((uint8_t *)t306));
            uint32_t t308 = gm_l17;
            uint32_t r309; bool o310 = __builtin_add_overflow(t307, t308, &r309);
            uint32_t t311 = r309;
            GM_ST(uint32_t, ((uint8_t *)t305), t311);
        }
    }
    {
        {
            uint64_t t313 = (uintptr_t)((uint64_t)3ULL);
            void * t314 = gm_l0;
            if (t313 >= (uintptr_t)(8)) gm_trap_index(t313, 8, gm_srcintcpduj3u2g, 91, 5);
            void * t315 = (void *)(((uint8_t *)t314) + t313 * 4);
            gm_l35 = t315;
        }
        {
            void * t316 = gm_l35;
            void * t317 = gm_l35;
            uint32_t t318 = GM_LD(uint32_t, ((uint8_t *)t317));
            uint32_t t319 = gm_l18;
            uint32_t r320; bool o321 = __builtin_add_overflow(t318, t319, &r320);
            uint32_t t322 = r320;
            GM_ST(uint32_t, ((uint8_t *)t316), t322);
        }
    }
    {
        {
            uint64_t t324 = (uintptr_t)((uint64_t)4ULL);
            void * t325 = gm_l0;
            if (t324 >= (uintptr_t)(8)) gm_trap_index(t324, 8, gm_srcintcpduj3u2g, 92, 5);
            void * t326 = (void *)(((uint8_t *)t325) + t324 * 4);
            gm_l36 = t326;
        }
        {
            void * t327 = gm_l36;
            void * t328 = gm_l36;
            uint32_t t329 = GM_LD(uint32_t, ((uint8_t *)t328));
            uint32_t t330 = gm_l19;
            uint32_t r331; bool o332 = __builtin_add_overflow(t329, t330, &r331);
            uint32_t t333 = r331;
            GM_ST(uint32_t, ((uint8_t *)t327), t333);
        }
    }
    {
        {
            uint64_t t335 = (uintptr_t)((uint64_t)5ULL);
            void * t336 = gm_l0;
            if (t335 >= (uintptr_t)(8)) gm_trap_index(t335, 8, gm_srcintcpduj3u2g, 93, 5);
            void * t337 = (void *)(((uint8_t *)t336) + t335 * 4);
            gm_l37 = t337;
        }
        {
            void * t338 = gm_l37;
            void * t339 = gm_l37;
            uint32_t t340 = GM_LD(uint32_t, ((uint8_t *)t339));
            uint32_t t341 = gm_l20;
            uint32_t r342; bool o343 = __builtin_add_overflow(t340, t341, &r342);
            uint32_t t344 = r342;
            GM_ST(uint32_t, ((uint8_t *)t338), t344);
        }
    }
    {
        {
            uint64_t t346 = (uintptr_t)((uint64_t)6ULL);
            void * t347 = gm_l0;
            if (t346 >= (uintptr_t)(8)) gm_trap_index(t346, 8, gm_srcintcpduj3u2g, 94, 5);
            void * t348 = (void *)(((uint8_t *)t347) + t346 * 4);
            gm_l38 = t348;
        }
        {
            void * t349 = gm_l38;
            void * t350 = gm_l38;
            uint32_t t351 = GM_LD(uint32_t, ((uint8_t *)t350));
            uint32_t t352 = gm_l21;
            uint32_t r353; bool o354 = __builtin_add_overflow(t351, t352, &r353);
            uint32_t t355 = r353;
            GM_ST(uint32_t, ((uint8_t *)t349), t355);
        }
    }
    {
        {
            uint64_t t357 = (uintptr_t)((uint64_t)7ULL);
            void * t358 = gm_l0;
            if (t357 >= (uintptr_t)(8)) gm_trap_index(t357, 8, gm_srcintcpduj3u2g, 95, 5);
            void * t359 = (void *)(((uint8_t *)t358) + t357 * 4);
            gm_l39 = t359;
        }
        {
            void * t360 = gm_l39;
            void * t361 = gm_l39;
            uint32_t t362 = GM_LD(uint32_t, ((uint8_t *)t361));
            uint32_t t363 = gm_l22;
            uint32_t r364; bool o365 = __builtin_add_overflow(t362, t363, &r364);
            uint32_t t366 = r364;
            GM_ST(uint32_t, ((uint8_t *)t360), t366);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_final_rg5ko3p(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_final_rg5ko3p F;
    void * gm_l0; /* s */
    uint64_t gm_l2; /* bits */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_t7mlrlibdq1ja gm_l12; /* out */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 938u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tq14qvifgdvpm t3 = GM_LD(gm_tq14qvifgdvpm, ((uint8_t *)t2));
        F.l1 = t3;
    }
    {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 104));
        unsigned __int128 n5 = (unsigned __int128)((uint64_t)3ULL);
        if (n5 >= 64) gm_trap_shift((uint64_t)n5, "u64", gm_srcintcpduj3u2g, 128, 13);
        uint64_t r6 = (uint64_t)((uint64_t)t4 << (int)n5);
        bool o7 = (uint64_t)(r6 >> (int)n5) != t4;
        if (o7) gm_trap("integer overflow in `<<`", gm_srcintcpduj3u2g, 128, 13);
        uint64_t t8 = r6;
        gm_l2 = t8;
    }
    {
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 96));
        uint64_t t10 = (uintptr_t)t9;
        if (t10 >= (uintptr_t)(64)) gm_trap_index(t10, 64, gm_srcintcpduj3u2g, 129, 5);
        GM_ST(uint8_t, ((((uint8_t *)&F.l1) + 32) + t10 * 1), ((uint8_t)128ULL));
    }
    {
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 96));
        uint64_t r12; bool o13 = __builtin_add_overflow(t11, ((uint64_t)1ULL), &r12);
        if (o13) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 130, 5);
        uint64_t t14 = r12;
        GM_ST(uint64_t, (((uint8_t *)&F.l1) + 96), t14);
    }
    {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 96));
        uint8_t t17 = (t16 > ((uint64_t)56ULL));
        if (!t17) goto ie15;
        {
            {
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 96));
                gm_l4 = t20;
            }
            {
                gm_l3 = ((uint64_t)64ULL);
            }
            {
                for (;;) {
                    uint64_t t22 = gm_l4;
                    uint64_t t23 = gm_l3;
                    uint8_t t24 = (t22 < t23);
                    if (!t24) goto lb21;
                    {
                        uint64_t t27 = gm_l4;
                        uint64_t t30 = GM_LD(uint64_t, ((uint8_t *)&t27));
                        gm_l5 = t30;
                        {
                            uint64_t t32 = gm_l5;
                            uint64_t t33 = (uintptr_t)t32;
                            if (t33 >= (uintptr_t)(64)) gm_trap_index(t33, 64, gm_srcintcpduj3u2g, 132, 33);
                            GM_ST(uint8_t, ((((uint8_t *)&F.l1) + 32) + t33 * 1), ((uint8_t)0ULL));
                        }
                        goto me28;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srcintcpduj3u2g, 132, 9);
me28:;
                    }
lc21:;
                    {
                        uint64_t t35 = gm_l4;
                        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                        uint64_t t38 = r36;
                        gm_l4 = t38;
                    }
                }
lb21:;
            }
        }
        {
            void * t39 = (void *)(((uint8_t *)&F.l1) + 0);
            void * t40 = (((uint8_t *)&F.l1) + 32);
            if ((uintptr_t)0 > (uintptr_t)64 || (uintptr_t)64 > (uintptr_t)64) gm_trap_slice((uintptr_t)0, (uintptr_t)64, 64, gm_srcintcpduj3u2g, 133, 24);
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t40 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)((uintptr_t)64 - (uintptr_t)0));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_f_compress_t585fuj(t39, t42);
        }
        {
            GM_ST(uint64_t, (((uint8_t *)&F.l1) + 96), ((uint64_t)0ULL));
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        {
            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 96));
            gm_l7 = t44;
        }
        {
            gm_l6 = ((uint64_t)56ULL);
        }
        {
            for (;;) {
                uint64_t t46 = gm_l7;
                uint64_t t47 = gm_l6;
                uint8_t t48 = (t46 < t47);
                if (!t48) goto lb45;
                {
                    uint64_t t51 = gm_l7;
                    uint64_t t54 = GM_LD(uint64_t, ((uint8_t *)&t51));
                    gm_l8 = t54;
                    {
                        uint64_t t56 = gm_l8;
                        uint64_t t57 = (uintptr_t)t56;
                        if (t57 >= (uintptr_t)(64)) gm_trap_index(t57, 64, gm_srcintcpduj3u2g, 136, 29);
                        GM_ST(uint8_t, ((((uint8_t *)&F.l1) + 32) + t57 * 1), ((uint8_t)0ULL));
                    }
                    goto me52;
mn53:;
                    gm_trap("no match arm matched the scrutinee", gm_srcintcpduj3u2g, 136, 5);
me52:;
                }
lc45:;
                {
                    uint64_t t59 = gm_l7;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l7 = t62;
                }
            }
lb45:;
        }
    }
    {
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            gm_l9 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t65 = gm_l10;
                uint64_t t66 = gm_l9;
                uint8_t t67 = (t65 < t66);
                if (!t67) goto lb64;
                {
                    uint64_t t70 = gm_l10;
                    uint64_t t73 = GM_LD(uint64_t, ((uint8_t *)&t70));
                    gm_l11 = t73;
                    {
                        uint64_t t75 = gm_l11;
                        uint64_t r76; bool o77 = __builtin_add_overflow(((uint64_t)56ULL), t75, &r76);
                        if (o77) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 137, 29);
                        uint64_t t78 = r76;
                        uint64_t t79 = (uintptr_t)t78;
                        if (t79 >= (uintptr_t)(64)) gm_trap_index(t79, 64, gm_srcintcpduj3u2g, 137, 21);
                        uint64_t t80 = gm_l2;
                        uint64_t t81 = gm_l11;
                        uint32_t t82 = (uint32_t)t81;
                        uint32_t r83; bool o84 = __builtin_mul_overflow(((uint32_t)8ULL), t82, &r83);
                        if (o84) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 137, 54);
                        uint32_t t85 = r83;
                        uint32_t r86; bool o87 = __builtin_sub_overflow(((uint32_t)56ULL), t85, &r86);
                        if (o87) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 137, 49);
                        uint32_t t88 = r86;
                        unsigned __int128 n89 = (unsigned __int128)t88;
                        if (n89 >= 64) gm_trap_shift((uint64_t)n89, "u64", gm_srcintcpduj3u2g, 137, 40);
                        uint64_t t90 = (uint64_t)(t80 >> (int)n89);
                        uint8_t t91 = (uint8_t)t90;
                        GM_ST(uint8_t, ((((uint8_t *)&F.l1) + 32) + t79 * 1), t91);
                    }
                    goto me71;
mn72:;
                    gm_trap("no match arm matched the scrutinee", gm_srcintcpduj3u2g, 137, 5);
me71:;
                }
lc64:;
                {
                    uint64_t t93 = gm_l10;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l10 = t96;
                }
            }
lb64:;
        }
    }
    {
        void * t97 = (void *)(((uint8_t *)&F.l1) + 0);
        void * t98 = (((uint8_t *)&F.l1) + 32);
        if ((uintptr_t)0 > (uintptr_t)64 || (uintptr_t)64 > (uintptr_t)64) gm_trap_slice((uintptr_t)0, (uintptr_t)64, 64, gm_srcintcpduj3u2g, 138, 20);
        gm_b16_8 t99;
        GM_ST(void *, (uint8_t *)&t99, (void *)(((uint8_t *)t98 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)((uintptr_t)64 - (uintptr_t)0));
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
        gm_f_compress_t585fuj(t97, t100);
    }
    {
    }
    {
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            gm_l13 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t103 = gm_l14;
                uint64_t t104 = gm_l13;
                uint8_t t105 = (t103 < t104);
                if (!t105) goto lb102;
                {
                    uint64_t t108 = gm_l14;
                    uint64_t t111 = GM_LD(uint64_t, ((uint8_t *)&t108));
                    gm_l15 = t111;
                    {
                        uint64_t t113 = gm_l15;
                        uint64_t r114; bool o115 = __builtin_mul_overflow(((uint64_t)4ULL), t113, &r114);
                        if (o115) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 141, 13);
                        uint64_t t116 = r114;
                        uint64_t t117 = (uintptr_t)t116;
                        if (t117 >= (uintptr_t)(32)) gm_trap_index(t117, 32, gm_srcintcpduj3u2g, 141, 9);
                        uint64_t t118 = gm_l15;
                        uint64_t t119 = (uintptr_t)t118;
                        if (t119 >= (uintptr_t)(8)) gm_trap_index(t119, 8, gm_srcintcpduj3u2g, 141, 23);
                        uint32_t t120 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 0) + t119 * 4));
                        unsigned __int128 n121 = (unsigned __int128)((uint64_t)24ULL);
                        if (n121 >= 32) gm_trap_shift((uint64_t)n121, "u32", gm_srcintcpduj3u2g, 141, 23);
                        uint32_t t122 = (uint32_t)(t120 >> (int)n121);
                        uint8_t t123 = (uint8_t)t122;
                        GM_ST(uint8_t, (((uint8_t *)&gm_l12) + t117 * 1), t123);
                    }
                    {
                        uint64_t t124 = gm_l15;
                        uint64_t r125; bool o126 = __builtin_mul_overflow(((uint64_t)4ULL), t124, &r125);
                        if (o126) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 142, 13);
                        uint64_t t127 = r125;
                        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                        if (o129) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 142, 13);
                        uint64_t t130 = r128;
                        uint64_t t131 = (uintptr_t)t130;
                        if (t131 >= (uintptr_t)(32)) gm_trap_index(t131, 32, gm_srcintcpduj3u2g, 142, 9);
                        uint64_t t132 = gm_l15;
                        uint64_t t133 = (uintptr_t)t132;
                        if (t133 >= (uintptr_t)(8)) gm_trap_index(t133, 8, gm_srcintcpduj3u2g, 142, 27);
                        uint32_t t134 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 0) + t133 * 4));
                        unsigned __int128 n135 = (unsigned __int128)((uint64_t)16ULL);
                        if (n135 >= 32) gm_trap_shift((uint64_t)n135, "u32", gm_srcintcpduj3u2g, 142, 27);
                        uint32_t t136 = (uint32_t)(t134 >> (int)n135);
                        uint8_t t137 = (uint8_t)t136;
                        GM_ST(uint8_t, (((uint8_t *)&gm_l12) + t131 * 1), t137);
                    }
                    {
                        uint64_t t138 = gm_l15;
                        uint64_t r139; bool o140 = __builtin_mul_overflow(((uint64_t)4ULL), t138, &r139);
                        if (o140) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 143, 13);
                        uint64_t t141 = r139;
                        uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)2ULL), &r142);
                        if (o143) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 143, 13);
                        uint64_t t144 = r142;
                        uint64_t t145 = (uintptr_t)t144;
                        if (t145 >= (uintptr_t)(32)) gm_trap_index(t145, 32, gm_srcintcpduj3u2g, 143, 9);
                        uint64_t t146 = gm_l15;
                        uint64_t t147 = (uintptr_t)t146;
                        if (t147 >= (uintptr_t)(8)) gm_trap_index(t147, 8, gm_srcintcpduj3u2g, 143, 27);
                        uint32_t t148 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 0) + t147 * 4));
                        unsigned __int128 n149 = (unsigned __int128)((uint64_t)8ULL);
                        if (n149 >= 32) gm_trap_shift((uint64_t)n149, "u32", gm_srcintcpduj3u2g, 143, 27);
                        uint32_t t150 = (uint32_t)(t148 >> (int)n149);
                        uint8_t t151 = (uint8_t)t150;
                        GM_ST(uint8_t, (((uint8_t *)&gm_l12) + t145 * 1), t151);
                    }
                    {
                        uint64_t t152 = gm_l15;
                        uint64_t r153; bool o154 = __builtin_mul_overflow(((uint64_t)4ULL), t152, &r153);
                        if (o154) gm_trap("integer overflow in `*`", gm_srcintcpduj3u2g, 144, 13);
                        uint64_t t155 = r153;
                        uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)3ULL), &r156);
                        if (o157) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 144, 13);
                        uint64_t t158 = r156;
                        uint64_t t159 = (uintptr_t)t158;
                        if (t159 >= (uintptr_t)(32)) gm_trap_index(t159, 32, gm_srcintcpduj3u2g, 144, 9);
                        uint64_t t160 = gm_l15;
                        uint64_t t161 = (uintptr_t)t160;
                        if (t161 >= (uintptr_t)(8)) gm_trap_index(t161, 8, gm_srcintcpduj3u2g, 144, 26);
                        uint32_t t162 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 0) + t161 * 4));
                        uint8_t t163 = (uint8_t)t162;
                        GM_ST(uint8_t, (((uint8_t *)&gm_l12) + t159 * 1), t163);
                    }
                    goto me109;
mn110:;
                    gm_trap("no match arm matched the scrutinee", gm_srcintcpduj3u2g, 140, 5);
me109:;
                }
lc102:;
                {
                    uint64_t t165 = gm_l14;
                    uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                    uint64_t t168 = r166;
                    gm_l14 = t168;
                }
            }
lb102:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l12), 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t7mlrlibdq1ja gm_f_final_rg5ko3p_c(void * a0) { gm_t7mlrlibdq1ja gm_r; gm_f_final_rg5ko3p(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out);
__attribute__((aligned(4))) gm_cskfp28qopl236 gm_f_init_s32ien0_c(void) { gm_tq14qvifgdvpm gm_r; gm_f_init_s32ien0(&gm_r); return GM_CONV(gm_cskfp28qopl236, gm_r); }
extern __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t7mlrlibdq1ja gm_f_sum_ugu720i_c(gm_b16_8 a0) { gm_t7mlrlibdq1ja gm_r; gm_f_sum_ugu720i(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_update_pn3fq6a(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_update_pn3fq6a F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* data */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* take */
    uint64_t gm_l4; /* rest */
    GM_STACK_CHECK_SHALLOW(&F, 741u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t6 = (uint64_t)t5;
        uint64_t r7; bool o8 = __builtin_add_overflow(t4, t6, &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 100, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t2) + 104), t9);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 96));
        uint8_t t13 = (t12 > ((uint64_t)0ULL));
        if (!t13) goto ie10;
        {
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 96));
            uint64_t r17; bool o18 = __builtin_sub_overflow(((uint64_t)64ULL), t16, &r17);
            if (o18) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 103, 17);
            uint64_t t19 = r17;
            gm_l3 = t19;
        }
        {
            uint64_t t21 = gm_l3;
            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t23 = (t21 > t22);
            if (!t23) goto ie20;
            {
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                gm_l3 = t25;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            void * t26 = gm_l0;
            void * t27 = (((uint8_t *)t26) + 32);
            void * t28 = gm_l0;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 96));
            uint64_t t30 = (uintptr_t)t29;
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 96));
            uint64_t t33 = gm_l3;
            uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
            if (o35) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 105, 27);
            uint64_t t36 = r34;
            uint64_t t37 = (uintptr_t)t36;
            if (t30 > t37 || t37 > (uintptr_t)64) gm_trap_slice(t30, t37, 64, gm_srcintcpduj3u2g, 105, 9);
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t27 + t30 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t30));
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t40 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t42 = gm_l3;
            uint64_t t43 = (uintptr_t)t42;
            if (t41 > t43 || t43 > (uintptr_t)t39) gm_trap_slice(t41, t43, t39, gm_srcintcpduj3u2g, 105, 54);
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t40 + t41 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43 - t41));
            gm_f_copy_from_f7j6p05(t38, t44);
        }
        {
            void * t45 = gm_l0;
            void * t46 = gm_l0;
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 96));
            uint64_t t48 = gm_l3;
            uint64_t r49; bool o50 = __builtin_add_overflow(t47, t48, &r49);
            if (o50) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 106, 9);
            uint64_t t51 = r49;
            GM_ST(uint64_t, (((uint8_t *)t45) + 96), t51);
        }
        {
            uint64_t t52 = gm_l3;
            gm_l2 = t52;
        }
        {
            void * t54 = gm_l0;
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 96));
            uint8_t t56 = ((t55 == ((uint64_t)64ULL)));
            if (!t56) goto ie53;
            {
                void * t58 = gm_l0;
                void * t59 = (void *)(((uint8_t *)t58) + 0);
                void * t60 = gm_l0;
                void * t61 = (((uint8_t *)t60) + 32);
                if ((uintptr_t)0 > (uintptr_t)64 || (uintptr_t)64 > (uintptr_t)64) gm_trap_slice((uintptr_t)0, (uintptr_t)64, 64, gm_srcintcpduj3u2g, 109, 28);
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t61 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)((uintptr_t)64 - (uintptr_t)0));
                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                gm_f_compress_t585fuj(t59, t63);
            }
            {
                void * t64 = gm_l0;
                GM_ST(uint64_t, (((uint8_t *)t64) + 96), ((uint64_t)0ULL));
            }
            goto ix53;
ie53:;
ix53:;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        for (;;) {
            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t67 = gm_l2;
            uint64_t r68; bool o69 = __builtin_sub_overflow(t66, t67, &r68);
            if (o69) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 113, 11);
            uint64_t t70 = r68;
            uint8_t t71 = (t70 >= ((uint64_t)64ULL));
            if (!t71) goto lb65;
            {
                void * t74 = gm_l0;
                void * t75 = (void *)(((uint8_t *)t74) + 0);
                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t77 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t78 = gm_l2;
                uint64_t t79 = (uintptr_t)t78;
                uint64_t t80 = gm_l2;
                uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)64ULL), &r81);
                if (o82) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 114, 32);
                uint64_t t83 = r81;
                uint64_t t84 = (uintptr_t)t83;
                if (t79 > t84 || t84 > (uintptr_t)t76) gm_trap_slice(t79, t84, t76, gm_srcintcpduj3u2g, 114, 24);
                gm_b16_8 t85;
                GM_ST(void *, (uint8_t *)&t85, (void *)(((uint8_t *)t77 + t79 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(t84 - t79));
                gm_f_compress_t585fuj(t75, t85);
            }
            {
                uint64_t t86 = gm_l2;
                uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)64ULL), &r87);
                if (o88) gm_trap("integer overflow in `+`", gm_srcintcpduj3u2g, 115, 9);
                uint64_t t89 = r87;
                gm_l2 = t89;
            }
lc65:;
        }
lb65:;
    }
    {
        uint64_t t91 = gm_l2;
        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t93 = (t91 < t92);
        if (!t93) goto ie90;
        {
            uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t96 = gm_l2;
            uint64_t r97; bool o98 = __builtin_sub_overflow(t95, t96, &r97);
            if (o98) gm_trap("integer overflow in `-`", gm_srcintcpduj3u2g, 118, 17);
            uint64_t t99 = r97;
            gm_l4 = t99;
        }
        {
            void * t100 = gm_l0;
            void * t101 = (((uint8_t *)t100) + 32);
            uint64_t t102 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t103 = gm_l4;
            uint64_t t104 = (uintptr_t)t103;
            if (t102 > t104 || t104 > (uintptr_t)64) gm_trap_slice(t102, t104, 64, gm_srcintcpduj3u2g, 119, 9);
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(((uint8_t *)t101 + t102 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(t104 - t102));
            uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t107 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t108 = gm_l2;
            uint64_t t109 = (uintptr_t)t108;
            if (t109 > (uintptr_t)t106 || (uintptr_t)t106 > (uintptr_t)t106) gm_trap_slice(t109, (uintptr_t)t106, t106, gm_srcintcpduj3u2g, 119, 36);
            gm_b16_8 t110;
            GM_ST(void *, (uint8_t *)&t110, (void *)(((uint8_t *)t107 + t109 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)((uintptr_t)t106 - t109));
            gm_f_copy_from_f7j6p05(t105, t110);
        }
        {
            void * t111 = gm_l0;
            uint64_t t112 = gm_l4;
            GM_ST(uint64_t, (((uint8_t *)t111) + 96), t112);
        }
        goto ix90;
ie90:;
ix90:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}

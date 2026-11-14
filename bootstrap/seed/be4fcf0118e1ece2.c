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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
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
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_range_loop_44fv3au(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_t88rcucg1b7uk a4, uint8_t a5);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_range_test_tqcppmu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3, void * a4, gm_t88rcucg1b7uk a5, uint8_t a6, uint8_t a7, gm_tl874t2atnlh3 a8);
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 120)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
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
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
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
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t66j1im9ip128 t29 = gm_l2;
        gm_td8hfgklo8dil t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 36, 0);
        GM_ST(gm_t66j1im9ip128, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[32] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_trqfvmjtt4sij t29 = F.l2;
        gm_tqetrgo0q24b5 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[40] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t8enl5g7b22jb t29 = F.l2;
        gm_te5gbhu3us3k4 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 52, 0);
        GM_ST(gm_t8enl5g7b22jb, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[48] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
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
    GM_STACK_CHECK_SHALLOW(&F, 272u);
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
        gm_td8hfgklo8dil t14 = t13;
        if (!((((uint8_t *)&t14))[32] != 0)) goto ie10;
        gm_t66j1im9ip128 t15 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)&t14));
        gm_l1 = t15;
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_f_restore_scope_edqq1v8(t18, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
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
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        uint8_t t10 = 0;
        F.ret = t9;
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
    GM_STACK_CHECK(&F, 196u);
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
    gm_b16_8 t5 = t4;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
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
    GM_STACK_CHECK(&F, 551u);
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
        gm_tftb1odc739t5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srclfnv05vuf0b8, 266, 5);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l10);
        uint32_t t26 = gm_l3;
        gm_t88rcucg1b7uk t27 = gm_l4;
        gm_tl874t2atnlh3 t28 = gm_l7;
        gm_trcmmd12cefb1 t29;
        gm_f_increment_12n73t5((void *)&t29, (void *)NULL, cx, NULL, t26, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t30 = gm_f_push_d6c76ud_2(t25, &t29);
        gm_tftb1odc739t5 t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srclfnv05vuf0b8, 268, 5);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    {
        void * t33 = gm_l0;
        void * t34 = gm_l1;
        gm_t88rcucg1b7uk t35 = gm_l2;
        void * t36 = gm_l6;
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_tl874t2atnlh3 t41 = gm_l7;
        void * t42 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t39, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t43 = gm_f_loop_with_pattern_d8vl0mj((void *)NULL, cx, NULL, t33, t34, t35, t36, t37, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44 = 0;
        F.ret = t43;
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
    GM_STACK_CHECK(&F, 4978u);
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
        gm_t8cc1mtbrhlpt t14 = t13;
        if (!((((uint8_t *)&t14))[32] != 0)) goto ie10;
        gm_tjpqvfd72o62u t15 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)&t14));
        gm_l6 = t15;
        {
            void * t17 = gm_l0;
            void * t18 = gm_l1;
            void * t19 = gm_l5;
            void * t20 = gm_f_soa_for_ovoqsoj((void *)NULL, cx, NULL, t17, t18, t19, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 96);
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = gm_l5;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
        gm_t88rcucg1b7uk t28 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t23, t25, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t28;
    }
    {
        void * t29 = gm_l5;
        gm_l8 = t29;
    }
    {
        gm_t88rcucg1b7uk t30 = gm_l7;
        gm_l9 = t30;
    }
    {
        void * t31 = gm_l3;
        gm_t88rcucg1b7uk t32 = gm_l7;
        gm_t53at80pfdus3 t33;
        gm_f_kind_jcnt4hm((void *)&t33, t31, GM_CONV(gm_csguankko4lmhd, t32));
        gm_t53at80pfdus3 t34 = t33;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        switch (t44) { case ((uint8_t)0ULL): goto ma42; case ((uint8_t)1ULL): goto ma42; case ((uint8_t)2ULL): goto ma42; case ((uint8_t)3ULL): goto ma42; case ((uint8_t)4ULL): goto ma42; case ((uint8_t)5ULL): goto ma42; case ((uint8_t)6ULL): goto ma42; case ((uint8_t)7ULL): goto ma42; case ((uint8_t)8ULL): goto ma38; case ((uint8_t)9ULL): goto ma42; case ((uint8_t)10ULL): goto ma36; case ((uint8_t)11ULL): goto ma37; case ((uint8_t)12ULL): goto ma40; case ((uint8_t)13ULL): goto ma42; case ((uint8_t)14ULL): goto ma42; case ((uint8_t)15ULL): goto ma42; case ((uint8_t)16ULL): goto ma42; case ((uint8_t)17ULL): goto ma41; case ((uint8_t)18ULL): goto ma42; case ((uint8_t)19ULL): goto ma42; case ((uint8_t)20ULL): goto ma37; case ((uint8_t)21ULL): goto ma42; case ((uint8_t)22ULL): goto ma42; case ((uint8_t)23ULL): goto ma42; case ((uint8_t)24ULL): goto ma42; case ((uint8_t)25ULL): goto ma42; case ((uint8_t)26ULL): goto ma39; default: goto ma42; }
ma36:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t46 != ((uint8_t)10ULL)) goto mn45;
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 4));
        gm_l10 = t47;
        {
            gm_t88rcucg1b7uk t49 = gm_l10;
            gm_l9 = t49;
        }
        goto me35;
mn45:;
ma37:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t53 != ((uint8_t)11ULL)) goto pa52;
        gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 8));
        gm_l11 = t54;
        goto po51;
pa52:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t56 != ((uint8_t)20ULL)) goto pa55;
        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 4));
        gm_l11 = t57;
        goto po51;
pa55:;
        goto mn50;
po51:;
        {
            gm_t88rcucg1b7uk t59 = gm_l11;
            gm_l9 = t59;
        }
        {
            void * t60 = gm_l0;
            void * t61 = gm_l5;
            gm_tqs5p4n2tq19v t62 = gm_f_place_mutability_0tlm4g8(t60, t61);
            gm_l13 = t62;
        }
        {
            void * t64 = gm_l0;
            void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
            void * t66 = gm_l5;
            uint8_t t67 = gm_f_packed_place_5ck3439(t65, t66);
            if (!t67) goto ie63;
            {
                gm_tqs5p4n2tq19v t69;
                memset(&t69, 0, sizeof t69);
                gm_l13 = t69;
            }
            goto ix63;
ie63:;
ix63:;
        }
        {
            if (!((((uint8_t *)&gm_l13))[1] != 0)) goto ie70;
            uint8_t t71 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
            gm_l14 = t71;
            {
                void * t73 = gm_l3;
                uint8_t t74 = gm_l14;
                uint8_t t75 = (uint8_t)!t74;
                gm_t88rcucg1b7uk t76 = gm_l11;
                gm_t88rcucg1b7uk t77 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t73, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tao63pk3r18v5 t78;
                memset((uint8_t *)&t78 + 25, 0, 7);
                void * t79 = gm_l5;
                GM_ST(void *, (uint8_t *)&t78 + 0, t79);
                GM_ST(void *, (uint8_t *)&t78 + 8, ((void *)0));
                GM_ST(void *, (uint8_t *)&t78 + 16, ((void *)0));
                GM_ST(uint8_t, (uint8_t *)&t78 + 24, ((uint8_t)0));
                gm_teti411eujvf4 t80;
                GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t80 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t80 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t80 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t80 + 8), t78);
                gm_tl874t2atnlh3 t81 = F.l4;
                void * t82 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t77, &t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t82;
            }
            goto ix70;
ie70:;
            {
                void * t84 = gm_l0;
                gm_t88rcucg1b7uk t85 = gm_l7;
                gm_tl874t2atnlh3 t86 = F.l4;
                uint32_t t87 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t84, t85, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t87;
            }
            {
                void * t88 = gm_l3;
                gm_t88rcucg1b7uk t89 = gm_l11;
                gm_t88rcucg1b7uk t90 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t88, ((uint8_t)1), t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t90;
            }
            {
                gm_t88rcucg1b7uk t91 = gm_l16;
                gm_tao63pk3r18v5 t92;
                memset((uint8_t *)&t92 + 25, 0, 7);
                gm_t88rcucg1b7uk t93 = gm_l7;
                uint32_t t94 = gm_l15;
                gm_teti411eujvf4 t95;
                memset(&t95, 0, sizeof t95);
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t95 + 8), t94);
                gm_tl874t2atnlh3 t96 = F.l4;
                void * t97 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t93, &t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(void *, (uint8_t *)&t92 + 0, t97);
                GM_ST(void *, (uint8_t *)&t92 + 8, ((void *)0));
                GM_ST(void *, (uint8_t *)&t92 + 16, ((void *)0));
                GM_ST(uint8_t, (uint8_t *)&t92 + 24, ((uint8_t)0));
                gm_teti411eujvf4 t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t98 + 8), t92);
                gm_tl874t2atnlh3 t99 = F.l4;
                void * t100 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t91, &t98, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t100;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l18, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t101 = (void *)((uint8_t *)&F.l18);
                uint32_t t102 = gm_l15;
                void * t103 = gm_l5;
                gm_ttggq7ng4kp3p t104;
                GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t104 + 0), t102);
                GM_ST(void *, ((uint8_t *)&t104 + 8), t103);
                gm_trcmmd12cefb1 t105;
                GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t105 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t105 + 8), t104);
                gm_tftb1odc739t5 t106 = gm_f_push_d6c76ud_2(t101, &t105);
                gm_tftb1odc739t5 t107 = t106;
                if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srclfnv05vuf0b8, 337, 17);
                gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
            }
            {
                gm_t88rcucg1b7uk t109 = gm_l16;
                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                void * t111 = gm_l17;
                gm_tl874t2atnlh3 t112 = F.l4;
                void * t113 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t109, t110, t111, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t113;
            }
ix70:;
        }
        goto me35;
mn50:;
ma38:;
        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t115 != ((uint8_t)8ULL)) goto mn114;
        uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)&t34) + 8) + 0));
        gm_l19 = t116;
        gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 4));
        gm_l20 = t117;
        {
            void * t119 = gm_l3;
            gm_t88rcucg1b7uk t120 = gm_l20;
            gm_t53at80pfdus3 t121;
            gm_f_kind_jcnt4hm((void *)&t121, t119, GM_CONV(gm_csguankko4lmhd, t120));
            gm_t53at80pfdus3 t122 = t121;
            uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&t122) + 0));
            if (t127 != ((uint8_t)11ULL)) goto pa126;
            gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t122) + 8) + 8));
            gm_l21 = t128;
            goto po125;
pa126:;
            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t122) + 0));
            if (t130 != ((uint8_t)20ULL)) goto pa129;
            gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t122) + 8) + 4));
            gm_l21 = t131;
            goto po125;
pa129:;
            goto mn124;
po125:;
            {
                gm_t88rcucg1b7uk t133 = gm_l21;
                gm_l9 = t133;
            }
            {
                gm_t88rcucg1b7uk t134 = gm_l20;
                void * t135 = gm_l5;
                gm_teti411eujvf4 t136;
                memset(&t136, 0, sizeof t136);
                GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)18ULL));
                GM_ST(void *, ((uint8_t *)&t136 + 8), t135);
                gm_tl874t2atnlh3 t137 = F.l4;
                void * t138 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t134, &t136, t137);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t138;
            }
            {
                void * t139 = gm_l3;
                uint8_t t140 = gm_l19;
                gm_t88rcucg1b7uk t141 = gm_l21;
                gm_t88rcucg1b7uk t142 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t139, t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tao63pk3r18v5 t143;
                memset((uint8_t *)&t143 + 25, 0, 7);
                void * t144 = gm_l23;
                GM_ST(void *, (uint8_t *)&t143 + 0, t144);
                GM_ST(void *, (uint8_t *)&t143 + 8, ((void *)0));
                GM_ST(void *, (uint8_t *)&t143 + 16, ((void *)0));
                GM_ST(uint8_t, (uint8_t *)&t143 + 24, ((uint8_t)0));
                gm_teti411eujvf4 t145;
                GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t145 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t145 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t145 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)21ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t145 + 8), t143);
                gm_tl874t2atnlh3 t146 = F.l4;
                void * t147 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t142, &t145, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t147;
            }
            goto me123;
mn124:;
            uint8_t t149 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
            goto me123;
mn148:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 342, 13);
me123:;
        }
        goto me35;
mn114:;
ma39:;
        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t151 != ((uint8_t)26ULL)) goto mn150;
        void * t152 = gm_l0;
        void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
        gm_tl874t2atnlh3 t154 = F.l4;
        void * t155 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t153, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t156 = 0;
        F.ret = t155;
        goto gm_fret;
        goto me35;
mn150:;
ma40:;
        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t158 != ((uint8_t)12ULL)) goto mn157;
        gm_b16_8 t159 = GM_LD(gm_b16_8, ((((uint8_t *)&t34) + 8) + 0));
        gm_l24 = t159;
        {
            uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
            uint8_t t163 = ((t162 == ((uint64_t)0ULL)));
            if (!t163) goto ie161;
            {
                uint8_t t165 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            goto ix161;
ie161:;
ix161:;
        }
        {
            uint64_t t166 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t167 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
            if (t166 >= (uintptr_t)(t167)) gm_trap_index(t166, t167, gm_srclfnv05vuf0b8, 356, 19);
            gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t166 * 4));
            gm_l25 = t168;
        }
        {
            void * t169 = gm_l0;
            gm_b16_8 t170 = gm_l24;
            gm_tkn42u7na3t81 t171 = gm_f_tuple_elem_join_om8llaj((void *)NULL, cx, NULL, t169, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t171;
        }
        {
            if (!((((uint8_t *)&gm_l26))[4] != 0)) goto ie172;
            gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26));
            gm_l27 = t173;
            {
                gm_t88rcucg1b7uk t175 = gm_l27;
                gm_l25 = t175;
            }
            goto ix172;
ie172:;
ix172:;
        }
        {
            {
                gm_l29 = ((uint64_t)1ULL);
            }
            {
                uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                gm_l28 = t177;
            }
            {
                for (;;) {
                    uint64_t t179 = gm_l29;
                    uint64_t t180 = gm_l28;
                    uint8_t t181 = (t179 < t180);
                    if (!t181) goto lb178;
                    {
                        uint64_t t184 = gm_l29;
                        uint64_t t185 = t184;
                        uint64_t t188 = GM_LD(uint64_t, ((uint8_t *)&t185));
                        gm_l30 = t188;
                        {
                            uint8_t t191;
                            if (((((uint8_t *)&gm_l26))[4] != 0)) goto mn193;
                            t191 = ((uint8_t)1);
                            goto me192;
mn193:;
                            t191 = ((uint8_t)0);
                            goto me192;
mn194:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 361, 20);
me192:;
                            if (!t191) goto ie190;
                            void * t195 = gm_l0;
                            void * t196 = (void *)(((uint8_t *)t195) + 96);
                            void * t197 = gm_l3;
                            gm_t88rcucg1b7uk t198 = gm_l25;
                            uint64_t t199 = gm_l30;
                            uint64_t t200 = (uintptr_t)t199;
                            uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                            if (t200 >= (uintptr_t)(t201)) gm_trap_index(t200, t201, gm_srclfnv05vuf0b8, 361, 63);
                            gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t200 * 4));
                            gm_t9qkh0rsooc8d t203 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t196, t197, t198, t202);
                            if (gm_unw.kind) {
                                goto gm_ll182;
                            }
                            gm_t9qkh0rsooc8d t204 = t203;
                            uint8_t t205;
                            if (!((((uint8_t *)&t204))[0] != 0)) goto mn207;
                            t205 = ((uint8_t)1);
                            goto me206;
mn207:;
                            t205 = ((uint8_t)0);
                            goto me206;
mn208:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 361, 39);
me206:;
                            if (!t205) goto ie190;
                            {
                                void * t210 = gm_l0;
                                void * t211 = (void *)(((uint8_t *)t210) + 96);
                                void * t212 = gm_l0;
                                void * t213 = GM_LD(void *, (((uint8_t *)t212) + 0));
                                gm_t88rcucg1b7uk t214 = gm_l25;
                                gm_t88rcucg1b7uk t215 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t211, t213, t214);
                                if (gm_unw.kind) {
                                    goto gm_ll182;
                                }
                                gm_l31 = t215;
                            }
                            {
                                void * t216 = gm_l0;
                                void * t217 = (void *)(((uint8_t *)t216) + 96);
                                void * t218 = gm_l0;
                                void * t219 = GM_LD(void *, (((uint8_t *)t218) + 0));
                                uint64_t t220 = gm_l30;
                                uint64_t t221 = (uintptr_t)t220;
                                uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                if (t221 >= (uintptr_t)(t222)) gm_trap_index(t221, t222, gm_srclfnv05vuf0b8, 363, 49);
                                gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t221 * 4));
                                gm_t88rcucg1b7uk t224 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t217, t219, t223);
                                if (gm_unw.kind) {
                                    goto gm_ll182;
                                }
                                gm_l32 = t224;
                            }
                            {
                                void * t225 = gm_l0;
                                void * t226 = gm_l1;
                                void * t227 = GM_LD(void *, (((uint8_t *)t226) + 56));
                                gm_tl874t2atnlh3 t228 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t227) + 72));
                                gm_t6j6smj7qoctu t229;
                                void * t230 = gm_l0;
                                gm_t88rcucg1b7uk t231 = gm_l31;
                                gm_b16_8 t232 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t230, t231);
                                if (gm_unw.kind) {
                                    goto gm_ll182;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t229 + 0, t232);
                                void * t233 = gm_l0;
                                gm_t88rcucg1b7uk t234 = gm_l32;
                                gm_b16_8 t235 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t233, t234);
                                if (gm_unw.kind) {
                                    goto gm_ll182;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t229 + 16, t235);
                                gm_tabb710d9mq2e t236;
                                gm_f_format_t00o2ec((void *)&t236, (void *)NULL, cx, NULL, &t229);
                                if (gm_unw.kind) {
                                    goto gm_ll182;
                                }
                                gm_tabb710d9mq2e t237 = t236;
                                if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 8, gm_srclfnv05vuf0b8, 364, 43);
                                gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&t237) + 8));
                                gm_b16_8 t239 = t238;
                                gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
                                gm_b0_1 t241 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t225, t228, t240);
                                if (gm_unw.kind) {
                                    goto gm_ll182;
                                }
                            }
                            {
                                void * t242 = gm_l0;
                                void * t243 = GM_LD(void *, (((uint8_t *)t242) + 0));
                                gm_tl874t2atnlh3 t244 = F.l4;
                                void * t245 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t243, t244);
                                if (gm_unw.kind) {
                                    goto gm_ll182;
                                }
                                uint8_t t246 = 0;
                                F.ret = t245;
                                goto gm_fret;
                            }
                            goto ix190;
ie190:;
ix190:;
                        }
                        goto me186;
mn187:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 360, 13);
me186:;
                    }
lc178:;
                    {
                        uint64_t t248 = gm_l29;
                        uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                        uint64_t t251 = r249;
                        gm_l29 = t251;
                    }
                }
gm_ll182:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb178; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc178; }
                }
                goto gm_lad;
lb178:;
            }
        }
        {
            void * t252 = gm_l0;
            gm_t88rcucg1b7uk t253 = gm_l7;
            gm_tl874t2atnlh3 t254 = F.l4;
            uint32_t t255 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t252, t253, t254);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t255;
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
                uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                gm_l35 = t257;
            }
            {
                for (;;) {
                    uint64_t t259 = gm_l36;
                    uint64_t t260 = gm_l35;
                    uint8_t t261 = (t259 < t260);
                    if (!t261) goto lb258;
                    {
                        uint64_t t264 = gm_l36;
                        uint64_t t265 = t264;
                        uint64_t t268 = GM_LD(uint64_t, ((uint8_t *)&t265));
                        gm_l37 = t268;
                        {
                            uint64_t t270 = gm_l37;
                            uint64_t t271 = (uintptr_t)t270;
                            uint64_t t272 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                            if (t271 >= (uintptr_t)(t272)) gm_trap_index(t271, t272, gm_srclfnv05vuf0b8, 371, 33);
                            gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t271 * 4));
                            gm_t88rcucg1b7uk t274 = gm_l7;
                            uint32_t t275 = gm_l33;
                            gm_teti411eujvf4 t276;
                            memset(&t276, 0, sizeof t276);
                            GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t276 + 8), t275);
                            gm_tl874t2atnlh3 t277 = F.l4;
                            void * t278 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t274, &t276, t277);
                            if (gm_unw.kind) {
                                goto gm_ll262;
                            }
                            uint64_t t279 = gm_l37;
                            uint32_t t280 = (uint32_t)t279;
                            gm_tbl8il92l97ut t281;
                            GM_ST(uint32_t, (uint8_t *)&t281 + 12, 0);
                            GM_ST(void *, ((uint8_t *)&t281 + 0), t278);
                            GM_ST(uint32_t, ((uint8_t *)&t281 + 8), t280);
                            gm_teti411eujvf4 t282;
                            memset(&t282, 0, sizeof t282);
                            GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)19ULL));
                            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t282 + 8), t281);
                            gm_tl874t2atnlh3 t283 = F.l4;
                            void * t284 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t273, &t282, t283);
                            if (gm_unw.kind) {
                                goto gm_ll262;
                            }
                            gm_l38 = t284;
                        }
                        {
                            void * t285 = (void *)((uint8_t *)&F.l34);
                            void * t286 = gm_l0;
                            void * t287 = gm_l38;
                            gm_t88rcucg1b7uk t288 = gm_l25;
                            void * t289 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t286, t287, t288);
                            if (gm_unw.kind) {
                                goto gm_ll262;
                            }
                            gm_tftb1odc739t5 t290 = gm_f_push_d943vgp_2(t285, &(*(gm_t69m56meqefs8 *)((uint8_t *)t289)));
                            gm_tftb1odc739t5 t291 = t290;
                            if (((((uint8_t *)&t291))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t291) + 1, gm_srclfnv05vuf0b8, 372, 17);
                            gm_b0_1 t292 = GM_LD(gm_b0_1, (((uint8_t *)&t291) + 1));
                        }
                        goto me266;
mn267:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 370, 13);
me266:;
                    }
lc258:;
                    {
                        uint64_t t294 = gm_l36;
                        uint64_t r295; bool o296 = __builtin_add_overflow(t294, ((uint64_t)1ULL), &r295);
                        uint64_t t297 = r295;
                        gm_l36 = t297;
                    }
                }
gm_ll262:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb258; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc258; }
                }
                goto gm_lad;
lb258:;
            }
        }
        {
            void * t298 = gm_l3;
            uint64_t t299 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
            uint64_t t300 = (uint64_t)t299;
            gm_t88rcucg1b7uk t301 = gm_l25;
            gm_t88rcucg1b7uk t302 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t298, t300, t301);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
            gm_teti411eujvf4 t304;
            memset(&t304, 0, sizeof t304);
            GM_ST(uint8_t, ((uint8_t *)&t304 + 0), ((uint8_t)36ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t304 + 8), t303);
            gm_tl874t2atnlh3 t305 = F.l4;
            void * t306 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t302, &t304, t305);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l39 = t306;
        }
        {
            void * t307 = gm_l0;
            void * t308 = gm_l1;
            void * t309 = gm_l39;
            void * t310 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t307, t308, t309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t311 = t310;
            void * t312;
            if ((GM_LD(void *, ((uint8_t *)&t311)) != 0)) {
                void * t313 = GM_LD(void *, (((uint8_t *)&t311) + 0));
                t312 = t313;
            } else {
                uint8_t t314 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l40 = t312;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l41, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t315 = (void *)((uint8_t *)&F.l41);
            uint32_t t316 = gm_l33;
            void * t317 = gm_l5;
            gm_ttggq7ng4kp3p t318;
            GM_ST(uint32_t, (uint8_t *)&t318 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t318 + 0), t316);
            GM_ST(void *, ((uint8_t *)&t318 + 8), t317);
            gm_trcmmd12cefb1 t319;
            GM_ST(uint8_t, (uint8_t *)&t319 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t319 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t319 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t319 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t319 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t319 + 8), t318);
            gm_tftb1odc739t5 t320 = gm_f_push_d6c76ud_2(t315, &t319);
            gm_tftb1odc739t5 t321 = t320;
            if (((((uint8_t *)&t321))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t321) + 1, gm_srclfnv05vuf0b8, 377, 13);
            gm_b0_1 t322 = GM_LD(gm_b0_1, (((uint8_t *)&t321) + 1));
        }
        {
            void * t323 = (void *)((uint8_t *)&F.l41);
            void * t324 = gm_l40;
            gm_trcmmd12cefb1 t325;
            GM_ST(uint8_t, (uint8_t *)&t325 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t325 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t325 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t325 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t325 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t325 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t325 + 8), t324);
            gm_tftb1odc739t5 t326 = gm_f_push_d6c76ud_2(t323, &t325);
            gm_tftb1odc739t5 t327 = t326;
            if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 1, gm_srclfnv05vuf0b8, 378, 13);
            gm_b0_1 t328 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 1));
        }
        {
            void * t329 = gm_l0;
            void * t330 = GM_LD(void *, (((uint8_t *)t329) + 0));
            gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
            gm_tl874t2atnlh3 t332 = F.l4;
            void * t333 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t330, t331, t332);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t334 = 0;
            F.ret = t333;
            goto gm_fret;
        }
        goto me35;
mn157:;
ma41:;
        uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t336 != ((uint8_t)17ULL)) goto mn335;
        {
            void * t338 = gm_l0;
            void * t339 = GM_LD(void *, (((uint8_t *)t338) + 0));
            gm_t88rcucg1b7uk t340 = gm_l7;
            gm_thetuhtouve7f t341 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t339, t340);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thetuhtouve7f t342 = t341;
            gm_tbngattl1fo51 t343;
            if (((((uint8_t *)&t342))[8] != 0)) {
                gm_tbngattl1fo51 t344 = GM_LD(gm_tbngattl1fo51, (((uint8_t *)&t342) + 0));
                t343 = t344;
            } else {
                uint8_t t345 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
            }
            gm_l42 = t343;
        }
        {
            void * t347 = gm_l3;
            gm_t88rcucg1b7uk t348 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            uint8_t t349 = gm_f_is_integer_8d4f9ek(t347, GM_CONV(gm_csguankko4lmhd, t348));
            uint8_t t350 = (uint8_t)!t349;
            if (!t350) goto ie346;
            {
                void * t352 = gm_l0;
                void * t353 = gm_l1;
                void * t354 = GM_LD(void *, (((uint8_t *)t353) + 56));
                gm_tl874t2atnlh3 t355 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t354) + 72));
                void * t356 = gm_l0;
                gm_t88rcucg1b7uk t357 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
                gm_b16_8 t358 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t356, t357);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t359;
                GM_ST(gm_b16_8, ((uint8_t *)&t359 + 0), t358);
                gm_tabb710d9mq2e t360;
                gm_f_format_h1n6lc9((void *)&t360, (void *)NULL, cx, NULL, t359);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t361 = t360;
                if (((((uint8_t *)&t361))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t361) + 8, gm_srclfnv05vuf0b8, 385, 39);
                gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&t361) + 8));
                gm_b16_8 t363 = t362;
                gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&t363));
                gm_b0_1 t365 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t352, t355, t364);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t366 = gm_l0;
                void * t367 = GM_LD(void *, (((uint8_t *)t366) + 0));
                gm_tl874t2atnlh3 t368 = F.l4;
                void * t369 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t367, t368);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t370 = 0;
                F.ret = t369;
                goto gm_fret;
            }
            goto ix346;
ie346:;
ix346:;
        }
        {
            void * t371 = gm_l0;
            gm_t88rcucg1b7uk t372 = gm_l7;
            gm_tl874t2atnlh3 t373 = F.l4;
            uint32_t t374 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t371, t372, t373);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t374;
        }
        {
            gm_t88rcucg1b7uk t375 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            gm_t88rcucg1b7uk t376 = gm_l7;
            uint32_t t377 = gm_l43;
            gm_teti411eujvf4 t378;
            memset(&t378, 0, sizeof t378);
            GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t378 + 8), t377);
            gm_tl874t2atnlh3 t379 = F.l4;
            void * t380 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t376, &t378, t379);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t381;
            GM_ST(uint32_t, (uint8_t *)&t381 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t381 + 0), t380);
            GM_ST(uint32_t, ((uint8_t *)&t381 + 8), ((uint32_t)0ULL));
            gm_teti411eujvf4 t382;
            memset(&t382, 0, sizeof t382);
            GM_ST(uint8_t, ((uint8_t *)&t382 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t382 + 8), t381);
            gm_tl874t2atnlh3 t383 = F.l4;
            void * t384 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t375, &t382, t383);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t384;
        }
        {
            gm_t88rcucg1b7uk t385 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            gm_t88rcucg1b7uk t386 = gm_l7;
            uint32_t t387 = gm_l43;
            gm_teti411eujvf4 t388;
            memset(&t388, 0, sizeof t388);
            GM_ST(uint8_t, ((uint8_t *)&t388 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t388 + 8), t387);
            gm_tl874t2atnlh3 t389 = F.l4;
            void * t390 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t386, &t388, t389);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t391;
            GM_ST(uint32_t, (uint8_t *)&t391 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t391 + 0), t390);
            GM_ST(uint32_t, ((uint8_t *)&t391 + 8), ((uint32_t)1ULL));
            gm_teti411eujvf4 t392;
            memset(&t392, 0, sizeof t392);
            GM_ST(uint8_t, ((uint8_t *)&t392 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t392 + 8), t391);
            gm_tl874t2atnlh3 t393 = F.l4;
            void * t394 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t385, &t392, t393);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l45 = t394;
        }
        {
            void * t395 = gm_l0;
            void * t396 = gm_l1;
            void * t397 = gm_l44;
            void * t398 = gm_l45;
            gm_t88rcucg1b7uk t399 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
            uint8_t t400 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
            void * t401 = gm_f_range_loop_44fv3au((void *)NULL, cx, NULL, t395, t396, t397, t398, t399, t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l46 = t401;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l47, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t402 = (void *)((uint8_t *)&F.l47);
            uint32_t t403 = gm_l43;
            void * t404 = gm_l5;
            gm_ttggq7ng4kp3p t405;
            GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t405 + 0), t403);
            GM_ST(void *, ((uint8_t *)&t405 + 8), t404);
            gm_trcmmd12cefb1 t406;
            GM_ST(uint8_t, (uint8_t *)&t406 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t406 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t406 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t406 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t406 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t406 + 8), t405);
            gm_tftb1odc739t5 t407 = gm_f_push_d6c76ud_2(t402, &t406);
            gm_tftb1odc739t5 t408 = t407;
            if (((((uint8_t *)&t408))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t408) + 1, gm_srclfnv05vuf0b8, 393, 13);
            gm_b0_1 t409 = GM_LD(gm_b0_1, (((uint8_t *)&t408) + 1));
        }
        {
            void * t410 = (void *)((uint8_t *)&F.l47);
            void * t411 = gm_l46;
            gm_trcmmd12cefb1 t412;
            GM_ST(uint8_t, (uint8_t *)&t412 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t412 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t412 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t412 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t412 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t412 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t412 + 8), t411);
            gm_tftb1odc739t5 t413 = gm_f_push_d6c76ud_2(t410, &t412);
            gm_tftb1odc739t5 t414 = t413;
            if (((((uint8_t *)&t414))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t414) + 1, gm_srclfnv05vuf0b8, 394, 13);
            gm_b0_1 t415 = GM_LD(gm_b0_1, (((uint8_t *)&t414) + 1));
        }
        {
            void * t416 = gm_l0;
            void * t417 = GM_LD(void *, (((uint8_t *)t416) + 0));
            gm_b16_8 t418 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 0));
            gm_tl874t2atnlh3 t419 = F.l4;
            void * t420 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t417, t418, t419);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t421 = 0;
            F.ret = t420;
            goto gm_fret;
        }
        goto me35;
mn335:;
ma42:;
        uint8_t t423 = 0;
        F.ret = ((void *)0);
        goto gm_fret;
        goto me35;
mn422:;
ma43:;
        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 321, 5);
me35:;
    }
    {
        void * t424 = gm_l8;
        gm_t88rcucg1b7uk t425 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t424) + 0));
        gm_l48 = t425;
    }
    {
        void * t426 = gm_l3;
        gm_t88rcucg1b7uk t427 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t426);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l49 = t427;
    }
    {
        void * t428 = gm_l0;
        gm_t88rcucg1b7uk t429 = gm_l48;
        gm_tl874t2atnlh3 t430 = F.l4;
        uint32_t t431 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t428, t429, t430);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l50 = t431;
    }
    {
        void * t432 = gm_l0;
        gm_t88rcucg1b7uk t433 = gm_l49;
        gm_tl874t2atnlh3 t434 = F.l4;
        uint32_t t435 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t432, t433, t434);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l51 = t435;
    }
    {
        gm_closure t436 = ((gm_closure){(void *)gm_f_local_l5o3d47, (void *)&F.h, cx});
        gm_l52 = t436;
    }
    {
        gm_t88rcucg1b7uk t437 = gm_l49;
        gm_closure t438 = gm_l52;
        uint32_t t439 = gm_l50;
        gm_t88rcucg1b7uk t440 = gm_l48;
        gm_closure t441 = t438;
        gm_node *ccx442 = t441.cx;
        gm_node cn443; { gm_node *f = gm_find(cx, 587034433); if (f) { cn443 = *f; cn443.next = ccx442; ccx442 = &cn443; } }
        void * t444 = t441.code;
        void * t445;
        if (GM_PTR_TAGGED(t444)) {
            t445 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t444))(t439, GM_CONV(gm_csguankko4lmhd, t440));
        } else {
            t445 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t444)(t441.env, ccx442, NULL, t439, t440);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t446;
        memset(&t446, 0, sizeof t446);
        GM_ST(uint8_t, ((uint8_t *)&t446 + 0), ((uint8_t)22ULL));
        GM_ST(void *, ((uint8_t *)&t446 + 8), t445);
        gm_tl874t2atnlh3 t447 = F.l4;
        void * t448 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t437, &t446, t447);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l53 = t448;
    }
    {
        gm_t88rcucg1b7uk t449 = gm_l9;
        gm_tlcl4u558l6p5 t450;
        memset((uint8_t *)&t450 + 17, 0, 7);
        gm_closure t451 = gm_l52;
        uint32_t t452 = gm_l50;
        gm_t88rcucg1b7uk t453 = gm_l48;
        gm_closure t454 = t451;
        gm_node *ccx455 = t454.cx;
        gm_node cn456; { gm_node *f = gm_find(cx, 587034433); if (f) { cn456 = *f; cn456.next = ccx455; ccx455 = &cn456; } }
        void * t457 = t454.code;
        void * t458;
        if (GM_PTR_TAGGED(t457)) {
            t458 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t457))(t452, GM_CONV(gm_csguankko4lmhd, t453));
        } else {
            t458 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t457)(t454.env, ccx455, NULL, t452, t453);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t450 + 0, t458);
        gm_closure t459 = gm_l52;
        uint32_t t460 = gm_l51;
        gm_t88rcucg1b7uk t461 = gm_l49;
        gm_closure t462 = t459;
        gm_node *ccx463 = t462.cx;
        gm_node cn464; { gm_node *f = gm_find(cx, 587034433); if (f) { cn464 = *f; cn464.next = ccx463; ccx463 = &cn464; } }
        void * t465 = t462.code;
        void * t466;
        if (GM_PTR_TAGGED(t465)) {
            t466 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t465))(t460, GM_CONV(gm_csguankko4lmhd, t461));
        } else {
            t466 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t465)(t462.env, ccx463, NULL, t460, t461);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t450 + 8, t466);
        GM_ST(uint8_t, (uint8_t *)&t450 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t467;
        memset(&t467, 0, sizeof t467);
        GM_ST(uint8_t, ((uint8_t *)&t467 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t467 + 8), t450);
        gm_tl874t2atnlh3 t468 = F.l4;
        void * t469 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t449, &t467, t468);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l54 = t469;
    }
    {
        void * t470 = gm_l0;
        void * t471 = gm_l1;
        gm_t88rcucg1b7uk t472 = gm_l9;
        uint32_t t473 = gm_l51;
        gm_t88rcucg1b7uk t474 = gm_l49;
        void * t475 = gm_l53;
        void * t476 = gm_l54;
        void * t477 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t470, t471, t472, t473, t474, t475, t476);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l55 = t477;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l56, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t478 = (void *)((uint8_t *)&F.l56);
        uint32_t t479 = gm_l50;
        void * t480 = gm_l8;
        gm_ttggq7ng4kp3p t481;
        GM_ST(uint32_t, (uint8_t *)&t481 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t481 + 0), t479);
        GM_ST(void *, ((uint8_t *)&t481 + 8), t480);
        gm_trcmmd12cefb1 t482;
        GM_ST(uint8_t, (uint8_t *)&t482 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t482 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t482 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t482 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t482 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t482 + 8), t481);
        gm_tftb1odc739t5 t483 = gm_f_push_d6c76ud_2(t478, &t482);
        gm_tftb1odc739t5 t484 = t483;
        if (((((uint8_t *)&t484))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t484) + 1, gm_srclfnv05vuf0b8, 409, 5);
        gm_b0_1 t485 = GM_LD(gm_b0_1, (((uint8_t *)&t484) + 1));
    }
    {
        void * t486 = (void *)((uint8_t *)&F.l56);
        uint32_t t487 = gm_l51;
        gm_t88rcucg1b7uk t488 = gm_l49;
        gm_tbci5ospk5nnn t489;
        gm_f_zero_s25l990((void *)&t489, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t490;
        GM_ST(uint8_t, (uint8_t *)&t490 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t490 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t490 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t490 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t490 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t490 + 8), t489);
        gm_tl874t2atnlh3 t491 = F.l4;
        void * t492 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t488, &t490, t491);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t493;
        GM_ST(uint32_t, (uint8_t *)&t493 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t493 + 0), t487);
        GM_ST(void *, ((uint8_t *)&t493 + 8), t492);
        gm_trcmmd12cefb1 t494;
        GM_ST(uint8_t, (uint8_t *)&t494 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t494 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t494 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t494 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t494 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t494 + 8), t493);
        gm_tftb1odc739t5 t495 = gm_f_push_d6c76ud_2(t486, &t494);
        gm_tftb1odc739t5 t496 = t495;
        if (((((uint8_t *)&t496))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t496) + 1, gm_srclfnv05vuf0b8, 410, 5);
        gm_b0_1 t497 = GM_LD(gm_b0_1, (((uint8_t *)&t496) + 1));
    }
    {
        void * t498 = (void *)((uint8_t *)&F.l56);
        void * t499 = gm_l55;
        gm_trcmmd12cefb1 t500;
        GM_ST(uint8_t, (uint8_t *)&t500 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t500 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t500 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t500 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t500 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t500 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t500 + 8), t499);
        gm_tftb1odc739t5 t501 = gm_f_push_d6c76ud_2(t498, &t500);
        gm_tftb1odc739t5 t502 = t501;
        if (((((uint8_t *)&t502))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t502) + 1, gm_srclfnv05vuf0b8, 411, 5);
        gm_b0_1 t503 = GM_LD(gm_b0_1, (((uint8_t *)&t502) + 1));
    }
    {
        void * t504 = gm_l0;
        void * t505 = GM_LD(void *, (((uint8_t *)t504) + 0));
        gm_b16_8 t506 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 0));
        gm_tl874t2atnlh3 t507 = F.l4;
        void * t508 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t505, t506, t507);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t509 = 0;
        F.ret = t508;
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
    GM_STACK_CHECK(&F, 1365u);
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
            gm_t9ldr1q5hn9fq t14 = t13;
            uint8_t t15 = 0;
            gm_ts6oqppjh0ba3 t16 = t10;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t14) + 0, ((uint8_t *)&t16))) t15 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t14) + 2, ((uint8_t *)&t16))) t15 = 1;
            uint8_t t17 = (uint8_t)(t15);
            gm_l3 = t17;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t19 = gm_l3;
        uint8_t t20 = (uint8_t)!t19;
        if (!t20) goto ie18;
        {
            void * t23 = gm_l1;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 56));
            uint8_t t25;
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            if (t28 != ((uint8_t)19ULL)) goto mn27;
            t25 = ((uint8_t)1);
            goto me26;
mn27:;
            t25 = ((uint8_t)0);
            goto me26;
mn29:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 194, 12);
me26:;
            if (!t25) goto ie22;
            {
                void * t31 = gm_l0;
                void * t32 = gm_l1;
                void * t33 = gm_f_for_iterator_cc5qo7n((void *)NULL, cx, NULL, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            void * t35 = gm_l0;
            void * t36 = gm_l1;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 56));
            gm_tkn42u7na3t81 t38;
            memset(&t38, 0, sizeof t38);
            void * t39 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t35, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t39;
        }
        {
            void * t41 = gm_l0;
            void * t42 = gm_l1;
            void * t43 = gm_l5;
            void * t44 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t45 = t44;
            if (!(GM_LD(void *, ((uint8_t *)&t45)) != 0)) goto ie40;
            void * t46 = GM_LD(void *, ((uint8_t *)&t45));
            gm_l6 = t46;
            {
                void * t48 = gm_l6;
                uint8_t t49 = 0;
                F.ret = t48;
                goto gm_fret;
            }
            goto ix40;
ie40:;
ix40:;
        }
        {
            void * t50 = gm_l0;
            void * t51 = gm_l1;
            void * t52 = GM_LD(void *, (((uint8_t *)t51) + 56));
            gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 72));
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_skue5huof65fj));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(79));
            gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t50, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
            gm_tl874t2atnlh3 t58 = gm_l2;
            void * t59 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t62 = gm_l1;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 56));
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t63) + 8) + 0));
        if (t64 != ((uint8_t)13ULL)) goto ie61;
        gm_ts6oqppjh0ba3 t65 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t63) + 8) + 8) + 0));
        gm_l7 = t65;
        void * t66 = GM_LD(void *, (((((uint8_t *)t63) + 8) + 8) + 8));
        gm_l8 = t66;
        void * t67 = GM_LD(void *, (((((uint8_t *)t63) + 8) + 8) + 16));
        gm_l9 = t67;
        {
            void * t69 = gm_l0;
            void * t70 = gm_l8;
            gm_tkn42u7na3t81 t71;
            memset(&t71, 0, sizeof t71);
            void * t72 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t69, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t72;
        }
        {
            void * t73 = gm_l0;
            void * t74 = gm_l9;
            gm_tkn42u7na3t81 t75;
            memset(&t75, 0, sizeof t75);
            void * t76 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t73, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t76;
        }
        {
            void * t77 = gm_l0;
            void * t78 = gm_l10;
            gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t78) + 0));
            void * t80 = gm_l11;
            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t80) + 0));
            void * t82 = gm_l1;
            void * t83 = GM_LD(void *, (((uint8_t *)t82) + 56));
            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
            gm_b16_8 t85;
            GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sp6ona76fn0di));
            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(12));
            gm_t88rcucg1b7uk t86 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t77, t79, t81, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t86;
        }
        {
            {
                {
                    gm_t88rcucg1b7uk t89 = gm_l12;
                    void * t90 = gm_l10;
                    gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t90) + 0));
                    void * t92 = gm_l11;
                    gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 0));
                    gm_t10fqegjtup5b t94;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94 + 0), t89);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94 + 4), t91);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94 + 8), t93);
                    gm_l13 = t94;
                }
                void * t95 = ((uint8_t *)&gm_l13);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srclfnv05vuf0b8, 204, 5);
                gm_b16_8 t96;
                GM_ST(void *, (uint8_t *)&t96, (void *)(((uint8_t *)t95 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t97 = t96;
                gm_l14 = t97;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t99 = gm_l15;
                    uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t101 = (t99 < t100);
                    if (!t101) goto lb98;
                    {
                        uint64_t t104 = gm_l15;
                        uint64_t t105 = (uintptr_t)t104;
                        uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t105 * 4));
                        gm_l16 = t109;
                        {
                            void * t112 = gm_l0;
                            void * t113 = (void *)(((uint8_t *)t112) + 96);
                            void * t114 = gm_l0;
                            void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
                            void * t116 = (void *)(((uint8_t *)t115) + 8);
                            gm_t88rcucg1b7uk t117 = gm_l16;
                            gm_t9qkh0rsooc8d t118;
                            GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)1ULL));
                            gm_t8uadrbfpr7kk t119;
                            gm_f_set_fallback_s66n99d((void *)&t119, (void *)NULL, cx, NULL, t113, t116, t117, t118);
                            if (gm_unw.kind) {
                                goto gm_ll102;
                            }
                            gm_t8uadrbfpr7kk t120 = t119;
                            if (!((((uint8_t *)&t120))[16] != 0)) goto ie111;
                            gm_t2i8ne5ibd7v4 t121 = GM_LD(gm_t2i8ne5ibd7v4, ((uint8_t *)&t120));
                            gm_l17 = t121;
                            {
                                void * t123 = gm_l0;
                                void * t124 = GM_LD(void *, (((uint8_t *)t123) + 0));
                                void * t125 = gm_l0;
                                void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
                                void * t127 = gm_l1;
                                void * t128 = GM_LD(void *, (((uint8_t *)t127) + 56));
                                gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t128) + 72));
                                gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
                                gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 4));
                                gm_t4gut2c9fj1kt t132;
                                gm_f_early_fallback_error_4lq49mb((void *)&t132, (void *)NULL, cx, NULL, t126, t129, t130, t131);
                                if (gm_unw.kind) {
                                    goto gm_ll102;
                                }
                                gm_b0_1 t133 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t124, &t132);
                                if (gm_unw.kind) {
                                    goto gm_ll102;
                                }
                            }
                            {
                                goto lb98;
                            }
                            goto ix111;
ie111:;
ix111:;
                        }
                        goto me107;
mn108:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 204, 5);
me107:;
                    }
lc98:;
                    {
                        uint64_t t135 = gm_l15;
                        uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
                        uint64_t t138 = r136;
                        gm_l15 = t138;
                    }
                }
gm_ll102:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb98; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc98; }
                }
                goto gm_lad;
lb98:;
            }
        }
        {
            void * t139 = gm_l0;
            void * t140 = (void *)(((uint8_t *)t139) + 320);
            gm_t91vm1g0k2rpm t141;
            memset((uint8_t *)&t141 + 34, 0, 6);
            gm_t88rcucg1b7uk t142 = gm_l12;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t141 + 0, t142);
            void * t143 = gm_l1;
            void * t144 = GM_LD(void *, (((uint8_t *)t143) + 56));
            gm_tl874t2atnlh3 t145 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t144) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t141 + 4, t145);
            gm_b16_8 t146;
            GM_ST(void *, (uint8_t *)&t146, (void *)(gm_sl4ns6hobicht));
            GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t141 + 16, t146);
            GM_ST(uint8_t, (uint8_t *)&t141 + 32, ((uint8_t)1));
            GM_ST(uint8_t, (uint8_t *)&t141 + 33, ((uint8_t)0));
            gm_tpm220j5qhjke t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t147 + 8), t141);
            gm_tftb1odc739t5 t148 = gm_f_push_7koe9lk(t140, &t147);
            gm_tftb1odc739t5 t149 = t148;
            if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_srclfnv05vuf0b8, 210, 5);
            gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
        }
        {
            void * t151 = gm_l0;
            void * t152 = gm_l1;
            void * t153 = gm_l10;
            void * t154 = gm_l11;
            gm_t88rcucg1b7uk t155 = gm_l12;
            uint8_t t156 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l7) + 0) == ((uint8_t)11ULL)));
            void * t157 = gm_f_range_loop_44fv3au((void *)NULL, cx, NULL, t151, t152, t153, t154, t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t158 = 0;
            F.ret = t157;
            goto gm_fret;
        }
        goto ix61;
ie61:;
        {
            void * t160 = gm_l0;
            void * t161 = GM_LD(void *, (((uint8_t *)t160) + 0));
            gm_tl874t2atnlh3 t162 = gm_l2;
            void * t163 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t161, t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t164 = 0;
            F.ret = t163;
            goto gm_fret;
        }
ix61:;
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
    GM_STACK_CHECK(&F, 6118u);
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
        gm_t9ldr1q5hn9fq t16 = t15;
        uint8_t t17 = 0;
        gm_ts6oqppjh0ba3 t18 = t12;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t16) + 0, ((uint8_t *)&t18))) t17 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t16) + 2, ((uint8_t *)&t18))) t17 = 1;
        uint8_t t19 = (uint8_t)(t17);
        if (!t19) goto ie6;
        {
            void * t21 = gm_l0;
            void * t22 = gm_l8;
            void * t23 = gm_l6;
            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
            gm_tkn42u7na3t81 t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[4] = 1;
            void * t26 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t21, t22, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t26;
        }
        {
            void * t27 = gm_l0;
            void * t28 = gm_l9;
            void * t29 = gm_l6;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
            gm_tkn42u7na3t81 t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t31), t30);
            ((uint8_t *)&t31)[4] = 1;
            void * t32 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t27, t28, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t32;
        }
        {
            void * t33 = gm_l0;
            void * t34 = gm_l6;
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
            void * t36 = gm_l10;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t36) + 0));
            gm_tl874t2atnlh3 t38 = gm_l4;
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_su22guvkav5b5));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(20));
            gm_t88rcucg1b7uk t40 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t33, t35, t37, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t40;
        }
        {
            {
                void * t42 = gm_l0;
                gm_t88rcucg1b7uk t43 = gm_l12;
                void * t44 = gm_l11;
                gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + 0));
                gm_tl874t2atnlh3 t46 = gm_l4;
                gm_b16_8 t47;
                GM_ST(void *, (uint8_t *)&t47, (void *)(gm_su22guvkav5b5));
                GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(20));
                gm_t88rcucg1b7uk t48 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t42, t43, t45, t46, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t49 = gm_l0;
            void * t50 = (void *)(((uint8_t *)t49) + 320);
            gm_t88rcucg1b7uk t51 = gm_l12;
            gm_tl874t2atnlh3 t52 = gm_l4;
            gm_t2i8ne5ibd7v4 t53;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t53 + 0), t51);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t53 + 4), t52);
            gm_tpm220j5qhjke t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)&t54 + 8), t53);
            gm_tftb1odc739t5 t55 = gm_f_push_7koe9lk(t50, &t54);
            gm_tftb1odc739t5 t56 = t55;
            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srclfnv05vuf0b8, 14, 9);
            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
        }
        {
            void * t58 = gm_l0;
            void * t59 = gm_l6;
            gm_b16_8 t60 = gm_f_empty_slice_vj3ckdj_2();
            gm_b16_8 t61 = t60;
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
            void * t63 = gm_l10;
            void * t64 = gm_l11;
            gm_t88rcucg1b7uk t65 = gm_l12;
            uint8_t t66 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l7) + 0) == ((uint8_t)11ULL)));
            uint8_t t67 = gm_l3;
            gm_tl874t2atnlh3 t68 = gm_l4;
            void * t69 = gm_f_range_test_tqcppmu((void *)NULL, cx, NULL, t58, t59, t62, t63, t64, t65, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t72 = gm_l2;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t72) + 8) + 0));
        if (t73 != ((uint8_t)10ULL)) goto ie71;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((((uint8_t *)t72) + 8) + 8));
        gm_l13 = t74;
        gm_b16_8 t75 = gm_l13;
        gm_b16_8 t76 = t75;
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
        uint8_t t78 = gm_f_has_spread_bndtpd6(t77);
        uint8_t t79 = (uint8_t)!t78;
        if (!t79) goto ie71;
        {
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                gm_l14 = t82;
            }
            {
                for (;;) {
                    uint64_t t84 = gm_l15;
                    uint64_t t85 = gm_l14;
                    uint8_t t86 = (t84 < t85);
                    if (!t86) goto lb83;
                    {
                        uint64_t t89 = gm_l15;
                        uint64_t t90 = t89;
                        uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)&t90));
                        gm_l16 = t93;
                        {
                            void * t96 = gm_l0;
                            uint64_t t97 = gm_l16;
                            uint64_t t98 = (uintptr_t)t97;
                            uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            if (t98 >= (uintptr_t)(t99)) gm_trap_index(t98, t99, gm_srclfnv05vuf0b8, 21, 39);
                            void * t100 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t98 * 136) + 40);
                            void * t101 = gm_l6;
                            gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t101) + 0));
                            gm_toqf51949ppls t103;
                            gm_f_is_pattern_operand_p51tban((void *)&t103, (void *)NULL, cx, NULL, t96, t100, t102);
                            if (gm_unw.kind) {
                                goto gm_ll87;
                            }
                            gm_toqf51949ppls t104 = t103;
                            uint8_t t105;
                            if (((((uint8_t *)&t104))[64] != 0)) goto mn107;
                            t105 = ((uint8_t)0);
                            goto me106;
mn107:;
                            t105 = ((uint8_t)1);
                            goto me106;
mn108:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 21, 16);
me106:;
                            if (!t105) goto ie95;
                            {
                                void * t110 = gm_l0;
                                void * t111 = gm_l6;
                                gm_b16_8 t112 = gm_l13;
                                gm_b16_8 t113 = t112;
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                                uint8_t t115 = gm_l3;
                                gm_tl874t2atnlh3 t116 = gm_l4;
                                void * t117 = gm_f_in_patterns_mi6p7hb((void *)NULL, cx, NULL, t110, t111, t114, t115, t116);
                                if (gm_unw.kind) {
                                    goto gm_ll87;
                                }
                                uint8_t t118 = 0;
                                F.ret = t117;
                                goto gm_fret;
                            }
                            goto ix95;
ie95:;
ix95:;
                        }
                        goto me91;
mn92:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 20, 9);
me91:;
                    }
lc83:;
                    {
                        uint64_t t120 = gm_l15;
                        uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                        uint64_t t123 = r121;
                        gm_l15 = t123;
                    }
                }
gm_ll87:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb83; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc83; }
                }
                goto gm_lad;
lb83:;
            }
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        gm_l17 = ((uint8_t)0);
    }
    {
        void * t125 = gm_l2;
        uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t125) + 8) + 0));
        if (t126 != ((uint8_t)10ULL)) goto ie124;
        gm_b16_8 t127 = GM_LD(gm_b16_8, ((((uint8_t *)t125) + 8) + 8));
        gm_l18 = t127;
        gm_b16_8 t128 = gm_l18;
        gm_b16_8 t129 = t128;
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
        uint8_t t131 = gm_f_has_spread_bndtpd6(t130);
        uint8_t t132 = (uint8_t)!t131;
        if (!t132) goto ie124;
        {
            gm_l17 = ((uint8_t)1);
        }
        goto ix124;
ie124:;
ix124:;
    }
    {
        void * t134;
        uint8_t t136 = gm_l17;
        if (!t136) goto ie135;
        void * t137 = gm_l2;
        uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t137) + 8) + 0));
        if (t138 != ((uint8_t)10ULL)) goto ie135;
        gm_b16_8 t139 = GM_LD(gm_b16_8, ((((uint8_t *)t137) + 8) + 8));
        gm_l19 = t139;
        {
            gm_f_list_s1kpkpr((void *)&F.l20, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t142 = gm_l0;
            void * t143 = (void *)(((uint8_t *)t142) + 96);
            void * t144 = gm_l0;
            void * t145 = GM_LD(void *, (((uint8_t *)t144) + 0));
            void * t146 = gm_l6;
            gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t146) + 0));
            gm_t88rcucg1b7uk t148 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t143, t145, t147);
            if (gm_unw.kind) {
                goto gm_ld140_1;
            }
            gm_l21 = t148;
        }
        {
            uint8_t t149 = 0;
            void * t150 = gm_l5;
            gm_t88rcucg1b7uk t151 = gm_l21;
            gm_t53at80pfdus3 t152;
            gm_f_kind_jcnt4hm((void *)&t152, t150, GM_CONV(gm_csguankko4lmhd, t151));
            gm_t53at80pfdus3 t153 = t152;
            uint8_t t154;
            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&t153) + 0));
            switch (t160) { case ((uint8_t)0ULL): goto ma158; case ((uint8_t)1ULL): goto ma158; case ((uint8_t)2ULL): goto ma158; case ((uint8_t)3ULL): goto ma158; case ((uint8_t)4ULL): goto ma158; case ((uint8_t)5ULL): goto ma158; case ((uint8_t)6ULL): goto ma158; case ((uint8_t)7ULL): goto ma158; case ((uint8_t)8ULL): goto ma158; case ((uint8_t)9ULL): goto ma158; case ((uint8_t)10ULL): goto ma158; case ((uint8_t)11ULL): goto ma158; case ((uint8_t)12ULL): goto ma158; case ((uint8_t)13ULL): goto ma156; case ((uint8_t)14ULL): goto ma157; case ((uint8_t)15ULL): goto ma158; case ((uint8_t)16ULL): goto ma158; case ((uint8_t)17ULL): goto ma158; case ((uint8_t)18ULL): goto ma158; case ((uint8_t)19ULL): goto ma158; case ((uint8_t)20ULL): goto ma158; case ((uint8_t)21ULL): goto ma158; case ((uint8_t)22ULL): goto ma158; case ((uint8_t)23ULL): goto ma158; case ((uint8_t)24ULL): goto ma158; case ((uint8_t)25ULL): goto ma158; case ((uint8_t)26ULL): goto ma158; default: goto ma158; }
ma156:;
            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&t153) + 0));
            if (t162 != ((uint8_t)13ULL)) goto mn161;
            t154 = ((uint8_t)1);
            goto me155;
mn161:;
ma157:;
            uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&t153) + 0));
            if (t164 != ((uint8_t)14ULL)) goto mn163;
            t154 = ((uint8_t)1);
            goto me155;
mn163:;
ma158:;
            t154 = ((uint8_t)0);
            goto me155;
mn165:;
ma159:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 34, 18);
me155:;
            if (t154) {
                void * t166 = gm_l5;
                gm_t88rcucg1b7uk t167 = gm_l21;
                uint8_t t168 = gm_f_has_infer_3o82a0b(t166, GM_CONV(gm_csguankko4lmhd, t167));
                uint8_t t169 = (uint8_t)!t168;
                t149 = t169;
            } else t149 = 0;
            gm_l22 = t149;
        }
        {
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                gm_l23 = t171;
            }
            {
                for (;;) {
                    uint64_t t173 = gm_l24;
                    uint64_t t174 = gm_l23;
                    uint8_t t175 = (t173 < t174);
                    if (!t175) goto lb172;
                    {
                        uint64_t t178 = gm_l24;
                        uint64_t t179 = t178;
                        uint64_t t182 = GM_LD(uint64_t, ((uint8_t *)&t179));
                        gm_l25 = t182;
                        {
                            void * t184 = gm_l0;
                            uint64_t t185 = gm_l25;
                            uint64_t t186 = (uintptr_t)t185;
                            uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                            if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_srclfnv05vuf0b8, 36, 27);
                            void * t188 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t186 * 136) + 40);
                            void * t189 = gm_l6;
                            gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t189) + 0));
                            gm_tkn42u7na3t81 t191;
                            GM_ST(uint8_t, (uint8_t *)&t191 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t191 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t191), t190);
                            ((uint8_t *)&t191)[4] = 1;
                            void * t192 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t184, t188, t191);
                            if (gm_unw.kind) {
                                goto gm_ll176;
                            }
                            gm_l26 = t192;
                        }
                        {
                            void * t193 = gm_l0;
                            void * t194 = (void *)(((uint8_t *)t193) + 96);
                            void * t195 = gm_l0;
                            void * t196 = GM_LD(void *, (((uint8_t *)t195) + 0));
                            void * t197 = gm_l26;
                            gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t197) + 0));
                            gm_t88rcucg1b7uk t199 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t194, t196, t198);
                            if (gm_unw.kind) {
                                goto gm_ll176;
                            }
                            gm_l27 = t199;
                        }
                        {
                            uint8_t t201 = gm_l22;
                            if (!t201) goto ie200;
                            gm_t88rcucg1b7uk t202 = gm_l27;
                            gm_t88rcucg1b7uk t203 = gm_l21;
                            gm_t88rcucg1b7uk t204 = t202;
                            gm_t88rcucg1b7uk t205 = t203;
                            uint8_t t206 = (!gm_eq9m6mvtmgde(((uint8_t *)&t204), ((uint8_t *)&t205)));
                            if (!t206) goto ie200;
                            uint8_t t207 = 0;
                            void * t208 = gm_l5;
                            gm_t88rcucg1b7uk t209 = gm_l27;
                            gm_t53at80pfdus3 t210;
                            gm_f_kind_jcnt4hm((void *)&t210, t208, GM_CONV(gm_csguankko4lmhd, t209));
                            gm_t53at80pfdus3 t211 = t210;
                            uint8_t t212;
                            uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&t211) + 0));
                            if (t215 != ((uint8_t)23ULL)) goto mn214;
                            t212 = ((uint8_t)1);
                            goto me213;
mn214:;
                            t212 = ((uint8_t)0);
                            goto me213;
mn216:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 38, 44);
me213:;
                            if (!t212) {
                                void * t217 = gm_l0;
                                gm_t88rcucg1b7uk t218 = gm_l27;
                                gm_t88rcucg1b7uk t219 = gm_l21;
                                gm_th2lqbo77pvsc t220;
                                gm_f_coercion_steps_ibqvhta((void *)&t220, (void *)NULL, cx, NULL, t217, t218, t219);
                                if (gm_unw.kind) {
                                    goto gm_ll176;
                                }
                                gm_th2lqbo77pvsc t221 = t220;
                                uint8_t t222;
                                if (((((uint8_t *)&t221))[16] != 0)) goto mn224;
                                t222 = ((uint8_t)0);
                                goto me223;
mn224:;
                                t222 = ((uint8_t)1);
                                goto me223;
mn225:;
                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 38, 73);
me223:;
                                t207 = t222;
                            } else t207 = 1;
                            if (!t207) goto ie200;
                            {
                                void * t227 = gm_l0;
                                void * t228 = gm_l26;
                                gm_t88rcucg1b7uk t229 = gm_l21;
                                void * t230 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t227, t228, t229);
                                if (gm_unw.kind) {
                                    goto gm_ll176;
                                }
                                gm_l26 = t230;
                            }
                            goto ix200;
ie200:;
ix200:;
                        }
                        {
                            void * t231 = (void *)((uint8_t *)&F.l20);
                            void * t232 = gm_l26;
                            gm_tftb1odc739t5 t233 = gm_f_push_pf02nv6(t231, t232);
                            gm_tftb1odc739t5 t234 = t233;
                            if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 1, gm_srclfnv05vuf0b8, 41, 13);
                            gm_b0_1 t235 = GM_LD(gm_b0_1, (((uint8_t *)&t234) + 1));
                        }
                        goto me180;
mn181:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 35, 9);
me180:;
                    }
lc172:;
                    {
                        uint64_t t237 = gm_l24;
                        uint64_t r238; bool o239 = __builtin_add_overflow(t237, ((uint64_t)1ULL), &r238);
                        uint64_t t240 = r238;
                        gm_l24 = t240;
                    }
                }
gm_ll176:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb172; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc172; }
                }
                goto gm_ld140_1;
lb172:;
            }
        }
        {
            void * t241 = gm_l0;
            void * t242 = gm_l6;
            gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
            gm_b16_8 t244 = t243;
            gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
            gm_t88rcucg1b7uk t246 = gm_f_join_members_97aedtg((void *)NULL, cx, NULL, t241, t242, t245);
            if (gm_unw.kind) {
                goto gm_ld140_1;
            }
            gm_l28 = t246;
        }
        {
            gm_t88rcucg1b7uk t248 = gm_l28;
            void * t249 = gm_l6;
            gm_t88rcucg1b7uk t250 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t249) + 0));
            gm_t88rcucg1b7uk t251 = t248;
            gm_t88rcucg1b7uk t252 = t250;
            uint8_t t253 = (!gm_eq9m6mvtmgde(((uint8_t *)&t251), ((uint8_t *)&t252)));
            if (!t253) goto ie247;
            {
                void * t255 = gm_l0;
                void * t256 = gm_l6;
                gm_t88rcucg1b7uk t257 = gm_l28;
                void * t258 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t255, t256, t257);
                if (gm_unw.kind) {
                    goto gm_ld140_1;
                }
                gm_l6 = t258;
            }
            goto ix247;
ie247:;
ix247:;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l29, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld140_1;
            }
        }
        {
            gm_f_list_nof7vfj((void *)&F.l30, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld140_1;
            }
        }
        {
            void * t259 = gm_l0;
            void * t260 = (void *)(((uint8_t *)t259) + 96);
            void * t261 = gm_l0;
            void * t262 = GM_LD(void *, (((uint8_t *)t261) + 0));
            gm_t88rcucg1b7uk t263 = gm_l28;
            gm_t88rcucg1b7uk t264 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t260, t262, t263);
            if (gm_unw.kind) {
                goto gm_ld140_1;
            }
            gm_l31 = t264;
        }
        {
            {
                gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                gm_l32 = t266;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t268 = gm_l33;
                    uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t270 = (t268 < t269);
                    if (!t270) goto lb267;
                    {
                        uint64_t t273 = gm_l33;
                        uint64_t t274 = (uintptr_t)t273;
                        uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        void * t276 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t274 * 8));
                        void * t277 = t276;
                        void * t280 = GM_LD(void *, ((uint8_t *)&t277));
                        gm_l34 = t280;
                        {
                            void * t282 = gm_l34;
                            gm_l35 = t282;
                        }
                        {
                            void * t283 = gm_l0;
                            void * t284 = (void *)(((uint8_t *)t283) + 96);
                            void * t285 = gm_l0;
                            void * t286 = GM_LD(void *, (((uint8_t *)t285) + 0));
                            void * t287 = gm_l35;
                            gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t287) + 0));
                            gm_t88rcucg1b7uk t289 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t284, t286, t288);
                            if (gm_unw.kind) {
                                goto gm_ll271;
                            }
                            gm_l36 = t289;
                        }
                        {
                            gm_t88rcucg1b7uk t291 = gm_l36;
                            gm_t88rcucg1b7uk t292 = gm_l31;
                            gm_t88rcucg1b7uk t293 = t291;
                            gm_t88rcucg1b7uk t294 = t292;
                            uint8_t t295 = (!gm_eq9m6mvtmgde(((uint8_t *)&t293), ((uint8_t *)&t294)));
                            if (!t295) goto ie290;
                            void * t296 = gm_l5;
                            gm_t88rcucg1b7uk t297 = gm_l36;
                            uint8_t t298 = gm_f_has_infer_3o82a0b(t296, GM_CONV(gm_csguankko4lmhd, t297));
                            uint8_t t299 = (uint8_t)!t298;
                            if (!t299) goto ie290;
                            void * t300 = gm_l0;
                            gm_t88rcucg1b7uk t301 = gm_l36;
                            gm_t88rcucg1b7uk t302 = gm_l31;
                            gm_th2lqbo77pvsc t303;
                            gm_f_coercion_steps_ibqvhta((void *)&t303, (void *)NULL, cx, NULL, t300, t301, t302);
                            if (gm_unw.kind) {
                                goto gm_ll271;
                            }
                            gm_th2lqbo77pvsc t304 = t303;
                            uint8_t t305;
                            if (((((uint8_t *)&t304))[16] != 0)) goto mn307;
                            t305 = ((uint8_t)0);
                            goto me306;
mn307:;
                            t305 = ((uint8_t)1);
                            goto me306;
mn308:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 51, 59);
me306:;
                            if (!t305) goto ie290;
                            {
                                void * t310 = gm_l0;
                                void * t311 = gm_l35;
                                gm_t88rcucg1b7uk t312 = gm_l31;
                                void * t313 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t310, t311, t312);
                                if (gm_unw.kind) {
                                    goto gm_ll271;
                                }
                                gm_l35 = t313;
                            }
                            goto ix290;
ie290:;
ix290:;
                        }
                        {
                            {
                                void * t315 = gm_l0;
                                void * t316 = gm_l6;
                                gm_t88rcucg1b7uk t317 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t316) + 0));
                                void * t318 = gm_l35;
                                gm_t88rcucg1b7uk t319 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t318) + 0));
                                void * t320 = gm_l35;
                                gm_tl874t2atnlh3 t321 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t320) + 72));
                                gm_b16_8 t322;
                                GM_ST(void *, (uint8_t *)&t322, (void *)(gm_smdscldu1a8hg));
                                GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(41));
                                gm_t88rcucg1b7uk t323 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t315, t317, t319, t321, t322);
                                if (gm_unw.kind) {
                                    goto gm_ll271;
                                }
                            }
                        }
                        {
                            void * t324 = (void *)((uint8_t *)&F.l29);
                            void * t325 = gm_l35;
                            gm_tftb1odc739t5 t326 = gm_f_push_d943vgp_2(t324, &(*(gm_t69m56meqefs8 *)((uint8_t *)t325)));
                            gm_tftb1odc739t5 t327 = t326;
                            if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 1, gm_srclfnv05vuf0b8, 53, 13);
                            gm_b0_1 t328 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 1));
                        }
                        {
                            void * t329 = (void *)((uint8_t *)&F.l30);
                            void * t330 = gm_l35;
                            gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t330) + 0));
                            gm_tftb1odc739t5 t332 = gm_f_push_k3l9nic(t329, GM_CONV(gm_csguankko4lmhd, t331));
                            gm_tftb1odc739t5 t333 = t332;
                            if (((((uint8_t *)&t333))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t333) + 1, gm_srclfnv05vuf0b8, 54, 13);
                            gm_b0_1 t334 = GM_LD(gm_b0_1, (((uint8_t *)&t333) + 1));
                        }
                        goto me278;
mn279:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 48, 9);
me278:;
                    }
lc267:;
                    {
                        uint64_t t336 = gm_l33;
                        uint64_t r337; bool o338 = __builtin_add_overflow(t336, ((uint64_t)1ULL), &r337);
                        uint64_t t339 = r337;
                        gm_l33 = t339;
                    }
                }
gm_ll271:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb267; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc267; }
                }
                goto gm_ld140_1;
lb267:;
            }
        }
        void * t340 = gm_l5;
        gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
        gm_b16_8 t342 = t341;
        gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
        gm_t88rcucg1b7uk t344 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t340, t343);
        if (gm_unw.kind) {
            goto gm_ld140_1;
        }
        gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
        gm_teti411eujvf4 t346;
        memset(&t346, 0, sizeof t346);
        GM_ST(uint8_t, ((uint8_t *)&t346 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t346 + 8), t345);
        void * t347 = gm_l2;
        gm_tl874t2atnlh3 t348 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t347) + 72));
        void * t349 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t344, &t346, t348);
        if (gm_unw.kind) {
            goto gm_ld140_1;
        }
        void * t350 = t349;
        {
            void * t351 = (void *)((uint8_t *)&F.l20);
            gm_f_deinit_pal9hnf(t351);
        }
        goto gm_ls352;
gm_ld140_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t353 = (void *)((uint8_t *)&F.l20);
            gm_f_deinit_pal9hnf(t353);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls352:;
        t134 = t350;
        goto ix135;
ie135:;
        void * t355 = gm_l0;
        void * t356 = gm_l2;
        gm_tkn42u7na3t81 t357;
        memset(&t357, 0, sizeof t357);
        void * t358 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t355, t356, t357);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t359 = t358;
        t134 = t359;
ix135:;
        gm_l37 = t134;
    }
    {
        void * t360 = gm_l0;
        void * t361 = (void *)(((uint8_t *)t360) + 96);
        void * t362 = gm_l0;
        void * t363 = GM_LD(void *, (((uint8_t *)t362) + 0));
        void * t364 = gm_l37;
        gm_t88rcucg1b7uk t365 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t364) + 0));
        gm_t88rcucg1b7uk t366 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t361, t363, t365);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l38 = t366;
    }
    {
        void * t368 = gm_l0;
        void * t369 = GM_LD(void *, (((uint8_t *)t368) + 0));
        gm_t88rcucg1b7uk t370 = gm_l38;
        gm_thetuhtouve7f t371 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t369, t370);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_thetuhtouve7f t372 = t371;
        if (!((((uint8_t *)&t372))[8] != 0)) goto ie367;
        gm_tbngattl1fo51 t373 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&t372));
        gm_l39 = t373;
        {
            {
                void * t376 = gm_l0;
                void * t377 = gm_l6;
                gm_t88rcucg1b7uk t378 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t377) + 0));
                gm_t88rcucg1b7uk t379 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
                gm_tl874t2atnlh3 t380 = gm_l4;
                gm_b16_8 t381;
                GM_ST(void *, (uint8_t *)&t381, (void *)(gm_sg8s3e7n1hsck));
                GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)(33));
                gm_t88rcucg1b7uk t382 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t376, t378, t379, t380, t381);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t383 = gm_l0;
            gm_t88rcucg1b7uk t384 = gm_l38;
            gm_tl874t2atnlh3 t385 = gm_l4;
            uint32_t t386 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t383, t384, t385);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l40 = t386;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l41, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t387 = (void *)((uint8_t *)&F.l41);
            uint32_t t388 = gm_l40;
            void * t389 = gm_l37;
            gm_ttggq7ng4kp3p t390;
            GM_ST(uint32_t, (uint8_t *)&t390 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t390 + 0), t388);
            GM_ST(void *, ((uint8_t *)&t390 + 8), t389);
            gm_trcmmd12cefb1 t391;
            GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t391 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t391 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t391 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t391 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t391 + 8), t390);
            gm_tftb1odc739t5 t392 = gm_f_push_d6c76ud_2(t387, &t391);
            gm_tftb1odc739t5 t393 = t392;
            if (((((uint8_t *)&t393))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t393) + 1, gm_srclfnv05vuf0b8, 66, 9);
            gm_b0_1 t394 = GM_LD(gm_b0_1, (((uint8_t *)&t393) + 1));
        }
        {
            gm_t88rcucg1b7uk t395 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
            gm_t88rcucg1b7uk t396 = gm_l38;
            uint32_t t397 = gm_l40;
            gm_teti411eujvf4 t398;
            memset(&t398, 0, sizeof t398);
            GM_ST(uint8_t, ((uint8_t *)&t398 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t398 + 8), t397);
            gm_tl874t2atnlh3 t399 = gm_l4;
            void * t400 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t396, &t398, t399);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t401;
            GM_ST(uint32_t, (uint8_t *)&t401 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t401 + 0), t400);
            GM_ST(uint32_t, ((uint8_t *)&t401 + 8), ((uint32_t)0ULL));
            gm_teti411eujvf4 t402;
            memset(&t402, 0, sizeof t402);
            GM_ST(uint8_t, ((uint8_t *)&t402 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t402 + 8), t401);
            gm_tl874t2atnlh3 t403 = gm_l4;
            void * t404 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t395, &t402, t403);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l42 = t404;
        }
        {
            gm_t88rcucg1b7uk t405 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
            gm_t88rcucg1b7uk t406 = gm_l38;
            uint32_t t407 = gm_l40;
            gm_teti411eujvf4 t408;
            memset(&t408, 0, sizeof t408);
            GM_ST(uint8_t, ((uint8_t *)&t408 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t408 + 8), t407);
            gm_tl874t2atnlh3 t409 = gm_l4;
            void * t410 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t406, &t408, t409);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbl8il92l97ut t411;
            GM_ST(uint32_t, (uint8_t *)&t411 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t411 + 0), t410);
            GM_ST(uint32_t, ((uint8_t *)&t411 + 8), ((uint32_t)1ULL));
            gm_teti411eujvf4 t412;
            memset(&t412, 0, sizeof t412);
            GM_ST(uint8_t, ((uint8_t *)&t412 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t412 + 8), t411);
            gm_tl874t2atnlh3 t413 = gm_l4;
            void * t414 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t405, &t412, t413);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t414;
        }
        {
            void * t415 = gm_l0;
            void * t416 = gm_l6;
            gm_b16_8 t417 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
            gm_b16_8 t418 = t417;
            gm_b16_8 t419 = GM_LD(gm_b16_8, ((uint8_t *)&t418));
            void * t420 = gm_l42;
            void * t421 = gm_l43;
            gm_t88rcucg1b7uk t422 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
            uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 0));
            uint8_t t424 = gm_l3;
            gm_tl874t2atnlh3 t425 = gm_l4;
            void * t426 = gm_f_range_test_tqcppmu((void *)NULL, cx, NULL, t415, t416, t419, t420, t421, t422, t423, t424, t425);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t427 = 0;
            F.ret = t426;
            goto gm_fret;
        }
        goto ix367;
ie367:;
ix367:;
    }
    {
        void * t428 = gm_l6;
        gm_t88rcucg1b7uk t429 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t428) + 0));
        gm_l44 = t429;
    }
    {
        void * t430 = gm_l5;
        gm_t88rcucg1b7uk t431 = gm_l38;
        gm_t53at80pfdus3 t432;
        gm_f_kind_jcnt4hm((void *)&t432, t430, GM_CONV(gm_csguankko4lmhd, t431));
        gm_t53at80pfdus3 t433 = t432;
        uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)&t433) + 0));
        switch (t442) { case ((uint8_t)0ULL): goto ma440; case ((uint8_t)1ULL): goto ma440; case ((uint8_t)2ULL): goto ma440; case ((uint8_t)3ULL): goto ma440; case ((uint8_t)4ULL): goto ma440; case ((uint8_t)5ULL): goto ma440; case ((uint8_t)6ULL): goto ma440; case ((uint8_t)7ULL): goto ma440; case ((uint8_t)8ULL): goto ma440; case ((uint8_t)9ULL): goto ma440; case ((uint8_t)10ULL): goto ma437; case ((uint8_t)11ULL): goto ma435; case ((uint8_t)12ULL): goto ma438; case ((uint8_t)13ULL): goto ma440; case ((uint8_t)14ULL): goto ma440; case ((uint8_t)15ULL): goto ma440; case ((uint8_t)16ULL): goto ma440; case ((uint8_t)17ULL): goto ma440; case ((uint8_t)18ULL): goto ma440; case ((uint8_t)19ULL): goto ma440; case ((uint8_t)20ULL): goto ma436; case ((uint8_t)21ULL): goto ma440; case ((uint8_t)22ULL): goto ma440; case ((uint8_t)23ULL): goto ma440; case ((uint8_t)24ULL): goto ma440; case ((uint8_t)25ULL): goto ma440; case ((uint8_t)26ULL): goto ma439; default: goto ma440; }
ma435:;
        uint8_t t444 = GM_LD(uint8_t, (((uint8_t *)&t433) + 0));
        if (t444 != ((uint8_t)11ULL)) goto mn443;
        gm_t88rcucg1b7uk t445 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t433) + 8) + 8));
        gm_l45 = t445;
        {
            gm_t88rcucg1b7uk t447 = gm_l45;
            gm_l44 = t447;
        }
        goto me434;
mn443:;
ma436:;
        uint8_t t449 = GM_LD(uint8_t, (((uint8_t *)&t433) + 0));
        if (t449 != ((uint8_t)20ULL)) goto mn448;
        gm_t88rcucg1b7uk t450 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t433) + 8) + 4));
        gm_l46 = t450;
        {
            gm_t88rcucg1b7uk t452 = gm_l46;
            gm_l44 = t452;
        }
        goto me434;
mn448:;
ma437:;
        uint8_t t454 = GM_LD(uint8_t, (((uint8_t *)&t433) + 0));
        if (t454 != ((uint8_t)10ULL)) goto mn453;
        gm_t88rcucg1b7uk t455 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t433) + 8) + 4));
        gm_l47 = t455;
        {
            gm_t88rcucg1b7uk t457 = gm_l47;
            gm_l44 = t457;
        }
        goto me434;
mn453:;
ma438:;
        uint8_t t459 = GM_LD(uint8_t, (((uint8_t *)&t433) + 0));
        if (t459 != ((uint8_t)12ULL)) goto mn458;
        gm_b16_8 t460 = GM_LD(gm_b16_8, ((((uint8_t *)&t433) + 8) + 0));
        gm_l48 = t460;
        uint64_t t462 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
        uint8_t t463 = (t462 > ((uint64_t)0ULL));
        if (!t463) goto ie461;
        {
            uint64_t t465 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t466 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
            if (t465 >= (uintptr_t)(t466)) gm_trap_index(t465, t466, gm_srclfnv05vuf0b8, 76, 49);
            gm_t88rcucg1b7uk t467 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l48)) + t465 * 4));
            gm_l44 = t467;
        }
        goto ix461;
ie461:;
ix461:;
        goto me434;
mn458:;
ma439:;
        uint8_t t469 = GM_LD(uint8_t, (((uint8_t *)&t433) + 0));
        if (t469 != ((uint8_t)26ULL)) goto mn468;
        void * t470 = gm_l0;
        void * t471 = GM_LD(void *, (((uint8_t *)t470) + 0));
        gm_tl874t2atnlh3 t472 = gm_l4;
        void * t473 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t471, t472);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t474 = 0;
        F.ret = t473;
        goto gm_fret;
        goto me434;
mn468:;
ma440:;
        {
            void * t477 = gm_l0;
            void * t478 = gm_l2;
            gm_tl874t2atnlh3 t479 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t478) + 72));
            void * t480 = gm_l0;
            gm_t88rcucg1b7uk t481 = gm_l38;
            gm_b16_8 t482 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t480, t481);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t483;
            GM_ST(gm_b16_8, ((uint8_t *)&t483 + 0), t482);
            gm_tabb710d9mq2e t484;
            gm_f_format_vc9g56o((void *)&t484, (void *)NULL, cx, NULL, t483);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t485 = t484;
            if (((((uint8_t *)&t485))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t485) + 8, gm_srclfnv05vuf0b8, 79, 38);
            gm_b16_8 t486 = GM_LD(gm_b16_8, (((uint8_t *)&t485) + 8));
            gm_b16_8 t487 = t486;
            gm_b16_8 t488 = GM_LD(gm_b16_8, ((uint8_t *)&t487));
            gm_b0_1 t489 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t477, t479, t488);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t490 = gm_l0;
            void * t491 = GM_LD(void *, (((uint8_t *)t490) + 0));
            gm_tl874t2atnlh3 t492 = gm_l4;
            void * t493 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t491, t492);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t494 = 0;
            F.ret = t493;
            goto gm_fret;
        }
        goto me434;
mn475:;
ma441:;
        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 72, 5);
me434:;
    }
    {
        void * t496 = gm_l5;
        gm_t88rcucg1b7uk t497 = gm_l38;
        gm_t53at80pfdus3 t498;
        gm_f_kind_jcnt4hm((void *)&t498, t496, GM_CONV(gm_csguankko4lmhd, t497));
        gm_t53at80pfdus3 t499 = t498;
        uint8_t t500 = GM_LD(uint8_t, (((uint8_t *)&t499) + 0));
        if (t500 != ((uint8_t)12ULL)) goto ie495;
        gm_b16_8 t501 = GM_LD(gm_b16_8, ((((uint8_t *)&t499) + 8) + 0));
        gm_l49 = t501;
        uint64_t t502 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
        uint8_t t503 = (t502 > ((uint64_t)0ULL));
        if (!t503) goto ie495;
        uint8_t t504 = gm_l17;
        uint8_t t505 = (uint8_t)!t504;
        if (!t505) goto ie495;
        {
            void * t507 = gm_l0;
            gm_b16_8 t508 = gm_l49;
            gm_tkn42u7na3t81 t509 = gm_f_tuple_elem_join_om8llaj((void *)NULL, cx, NULL, t507, t508);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l50 = t509;
        }
        {
            {
                gm_l52 = ((uint64_t)1ULL);
            }
            {
                uint64_t t511 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                gm_l51 = t511;
            }
            {
                for (;;) {
                    uint64_t t513 = gm_l52;
                    uint64_t t514 = gm_l51;
                    uint8_t t515 = (t513 < t514);
                    if (!t515) goto lb512;
                    {
                        uint64_t t518 = gm_l52;
                        uint64_t t519 = t518;
                        uint64_t t522 = GM_LD(uint64_t, ((uint8_t *)&t519));
                        gm_l53 = t522;
                        {
                            uint8_t t525;
                            if (((((uint8_t *)&gm_l50))[4] != 0)) goto mn527;
                            t525 = ((uint8_t)1);
                            goto me526;
mn527:;
                            t525 = ((uint8_t)0);
                            goto me526;
mn528:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 89, 16);
me526:;
                            if (!t525) goto ie524;
                            void * t529 = gm_l0;
                            void * t530 = (void *)(((uint8_t *)t529) + 96);
                            void * t531 = gm_l5;
                            gm_t88rcucg1b7uk t532 = gm_l44;
                            uint64_t t533 = gm_l53;
                            uint64_t t534 = (uintptr_t)t533;
                            uint64_t t535 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                            if (t534 >= (uintptr_t)(t535)) gm_trap_index(t534, t535, gm_srclfnv05vuf0b8, 89, 61);
                            gm_t88rcucg1b7uk t536 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t534 * 4));
                            gm_t9qkh0rsooc8d t537 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t530, t531, t532, t536);
                            if (gm_unw.kind) {
                                goto gm_ll516;
                            }
                            gm_t9qkh0rsooc8d t538 = t537;
                            uint8_t t539;
                            if (!((((uint8_t *)&t538))[0] != 0)) goto mn541;
                            t539 = ((uint8_t)1);
                            goto me540;
mn541:;
                            t539 = ((uint8_t)0);
                            goto me540;
mn542:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 89, 35);
me540:;
                            if (!t539) goto ie524;
                            {
                                void * t544 = gm_l0;
                                void * t545 = (void *)(((uint8_t *)t544) + 96);
                                void * t546 = gm_l0;
                                void * t547 = GM_LD(void *, (((uint8_t *)t546) + 0));
                                gm_t88rcucg1b7uk t548 = gm_l44;
                                gm_t88rcucg1b7uk t549 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t545, t547, t548);
                                if (gm_unw.kind) {
                                    goto gm_ll516;
                                }
                                gm_l54 = t549;
                            }
                            {
                                void * t550 = gm_l0;
                                void * t551 = (void *)(((uint8_t *)t550) + 96);
                                void * t552 = gm_l0;
                                void * t553 = GM_LD(void *, (((uint8_t *)t552) + 0));
                                uint64_t t554 = gm_l53;
                                uint64_t t555 = (uintptr_t)t554;
                                uint64_t t556 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                                if (t555 >= (uintptr_t)(t556)) gm_trap_index(t555, t556, gm_srclfnv05vuf0b8, 91, 45);
                                gm_t88rcucg1b7uk t557 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t555 * 4));
                                gm_t88rcucg1b7uk t558 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t551, t553, t557);
                                if (gm_unw.kind) {
                                    goto gm_ll516;
                                }
                                gm_l55 = t558;
                            }
                            {
                                void * t559 = gm_l0;
                                void * t560 = gm_l2;
                                gm_tl874t2atnlh3 t561 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t560) + 72));
                                gm_t6j6smj7qoctu t562;
                                void * t563 = gm_l0;
                                gm_t88rcucg1b7uk t564 = gm_l54;
                                gm_b16_8 t565 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t563, t564);
                                if (gm_unw.kind) {
                                    goto gm_ll516;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t562 + 0, t565);
                                void * t566 = gm_l0;
                                gm_t88rcucg1b7uk t567 = gm_l55;
                                gm_b16_8 t568 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t566, t567);
                                if (gm_unw.kind) {
                                    goto gm_ll516;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t562 + 16, t568);
                                gm_tabb710d9mq2e t569;
                                gm_f_format_ftgbk9j((void *)&t569, (void *)NULL, cx, NULL, &t562);
                                if (gm_unw.kind) {
                                    goto gm_ll516;
                                }
                                gm_tabb710d9mq2e t570 = t569;
                                if (((((uint8_t *)&t570))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t570) + 8, gm_srclfnv05vuf0b8, 92, 42);
                                gm_b16_8 t571 = GM_LD(gm_b16_8, (((uint8_t *)&t570) + 8));
                                gm_b16_8 t572 = t571;
                                gm_b16_8 t573 = GM_LD(gm_b16_8, ((uint8_t *)&t572));
                                gm_b0_1 t574 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t559, t561, t573);
                                if (gm_unw.kind) {
                                    goto gm_ll516;
                                }
                            }
                            {
                                void * t575 = gm_l0;
                                void * t576 = GM_LD(void *, (((uint8_t *)t575) + 0));
                                gm_tl874t2atnlh3 t577 = gm_l4;
                                void * t578 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t576, t577);
                                if (gm_unw.kind) {
                                    goto gm_ll516;
                                }
                                uint8_t t579 = 0;
                                F.ret = t578;
                                goto gm_fret;
                            }
                            goto ix524;
ie524:;
ix524:;
                        }
                        goto me520;
mn521:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 88, 9);
me520:;
                    }
lc512:;
                    {
                        uint64_t t581 = gm_l52;
                        uint64_t r582; bool o583 = __builtin_add_overflow(t581, ((uint64_t)1ULL), &r582);
                        uint64_t t584 = r582;
                        gm_l52 = t584;
                    }
                }
gm_ll516:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb512; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc512; }
                }
                goto gm_lad;
lb512:;
            }
        }
        {
            if (!((((uint8_t *)&gm_l50))[4] != 0)) goto ie585;
            gm_t88rcucg1b7uk t586 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l50));
            gm_l56 = t586;
            {
                gm_t88rcucg1b7uk t588 = gm_l56;
                gm_l44 = t588;
            }
            {
                void * t589 = gm_l0;
                gm_t88rcucg1b7uk t590 = gm_l38;
                gm_tl874t2atnlh3 t591 = gm_l4;
                uint32_t t592 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t589, t590, t591);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l57 = t592;
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
                    uint64_t t594 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                    gm_l60 = t594;
                }
                {
                    for (;;) {
                        uint64_t t596 = gm_l61;
                        uint64_t t597 = gm_l60;
                        uint8_t t598 = (t596 < t597);
                        if (!t598) goto lb595;
                        {
                            uint64_t t601 = gm_l61;
                            uint64_t t602 = t601;
                            uint64_t t605 = GM_LD(uint64_t, ((uint8_t *)&t602));
                            gm_l62 = t605;
                            {
                                uint64_t t607 = gm_l62;
                                uint64_t t608 = (uintptr_t)t607;
                                uint64_t t609 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                                if (t608 >= (uintptr_t)(t609)) gm_trap_index(t608, t609, gm_srclfnv05vuf0b8, 102, 33);
                                gm_t88rcucg1b7uk t610 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t608 * 4));
                                gm_t88rcucg1b7uk t611 = gm_l38;
                                uint32_t t612 = gm_l57;
                                gm_teti411eujvf4 t613;
                                memset(&t613, 0, sizeof t613);
                                GM_ST(uint8_t, ((uint8_t *)&t613 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t613 + 8), t612);
                                gm_tl874t2atnlh3 t614 = gm_l4;
                                void * t615 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t611, &t613, t614);
                                if (gm_unw.kind) {
                                    goto gm_ll599;
                                }
                                uint64_t t616 = gm_l62;
                                uint32_t t617 = (uint32_t)t616;
                                gm_tbl8il92l97ut t618;
                                GM_ST(uint32_t, (uint8_t *)&t618 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t618 + 0), t615);
                                GM_ST(uint32_t, ((uint8_t *)&t618 + 8), t617);
                                gm_teti411eujvf4 t619;
                                memset(&t619, 0, sizeof t619);
                                GM_ST(uint8_t, ((uint8_t *)&t619 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t619 + 8), t618);
                                gm_tl874t2atnlh3 t620 = gm_l4;
                                void * t621 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t610, &t619, t620);
                                if (gm_unw.kind) {
                                    goto gm_ll599;
                                }
                                gm_l63 = t621;
                            }
                            {
                                void * t622 = (void *)((uint8_t *)&F.l58);
                                void * t623 = gm_l0;
                                void * t624 = gm_l63;
                                gm_t88rcucg1b7uk t625 = gm_l56;
                                void * t626 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t623, t624, t625);
                                if (gm_unw.kind) {
                                    goto gm_ll599;
                                }
                                gm_tftb1odc739t5 t627 = gm_f_push_d943vgp_2(t622, &(*(gm_t69m56meqefs8 *)((uint8_t *)t626)));
                                gm_tftb1odc739t5 t628 = t627;
                                if (((((uint8_t *)&t628))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t628) + 1, gm_srclfnv05vuf0b8, 103, 17);
                                gm_b0_1 t629 = GM_LD(gm_b0_1, (((uint8_t *)&t628) + 1));
                            }
                            {
                                void * t630 = (void *)((uint8_t *)&F.l59);
                                gm_t88rcucg1b7uk t631 = gm_l56;
                                gm_tftb1odc739t5 t632 = gm_f_push_k3l9nic(t630, GM_CONV(gm_csguankko4lmhd, t631));
                                gm_tftb1odc739t5 t633 = t632;
                                if (((((uint8_t *)&t633))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t633) + 1, gm_srclfnv05vuf0b8, 104, 17);
                                gm_b0_1 t634 = GM_LD(gm_b0_1, (((uint8_t *)&t633) + 1));
                            }
                            goto me603;
mn604:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 101, 13);
me603:;
                        }
lc595:;
                        {
                            uint64_t t636 = gm_l61;
                            uint64_t r637; bool o638 = __builtin_add_overflow(t636, ((uint64_t)1ULL), &r637);
                            uint64_t t639 = r637;
                            gm_l61 = t639;
                        }
                    }
gm_ll599:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb595; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc595; }
                    }
                    goto gm_lad;
lb595:;
                }
            }
            {
                void * t640 = gm_l5;
                gm_b16_8 t641 = GM_LD(gm_b16_8, (((uint8_t *)&F.l59) + 0));
                gm_b16_8 t642 = t641;
                gm_b16_8 t643 = GM_LD(gm_b16_8, ((uint8_t *)&t642));
                gm_t88rcucg1b7uk t644 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t640, t643);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l64 = t644;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l65, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t645 = (void *)((uint8_t *)&F.l65);
                uint32_t t646 = gm_l57;
                void * t647 = gm_l37;
                gm_ttggq7ng4kp3p t648;
                GM_ST(uint32_t, (uint8_t *)&t648 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t648 + 0), t646);
                GM_ST(void *, ((uint8_t *)&t648 + 8), t647);
                gm_trcmmd12cefb1 t649;
                GM_ST(uint8_t, (uint8_t *)&t649 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t649 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t649 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t649 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t649 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t649 + 8), t648);
                gm_tftb1odc739t5 t650 = gm_f_push_d6c76ud_2(t645, &t649);
                gm_tftb1odc739t5 t651 = t650;
                if (((((uint8_t *)&t651))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t651) + 1, gm_srclfnv05vuf0b8, 108, 13);
                gm_b0_1 t652 = GM_LD(gm_b0_1, (((uint8_t *)&t651) + 1));
            }
            {
                gm_t88rcucg1b7uk t653 = gm_l64;
                gm_b16_8 t654 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 0));
                gm_teti411eujvf4 t655;
                memset(&t655, 0, sizeof t655);
                GM_ST(uint8_t, ((uint8_t *)&t655 + 0), ((uint8_t)35ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t655 + 8), t654);
                gm_tl874t2atnlh3 t656 = gm_l4;
                void * t657 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t653, &t655, t656);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l66 = t657;
            }
            {
                gm_t88rcucg1b7uk t658 = gm_l64;
                gm_b16_8 t659 = GM_LD(gm_b16_8, (((uint8_t *)&F.l65) + 0));
                void * t660 = gm_l66;
                gm_tl874t2atnlh3 t661 = gm_l4;
                void * t662 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t658, t659, t660, t661);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t662;
            }
            goto ix585;
ie585:;
ix585:;
        }
        goto ix495;
ie495:;
ix495:;
    }
    {
        void * t663 = gm_l0;
        void * t664 = (void *)(((uint8_t *)t663) + 96);
        void * t665 = gm_l0;
        void * t666 = GM_LD(void *, (((uint8_t *)t665) + 0));
        void * t667 = gm_l6;
        gm_t88rcucg1b7uk t668 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t667) + 0));
        gm_t88rcucg1b7uk t669 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t664, t666, t668);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l67 = t669;
    }
    {
        void * t670 = gm_l0;
        void * t671 = (void *)(((uint8_t *)t670) + 96);
        void * t672 = gm_l0;
        void * t673 = GM_LD(void *, (((uint8_t *)t672) + 0));
        gm_t88rcucg1b7uk t674 = gm_l44;
        gm_t88rcucg1b7uk t675 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t671, t673, t674);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l68 = t675;
    }
    {
        gm_t88rcucg1b7uk t677 = gm_l67;
        gm_t88rcucg1b7uk t678 = gm_l68;
        gm_t88rcucg1b7uk t679 = t677;
        gm_t88rcucg1b7uk t680 = t678;
        uint8_t t681 = (!gm_eq9m6mvtmgde(((uint8_t *)&t679), ((uint8_t *)&t680)));
        if (!t681) goto ie676;
        void * t682 = gm_l5;
        gm_t88rcucg1b7uk t683 = gm_l67;
        uint8_t t684 = gm_f_has_infer_3o82a0b(t682, GM_CONV(gm_csguankko4lmhd, t683));
        uint8_t t685 = (uint8_t)!t684;
        if (!t685) goto ie676;
        void * t686 = gm_l5;
        gm_t88rcucg1b7uk t687 = gm_l68;
        uint8_t t688 = gm_f_has_infer_3o82a0b(t686, GM_CONV(gm_csguankko4lmhd, t687));
        uint8_t t689 = (uint8_t)!t688;
        if (!t689) goto ie676;
        void * t690 = gm_l0;
        gm_t88rcucg1b7uk t691 = gm_l67;
        gm_t88rcucg1b7uk t692 = gm_l68;
        gm_th2lqbo77pvsc t693;
        gm_f_coercion_steps_ibqvhta((void *)&t693, (void *)NULL, cx, NULL, t690, t691, t692);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t694 = t693;
        uint8_t t695;
        if (((((uint8_t *)&t694))[16] != 0)) goto mn697;
        t695 = ((uint8_t)0);
        goto me696;
mn697:;
        t695 = ((uint8_t)1);
        goto me696;
mn698:;
        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 117, 77);
me696:;
        if (!t695) goto ie676;
        {
            void * t700 = gm_l0;
            void * t701 = gm_l6;
            gm_t88rcucg1b7uk t702 = gm_l68;
            void * t703 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t700, t701, t702);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t703;
        }
        goto ix676;
ie676:;
ix676:;
    }
    {
        {
            void * t705 = gm_l0;
            void * t706 = gm_l6;
            gm_t88rcucg1b7uk t707 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t706) + 0));
            gm_t88rcucg1b7uk t708 = gm_l44;
            gm_tl874t2atnlh3 t709 = gm_l4;
            gm_b16_8 t710;
            GM_ST(void *, (uint8_t *)&t710, (void *)(gm_sa4nk3mrdf2d0));
            GM_ST(uintptr_t, (uint8_t *)&t710 + 8, (uintptr_t)(35));
            gm_t88rcucg1b7uk t711 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t705, t707, t708, t709, t710);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t712 = gm_l0;
        void * t713 = (void *)(((uint8_t *)t712) + 96);
        void * t714 = gm_l0;
        void * t715 = GM_LD(void *, (((uint8_t *)t714) + 0));
        gm_t88rcucg1b7uk t716 = gm_l44;
        gm_t88rcucg1b7uk t717 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t713, t715, t716);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l69 = t717;
    }
    {
        void * t719 = gm_l5;
        gm_t88rcucg1b7uk t720 = gm_l69;
        uint8_t t721 = gm_f_has_infer_3o82a0b(t719, GM_CONV(gm_csguankko4lmhd, t720));
        uint8_t t722 = (uint8_t)!t721;
        if (!t722) goto ie718;
        void * t723 = gm_l5;
        gm_t88rcucg1b7uk t724 = gm_l69;
        uint8_t t725 = gm_f_is_error_8k27gd1(t723, GM_CONV(gm_csguankko4lmhd, t724));
        uint8_t t726 = (uint8_t)!t725;
        if (!t726) goto ie718;
        void * t727 = gm_l0;
        gm_t88rcucg1b7uk t728 = gm_l69;
        uint8_t t729 = gm_f_eq_allowed_d841u9b((void *)NULL, cx, NULL, t727, t728);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t730 = (uint8_t)!t729;
        if (!t730) goto ie718;
        {
            void * t732 = gm_l0;
            gm_tl874t2atnlh3 t733 = gm_l4;
            void * t734 = gm_l0;
            gm_t88rcucg1b7uk t735 = gm_l69;
            gm_b16_8 t736 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t734, t735);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t737;
            GM_ST(gm_b16_8, ((uint8_t *)&t737 + 0), t736);
            gm_tabb710d9mq2e t738;
            gm_f_format_2bpp75h((void *)&t738, (void *)NULL, cx, NULL, t737);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t739 = t738;
            if (((((uint8_t *)&t739))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t739) + 8, gm_srclfnv05vuf0b8, 124, 24);
            gm_b16_8 t740 = GM_LD(gm_b16_8, (((uint8_t *)&t739) + 8));
            gm_b16_8 t741 = t740;
            gm_b16_8 t742 = GM_LD(gm_b16_8, ((uint8_t *)&t741));
            gm_b0_1 t743 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t732, t733, t742);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix718;
ie718:;
ix718:;
    }
    {
        void * t744 = gm_l5;
        gm_t88rcucg1b7uk t745 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t744);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlcl4u558l6p5 t746;
        memset((uint8_t *)&t746 + 17, 0, 7);
        void * t747 = gm_l6;
        GM_ST(void *, (uint8_t *)&t746 + 0, t747);
        void * t748 = gm_l37;
        GM_ST(void *, (uint8_t *)&t746 + 8, t748);
        uint8_t t749 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&t746 + 16, t749);
        gm_teti411eujvf4 t750;
        memset(&t750, 0, sizeof t750);
        GM_ST(uint8_t, ((uint8_t *)&t750 + 0), ((uint8_t)45ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t750 + 8), t746);
        gm_tl874t2atnlh3 t751 = gm_l4;
        void * t752 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t745, &t750, t751);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t753 = 0;
        F.ret = t752;
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
    GM_STACK_CHECK(&F, 536u);
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
        gm_trcmmd12cefb1 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t27 + 8), t26);
        uint8_t t28 = 0;
        F.ret = t27;
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
    GM_STACK_CHECK(&F, 784u);
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
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srclfnv05vuf0b8, 136, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        {
            gm_b16_8 t13 = gm_l2;
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
                    void * t23 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t21 * 8));
                    void * t24 = t23;
                    void * t27 = GM_LD(void *, ((uint8_t *)&t24));
                    gm_l7 = t27;
                    {
                        void * t29 = gm_l0;
                        void * t30 = (void *)(((uint8_t *)t29) + 96);
                        void * t31 = gm_l0;
                        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                        void * t33 = gm_l7;
                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 0));
                        gm_t88rcucg1b7uk t35 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t30, t32, t34);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_l8 = t35;
                    }
                    {
                        uint8_t t37 = 0;
                        void * t38 = gm_l3;
                        gm_t88rcucg1b7uk t39 = gm_l8;
                        uint8_t t40 = gm_f_has_infer_3o82a0b(t38, GM_CONV(gm_csguankko4lmhd, t39));
                        if (!t40) {
                            void * t41 = gm_l3;
                            gm_t88rcucg1b7uk t42 = gm_l8;
                            uint8_t t43 = gm_f_is_error_8k27gd1(t41, GM_CONV(gm_csguankko4lmhd, t42));
                            t37 = t43;
                        } else t37 = 1;
                        if (!t37) goto ie36;
                        {
                            void * t45 = gm_l1;
                            gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 0));
                            uint8_t t47 = 0;
                            F.ret = t46;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t47;
                            goto gm_ll18;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    {
                        void * t48 = (void *)((uint8_t *)&F.l4);
                        gm_t88rcucg1b7uk t49 = gm_l8;
                        gm_tftb1odc739t5 t50 = gm_f_push_k3l9nic(t48, GM_CONV(gm_csguankko4lmhd, t49));
                        gm_tftb1odc739t5 t51 = t50;
                        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srclfnv05vuf0b8, 140, 9);
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 137, 5);
me25:;
                }
lc14:;
                {
                    uint64_t t54 = gm_l6;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l6 = t57;
                }
            }
gm_ll18:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
            }
            goto gm_ld1_1;
lb14:;
        }
    }
    {
        void * t59 = gm_l3;
        void * t60 = gm_l0;
        void * t61 = (void *)(((uint8_t *)t60) + 96);
        void * t62 = gm_l0;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
        void * t64 = gm_l1;
        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 0));
        gm_t88rcucg1b7uk t66 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t61, t63, t65);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t67 = gm_f_has_infer_3o82a0b(t59, GM_CONV(gm_csguankko4lmhd, t66));
        if (!t67) goto ie58;
        {
            void * t69 = gm_l1;
            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 0));
            uint8_t t71 = 0;
            F.ret = t70;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t71;
            goto gm_ld1_1;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        void * t72 = gm_l0;
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t74 = t73;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        gm_t88rcucg1b7uk t76 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t72, t75);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l9 = t76;
    }
    {
        void * t77 = gm_l0;
        void * t78 = (void *)(((uint8_t *)t77) + 96);
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
        gm_t88rcucg1b7uk t81 = gm_l9;
        gm_t88rcucg1b7uk t82 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t78, t80, t81);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l10 = t82;
    }
    {
        {
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l11 = t84;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t86 = gm_l12;
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t88 = (t86 < t87);
                if (!t88) goto lb85;
                {
                    uint64_t t91 = gm_l12;
                    uint64_t t92 = (uintptr_t)t91;
                    uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t92 * 4));
                    gm_l13 = t96;
                    {
                        void * t98 = gm_l0;
                        void * t99 = (void *)(((uint8_t *)t98) + 96);
                        void * t100 = gm_l0;
                        void * t101 = GM_LD(void *, (((uint8_t *)t100) + 0));
                        gm_t88rcucg1b7uk t102 = gm_l13;
                        gm_t88rcucg1b7uk t103 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t99, t101, t102);
                        if (gm_unw.kind) {
                            goto gm_ll89;
                        }
                        gm_l14 = t103;
                    }
                    {
                        gm_t88rcucg1b7uk t105 = gm_l14;
                        gm_t88rcucg1b7uk t106 = gm_l10;
                        gm_t88rcucg1b7uk t107 = t105;
                        gm_t88rcucg1b7uk t108 = t106;
                        uint8_t t109 = (!gm_eq9m6mvtmgde(((uint8_t *)&t107), ((uint8_t *)&t108)));
                        if (!t109) goto ie104;
                        void * t110 = gm_l0;
                        gm_t88rcucg1b7uk t111 = gm_l14;
                        gm_t88rcucg1b7uk t112 = gm_l10;
                        gm_th2lqbo77pvsc t113;
                        gm_f_coercion_steps_ibqvhta((void *)&t113, (void *)NULL, cx, NULL, t110, t111, t112);
                        if (gm_unw.kind) {
                            goto gm_ll89;
                        }
                        gm_th2lqbo77pvsc t114 = t113;
                        uint8_t t115;
                        if (((((uint8_t *)&t114))[16] != 0)) goto mn117;
                        t115 = ((uint8_t)1);
                        goto me116;
mn117:;
                        t115 = ((uint8_t)0);
                        goto me116;
mn118:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 148, 28);
me116:;
                        if (!t115) goto ie104;
                        {
                            void * t120 = gm_l1;
                            gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t120) + 0));
                            uint8_t t122 = 0;
                            F.ret = t121;
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t122;
                            goto gm_ll89;
                        }
                        goto ix104;
ie104:;
ix104:;
                    }
                    goto me94;
mn95:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 146, 5);
me94:;
                }
lc85:;
                {
                    uint64_t t124 = gm_l12;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l12 = t127;
                }
            }
gm_ll89:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb85; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc85; }
            }
            goto gm_ld1_1;
lb85:;
        }
    }
    {
        gm_t88rcucg1b7uk t128 = gm_l9;
        uint8_t t129 = 0;
        F.ret = t128;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t129;
        goto gm_ld1_1;
    }
    {
        void * t130 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_q21h982(t130);
    }
    goto gm_ls131;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t132 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_q21h982(t132);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls131:;
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
    GM_STACK_CHECK(&F, 2201u);
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
                            gm_tftb1odc739t5 t45 = t44;
                            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srclfnv05vuf0b8, 281, 27);
                            gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 281, 9);
me36:;
                    }
lc27:;
                    {
                        uint64_t t48 = gm_l11;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                        uint64_t t51 = r49;
                        gm_l11 = t51;
                    }
                }
lb27:;
            }
        }
        {
            uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
            void * t53 = gm_l1;
            uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)t53) + 40) + 8);
            if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srclfnv05vuf0b8, 282, 25);
            gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t53) + 40)) + t52 * 64) + 48));
            void * t56 = gm_l1;
            uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)t56) + 40) + 8);
            uint64_t r58; bool o59 = __builtin_sub_overflow(t57, ((uint64_t)1ULL), &r58);
            if (o59) gm_trap("integer overflow in `-`", gm_srclfnv05vuf0b8, 282, 48);
            uint64_t t60 = r58;
            uint64_t t61 = (uintptr_t)t60;
            void * t62 = gm_l1;
            uint64_t t63 = GM_LD(uintptr_t, (((uint8_t *)t62) + 40) + 8);
            if (t61 >= (uintptr_t)(t63)) gm_trap_index(t61, t63, gm_srclfnv05vuf0b8, 282, 41);
            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t62) + 40)) + t61 * 64) + 48));
            gm_tl874t2atnlh3 t65 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t55), GM_CONV(gm_cseahvg8v7p0vs, t64)));
            gm_l13 = t65;
        }
        {
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 4, 0, 4);
            memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 60, 0, 4);
            uint64_t t66 = (uintptr_t)((uint64_t)0ULL);
            void * t67 = gm_l1;
            uint64_t t68 = GM_LD(uintptr_t, (((uint8_t *)t67) + 40) + 8);
            if (t66 >= (uintptr_t)(t68)) gm_trap_index(t66, t68, gm_srclfnv05vuf0b8, 283, 35);
            uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t67) + 40)) + t66 * 64) + 0));
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_to361lgknsu69 t71;
            GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t71 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t71 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)8ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 8), t70);
            gm_tl874t2atnlh3 t72 = gm_l13;
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 0, t69);
            GM_ST(gm_to361lgknsu69, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 8, t71);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l8)) + 48, t72);
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
        void * t73 = gm_l0;
        void * t74 = (void *)((uint8_t *)&F.l8);
        gm_t88rcucg1b7uk t75 = gm_l2;
        gm_t9qkh0rsooc8d t76;
        GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)1ULL));
        void * t77 = (void *)((uint8_t *)&F.l14);
        void * t78 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t73, t74, t75, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t78;
    }
    {
        void * t80 = gm_l0;
        void * t81 = GM_LD(void *, (((uint8_t *)t80) + 0));
        void * t82 = (void *)(((uint8_t *)t81) + 8);
        gm_t88rcucg1b7uk t83 = gm_l2;
        uint8_t t84 = gm_f_is_error_8k27gd1(t82, GM_CONV(gm_csguankko4lmhd, t83));
        uint8_t t85 = (uint8_t)!t84;
        if (!t85) goto ie79;
        void * t86 = gm_l0;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = gm_l15;
        uint8_t t89 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t87, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t90 = (uint8_t)!t89;
        if (!t90) goto ie79;
        {
            void * t92 = gm_l0;
            gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 48));
            gm_b16_8 t94;
            GM_ST(void *, (uint8_t *)&t94, (void *)(gm_so4k586t2v79t));
            GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(63));
            gm_b0_1 t95 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t92, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        void * t96 = gm_l0;
        void * t97 = (void *)((uint8_t *)&F.l14);
        gm_b0_1 t98 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t96, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t99;
        memset(&t99, 0, sizeof t99);
        gm_l16 = t99;
    }
    {
        void * t101 = gm_l1;
        if (!(((((uint8_t *)t101) + 0))[32] != 0)) goto ie100;
        gm_t08olq97rscn3 t102 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t101) + 0));
        gm_l17 = t102;
        {
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
            gm_th2lqbo77pvsc t105;
            GM_ST(uint8_t, (uint8_t *)&t105 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t105 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t105 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t105), t104);
            ((uint8_t *)&t105)[16] = 1;
            gm_l16 = t105;
        }
        goto ix100;
ie100:;
ix100:;
    }
    {
        void * t106 = gm_l0;
        gm_th2lqbo77pvsc t107 = gm_l16;
        void * t108 = gm_l0;
        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
        void * t110 = (void *)(((uint8_t *)t109) + 8);
        gm_t88rcucg1b7uk t111 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1qr0p6kln6tf t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t112 + 4), t111);
        uint32_t t113 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t106, &t107, ((uint8_t)1), t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t113;
    }
    {
        void * t114 = gm_l0;
        void * t115 = gm_l1;
        void * t116 = (void *)(((uint8_t *)t115) + 64);
        void * t117 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t114, t116, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t117;
    }
    {
        {
            void * t119 = gm_l0;
            void * t120 = (void *)(((uint8_t *)t119) + 232);
            gm_te5gbhu3us3k4 t121;
            gm_f_pop_ruvva9t((void *)&t121, t120);
        }
    }
    {
        {
            void * t123 = gm_l0;
            uint32_t t124 = gm_l18;
            uint8_t t125 = gm_f_forget_label_breaks_vl4mn72(t123, t124);
        }
    }
    {
        void * t126 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t126);
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t127 = (void *)((uint8_t *)&F.l20);
        gm_tbbqefuhv2r8h t128;
        void * t129 = gm_l15;
        GM_ST(void *, (uint8_t *)&t128 + 0, t129);
        GM_ST(void *, (uint8_t *)&t128 + 8, ((void *)0));
        void * t130 = gm_l19;
        GM_ST(void *, (uint8_t *)&t128 + 16, t130);
        gm_tftb1odc739t5 t131 = gm_f_push_bpovr2k_3(t127, &t128);
        gm_tftb1odc739t5 t132 = t131;
        if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_srclfnv05vuf0b8, 299, 5);
        gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
    }
    {
        void * t134 = gm_l0;
        void * t135 = GM_LD(void *, (((uint8_t *)t134) + 0));
        void * t136 = (void *)(((uint8_t *)t135) + 8);
        gm_t88rcucg1b7uk t137 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkaapcii86udk t138;
        void * t139 = gm_l3;
        GM_ST(void *, (uint8_t *)&t138 + 0, t139);
        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t138 + 8, t140);
        gm_teti411eujvf4 t141;
        memset(&t141, 0, sizeof t141);
        GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t141 + 8), t138);
        gm_tl874t2atnlh3 t142 = gm_l6;
        void * t143 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t141, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l21 = t143;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t144 = (void *)((uint8_t *)&F.l22);
        void * t145 = gm_l21;
        gm_trcmmd12cefb1 t146;
        GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t146 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t146 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t146 + 8), t145);
        gm_tftb1odc739t5 t147 = gm_f_push_d6c76ud_2(t144, &t146);
        gm_tftb1odc739t5 t148 = t147;
        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 1, gm_srclfnv05vuf0b8, 302, 5);
        gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 1));
    }
    {
        gm_tgabaikr08mjg t150;
        memset((uint8_t *)&t150 + 36, 0, 4);
        gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t150 + 0, t151);
        GM_ST(void *, (uint8_t *)&t150 + 16, ((void *)0));
        gm_tl874t2atnlh3 t152 = gm_l6;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t150 + 24, t152);
        gm_tq7s98ihonkju t153 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t150, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0nv110r163)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t154 = t153;
        if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 8, gm_srclfnv05vuf0b8, 303, 11);
        void * t155 = GM_LD(void *, (((uint8_t *)&t154) + 8));
        gm_l23 = t155;
    }
    {
        void * t156 = gm_l0;
        void * t157 = GM_LD(void *, (((uint8_t *)t156) + 0));
        void * t158 = (void *)(((uint8_t *)t157) + 8);
        gm_t88rcucg1b7uk t159 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t710lsrq1m8gk t160;
        memset((uint8_t *)&t160 + 4, 0, 4);
        uint32_t t161 = gm_l18;
        GM_ST(uint32_t, (uint8_t *)&t160 + 0, t161);
        gm_b16_8 t162 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t160 + 8, t162);
        void * t163 = gm_l23;
        GM_ST(void *, (uint8_t *)&t160 + 24, t163);
        void * t164 = gm_l5;
        GM_ST(void *, (uint8_t *)&t160 + 32, t164);
        gm_teti411eujvf4 t165;
        GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t165 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t165 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)48ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t165 + 8), t160);
        gm_tl874t2atnlh3 t166 = gm_l6;
        void * t167 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t159, &t165, t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t168 = 0;
        F.ret = t167;
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
    GM_STACK_CHECK(&F, 2203u);
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
        gm_tftb1odc739t5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srclfnv05vuf0b8, 230, 5);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l11);
        uint32_t t26 = gm_l8;
        void * t27 = gm_l3;
        gm_ttggq7ng4kp3p t28;
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t26);
        GM_ST(void *, ((uint8_t *)&t28 + 8), t27);
        gm_trcmmd12cefb1 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t29 + 8), t28);
        gm_tftb1odc739t5 t30 = gm_f_push_d6c76ud_2(t25, &t29);
        gm_tftb1odc739t5 t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srclfnv05vuf0b8, 231, 5);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    {
        uint8_t t34 = gm_l5;
        uint8_t t35 = (uint8_t)!t34;
        if (!t35) goto ie33;
        {
            void * t37 = (void *)((uint8_t *)&F.l11);
            void * t38 = gm_l0;
            void * t39 = gm_l1;
            gm_t88rcucg1b7uk t40 = gm_l4;
            uint32_t t41 = gm_l9;
            gm_t88rcucg1b7uk t42 = gm_l4;
            gm_closure t43 = gm_l10;
            uint32_t t44 = gm_l8;
            gm_t88rcucg1b7uk t45 = gm_l4;
            gm_closure t46 = t43;
            gm_node *ccx47 = t46.cx;
            gm_node cn48; { gm_node *f = gm_find(cx, 587034433); if (f) { cn48 = *f; cn48.next = ccx47; ccx47 = &cn48; } }
            void * t49 = t46.code;
            void * t50;
            if (GM_PTR_TAGGED(t49)) {
                t50 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t49))(t44, GM_CONV(gm_csguankko4lmhd, t45));
            } else {
                t50 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t49)(t46.env, ccx47, NULL, t44, t45);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_closure t51 = gm_l10;
            uint32_t t52 = gm_l9;
            gm_t88rcucg1b7uk t53 = gm_l4;
            gm_closure t54 = t51;
            gm_node *ccx55 = t54.cx;
            gm_node cn56; { gm_node *f = gm_find(cx, 587034433); if (f) { cn56 = *f; cn56.next = ccx55; ccx55 = &cn56; } }
            void * t57 = t54.code;
            void * t58;
            if (GM_PTR_TAGGED(t57)) {
                t58 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t57))(t52, GM_CONV(gm_csguankko4lmhd, t53));
            } else {
                t58 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t57)(t54.env, ccx55, NULL, t52, t53);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t59 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t38, t39, t40, t41, t42, t50, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t60 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t60 + 8), t59);
            gm_tftb1odc739t5 t61 = gm_f_push_d6c76ud_2(t37, &t60);
            gm_tftb1odc739t5 t62 = t61;
            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srclfnv05vuf0b8, 233, 9);
            gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
        }
        {
            void * t64 = gm_l0;
            void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            gm_tl874t2atnlh3 t67 = F.l6;
            void * t68 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t65, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t70 = gm_l0;
        gm_t88rcucg1b7uk t71 = gm_l7;
        gm_tl874t2atnlh3 t72 = F.l6;
        uint32_t t73 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t70, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t73;
    }
    {
        void * t74 = (void *)((uint8_t *)&F.l11);
        uint32_t t75 = gm_l12;
        gm_t88rcucg1b7uk t76 = gm_l7;
        gm_trl9d7m1k5lbd t77;
        memset((uint8_t *)&t77 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)11ULL));
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t77 + 0, t78);
        gm_closure t79 = gm_l10;
        uint32_t t80 = gm_l9;
        gm_t88rcucg1b7uk t81 = gm_l4;
        gm_closure t82 = t79;
        gm_node *ccx83 = t82.cx;
        gm_node cn84; { gm_node *f = gm_find(cx, 587034433); if (f) { cn84 = *f; cn84.next = ccx83; ccx83 = &cn84; } }
        void * t85 = t82.code;
        void * t86;
        if (GM_PTR_TAGGED(t85)) {
            t86 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t85))(t80, GM_CONV(gm_csguankko4lmhd, t81));
        } else {
            t86 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t85)(t82.env, ccx83, NULL, t80, t81);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t77 + 8, t86);
        gm_closure t87 = gm_l10;
        uint32_t t88 = gm_l8;
        gm_t88rcucg1b7uk t89 = gm_l4;
        gm_closure t90 = t87;
        gm_node *ccx91 = t90.cx;
        gm_node cn92; { gm_node *f = gm_find(cx, 587034433); if (f) { cn92 = *f; cn92.next = ccx91; ccx91 = &cn92; } }
        void * t93 = t90.code;
        void * t94;
        if (GM_PTR_TAGGED(t93)) {
            t94 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t93))(t88, GM_CONV(gm_csguankko4lmhd, t89));
        } else {
            t94 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t93)(t90.env, ccx91, NULL, t88, t89);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t77 + 16, t94);
        gm_teti411eujvf4 t95;
        memset(&t95, 0, sizeof t95);
        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)30ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t95 + 8), t77);
        gm_tl874t2atnlh3 t96 = F.l6;
        void * t97 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t76, &t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t98;
        GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t98 + 0), t75);
        GM_ST(void *, ((uint8_t *)&t98 + 8), t97);
        gm_trcmmd12cefb1 t99;
        GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t99 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t99 + 8), t98);
        gm_tftb1odc739t5 t100 = gm_f_push_d6c76ud_2(t74, &t99);
        gm_tftb1odc739t5 t101 = t100;
        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srclfnv05vuf0b8, 237, 5);
        gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
    }
    {
        gm_t88rcucg1b7uk t103 = gm_l7;
        gm_tlcl4u558l6p5 t104;
        memset((uint8_t *)&t104 + 17, 0, 7);
        gm_closure t105 = gm_l10;
        uint32_t t106 = gm_l9;
        gm_t88rcucg1b7uk t107 = gm_l4;
        gm_closure t108 = t105;
        gm_node *ccx109 = t108.cx;
        gm_node cn110; { gm_node *f = gm_find(cx, 587034433); if (f) { cn110 = *f; cn110.next = ccx109; ccx109 = &cn110; } }
        void * t111 = t108.code;
        void * t112;
        if (GM_PTR_TAGGED(t111)) {
            t112 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t111))(t106, GM_CONV(gm_csguankko4lmhd, t107));
        } else {
            t112 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t111)(t108.env, ccx109, NULL, t106, t107);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t104 + 0, t112);
        gm_closure t113 = gm_l10;
        uint32_t t114 = gm_l8;
        gm_t88rcucg1b7uk t115 = gm_l4;
        gm_closure t116 = t113;
        gm_node *ccx117 = t116.cx;
        gm_node cn118; { gm_node *f = gm_find(cx, 587034433); if (f) { cn118 = *f; cn118.next = ccx117; ccx117 = &cn118; } }
        void * t119 = t116.code;
        void * t120;
        if (GM_PTR_TAGGED(t119)) {
            t120 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t119))(t114, GM_CONV(gm_csguankko4lmhd, t115));
        } else {
            t120 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t119)(t116.env, ccx117, NULL, t114, t115);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t104 + 8, t120);
        GM_ST(uint8_t, (uint8_t *)&t104 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t121;
        memset(&t121, 0, sizeof t121);
        GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)43ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t121 + 8), t104);
        gm_tl874t2atnlh3 t122 = F.l6;
        void * t123 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t103, &t121, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t123;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t124 = (void *)((uint8_t *)&F.l14);
        gm_closure t125 = gm_l10;
        uint32_t t126 = gm_l12;
        gm_t88rcucg1b7uk t127 = gm_l7;
        gm_closure t128 = t125;
        gm_node *ccx129 = t128.cx;
        gm_node cn130; { gm_node *f = gm_find(cx, 587034433); if (f) { cn130 = *f; cn130.next = ccx129; ccx129 = &cn130; } }
        void * t131 = t128.code;
        void * t132;
        if (GM_PTR_TAGGED(t131)) {
            t132 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t131))(t126, GM_CONV(gm_csguankko4lmhd, t127));
        } else {
            t132 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t131)(t128.env, ccx129, NULL, t126, t127);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t133 = gm_l7;
        gm_teti411eujvf4 t134;
        memset(&t134, 0, sizeof t134);
        GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t134 + 8), ((uint8_t)0));
        gm_tl874t2atnlh3 t135 = F.l6;
        void * t136 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t133, &t134, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t137;
        GM_ST(void *, ((uint8_t *)&t137 + 0), t132);
        GM_ST(void *, ((uint8_t *)&t137 + 8), t136);
        gm_trcmmd12cefb1 t138;
        GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t138 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t138 + 8), t137);
        gm_tftb1odc739t5 t139 = gm_f_push_d6c76ud_2(t124, &t138);
        gm_tftb1odc739t5 t140 = t139;
        if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_srclfnv05vuf0b8, 240, 5);
        gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t142 = (void *)((uint8_t *)&F.l15);
        uint32_t t143 = gm_l9;
        gm_t88rcucg1b7uk t144 = gm_l4;
        gm_tl874t2atnlh3 t145 = F.l6;
        gm_trcmmd12cefb1 t146;
        gm_f_increment_12n73t5((void *)&t146, (void *)NULL, cx, NULL, t143, t144, t145);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t147 = gm_f_push_d6c76ud_2(t142, &t146);
        gm_tftb1odc739t5 t148 = t147;
        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 1, gm_srclfnv05vuf0b8, 242, 5);
        gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 1));
    }
    {
        gm_f_list_v5d1o9b_2((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t150 = (void *)((uint8_t *)&F.l16);
        void * t151 = gm_l13;
        gm_tlr9dp305htm7 t152;
        GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t152 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t152 + 8), t151);
        gm_tftb1odc739t5 t153 = gm_f_push_eb7kbhp_2(t150, &t152);
        gm_tftb1odc739t5 t154 = t153;
        if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srclfnv05vuf0b8, 244, 5);
        gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
    }
    {
        void * t156 = gm_l0;
        void * t157 = GM_LD(void *, (((uint8_t *)t156) + 0));
        void * t158 = (void *)(((uint8_t *)t157) + 8);
        gm_t88rcucg1b7uk t159 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trl2khmuphncd t160;
        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t160 + 0, t161);
        void * t162 = gm_l0;
        void * t163 = GM_LD(void *, (((uint8_t *)t162) + 0));
        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_tl874t2atnlh3 t165 = F.l6;
        void * t166 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t163, t164, t165);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t160 + 16, t166);
        void * t167 = gm_l0;
        void * t168 = GM_LD(void *, (((uint8_t *)t167) + 0));
        gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        gm_tl874t2atnlh3 t170 = F.l6;
        void * t171 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t168, t169, t170);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t160 + 24, t171);
        gm_teti411eujvf4 t172;
        GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t172 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t172 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t172 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)47ULL));
        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t172 + 8), t160);
        gm_tl874t2atnlh3 t173 = F.l6;
        void * t174 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t159, &t172, t173);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t174;
    }
    {
        gm_f_list_v5d1o9b_2((void *)&F.l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t175 = (void *)((uint8_t *)&F.l18);
        gm_closure t176 = gm_l10;
        uint32_t t177 = gm_l12;
        gm_t88rcucg1b7uk t178 = gm_l7;
        gm_closure t179 = t176;
        gm_node *ccx180 = t179.cx;
        gm_node cn181; { gm_node *f = gm_find(cx, 587034433); if (f) { cn181 = *f; cn181.next = ccx180; ccx180 = &cn181; } }
        void * t182 = t179.code;
        void * t183;
        if (GM_PTR_TAGGED(t182)) {
            t183 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t182))(t177, GM_CONV(gm_csguankko4lmhd, t178));
        } else {
            t183 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t182)(t179.env, ccx180, NULL, t177, t178);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlr9dp305htm7 t184;
        GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t184 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t184 + 8), t183);
        gm_tftb1odc739t5 t185 = gm_f_push_eb7kbhp_2(t175, &t184);
        gm_tftb1odc739t5 t186 = t185;
        if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_srclfnv05vuf0b8, 247, 5);
        gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
    }
    {
        void * t188 = gm_l0;
        void * t189 = gm_l1;
        gm_t88rcucg1b7uk t190 = gm_l4;
        gm_closure t191 = gm_l10;
        uint32_t t192 = gm_l9;
        gm_t88rcucg1b7uk t193 = gm_l4;
        gm_closure t194 = t191;
        gm_node *ccx195 = t194.cx;
        gm_node cn196; { gm_node *f = gm_find(cx, 587034433); if (f) { cn196 = *f; cn196.next = ccx195; ccx195 = &cn196; } }
        void * t197 = t194.code;
        void * t198;
        if (GM_PTR_TAGGED(t197)) {
            t198 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t197))(t192, GM_CONV(gm_csguankko4lmhd, t193));
        } else {
            t198 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t197)(t194.env, ccx195, NULL, t192, t193);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        void * t200 = gm_l17;
        void * t201 = gm_f_loop_with_pattern_d8vl0mj((void *)NULL, cx, NULL, t188, t189, t190, t198, t199, t200);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t201;
    }
    {
        void * t202 = (void *)((uint8_t *)&F.l11);
        void * t203 = gm_l19;
        gm_trcmmd12cefb1 t204;
        GM_ST(uint8_t, (uint8_t *)&t204 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t204 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t204 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t204 + 8), t203);
        gm_tftb1odc739t5 t205 = gm_f_push_d6c76ud_2(t202, &t204);
        gm_tftb1odc739t5 t206 = t205;
        if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 1, gm_srclfnv05vuf0b8, 249, 5);
        gm_b0_1 t207 = GM_LD(gm_b0_1, (((uint8_t *)&t206) + 1));
    }
    {
        void * t208 = gm_l0;
        void * t209 = GM_LD(void *, (((uint8_t *)t208) + 0));
        gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_tl874t2atnlh3 t211 = F.l6;
        void * t212 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t209, t210, t211);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t213 = 0;
        F.ret = t212;
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
    GM_STACK_CHECK(&F, 942u);
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
        gm_tftb1odc739t5 t67 = t66;
        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srclfnv05vuf0b8, 183, 5);
        gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
    }
    {
        void * t69 = (void *)((uint8_t *)&F.l14);
        gm_b16_8 t70 = gm_l2;
        gm_tftb1odc739t5 t71 = gm_f_extend_nkql463(t69, t70);
        gm_tftb1odc739t5 t72 = t71;
        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srclfnv05vuf0b8, 184, 5);
        gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
    }
    {
        gm_t88rcucg1b7uk t74 = gm_l9;
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t76 = gm_l13;
        gm_tl874t2atnlh3 t77 = F.l8;
        void * t78 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t74, t75, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t79 = 0;
        F.ret = t78;
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
    GM_STACK_CHECK(&F, 808u);
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
                        gm_t88rcucg1b7uk t48 = t46;
                        gm_t88rcucg1b7uk t49 = t47;
                        uint8_t t50 = (!gm_eq9m6mvtmgde(((uint8_t *)&t48), ((uint8_t *)&t49)));
                        if (!t50) goto ie45;
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
                    uint64_t t53 = gm_l6;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l6 = t56;
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
        uint8_t t58 = gm_l4;
        uint8_t t59 = (uint8_t)!t58;
        if (!t59) goto ie57;
        {
            gm_tkn42u7na3t81 t61;
            memset(&t61, 0, sizeof t61);
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        void * t63 = gm_l0;
        void * t64 = (void *)(((uint8_t *)t63) + 96);
        void * t65 = gm_l0;
        void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
        void * t67 = gm_l0;
        gm_b16_8 t68 = gm_l1;
        gm_t88rcucg1b7uk t69 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t70 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t64, t66, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t70;
    }
    {
        {
            gm_b16_8 t72 = gm_l1;
            gm_l10 = t72;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t74 = gm_l11;
                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t76 = (t74 < t75);
                if (!t76) goto lb73;
                {
                    uint64_t t79 = gm_l11;
                    uint64_t t80 = (uintptr_t)t79;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t80 * 4));
                    gm_l12 = t84;
                    {
                        void * t86 = gm_l0;
                        void * t87 = (void *)(((uint8_t *)t86) + 96);
                        void * t88 = gm_l0;
                        void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
                        gm_t88rcucg1b7uk t90 = gm_l12;
                        gm_t88rcucg1b7uk t91 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t87, t89, t90);
                        if (gm_unw.kind) {
                            goto gm_ll77;
                        }
                        gm_l13 = t91;
                    }
                    {
                        gm_t88rcucg1b7uk t93 = gm_l13;
                        gm_t88rcucg1b7uk t94 = gm_l9;
                        gm_t88rcucg1b7uk t95 = t93;
                        gm_t88rcucg1b7uk t96 = t94;
                        uint8_t t97 = (!gm_eq9m6mvtmgde(((uint8_t *)&t95), ((uint8_t *)&t96)));
                        if (!t97) goto ie92;
                        void * t98 = gm_l0;
                        gm_t88rcucg1b7uk t99 = gm_l13;
                        gm_t88rcucg1b7uk t100 = gm_l9;
                        gm_th2lqbo77pvsc t101;
                        gm_f_coercion_steps_ibqvhta((void *)&t101, (void *)NULL, cx, NULL, t98, t99, t100);
                        if (gm_unw.kind) {
                            goto gm_ll77;
                        }
                        gm_th2lqbo77pvsc t102 = t101;
                        uint8_t t103;
                        if (((((uint8_t *)&t102))[16] != 0)) goto mn105;
                        t103 = ((uint8_t)1);
                        goto me104;
mn105:;
                        t103 = ((uint8_t)0);
                        goto me104;
mn106:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 169, 32);
me104:;
                        if (!t103) goto ie92;
                        uint8_t t107 = 0;
                        void * t108 = gm_l2;
                        gm_t88rcucg1b7uk t109 = gm_l13;
                        uint8_t t110 = gm_f_is_positional_array_6sufsk8(t108, GM_CONV(gm_csguankko4lmhd, t109));
                        if (t110) {
                            void * t111 = gm_l2;
                            gm_t88rcucg1b7uk t112 = gm_l9;
                            gm_t53at80pfdus3 t113;
                            gm_f_kind_jcnt4hm((void *)&t113, t111, GM_CONV(gm_csguankko4lmhd, t112));
                            gm_t53at80pfdus3 t114 = t113;
                            uint8_t t115;
                            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t114) + 0));
                            if (t118 != ((uint8_t)10ULL)) goto mn117;
                            t115 = ((uint8_t)1);
                            goto me116;
mn117:;
                            t115 = ((uint8_t)0);
                            goto me116;
mn119:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 169, 109);
me116:;
                            t107 = t115;
                        } else t107 = 0;
                        uint8_t t120 = (uint8_t)!t107;
                        if (!t120) goto ie92;
                        {
                            gm_tkn42u7na3t81 t122;
                            memset(&t122, 0, sizeof t122);
                            uint8_t t123 = 0;
                            F.ret = t122;
                            goto gm_fret;
                        }
                        goto ix92;
ie92:;
ix92:;
                    }
                    goto me82;
mn83:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 167, 5);
me82:;
                }
lc73:;
                {
                    uint64_t t125 = gm_l11;
                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                    uint64_t t128 = r126;
                    gm_l11 = t128;
                }
            }
gm_ll77:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb73; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc73; }
            }
            goto gm_lad;
lb73:;
        }
    }
    {
        gm_t88rcucg1b7uk t129 = gm_l9;
        gm_tkn42u7na3t81 t130;
        GM_ST(uint8_t, (uint8_t *)&t130 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t130 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t130), t129);
        ((uint8_t *)&t130)[4] = 1;
        uint8_t t131 = 0;
        F.ret = t130;
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

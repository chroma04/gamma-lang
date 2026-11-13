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
__attribute__((aligned(4))) gm_b16_8 gm_f_args_6h3q549(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_exit_jfn9nl4(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8edr2np(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g88hj6k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_kocuepv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_l1ll8kh(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_var_otupsib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c08nj74isec[14] __attribute__((aligned(1)));
extern uint8_t gm_c1m49utf476[4] __attribute__((aligned(1)));
extern uint8_t gm_c1tvtibs17m[11] __attribute__((aligned(1)));
extern uint8_t gm_c23qivcb2qk[16] __attribute__((aligned(8)));
extern uint8_t gm_c2djvocabvl[16] __attribute__((aligned(1)));
extern uint8_t gm_c2pgnmr38k4[4] __attribute__((aligned(1)));
extern uint8_t gm_c3seud9n9be[1] __attribute__((aligned(1)));
extern uint8_t gm_c4h6cr14ilf[11] __attribute__((aligned(1)));
extern uint8_t gm_c4i68jc4bv0[16] __attribute__((aligned(8)));
extern uint8_t gm_c5d0d4t81rg[7] __attribute__((aligned(1)));
extern uint8_t gm_c5llbmao9o5[3] __attribute__((aligned(1)));
extern uint8_t gm_c62sev3r509[1] __attribute__((aligned(1)));
extern uint8_t gm_c7167246mac[5] __attribute__((aligned(1)));
extern uint8_t gm_c72j1g8d455[1] __attribute__((aligned(1)));
extern uint8_t gm_c7k2o7e3uv7[3] __attribute__((aligned(1)));
extern uint8_t gm_c84n42e55fh[14] __attribute__((aligned(1)));
extern uint8_t gm_c8lries3c2a[1442] __attribute__((aligned(1)));
extern uint8_t gm_c8tck9qi6pl[16] __attribute__((aligned(8)));
extern uint8_t gm_c90nlp6169j[1] __attribute__((aligned(1)));
extern uint8_t gm_c9smt58mcql[5] __attribute__((aligned(1)));
extern uint8_t gm_casq7h2b03a[12] __attribute__((aligned(1)));
extern uint8_t gm_cavtdnb9dpe[15] __attribute__((aligned(1)));
extern uint8_t gm_cdgbrlsjvrb[16] __attribute__((aligned(8)));
extern uint8_t gm_cdi7ltupqm6[10] __attribute__((aligned(1)));
extern uint8_t gm_ce4024rufik[15] __attribute__((aligned(1)));
extern uint8_t gm_cesm14gq4bu[7] __attribute__((aligned(1)));
extern uint8_t gm_cf1srcc1v0o[11] __attribute__((aligned(1)));
extern uint8_t gm_cfhcj0skuhf[18] __attribute__((aligned(1)));
extern uint8_t gm_cfj2cvp0vvo[14] __attribute__((aligned(1)));
extern uint8_t gm_cg2gm9ndqso[4] __attribute__((aligned(1)));
extern uint8_t gm_cg9qjv4r84d[12] __attribute__((aligned(1)));
extern uint8_t gm_cga37fdnf9v[11] __attribute__((aligned(1)));
extern uint8_t gm_cgelusdvikr[150] __attribute__((aligned(1)));
extern uint8_t gm_ch037afqg9e[8] __attribute__((aligned(1)));
extern uint8_t gm_ci3ce1h10ud[41] __attribute__((aligned(1)));
extern uint8_t gm_cj6ujl6k5d4[13] __attribute__((aligned(1)));
extern uint8_t gm_cjcr2ffc4p4[7] __attribute__((aligned(1)));
extern uint8_t gm_cjvio2kihdk[3] __attribute__((aligned(1)));
extern uint8_t gm_ck18kd1aefa[14] __attribute__((aligned(1)));
extern uint8_t gm_ckl5vopi62l[15] __attribute__((aligned(1)));
extern uint8_t gm_ckrhi684euh[87] __attribute__((aligned(1)));
extern uint8_t gm_cmr76t2omsb[10] __attribute__((aligned(1)));
extern uint8_t gm_cnpqnha93in[8] __attribute__((aligned(1)));
extern uint8_t gm_cnsqull7v4u[13] __attribute__((aligned(1)));
extern uint8_t gm_cqphq1ertk0[26] __attribute__((aligned(1)));
extern uint8_t gm_cr2tjd0cqon[16] __attribute__((aligned(1)));
extern uint8_t gm_crh91qok27k[45] __attribute__((aligned(1)));
extern uint8_t gm_crmb99jt6u6[11] __attribute__((aligned(1)));
extern uint8_t gm_crmiom2fklj[92] __attribute__((aligned(1)));
extern uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
extern uint8_t gm_cta94eldhtr[24] __attribute__((aligned(1)));
extern uint8_t gm_cugnnent2c3[11] __attribute__((aligned(1)));
extern uint8_t gm_cui00p62je5[7] __attribute__((aligned(1)));
extern uint8_t gm_cuk7s23am23[4] __attribute__((aligned(1)));
extern uint8_t gm_cvjv0vg9s4c[7] __attribute__((aligned(1)));
extern void *const gm_hlcpgr8cgvbp[];
extern void *const gm_hlfkkokngdk1[];
extern void *const gm_hlinnrcqopuj[];
extern void *const gm_hln6al4vijjv[];
extern void *const gm_hlnhfkgd9bs8[];
extern uint8_t gm_k0gn7sac5pt[40] __attribute__((aligned(8)));
extern uint8_t gm_k3ho3jie8mm[40] __attribute__((aligned(8)));
extern uint8_t gm_k3pvi109nh9[40] __attribute__((aligned(8)));
extern uint8_t gm_k4hgfv8dkha[40] __attribute__((aligned(8)));
extern uint8_t gm_kcu3lrib6j4[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmasfkh2un[40] __attribute__((aligned(8)));
extern uint8_t gm_kdoa98a1tm0[40] __attribute__((aligned(8)));
extern uint8_t gm_khvdju4mt3n[40] __attribute__((aligned(8)));
extern uint8_t gm_kkpcsaqkj4k[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5b02juaoa[40] __attribute__((aligned(8)));
extern uint8_t gm_klsha2ujn8u[40] __attribute__((aligned(8)));
extern uint8_t gm_kp550dhuma7[40] __attribute__((aligned(8)));
extern uint8_t gm_kr85ffi32os[24] __attribute__((aligned(8)));
extern uint8_t gm_krjmevl5bkc[40] __attribute__((aligned(8)));
extern uint8_t gm_krr4pbvd2tj[40] __attribute__((aligned(8)));
extern uint8_t gm_ksdcjqjfqsj[40] __attribute__((aligned(8)));
extern uint8_t gm_ksrlu8s6v53[40] __attribute__((aligned(8)));
extern uint8_t gm_ktbnd3fuoao[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02ktme6orfkh[7];
extern const uint8_t gm_s041tbii048j3[8];
extern const uint8_t gm_s06mk2gjnlvf8[73];
extern const uint8_t gm_s06sbklusedgl[5];
extern const uint8_t gm_s08sdh8q6l3vj[49];
extern const uint8_t gm_s091o161s6kba[5];
extern const uint8_t gm_s0eemp7of8buc[3];
extern const uint8_t gm_s0eqrtq5mqb3a[16];
extern const uint8_t gm_s0ffe093dar6e[19];
extern const uint8_t gm_s0iid7nkkvf30[55];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0ljc8tssh8f7[8];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0mppcr6a4lgb[9];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0sg56e5fp4di[12];
extern const uint8_t gm_s0stes97dk54i[147];
extern const uint8_t gm_s10qn2lc8ck56[78];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bf3a5i0lcmm[21];
extern const uint8_t gm_s1btmm2esmgsd[27];
extern const uint8_t gm_s1dmrsprfdp53[8];
extern const uint8_t gm_s1gm12po6spua[18];
extern const uint8_t gm_s1in79aaeui99[10];
extern const uint8_t gm_s1qrqfh656g4q[6];
extern const uint8_t gm_s1sspofc2u3bb[37];
extern const uint8_t gm_s20tdu9mtc6fi[8];
extern const uint8_t gm_s22cd6dihn1b9[11];
extern const uint8_t gm_s2c267emk2nni[6];
extern const uint8_t gm_s2c5ns5u9qs90[35];
extern const uint8_t gm_s2cartb32o2of[10];
extern const uint8_t gm_s2gpk5q7mg4o8[5];
extern const uint8_t gm_s2h8cr50573go[8];
extern const uint8_t gm_s2hgdf6b9d53s[5];
extern const uint8_t gm_s2micd2hub1nf[26];
extern const uint8_t gm_s2n0v309tod60[70];
extern const uint8_t gm_s2n3d0ogcjggg[14];
extern const uint8_t gm_s2qp8mqi0c2le[48];
extern const uint8_t gm_s2r7keqsravo0[6];
extern const uint8_t gm_s2taci5crof3b[16];
extern const uint8_t gm_s2ttad1cg71bm[18];
extern const uint8_t gm_s2ttvhm3g7b1o[30];
extern const uint8_t gm_s30cnsbbn0c4o[4];
extern const uint8_t gm_s31holt846m75[10];
extern const uint8_t gm_s35hbb2rhhdvv[33];
extern const uint8_t gm_s3adv1e9meuc1[15];
extern const uint8_t gm_s3e0g1895m0j9[2];
extern const uint8_t gm_s3ergh2il1tjf[43];
extern const uint8_t gm_s3hersi16nhbp[34];
extern const uint8_t gm_s3i3n5bvpvmmi[16];
extern const uint8_t gm_s3ijb9gbhdbib[27];
extern const uint8_t gm_s3j69epdb3egk[26];
extern const uint8_t gm_s3kb1co7e63ka[37];
extern const uint8_t gm_s3lv7dnoanjke[5];
extern const uint8_t gm_s3o37fp7c0sqt[15];
extern const uint8_t gm_s3o8dsbklmfsh[10];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3s4n6ehcrs2b[17];
extern const uint8_t gm_s3tacjdl2ciei[13];
extern const uint8_t gm_s41047ga9d9tq[26];
extern const uint8_t gm_s42aeamnkrchf[31];
extern const uint8_t gm_s43ls0s4m665j[57];
extern const uint8_t gm_s45dibb0c9ub8[41];
extern const uint8_t gm_s45i8231l5fma[10];
extern const uint8_t gm_s465vmg9c4jq6[14];
extern const uint8_t gm_s46khuklhbvqd[21];
extern const uint8_t gm_s4a6o0t8isvlm[17];
extern const uint8_t gm_s4ft5s33591qg[7];
extern const uint8_t gm_s4ko774bb2ea9[56];
extern const uint8_t gm_s4l676io35g5u[12];
extern const uint8_t gm_s4pucugl3svdm[11];
extern const uint8_t gm_s4vqnsfknimfg[28];
extern const uint8_t gm_s52ul4dg63jhe[11];
extern const uint8_t gm_s56n05s0kn0p9[27];
extern const uint8_t gm_s58g91v8ecst5[13];
extern const uint8_t gm_s5974q1kmorlc[52];
extern const uint8_t gm_s59st1i1tm2m1[9];
extern const uint8_t gm_s5f9kvb1gm4j6[13];
extern const uint8_t gm_s5g1e6qrps9q6[19];
extern const uint8_t gm_s5irlrsnb30cc[24];
extern const uint8_t gm_s5j8t1cg2erp9[8];
extern const uint8_t gm_s5k9ll19r7m4g[4];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5nt88lj0viqk[4];
extern const uint8_t gm_s5pm13tivcrur[15];
extern const uint8_t gm_s5rb1e1n8ivr7[48];
extern const uint8_t gm_s62t7qmf3ajh5[69];
extern const uint8_t gm_s65ftmvtj458k[11];
extern const uint8_t gm_s69o347hkiq9a[61];
extern const uint8_t gm_s6d57grm686e3[27];
extern const uint8_t gm_s6h2ge6vgkmhm[9];
extern const uint8_t gm_s6hgf366ivqrg[34];
extern const uint8_t gm_s6hp19vgdsi0o[24];
extern const uint8_t gm_s6kq1mi4s1g58[3];
extern const uint8_t gm_s6p5i58jsib94[8];
extern const uint8_t gm_s6p6u3ogotq4f[50];
extern const uint8_t gm_s6pceda0js0g8[6];
extern const uint8_t gm_s6peoato8pau5[13];
extern const uint8_t gm_s6pilvbjelm5p[4];
extern const uint8_t gm_s6v6kgo7d00tn[41];
extern const uint8_t gm_s6vl5jv0tf2n4[8];
extern const uint8_t gm_s709tdhiijp4r[78];
extern const uint8_t gm_s76i8mdsri6em[70];
extern const uint8_t gm_s786i8duem00q[27];
extern const uint8_t gm_s7c2mvkr8kl49[35];
extern const uint8_t gm_s7cvig9qav6l5[3];
extern const uint8_t gm_s7i2mni64durc[18];
extern const uint8_t gm_s7mosjq6fpdqc[15];
extern const uint8_t gm_s7mqv7hf4fso0[27];
extern const uint8_t gm_s7phrfovrc6ul[239];
extern const uint8_t gm_s7vusn2lig3dp[65];
extern const uint8_t gm_s802sbfa6mtsg[27];
extern const uint8_t gm_s85hqf14lbkns[8];
extern const uint8_t gm_s87e746eia9j4[6];
extern const uint8_t gm_s8aj3oag1l1u0[14];
extern const uint8_t gm_s8b49h6svih4r[82];
extern const uint8_t gm_s8ckgohq4msba[19];
extern const uint8_t gm_s8cr606ter6p9[39];
extern const uint8_t gm_s8gaig0cs2uce[19];
extern const uint8_t gm_s8i44sm1b77f2[121];
extern const uint8_t gm_s8ki76moa277n[23];
extern const uint8_t gm_s8m45dpj5ugr4[6];
extern const uint8_t gm_s8pjstf2pdmbk[14];
extern const uint8_t gm_s8ptfv88uhmsa[41];
extern const uint8_t gm_s8qvuobg6jcbo[13];
extern const uint8_t gm_s8rsp9val5ful[5];
extern const uint8_t gm_s8ukphi4920cc[11];
extern const uint8_t gm_s92hgp9htn7k0[21];
extern const uint8_t gm_s930a5k4dd69o[10];
extern const uint8_t gm_s94v9o49bjk09[12];
extern const uint8_t gm_s99lgt46tmvfc[14];
extern const uint8_t gm_s99ra887gkc8u[39];
extern const uint8_t gm_s9dtt8vrvpd1h[16];
extern const uint8_t gm_s9eiod3sq367m[7];
extern const uint8_t gm_s9go9vvkpcjpd[48];
extern const uint8_t gm_s9grefqkso86a[3];
extern const uint8_t gm_s9loa14tklecu[3];
extern const uint8_t gm_s9tlur6oo59qa[31];
extern const uint8_t gm_s9vag5co36u2r[18];
extern const uint8_t gm_sa1pcp92k3aj3[7];
extern const uint8_t gm_sa39b9bt4jpna[36];
extern const uint8_t gm_sa9mckopi2o5a[86];
extern const uint8_t gm_saefgd8cv1aea[20];
extern const uint8_t gm_sajpbdgkjnse7[8];
extern const uint8_t gm_sal1kdi1rij5t[10];
extern const uint8_t gm_saoa4m7dh2vl3[55];
extern const uint8_t gm_sapu6l8enc7d7[7];
extern const uint8_t gm_sauknalgur9no[11];
extern const uint8_t gm_sav0q47tkm7j2[20];
extern const uint8_t gm_savlppb39i8g6[6];
extern const uint8_t gm_savvoucn0tcn5[41];
extern const uint8_t gm_sb1nc8gh4as5h[15];
extern const uint8_t gm_sb335j99bk4ch[41];
extern const uint8_t gm_sb3c9ehk423nf[6];
extern const uint8_t gm_sb3o3ij0n2htb[5];
extern const uint8_t gm_sb455a3lakuom[60];
extern const uint8_t gm_sb8ag7vcv52jt[5];
extern const uint8_t gm_sb8c0irhu65ra[140];
extern const uint8_t gm_sb8vbka031tie[21];
extern const uint8_t gm_sbajlb7u95nhh[10];
extern const uint8_t gm_sbe7f0bkvca75[43];
extern const uint8_t gm_sbec11r2rv4ti[23];
extern const uint8_t gm_sbkmg1rlq4fa7[27];
extern const uint8_t gm_sbm4560lv1lt5[25];
extern const uint8_t gm_sbvdvkd7bikd7[25];
extern const uint8_t gm_sc0brfbvonphe[64];
extern const uint8_t gm_sc3mvoa74bm5n[41];
extern const uint8_t gm_sc4daeflaltnv[3];
extern const uint8_t gm_sc4i6tq8tg5gu[13];
extern const uint8_t gm_sc4r4l9k6f577[3];
extern const uint8_t gm_sc5ekkhtqttfb[4];
extern const uint8_t gm_sc80lo6tf645h[31];
extern const uint8_t gm_sc88juptr3rgf[31];
extern const uint8_t gm_sc9p2e0c29ni7[29];
extern const uint8_t gm_scak4selic6pl[21];
extern const uint8_t gm_scbrbhepkh2rj[25];
extern const uint8_t gm_scc6ms0hrfo8j[9];
extern const uint8_t gm_scf54410fgl4j[39];
extern const uint8_t gm_scfdk5i1nqc6a[30];
extern const uint8_t gm_scifaj4dn1ok5[6];
extern const uint8_t gm_scn0a8pnba0ho[41];
extern const uint8_t gm_scn0pl9eq3k2k[24];
extern const uint8_t gm_scne9acm49p4h[11];
extern const uint8_t gm_scommn379heqq[14];
extern const uint8_t gm_scq0i5n3f7it5[41];
extern const uint8_t gm_sct928cj9v78e[137];
extern const uint8_t gm_sd1gv97l675vb[26];
extern const uint8_t gm_sd2ht7fkpnftp[17];
extern const uint8_t gm_sd4k4nkk0h9t7[14];
extern const uint8_t gm_sd4sd2h1n94bh[7];
extern const uint8_t gm_sd8o4m7695hpq[38];
extern const uint8_t gm_sd99er7oafmtu[17];
extern const uint8_t gm_sd9o9rh5qe8e7[11];
extern const uint8_t gm_sdarhudj711nk[17];
extern const uint8_t gm_sdfl2t7s13ucr[12];
extern const uint8_t gm_sdg1ovmjdibb5[6];
extern const uint8_t gm_sdh3pk0b3jsnu[67];
extern const uint8_t gm_sdinre9qvo2vq[28];
extern const uint8_t gm_sdogehgrilpeb[3];
extern const uint8_t gm_sdqssko83l66b[28];
extern const uint8_t gm_sdrevll5io08q[10];
extern const uint8_t gm_sds80esh09gni[51];
extern const uint8_t gm_sdvtt0l0jh9bp[10];
extern const uint8_t gm_se08n5d1gngu7[5];
extern const uint8_t gm_se25mtccq2mtg[42];
extern const uint8_t gm_se5tulnsn3o9s[10];
extern const uint8_t gm_se8ggmq7h9bin[35];
extern const uint8_t gm_sebnh3ba276b8[17];
extern const uint8_t gm_seeo26ergtcha[5];
extern const uint8_t gm_seg7e1ca8e64l[5];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_segn0pv9sd905[41];
extern const uint8_t gm_segr993uh2crm[11];
extern const uint8_t gm_seh85cpcgte1u[57];
extern const uint8_t gm_seimuk2ck2a6g[38];
extern const uint8_t gm_seojbs1tqr33k[46];
extern const uint8_t gm_sf2lr85hprd3e[40];
extern const uint8_t gm_sf46mekb18qm8[14];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfau4tfjh7ajf[15];
extern const uint8_t gm_sfc071avthns7[21];
extern const uint8_t gm_sfcm41q17pr37[46];
extern const uint8_t gm_sffd375a90o1u[3];
extern const uint8_t gm_sffd7ppku35fr[10];
extern const uint8_t gm_sfkqvfbp56vqq[11];
extern const uint8_t gm_sfl0m0vifcu3m[82];
extern const uint8_t gm_sfvsgjcg86bbl[45];
extern const uint8_t gm_sg0ns322676oc[26];
extern const uint8_t gm_sg39dr0kaebm0[10];
extern const uint8_t gm_sg4faca6q9u0h[8];
extern const uint8_t gm_sg6b42o7q3v50[64];
extern const uint8_t gm_sg91clgj6l88m[6];
extern const uint8_t gm_sg92g0je3abps[14];
extern const uint8_t gm_sg93rj30beob9[29];
extern const uint8_t gm_sgb0oed9c3voo[5];
extern const uint8_t gm_sge7ihldgqi49[20];
extern const uint8_t gm_sge9mrih5euc5[13];
extern const uint8_t gm_sgekdmqbrecpp[9];
extern const uint8_t gm_sgf1euukkdve3[14];
extern const uint8_t gm_sgh9nkjgl73of[27];
extern const uint8_t gm_sgmjl48sjjf69[22];
extern const uint8_t gm_sgmt0q35kl6hc[5];
extern const uint8_t gm_sgqifqduuc5vl[46];
extern const uint8_t gm_sgqs124m28gfg[7];
extern const uint8_t gm_sgtmc08t3k49t[42];
extern const uint8_t gm_sh0adpq1lnkpj[40];
extern const uint8_t gm_sh0drhj4ffllf[11];
extern const uint8_t gm_sh0is7j09prt2[23];
extern const uint8_t gm_sh11qqm8nqu68[8];
extern const uint8_t gm_sh2bu6u8oapg5[73];
extern const uint8_t gm_sh43rpq1oqm5d[6];
extern const uint8_t gm_sh5cd76h4vacq[102];
extern const uint8_t gm_sh5f7b7njpj3l[6];
extern const uint8_t gm_sharnvllqpvms[17];
extern const uint8_t gm_shb921rk4rkpo[5];
extern const uint8_t gm_shf0g22rjm3o7[5];
extern const uint8_t gm_shhn92c601tgo[29];
extern const uint8_t gm_shl9ceh0q5tpe[6];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_shncvaenjonsu[41];
extern const uint8_t gm_shntnl6m6g3am[27];
extern const uint8_t gm_shpi1i86ollcb[59];
extern const uint8_t gm_shq77is85iktk[17];
extern const uint8_t gm_shroj9d6fajb6[4];
extern const uint8_t gm_shror0skne60i[45];
extern const uint8_t gm_shs874j97ior1[14];
extern const uint8_t gm_shsoptr61c2h5[41];
extern const uint8_t gm_shv5ofgc65v68[3];
extern const uint8_t gm_si8jv2q8d22kl[10];
extern const uint8_t gm_sia6bdhdcj5co[13];
extern const uint8_t gm_siabpto2siqmj[7];
extern const uint8_t gm_sidiiakhbvqqi[9];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sigjilgl161c6[5];
extern const uint8_t gm_sigmj6bktcbbh[80];
extern const uint8_t gm_sigq4ahgd0l2a[64];
extern const uint8_t gm_siihmb4jo3dsn[125];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sijp8lh2g2ld5[34];
extern const uint8_t gm_sipi37glknm5f[16];
extern const uint8_t gm_siuacbf0blrje[9];
extern const uint8_t gm_siv8n5ojlp0dd[18];
extern const uint8_t gm_sj2cfogjb0pbr[41];
extern const uint8_t gm_sj49v74ko3ghf[15];
extern const uint8_t gm_sj56dkk1peu3m[54];
extern const uint8_t gm_sj5j224m4ulf9[3];
extern const uint8_t gm_sj9kuut55po7f[46];
extern const uint8_t gm_sjbjet56e96co[17];
extern const uint8_t gm_sjdrosvte680q[12];
extern const uint8_t gm_sjg4ie5b5ufsm[8];
extern const uint8_t gm_sjiao13m1cclm[24];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjm54r6h90i7e[63];
extern const uint8_t gm_sjssolp3qfpqg[7];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sjtqic917nd2g[6];
extern const uint8_t gm_sk34jm61h5m0d[3];
extern const uint8_t gm_sk3i3et1jqa7c[12];
extern const uint8_t gm_sk531tvbsm11t[44];
extern const uint8_t gm_sk5om4a4762gd[88];
extern const uint8_t gm_sk721elh8ouv2[11];
extern const uint8_t gm_sk8hn2ar9njah[15];
extern const uint8_t gm_sk8r2qbdun3l0[35];
extern const uint8_t gm_skbd3tdpfkbk5[24];
extern const uint8_t gm_skedh3k15mgd3[18];
extern const uint8_t gm_skff6pclobvnu[40];
extern const uint8_t gm_skjg6jfphs2d0[10];
extern const uint8_t gm_skk0hh6be703h[20];
extern const uint8_t gm_skm5ng1ir37l8[8];
extern const uint8_t gm_skmai4uvseje8[24];
extern const uint8_t gm_skmfilbqoc8lo[87];
extern const uint8_t gm_skmueji31ds5r[49];
extern const uint8_t gm_skpueg68n121l[41];
extern const uint8_t gm_skq0s129a3p6p[41];
extern const uint8_t gm_skr00pujn28ln[29];
extern const uint8_t gm_sl2gfi3ik4b1u[11];
extern const uint8_t gm_sl58ekc3js5bs[30];
extern const uint8_t gm_sl6br25hfjb5f[9];
extern const uint8_t gm_sl84tf4h2e7v7[29];
extern const uint8_t gm_sla3unrbmgc0v[6];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_slcgg5l8i9236[50];
extern const uint8_t gm_sli291jkgotin[31];
extern const uint8_t gm_sljdfefkkeqgn[90];
extern const uint8_t gm_sljqt9ip2cpnb[41];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_sln88h001q3mp[19];
extern const uint8_t gm_slof5tl7m8grj[4];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_slpqve1dqssq0[8];
extern const uint8_t gm_slsa15rinti36[12];
extern const uint8_t gm_sm0105pgqu27v[17];
extern const uint8_t gm_sm57fhlj6aanb[62];
extern const uint8_t gm_sm5t84rdabed6[41];
extern const uint8_t gm_sm78jth420g98[9];
extern const uint8_t gm_smcv6j7b18cjr[17];
extern const uint8_t gm_smiavg3q7ucd9[8];
extern const uint8_t gm_smpb7l3te3i8n[52];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_smrla0cd2esrp[14];
extern const uint8_t gm_sn1m0t4k3vpa5[5];
extern const uint8_t gm_sn2qlgie5sklu[45];
extern const uint8_t gm_sn4e8vibva5m7[45];
extern const uint8_t gm_sn5pv7k63ni4c[11];
extern const uint8_t gm_sn5rcg6kpf9gq[36];
extern const uint8_t gm_sn9fg2capllh2[79];
extern const uint8_t gm_snauhge6bheis[14];
extern const uint8_t gm_snbkfu70dq3fp[33];
extern const uint8_t gm_sngn7trhgdnc2[43];
extern const uint8_t gm_snhr0nlm0epj5[60];
extern const uint8_t gm_snl0g0eaf7ift[8];
extern const uint8_t gm_sno069vrt5g9q[4];
extern const uint8_t gm_snq4uaiieftl8[6];
extern const uint8_t gm_sns2lv549pkqt[53];
extern const uint8_t gm_snsu3jrv1dbtj[32];
extern const uint8_t gm_snt446oeijaq5[13];
extern const uint8_t gm_snug45ulggroo[5];
extern const uint8_t gm_snurnbocs9786[49];
extern const uint8_t gm_so4bvja70211d[63];
extern const uint8_t gm_so6kuvn2nvuue[4];
extern const uint8_t gm_so77sfkiulkfr[11];
extern const uint8_t gm_soakgb2h3g40i[4];
extern const uint8_t gm_socribpa518de[13];
extern const uint8_t gm_sod98slk3vnra[20];
extern const uint8_t gm_sodpgd6927k7l[33];
extern const uint8_t gm_sof77pc3u6m60[25];
extern const uint8_t gm_sog4l3h5q9oa6[84];
extern const uint8_t gm_soohe0i8iafj3[20];
extern const uint8_t gm_sopmtpdd3jepm[8];
extern const uint8_t gm_soq2d9k5a34bc[8];
extern const uint8_t gm_sotb03b2eqtp5[20];
extern const uint8_t gm_soulf4opjqcvv[10];
extern const uint8_t gm_sovcjofctijvj[47];
extern const uint8_t gm_sovg8lhrcqaa0[34];
extern const uint8_t gm_sovtt6julbcd8[12];
extern const uint8_t gm_sp1cnbsshsv9k[18];
extern const uint8_t gm_sp39731r4i1pk[10];
extern const uint8_t gm_sp3k6emq42nh4[10];
extern const uint8_t gm_sp3rdossibc7f[4];
extern const uint8_t gm_sp8o48ld4n4jq[24];
extern const uint8_t gm_sp929adkqo1cd[93];
extern const uint8_t gm_spak5smc8j24b[14];
extern const uint8_t gm_spbqvmg1179vc[24];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const uint8_t gm_spf3a0kmdpgm5[6];
extern const uint8_t gm_spmlh93537vbl[26];
extern const uint8_t gm_spmv5l6cklh5d[18];
extern const uint8_t gm_spn4jhv77b56t[23];
extern const uint8_t gm_sppohbjp90mol[3];
extern const uint8_t gm_sprospqi1g16o[7];
extern const uint8_t gm_spun41oorb00o[30];
extern const uint8_t gm_spv9savsu9h0b[4];
extern const uint8_t gm_spvdlp3fsj44k[2];
extern const uint8_t gm_sq4o785gt8bsh[10];
extern const uint8_t gm_sq5khm5l62ens[37];
extern const uint8_t gm_sq94kuvk30n36[6];
extern const uint8_t gm_sqahda31evi4f[9];
extern const uint8_t gm_sqci13ris3rb0[16];
extern const uint8_t gm_sqcpnesgf4t4h[41];
extern const uint8_t gm_sqcvjnsub5bfs[53];
extern const uint8_t gm_sqf7ghomulo54[41];
extern const uint8_t gm_sqhj00be09q5v[9];
extern const uint8_t gm_sqirjnd2segia[40];
extern const uint8_t gm_sqldj8dr0j7rf[12];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr1dn90ca9rnt[9];
extern const uint8_t gm_sr2kfei26cj64[18];
extern const uint8_t gm_sr8de1o4do8v1[11];
extern const uint8_t gm_srbq5qj276c59[30];
extern const char gm_src2955num0q9tm[];
extern const char gm_src2u46tincrqmv[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_src94heo671ss5e[];
extern const char gm_srcfde9lsgge1ld[];
extern const char gm_srcgc0h5so8ul3j[];
extern const char gm_srcn13n69hd0cht[];
extern const char gm_srcns60k96jj1mt[];
extern const char gm_srco24pprak16p4[];
extern const char gm_srco4ljquuk5d87[];
extern const char gm_srcrchqfd7obmqe[];
extern const char gm_srcsk3h2du2ldnc[];
extern const uint8_t gm_srcsoo8lineh7[3];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_srjddrdnspo96[29];
extern const uint8_t gm_srk8hgivpmv60[18];
extern const uint8_t gm_srkue5qoqvtpg[41];
extern const uint8_t gm_srol0ktjpbtfv[58];
extern const uint8_t gm_srs4ljjaijhd2[24];
extern const uint8_t gm_srvm34bc3an1a[113];
extern const uint8_t gm_ss3lpq4se4ir5[5];
extern const uint8_t gm_ss65eeqhkg7mg[12];
extern const uint8_t gm_ss6cscjfm4jfc[11];
extern const uint8_t gm_ss77f3h3rt6l8[30];
extern const uint8_t gm_ss7pu45qp7oic[6];
extern const uint8_t gm_ss7qhfi2s00na[11];
extern const uint8_t gm_ss8fufid8t1o1[32];
extern const uint8_t gm_ss8q42tvppqf5[79];
extern const uint8_t gm_ssbpo4qatmvlq[7];
extern const uint8_t gm_ssg9s8mvpv2la[45];
extern const uint8_t gm_sshj97ls6n4f3[18];
extern const uint8_t gm_sskd2e48pqlgu[13];
extern const uint8_t gm_ssmuvuejgvj6b[27];
extern const uint8_t gm_ssor6j0m3frue[14];
extern const uint8_t gm_ssqifgr2icd6s[3];
extern const uint8_t gm_ssri9mbl4p6m3[68];
extern const uint8_t gm_st5nnur2j246q[36];
extern const uint8_t gm_st8ki5f85lc2j[13];
extern const uint8_t gm_st9n2vprlv3c1[22];
extern const uint8_t gm_staavgn0someg[56];
extern const uint8_t gm_stdh0kbfgj33k[9];
extern const uint8_t gm_stidgejoe9ul2[11];
extern const uint8_t gm_stl5olh2jfr0d[40];
extern const uint8_t gm_stq0db711o573[15];
extern const uint8_t gm_su13ngtdjura3[15];
extern const uint8_t gm_su1oqg46rlg2d[10];
extern const uint8_t gm_su2eceicte7s7[6];
extern const uint8_t gm_su4retifs0sre[9];
extern const uint8_t gm_su5mku12ctc3k[92];
extern const uint8_t gm_su823o267lacu[103];
extern const uint8_t gm_su8dibgnmpcpa[42];
extern const uint8_t gm_su9t2v4p35eij[8];
extern const uint8_t gm_su9tevpu8h5ad[19];
extern const uint8_t gm_suaek0bfou9a6[14];
extern const uint8_t gm_suctpm7bivu2g[48];
extern const uint8_t gm_sue26fe85rntf[5];
extern const uint8_t gm_suj5r301h9vv4[9];
extern const uint8_t gm_suj9anpi7p1mq[25];
extern const uint8_t gm_sujdc8gmukk24[16];
extern const uint8_t gm_sujipgkhb2fih[8];
extern const uint8_t gm_sulfrs2bmdlu2[6];
extern const uint8_t gm_suno9ilftpfht[12];
extern const uint8_t gm_suor7sr2pkoei[12];
extern const uint8_t gm_sus38q16nfojv[7];
extern const uint8_t gm_sv10m8g115ng3[59];
extern const uint8_t gm_sv1iq1n2a39tg[17];
extern const uint8_t gm_sv3d9namfmaua[9];
extern const uint8_t gm_sv61f25m0b9gs[4];
extern const uint8_t gm_sv89ta651gq7u[20];
extern const uint8_t gm_svaet5ugb1fj3[22];
extern const uint8_t gm_svdi2pk0uienp[45];
extern const uint8_t gm_svegu6hi5aibp[40];
extern const uint8_t gm_svl5ddjf64ul7[18];
extern const uint8_t gm_svp314e9ficjs[14];
extern const uint8_t gm_svqq3k9j6h1lu[8];
extern const uint8_t gm_svraiu8av4tm8[41];
extern const uint8_t gm_svsegvhrrmf7h[18];
extern const uint8_t gm_svthrqc59hf0v[21];
extern const gm_type gm_ty9oqmmiu8qc;
const char gm_src2955num0q9tm[] = "compiler/check/optionals.gm";
const char gm_src2u46tincrqmv[] = "compiler/lsp/analysis.gm";
const char gm_src6hq50r4rk7e6[] = "compiler/interp/frames.gm";
const char gm_src94heo671ss5e[] = "compiler/ide/items.gm";
const char gm_srcfde9lsgge1ld[] = "compiler/interp/format.gm";
const char gm_srcgc0h5so8ul3j[] = "compiler/interp/vm.gm";
const char gm_srcn13n69hd0cht[] = "compiler/lsp/refs.gm";
const char gm_srcns60k96jj1mt[] = "compiler/interp/vector.gm";
const char gm_srco24pprak16p4[] = "compiler/driver/options.gm";
const char gm_srco4ljquuk5d87[] = "compiler/check/dump.gm";
const char gm_srcrchqfd7obmqe[] = "compiler/fmt/chars.gm";
const char gm_srcsk3h2du2ldnc[] = "compiler/check/pattern.gm";
const char gm_srcuiab49hlgv7g[] = "compiler/parse/parser.gm";
const gm_type gm_ty9oqmmiu8qc = {.size = 2, .align = 2, .kind = GM_K_INT, .bits = 16, .is_signed = false};
const gm_type gm_ty9oqmmiu8qc;
const uint8_t gm_s02ktme6orfkh[7] = "gm_dir";
const uint8_t gm_s041tbii048j3[8] = "newName";
const uint8_t gm_s06mk2gjnlvf8[73] = "the elements of a `#soa` array are written positionally, or as `fill: s`";
const uint8_t gm_s06sbklusedgl[5] = " \077 (";
const uint8_t gm_s08sdh8q6l3vj[49] = "` is an effect set, not a single effect (\302\24716.6)";
const uint8_t gm_s091o161s6kba[5] = "mn{}";
const uint8_t gm_s0eemp7of8buc[3] = "->";
const uint8_t gm_s0eqrtq5mqb3a[16] = "ArrayPtrToSlice";
const uint8_t gm_s0ffe093dar6e[19] = "cannot complement ";
const uint8_t gm_s0iid7nkkvf30[55] = "error: input is not valid UTF-8; the line was ignored\012";
const uint8_t gm_s0jho24omhfui[7] = "branch";
const uint8_t gm_s0ljc8tssh8f7[8] = "{} - {}";
const uint8_t gm_s0m0qaokk51g0[4] = " + ";
const uint8_t gm_s0mppcr6a4lgb[9] = "{:.1} {}";
const uint8_t gm_s0nf1rg04f3m5[4] = "all";
const uint8_t gm_s0sg56e5fp4di[12] = "(*({} *){})";
const uint8_t gm_s0stes97dk54i[147] = "run those of the dependency <name> of the input's\012package (or of one of its dependencies) instead, with the\012dependency versions this package locks";
const uint8_t gm_s10qn2lc8ck56[78] = "` refers to itself, so it needs a declared return type: write `-> R` (\302\24711.1)";
const uint8_t gm_s1asl1e1tvb0k[2] = ")";
const uint8_t gm_s1bf3a5i0lcmm[21] = "Library/Caches/gamma";
const uint8_t gm_s1btmm2esmgsd[27] = "`{}` has no variant `{}`{}";
const uint8_t gm_s1dmrsprfdp53[8] = "{} = {}";
const uint8_t gm_s1gm12po6spua[18] = "; gm_unw.frame = ";
const uint8_t gm_s1in79aaeui99[10] = "gm_socket";
const uint8_t gm_s1qrqfh656g4q[6] = "Alloc";
const uint8_t gm_s1sspofc2u3bb[37] = "`.cross` needs three elements, and `";
const uint8_t gm_s20tdu9mtc6fi[8] = " = {0};";
const uint8_t gm_s22cd6dihn1b9[11] = "({} && {})";
const uint8_t gm_s2c267emk2nni[6] = ".data";
const uint8_t gm_s2c5ns5u9qs90[35] = "` contains the second-class type `";
const uint8_t gm_s2cartb32o2of[10] = ") { gm_gi";
const uint8_t gm_s2gpk5q7mg4o8[5] = "swap";
const uint8_t gm_s2h8cr50573go[8] = "PtrDiff";
const uint8_t gm_s2hgdf6b9d53s[5] = "Some";
const uint8_t gm_s2micd2hub1nf[26] = "F.h.up = (gm_frame *)env;";
const uint8_t gm_s2n0v309tod60[70] = "`--tag`, `--branch`, `--rev`, `--version` and `--subdir` need `--git`";
const uint8_t gm_s2n3d0ogcjggg[14] = "     Created ";
const uint8_t gm_s2qp8mqi0c2le[48] = "internal error: a vector access of a non-vector";
const uint8_t gm_s2r7keqsravo0[6] = "print";
const uint8_t gm_s2taci5crof3b[16] = "    return e \077 ";
const uint8_t gm_s2ttad1cg71bm[18] = "`}` to close the ";
const uint8_t gm_s2ttvhm3g7b1o[30] = "    double x = (double)GM_LD(";
const uint8_t gm_s30cnsbbn0c4o[4] = "vcs";
const uint8_t gm_s31holt846m75[10] = "gm_resume";
const uint8_t gm_s35hbb2rhhdvv[33] = "to close the attribute arguments";
const uint8_t gm_s3adv1e9meuc1[15] = "gm_thread_join";
const uint8_t gm_s3e0g1895m0j9[2] = "B";
const uint8_t gm_s3ergh2il1tjf[43] = "from_tag of a value that is not an integer";
const uint8_t gm_s3hersi16nhbp[34] = "uninitialized values of this type";
const uint8_t gm_s3i3n5bvpvmmi[16] = "the initializer";
const uint8_t gm_s3ijb9gbhdbib[27] = "`in` is not defined for {}";
const uint8_t gm_s3j69epdb3egk[26] = ", cx->env, cx->hctx, cx{}";
const uint8_t gm_s3kb1co7e63ka[37] = "expected a value, found the effect `";
const uint8_t gm_s3lv7dnoanjke[5] = "long";
const uint8_t gm_s3o37fp7c0sqt[15] = "GM_INT_TO_PTR(";
const uint8_t gm_s3o8dsbklmfsh[10] = "workspace";
const uint8_t gm_s3p0rbs9hc3lg[2] = " ";
const uint8_t gm_s3s4n6ehcrs2b[17] = "builtin function";
const uint8_t gm_s3tacjdl2ciei[13] = "((uintptr_t)";
const uint8_t gm_s41047ga9d9tq[26] = " {\012    gm_frame_overflow(";
const uint8_t gm_s42aeamnkrchf[31] = "the calling conventions differ";
const uint8_t gm_s43ls0s4m665j[57] = "attribute `#callconv` does not apply to a foreign member";
const uint8_t gm_s45dibb0c9ub8[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\245\003\000\000\014\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s45i8231l5fma[10] = "fields = ";
const uint8_t gm_s465vmg9c4jq6[14] = " not applied\012";
const uint8_t gm_s46khuklhbvqd[21] = "cannot create {}: {}";
const uint8_t gm_s4a6o0t8isvlm[17] = "((uint8_t *)gm_g";
const uint8_t gm_s4ft5s33591qg[7] = " |= ((";
const uint8_t gm_s4ko774bb2ea9[56] = "expected a struct or union literal after the attributes";
const uint8_t gm_s4l676io35g5u[12] = "((void (*)(";
const uint8_t gm_s4pucugl3svdm[11] = "extern {}\012";
const uint8_t gm_s4vqnsfknimfg[28] = "`pub` cannot be applied to ";
const uint8_t gm_s52ul4dg63jhe[11] = " < 0) == (";
const uint8_t gm_s56n05s0kn0p9[27] = "cannot read GAMMA_RUNNER: ";
const uint8_t gm_s58g91v8ecst5[13] = "gm_proc_kill";
const uint8_t gm_s5974q1kmorlc[52] = "`.{}` is not supported on this target (x86-64 only)";
const uint8_t gm_s59st1i1tm2m1[9] = "`~` on `";
const uint8_t gm_s5f9kvb1gm4j6[13] = "gm_net_error";
const uint8_t gm_s5g1e6qrps9q6[19] = "definitionProvider";
const uint8_t gm_s5irlrsnb30cc[24] = "no gamma.toml found in ";
const uint8_t gm_s5j8t1cg2erp9[8] = " needs ";
const uint8_t gm_s5k9ll19r7m4g[4] = "j64";
const uint8_t gm_s5m9qimbv7e8k[3] = " }";
const uint8_t gm_s5nt88lj0viqk[4] = "b{}";
const uint8_t gm_s5pm13tivcrur[15] = "this performs ";
const uint8_t gm_s5rb1e1n8ivr7[48] = "the import path `{}` leaves the package (\302\2473.2)";
const uint8_t gm_s62t7qmf3ajh5[69] = "package `{}`: `subdir` must be a relative path inside the repository";
const uint8_t gm_s65ftmvtj458k[11] = "{}{}\012{}\012{}";
const uint8_t gm_s69o347hkiq9a[61] = "an `if` with an empty body and no `else`, or an empty `else`";
const uint8_t gm_s6d57grm686e3[27] = "compiler/check/template.gm";
const uint8_t gm_s6h2ge6vgkmhm[9] = "noreturn";
const uint8_t gm_s6hgf366ivqrg[34] = "duplicate tuple element name `{}`";
const uint8_t gm_s6hp19vgdsi0o[24] = "No space left on device";
const uint8_t gm_s6kq1mi4s1g58[3] = ":\077";
const uint8_t gm_s6p5i58jsib94[8] = "{:016x}";
const uint8_t gm_s6p6u3ogotq4f[50] = "` in the other; only pointers convert to pointers";
const uint8_t gm_s6pceda0js0g8[6] = "</p>\012";
const uint8_t gm_s6peoato8pau5[13] = "((uint8_t *)";
const uint8_t gm_s6pilvbjelm5p[4] = "str";
const uint8_t gm_s6v6kgo7d00tn[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000T\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s6vl5jv0tf2n4[8] = "\012note: ";
const uint8_t gm_s709tdhiijp4r[78] = "`&` needs a place (a variable, `p^`, field or element), not a temporary value";
const uint8_t gm_s76i8mdsri6em[70] = "invalid free of {:#x}: not the start of a live heap block of {} bytes";
const uint8_t gm_s786i8duem00q[27] = "compiler/check/matching.gm";
const uint8_t gm_s7c2mvkr8kl49[35] = "` is not available on vectors of `";
const uint8_t gm_s7cvig9qav6l5[3] = "0b";
const uint8_t gm_s7i2mni64durc[18] = "the target of `&`";
const uint8_t gm_s7mosjq6fpdqc[15] = "gm_format_spec";
const uint8_t gm_s7mqv7hf4fso0[27] = "     Nothing to create in ";
const uint8_t gm_s7phrfovrc6ul[239] = "pub Os :: union { Linux, MacOS, Windows, FreeBSD, Wasi, Freestanding }\012pub Arch :: union { X86_64, Aarch64, Riscv64, S390x, Wasm32, Other }\012pub Endian :: union { Little, Big }\012pub Mode :: union { Debug, Checked, Safe, Fast }\012pub os :: Os.";
const uint8_t gm_s7vusn2lig3dp[65] = "`{}` is an operation of `{}`; call it as `{}{}.{}(\342\200\246)` (\302\24712.1)";
const uint8_t gm_s802sbfa6mtsg[27] = "missing element {} of `{}`";
const uint8_t gm_s85hqf14lbkns[8] = "AbsDiff";
const uint8_t gm_s87e746eia9j4[6] = " > 0)";
const uint8_t gm_s8aj3oag1l1u0[14] = "optional-type";
const uint8_t gm_s8b49h6svih4r[82] = "(\342\200\246)` is an iterator, not a value: it cannot be passed for a parameter of type `";
const uint8_t gm_s8ckgohq4msba[19] = "{} was called here";
const uint8_t gm_s8cr606ter6p9[39] = "this `{}` does not match the open `{}`";
const uint8_t gm_s8gaig0cs2uce[19] = "dependency cycle: ";
const uint8_t gm_s8i44sm1b77f2[121] = "this program switches fibers, which on WASI needs Binaryen's `wasm-opt` (https://github.com/WebAssembly/binaryen), but `";
const uint8_t gm_s8ki76moa277n[23] = "write `(try x) else d`";
const uint8_t gm_s8m45dpj5ugr4[6] = "; /* ";
const uint8_t gm_s8pjstf2pdmbk[14] = ", a) < GM_LD(";
const uint8_t gm_s8ptfv88uhmsa[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\0008\002\000\000\011\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s8qvuobg6jcbo[13] = "((void *){})";
const uint8_t gm_s8rsp9val5ful[5] = "gm_s";
const uint8_t gm_s8ukphi4920cc[11] = "archive = ";
const uint8_t gm_s92hgp9htn7k0[21] = "gm_fret:\012    return ";
const uint8_t gm_s930a5k4dd69o[10] = "truncated";
const uint8_t gm_s94v9o49bjk09[12] = "gm_fs_lstat";
const uint8_t gm_s99lgt46tmvfc[14] = "; i++) GM_ST(";
const uint8_t gm_s99ra887gkc8u[39] = "parameter `{}` is given more than once";
const uint8_t gm_s9dtt8vrvpd1h[16] = "check run build";
const uint8_t gm_s9eiod3sq367m[7] = "utf-16";
const uint8_t gm_s9go9vvkpcjpd[48] = "size_t argn; const void *argp = gm_args(&argn);";
const uint8_t gm_s9grefqkso86a[3] = "pn";
const uint8_t gm_s9loa14tklecu[3] = "md";
const uint8_t gm_s9tlur6oo59qa[31] = "out of memory in the scheduler";
const uint8_t gm_s9vag5co36u2r[18] = "`{}` clause in {}";
const uint8_t gm_sa1pcp92k3aj3[7] = "GetPtr";
const uint8_t gm_sa39b9bt4jpna[36] = "the `Yield` effect is not available";
const uint8_t gm_sa9mckopi2o5a[86] = "a `ctl` clause cannot `continue`: end it with `resume`, `break` or an exit (\302\24712.7.1)";
const uint8_t gm_saefgd8cv1aea[20] = " && !gm_unw.kind) {";
const uint8_t gm_sajpbdgkjnse7[8] = "main.gm";
const uint8_t gm_sal1kdi1rij5t[10] = "fs_rename";
const uint8_t gm_saoa4m7dh2vl3[55] = "a character literal must contain exactly one character";
const uint8_t gm_sapu6l8enc7d7[7] = "</dd>\012";
const uint8_t gm_sauknalgur9no[11] = "({} == {})";
const uint8_t gm_sav0q47tkm7j2[20] = "` (known profiles: ";
const uint8_t gm_savlppb39i8g6[6] = "</th>";
const uint8_t gm_savvoucn0tcn5[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\307\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sb1nc8gh4as5h[15] = "the directory ";
const uint8_t gm_sb335j99bk4ch[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\030\001\000\000N\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sb3c9ehk423nf[6] = "Debug";
const uint8_t gm_sb3o3ij0n2htb[5] = "F.{}";
const uint8_t gm_sb455a3lakuom[60] = "`not` cannot be an operand here; parenthesize it: `(not x)`";
const uint8_t gm_sb8ag7vcv52jt[5] = "gm_k";
const uint8_t gm_sb8c0irhu65ra[140] = "a block body's last expression is not its value: write `return \342\200\246`, or `=> \342\200\246` for a body that is one expression (this function returns `";
const uint8_t gm_sb8vbka031tie[21] = "this variant pattern";
const uint8_t gm_sbajlb7u95nhh[10] = "low_block";
const uint8_t gm_sbe7f0bkvca75[43] = "a compile-time function value of this type";
const uint8_t gm_sbec11r2rv4ti[23] = "flag_offset = GM_NICHE";
const uint8_t gm_sbkmg1rlq4fa7[27] = "; choose one with `--name`";
const uint8_t gm_sbm4560lv1lt5[25] = "expected `bool`, found `";
const uint8_t gm_sbvdvkd7bikd7[25] = ", (void *){}, {}, NULL{}";
const uint8_t gm_sc0brfbvonphe[64] = "a function pointer performs only the effects in its type, and `";
const uint8_t gm_sc3mvoa74bm5n[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\204\001\000\000E\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sc4daeflaltnv[3] = "- ";
const uint8_t gm_sc4i6tq8tg5gu[13] = "gm_fmt_float";
const uint8_t gm_sc4r4l9k6f577[3] = "ev";
const uint8_t gm_sc5ekkhtqttfb[4] = "_ =";
const uint8_t gm_sc80lo6tf645h[31] = "imaginary literals are decimal";
const uint8_t gm_sc88juptr3rgf[31] = "] != 0; h = gm_mix(h, &e, 1);\012";
const uint8_t gm_sc9p2e0c29ni7[29] = "__asm__ volatile (\"\" : \"+r\"(";
const uint8_t gm_scak4selic6pl[21] = "invalid template: {}";
const uint8_t gm_scbrbhepkh2rj[25] = "        gm_buf_push(&b, ";
const uint8_t gm_scc6ms0hrfo8j[9] = "complex ";
const uint8_t gm_scf54410fgl4j[39] = "cannot run `curl` (is it installed\077): ";
const uint8_t gm_scfdk5i1nqc6a[30] = "unknown escape sequence `\\{}`";
const uint8_t gm_scifaj4dn1ok5[6] = "title";
const uint8_t gm_scn0a8pnba0ho[41] = "`{}` cannot take a receiver of type `{}`";
const uint8_t gm_scn0pl9eq3k2k[24] = "ratio: division by zero";
const uint8_t gm_scne9acm49p4h[11] = "1 value is";
const uint8_t gm_scommn379heqq[14] = "{}^{{commit}}";
const uint8_t gm_scq0i5n3f7it5[41] = "{} has no branch `{}` (required by `{}`)";
const uint8_t gm_sct928cj9v78e[137] = "` cannot be passed to a variadic parameter: use a C-compatible scalar of at least C `int` width or `f64`, converting explicitly (\302\24719.2)";
const uint8_t gm_sd1gv97l675vb[26] = "typedef union { _Alignas(";
const uint8_t gm_sd2ht7fkpnftp[17] = "get on a pointer";
const uint8_t gm_sd4k4nkk0h9t7[14] = "gm_spec_radix";
const uint8_t gm_sd4sd2h1n94bh[7] = ".index";
const uint8_t gm_sd8o4m7695hpq[38] = "internal error: not a network service";
const uint8_t gm_sd99er7oafmtu[17] = " is out of range";
const uint8_t gm_sd9o9rh5qe8e7[11] = "fs_symlink";
const uint8_t gm_sdarhudj711nk[17] = "this dereference";
const uint8_t gm_sdfl2t7s13ucr[12] = "continue {}";
const uint8_t gm_sdg1ovmjdibb5[6] = ":quit";
const uint8_t gm_sdh3pk0b3jsnu[67] = "`, which might be the same effect for some type arguments (\302\24716.6)";
const uint8_t gm_sdinre9qvo2vq[28] = "cannot convert `{}` to `{}`";
const uint8_t gm_sdogehgrilpeb[3] = "ul";
const uint8_t gm_sdqssko83l66b[28] = "GM_API int gm_checks = {};\012";
const uint8_t gm_sdrevll5io08q[10] = "gm_hblock";
const uint8_t gm_sds80esh09gni[51] = "internal error: the address of a value without one";
const uint8_t gm_sdvtt0l0jh9bp[10] = "timed out";
const uint8_t gm_se08n5d1gngu7[5] = "` = ";
const uint8_t gm_se25mtccq2mtg[42] = "({} & (((unsigned __int128)1 << {}) - 1))";
const uint8_t gm_se5tulnsn3o9s[10] = "handlers ";
const uint8_t gm_se8ggmq7h9bin[35] = "` lives until the end of its scope";
const uint8_t gm_sebnh3ba276b8[17] = "` is not (\302\2474.4)";
const uint8_t gm_seeo26ergtcha[5] = "Ceil";
const uint8_t gm_seg7e1ca8e64l[5] = "}`: ";
const uint8_t gm_segch6a5dme9u[5] = "-inf";
const uint8_t gm_segn0pv9sd905[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000(\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_segr993uh2crm[11] = "FsCloseDir";
const uint8_t gm_seh85cpcgte1u[57] = "a shift amount must have an unsigned integer type, not `";
const uint8_t gm_seimuk2ck2a6g[38] = " = gm_fiber_new(inst->fw, (uint8_t *)";
const uint8_t gm_seojbs1tqr33k[46] = "radix prefixes must be lowercase; write `0{}`";
const uint8_t gm_sf2lr85hprd3e[40] = "the value of this expression (of type `";
const uint8_t gm_sf46mekb18qm8[14] = "gm_fret:\012    ";
const uint8_t gm_sf9ilivr4c86m[4] = "rev";
const uint8_t gm_sfau4tfjh7ajf[15] = "gm_with_clause";
const uint8_t gm_sfc071avthns7[21] = "`{}` is deprecated{}";
const uint8_t gm_sfcm41q17pr37[46] = "error: the input ends where more is expected\012";
const uint8_t gm_sffd375a90o1u[3] = "-f";
const uint8_t gm_sffd7ppku35fr[10] = "--missing";
const uint8_t gm_sfkqvfbp56vqq[11] = "gm_env_cmp";
const uint8_t gm_sfl0m0vifcu3m[82] = "the template numbers its values, so this spread must be a tuple of them, not `{}`";
const uint8_t gm_sfvsgjcg86bbl[45] = "a function type cannot have a rest parameter";
const uint8_t gm_sg0ns322676oc[26] = "compiler/load/manifest.gm";
const uint8_t gm_sg39dr0kaebm0[10] = "branch={}";
const uint8_t gm_sg4faca6q9u0h[8] = "gm_wide";
const uint8_t gm_sg6b42o7q3v50[64] = "the result of this constant expression is too large (more than ";
const uint8_t gm_sg91clgj6l88m[6] = "curl:";
const uint8_t gm_sg92g0je3abps[14] = "` is an array";
const uint8_t gm_sg93rj30beob9[29] = "a handler is declared for `^";
const uint8_t gm_sgb0oed9c3voo[5] = "make";
const uint8_t gm_sge7ihldgqi49[20] = "min/max/clamp on {}";
const uint8_t gm_sge9mrih5euc5[13] = "the import `";
const uint8_t gm_sgekdmqbrecpp[9] = "{}({}){}";
const uint8_t gm_sgf1euukkdve3[14] = "many-ptr-type";
const uint8_t gm_sgh9nkjgl73of[27] = "textDocument/prepareRename";
const uint8_t gm_sgmjl48sjjf69[22] = "((uint8_t *)&{} + {})";
const uint8_t gm_sgmt0q35kl6hc[5] = "ret ";
const uint8_t gm_sgqifqduuc5vl[46] = "report every rule that would warn as an error";
const uint8_t gm_sgqs124m28gfg[7] = "Socket";
const uint8_t gm_sgtmc08t3k49t[42] = "\012\012| Module | Description |\012| --- | --- |\012";
const uint8_t gm_sh0adpq1lnkpj[40] = "`{}({})` is not a lossless conversion{}";
const uint8_t gm_sh0drhj4ffllf[11] = "gm_fmt_int";
const uint8_t gm_sh0is7j09prt2[23] = "to close the subscript";
const uint8_t gm_sh11qqm8nqu68[8] = "CstrLen";
const uint8_t gm_sh2bu6u8oapg5[73] = "`{}` is not a first-class type, so it cannot be a type argument (\302\24716.1)";
const uint8_t gm_sh43rpq1oqm5d[6] = "gm_xa";
const uint8_t gm_sh5cd76h4vacq[102] = "a `pub(package)` function or constant, or a `pub` one of a program package, that no other module uses";
const uint8_t gm_sh5f7b7njpj3l[6] = "apple";
const uint8_t gm_sharnvllqpvms[17] = "Invalid argument";
const uint8_t gm_shb921rk4rkpo[5] = "<td>";
const uint8_t gm_shf0g22rjm3o7[5] = "line";
const uint8_t gm_shhn92c601tgo[29] = "((((unsigned __int128)GM_LD(";
const uint8_t gm_shl9ceh0q5tpe[6] = "debug";
const uint8_t gm_shn8mjp9kc2n2[9] = "uint64_t";
const uint8_t gm_shncvaenjonsu[41] = "an alignment must be an integer constant";
const uint8_t gm_shntnl6m6g3am[27] = "compiler/interp/threads.gm";
const uint8_t gm_shpi1i86ollcb[59] = "a template in a generic function that was not instantiated";
const uint8_t gm_shq77is85iktk[17] = "soa_copy_columns";
const uint8_t gm_shroj9d6fajb6[4] = "+{}";
const uint8_t gm_shror0skne60i[45] = "`{}` is declared twice in this foreign block";
const uint8_t gm_shs874j97ior1[14] = "the statement";
const uint8_t gm_shsoptr61c2h5[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000T\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_shv5ofgc65v68[3] = "[[";
const uint8_t gm_si8jv2q8d22kl[10] = "proc_kill";
const uint8_t gm_sia6bdhdcj5co[13] = "gm_wasi_next";
const uint8_t gm_siabpto2siqmj[7] = "err = ";
const uint8_t gm_sidiiakhbvqqi[9] = "`; not `";
const uint8_t gm_sig8ahbvm8eb8[10] = "{} {} yet";
const uint8_t gm_sigjilgl161c6[5] = "cn{}";
const uint8_t gm_sigmj6bktcbbh[80] = "    uint64_t x = (uint64_t)(uintptr_t)GM_LD(void *, v); return gm_mix64(h, x);\012";
const uint8_t gm_sigq4ahgd0l2a[64] = "this call is not an iterator: its effects include no `Yield(T)`";
const uint8_t gm_siihmb4jo3dsn[125] = "    gm_buf b = {0};\012    uint8_t sb[GM_VIEW_SCRATCH];\012    const void *fd;\012    size_t fn = gm_format_view(&b, sb, d, v, &fd);\012";
const uint8_t gm_siji3oo7ud53t[10] = "void *env";
const uint8_t gm_sijp8lh2g2ld5[34] = "calls to foreign functions (`{}`)";
const uint8_t gm_sipi37glknm5f[16] = "gm_thread_stack";
const uint8_t gm_siuacbf0blrje[9] = "label {}";
const uint8_t gm_siv8n5ojlp0dd[18] = "triggerCharacters";
const uint8_t gm_sj2cfogjb0pbr[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\330\000\000\000\026\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sj49v74ko3ghf[15] = "net_local_port";
const uint8_t gm_sj56dkk1peu3m[54] = "unknown `cache` subcommand `{}`: use `dir` or `clean`";
const uint8_t gm_sj5j224m4ulf9[3] = "%=";
const uint8_t gm_sj9kuut55po7f[46] = "expected a continuation `cont(R)`, found `{}`";
const uint8_t gm_sjbjet56e96co[17] = "note: `main` in ";
const uint8_t gm_sjdrosvte680q[12] = "ProcCloseIn";
const uint8_t gm_sjg4ie5b5ufsm[8] = "{}..={}";
const uint8_t gm_sjiao13m1cclm[24] = "the second-class type `";
const uint8_t gm_sjkj5g78lejrj[4] = "int";
const uint8_t gm_sjm54r6h90i7e[63] = "`; the constraints are Eq, Ord, Num, Int, Neg, Float and Plain";
const uint8_t gm_sjssolp3qfpqg[7] = "stored";
const uint8_t gm_sjtp8ip8t28uh[7] = "({}){}";
const uint8_t gm_sjtqic917nd2g[6] = "since";
const uint8_t gm_sk34jm61h5m0d[3] = "-W";
const uint8_t gm_sk3i3et1jqa7c[12] = "gm_with_end";
const uint8_t gm_sk531tvbsm11t[44] = "`is` compares a type only with another type";
const uint8_t gm_sk5om4a4762gd[88] = "compile via C (the default) or run in the interpreter;\012(test, bench) only c is accepted";
const uint8_t gm_sk721elh8ouv2[11] = "FsTruncate";
const uint8_t gm_sk8hn2ar9njah[15] = "_Bool present;";
const uint8_t gm_sk8r2qbdun3l0[35] = "the {} has no parameter named `{}`";
const uint8_t gm_skbd3tdpfkbk5[24] = "invalid format spec `{:";
const uint8_t gm_skedh3k15mgd3[18] = "` this performs; ";
const uint8_t gm_skff6pclobvnu[40] = "a compile-time value of this float type";
const uint8_t gm_skjg6jfphs2d0[10] = "gm_fs_cwd";
const uint8_t gm_skk0hh6be703h[20] = "    uintptr_t na = ";
const uint8_t gm_skm5ng1ir37l8[8] = "Removed";
const uint8_t gm_skmai4uvseje8[24] = "empty character literal";
const uint8_t gm_skmfilbqoc8lo[87] = "`{}` uses local variables of the enclosing function, so it cannot call itself (\302\24711.8)";
const uint8_t gm_skmueji31ds5r[49] = "expected a closing run of exactly {} backtick(s)";
const uint8_t gm_skpueg68n121l[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000U\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_skq0s129a3p6p[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\"\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_skr00pujn28ln[29] = "typedef struct {{ {} }} {};\012";
const uint8_t gm_sl2gfi3ik4b1u[11] = "\")\012#undef ";
const uint8_t gm_sl58ekc3js5bs[30] = "invalid version requirement `";
const uint8_t gm_sl6br25hfjb5f[9] = "\377\377\377\377\377\377\377\377";
const uint8_t gm_sl84tf4h2e7v7[29] = "(({})code)(env, cx, NULL, a)";
const uint8_t gm_sla3unrbmgc0v[6] = "evn{}";
const uint8_t gm_slb6cj7ndgvdm[6] = "allow";
const uint8_t gm_slcgg5l8i9236[50] = "((((unsigned __int128)GM_LD({}, {})) >> {}) & {})";
const uint8_t gm_sli291jkgotin[31] = " entries, which is not allowed";
const uint8_t gm_sljdfefkkeqgn[90] = "`@copy_elems` takes a slice and a slice of the same element type: `@copy_elems(dst, src)`";
const uint8_t gm_sljqt9ip2cpnb[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\347\000\000\000,\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_sllcm1pqagh28[6] = "defer";
const uint8_t gm_sln88h001q3mp[19] = "Input/output error";
const uint8_t gm_slof5tl7m8grj[4] = "c{}";
const uint8_t gm_slolcr6ikvg33[7] = "rawptr";
const uint8_t gm_slpqve1dqssq0[8] = "GM_TLS ";
const uint8_t gm_slsa15rinti36[12] = "alloc_frame";
const uint8_t gm_sm0105pgqu27v[17] = "\000\000\000\000\000\000\000\000V\000\000\000\000\000\000\000";
const uint8_t gm_sm57fhlj6aanb[62] = "__repl_type :: fn(T: type, v: T) -> string => @type_name(T)\012\012";
const uint8_t gm_sm5t84rdabed6[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000>\001\000\000$\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sm78jth420g98[9] = "\240\206\001\000\000\000\000\000";
const uint8_t gm_smcv6j7b18cjr[17] = "gamma: <stdin>: ";
const uint8_t gm_smiavg3q7ucd9[8] = "gm_hval";
const uint8_t gm_smpb7l3te3i8n[52] = "`@stack_size` needs the name of a function (\302\24714.8)";
const uint8_t gm_smr5ddngcjuao[19] = "if (gm_unw.kind) {";
const uint8_t gm_smrla0cd2esrp[14] = "write `{}.{}`";
const uint8_t gm_sn1m0t4k3vpa5[5] = " fix";
const uint8_t gm_sn2qlgie5sklu[45] = "if (gm_unw.frame == &F.h && gm_unw.label == ";
const uint8_t gm_sn4e8vibva5m7[45] = "`break` and `continue` must be inside a loop";
const uint8_t gm_sn5pv7k63ni4c[11] = "` is empty";
const uint8_t gm_sn5rcg6kpf9gq[36] = "`@{}` takes {} positional arguments";
const uint8_t gm_sn9fg2capllh2[79] = "static bool {}(void *code, void *env, gm_node *cx, const void *d, uintptr_t n)";
const uint8_t gm_snauhge6bheis[14] = "--speed-limit";
const uint8_t gm_snbkfu70dq3fp[33] = "Resource temporarily unavailable";
const uint8_t gm_sngn7trhgdnc2[43] = "`{}` is indexed by `{}` values, not `{}`{}";
const uint8_t gm_snhr0nlm0epj5[60] = "`{}` is permitted only in the body of a `ctl` clause (\302\24714)";
const uint8_t gm_snl0g0eaf7ift[8] = "sparc64";
const uint8_t gm_sno069vrt5g9q[4] = " } ";
const uint8_t gm_snq4uaiieftl8[6] = "BitOr";
const uint8_t gm_sns2lv549pkqt[53] = "--format=%(refname)%09%(objectname)%09%(*objectname)";
const uint8_t gm_snsu3jrv1dbtj[32] = "` is the name of a builtin type";
const uint8_t gm_snt446oeijaq5[13] = "is_sorted_by";
const uint8_t gm_snug45ulggroo[5] = "po{}";
const uint8_t gm_snurnbocs9786[49] = "static __attribute__((aligned(4))) {}{}{} {}({})";
const uint8_t gm_so4bvja70211d[63] = "`in` compares with `is`, which is not defined for `{}` (\302\2477.6)";
const uint8_t gm_so6kuvn2nvuue[4] = "lex";
const uint8_t gm_so77sfkiulkfr[11] = "    if (a[";
const uint8_t gm_soakgb2h3g40i[4] = "'''";
const uint8_t gm_socribpa518de[13] = "gm_net_close";
const uint8_t gm_sod98slk3vnra[20] = "compiler/tir/tir.gm";
const uint8_t gm_sodpgd6927k7l[33] = "Error: failed to run main module";
const uint8_t gm_sof77pc3u6m60[25] = " __attribute__((aligned(";
const uint8_t gm_sog4l3h5q9oa6[84] = "the left side of an assignment must be a place (a variable, `p^`, field or element)";
const uint8_t gm_soohe0i8iafj3[20] = "unknown option `{}`";
const uint8_t gm_sopmtpdd3jepm[8] = ", h) : ";
const uint8_t gm_soq2d9k5a34bc[8] = "gm_segv";
const uint8_t gm_sotb03b2eqtp5[20] = "collapsible-else-if";
const uint8_t gm_soulf4opjqcvv[10] = "tag of {}";
const uint8_t gm_sovcjofctijvj[47] = "lane {} does not exist: `{}` has lanes 0 to {}";
const uint8_t gm_sovg8lhrcqaa0[34] = "`--jobs` needs a number, not `{}`";
const uint8_t gm_sovtt6julbcd8[12] = "`--profile=";
const uint8_t gm_sp1cnbsshsv9k[18] = "` has no method `";
const uint8_t gm_sp39731r4i1pk[10] = "rev-parse";
const uint8_t gm_sp3k6emq42nh4[10] = "` (\302\2477.6)";
const uint8_t gm_sp3rdossibc7f[4] = "<<\077";
const uint8_t gm_sp8o48ld4n4jq[24] = "compiler/parse/types.gm";
const uint8_t gm_sp929adkqo1cd[93] = "effect parameters are types or effect sets: `effect(T: type)`, `effect(E: effects)` (\302\24712.1)";
const uint8_t gm_spak5smc8j24b[14] = "{}(*({} *){})";
const uint8_t gm_spbqvmg1179vc[24] = "cannot {} `{}` and `{}`";
const uint8_t gm_spe8t3e79vnal[7] = "export";
const uint8_t gm_spevct0q3cjgb[5] = "fill";
const uint8_t gm_spf3a0kmdpgm5[6] = "{}#{}";
const uint8_t gm_spmlh93537vbl[26] = "compiler/parse/pattern.gm";
const uint8_t gm_spmv5l6cklh5d[18] = "the template has ";
const uint8_t gm_spn4jhv77b56t[23] = "\012pub endian :: Endian.";
const uint8_t gm_sppohbjp90mol[3] = "-C";
const uint8_t gm_sprospqi1g16o[7] = "op_neg";
const uint8_t gm_spun41oorb00o[30] = "\012    print(\"{}\\n\", __repl_v)\012";
const uint8_t gm_spv9savsu9h0b[4] = "fn ";
const uint8_t gm_spvdlp3fsj44k[2] = "\001";
const uint8_t gm_sq4o785gt8bsh[10] = "--package";
const uint8_t gm_sq5khm5l62ens[37] = "call through a null function pointer";
const uint8_t gm_sq94kuvk30n36[6] = "gm_ga";
const uint8_t gm_sqahda31evi4f[9] = " (int)((";
const uint8_t gm_sqci13ris3rb0[16] = "prepare_codegen";
const uint8_t gm_sqcpnesgf4t4h[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\306\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_sqcvjnsub5bfs[53] = "invalid template (in an instantiation used here): {}";
const uint8_t gm_sqf7ghomulo54[41] = "\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\022\001\000\000\020\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_sqhj00be09q5v[9] = " #packed";
const uint8_t gm_sqirjnd2segia[40] = "array elements are written positionally";
const uint8_t gm_sqldj8dr0j7rf[12] = "#line {} {}";
const uint8_t gm_sqtc9fffut4s2[13] = "const void *";
const uint8_t gm_sr1dn90ca9rnt[9] = "\000 \000\000\000\000\000\000";
const uint8_t gm_sr2kfei26cj64[18] = "GM_LD(uintptr_t, ";
const uint8_t gm_sr8de1o4do8v1[11] = "atomic_xor";
const uint8_t gm_srbq5qj276c59[30] = "invalid `url` of dependency `";
const uint8_t gm_srcsoo8lineh7[3] = "&=";
const uint8_t gm_srjddrdnspo96[29] = "unclosed array, expected `]`";
const uint8_t gm_srk8hgivpmv60[18] = "__atomic_store_n(";
const uint8_t gm_srkue5qoqvtpg[41] = "the name of the test as a string literal";
const uint8_t gm_srol0ktjpbtfv[58] = "cannot be checked against `{}`: unknown architecture `{}`";
const uint8_t gm_srs4ljjaijhd2[24] = " lanes with a slice of ";
const uint8_t gm_srvm34bc3an1a[113] = "\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000";
const uint8_t gm_ss3lpq4se4ir5[5] = "{} *";
const uint8_t gm_ss65eeqhkg7mg[12] = "starts_with";
const uint8_t gm_ss6cscjfm4jfc[11] = "--location";
const uint8_t gm_ss77f3h3rt6l8[30] = ": return GM_RUN_MAIN(gm_entry";
const uint8_t gm_ss7pu45qp7oic[6] = "<iter";
const uint8_t gm_ss7qhfi2s00na[11] = "<for body>";
const uint8_t gm_ss8fufid8t1o1[32] = "array length {} is out of range";
const uint8_t gm_ss8q42tvppqf5[79] = "invalid integer width in suffix `{}`: widths are 1 to 128 with no leading zero";
const uint8_t gm_ssbpo4qatmvlq[7] = ")0 - (";
const uint8_t gm_ssg9s8mvpv2la[45] = "expected a newline or `;` after {}, found {}";
const uint8_t gm_sshj97ls6n4f3[18] = "unmatched closing";
const uint8_t gm_sskd2e48pqlgu[13] = "gm_fiber_new";
const uint8_t gm_ssmuvuejgvj6b[27] = "` needs a mutable receiver";
const uint8_t gm_ssor6j0m3frue[14] = "(uintptr_t){}";
const uint8_t gm_ssqifgr2icd6s[3] = " *";
const uint8_t gm_ssri9mbl4p6m3[68] = "an `if` or `while` whose condition is the literal `true` or `false`";
const uint8_t gm_st5nnur2j246q[36] = ", line);\012    GM_ST(uint32_t, loc + ";
const uint8_t gm_st8ki5f85lc2j[13] = "StaticAssert";
const uint8_t gm_st9n2vprlv3c1[22] = "`is` on `#soa` slices";
const uint8_t gm_staavgn0someg[56] = "expected a handler, but no handler is declared for `{}`";
const uint8_t gm_stdh0kbfgj33k[9] = "` with `";
const uint8_t gm_stidgejoe9ul2[11] = "FsReadLink";
const uint8_t gm_stl5olh2jfr0d[40] = "; an array is not a slice: pass `a[..]`";
const uint8_t gm_stq0db711o573[15] = "</blockquote>\012";
const uint8_t gm_su13ngtdjura3[15] = "(void *)&F.w{}";
const uint8_t gm_su1oqg46rlg2d[10] = "this copy";
const uint8_t gm_su2eceicte7s7[6] = " gm_l";
const uint8_t gm_su4retifs0sre[9] = "__FILC__";
const uint8_t gm_su5mku12ctc3k[92] = "this compile-time evaluation calls `{}`, which depends on it; the cycle cannot be evaluated";
const uint8_t gm_su823o267lacu[103] = ") is no longer available in the repository (a tag moved or history was rewritten\077); run `gamma update ";
const uint8_t gm_su8dibgnmpcpa[42] = "effects without a handler at compile time";
const uint8_t gm_su9t2v4p35eij[8] = "gm_trap";
const uint8_t gm_su9tevpu8h5ad[19] = "stable_sort_by_key";
const uint8_t gm_suaek0bfou9a6[14] = "GM_ROUNDEVEN(";
const uint8_t gm_suctpm7bivu2g[48] = "`@bswap` needs an integer of whole bytes, not `";
const uint8_t gm_sue26fe85rntf[5] = "note";
const uint8_t gm_suj5r301h9vv4[9] = "indexing";
const uint8_t gm_suj9anpi7p1mq[25] = "compiler/cgen/program.gm";
const uint8_t gm_sujdc8gmukk24[16] = "gm_note_failure";
const uint8_t gm_sujipgkhb2fih[8] = "`, but ";
const uint8_t gm_sulfrs2bmdlu2[6] = "Child";
const uint8_t gm_suno9ilftpfht[12] = "net_connect";
const uint8_t gm_suor7sr2pkoei[12] = "__typeof__(";
const uint8_t gm_sus38q16nfojv[7] = "soa {}";
const uint8_t gm_sv10m8g115ng3[59] = "` is a variable, so its value is not known at compile time";
const uint8_t gm_sv1iq1n2a39tg[17] = "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sv3d9namfmaua[9] = "{} *ptr;";
const uint8_t gm_sv61f25m0b9gs[4] = "lib";
const uint8_t gm_sv89ta651gq7u[20] = "#define F (*gm_Fp)\012";
const uint8_t gm_svaet5ugb1fj3[22] = "0x7fffffffffffffffull";
const uint8_t gm_svdi2pk0uienp[45] = "a tuple pattern needs a tuple, but this is `";
const uint8_t gm_svegu6hi5aibp[40] = "`---` requires a plain-data type, but `";
const uint8_t gm_svl5ddjf64ul7[18] = "(); } return gm_g";
const uint8_t gm_svp314e9ficjs[14] = " + 0; memcpy(";
const uint8_t gm_svqq3k9j6h1lu[8] = "LaneMax";
const uint8_t gm_svraiu8av4tm8[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\340\001\000\000\022\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_svsegvhrrmf7h[18] = "cannot use {}: {}";
const uint8_t gm_svthrqc59hf0v[21] = "function returning `";
uint8_t gm_c08nj74isec[14] __attribute__((aligned(1)));
uint8_t gm_c1m49utf476[4] __attribute__((aligned(1)));
uint8_t gm_c1tvtibs17m[11] __attribute__((aligned(1)));
uint8_t gm_c23qivcb2qk[16] __attribute__((aligned(8)));
uint8_t gm_c2djvocabvl[16] __attribute__((aligned(1)));
uint8_t gm_c2pgnmr38k4[4] __attribute__((aligned(1)));
uint8_t gm_c3seud9n9be[1] __attribute__((aligned(1)));
uint8_t gm_c4h6cr14ilf[11] __attribute__((aligned(1)));
uint8_t gm_c4i68jc4bv0[16] __attribute__((aligned(8)));
uint8_t gm_c5d0d4t81rg[7] __attribute__((aligned(1)));
uint8_t gm_c5llbmao9o5[3] __attribute__((aligned(1)));
uint8_t gm_c62sev3r509[1] __attribute__((aligned(1)));
uint8_t gm_c7167246mac[5] __attribute__((aligned(1)));
uint8_t gm_c72j1g8d455[1] __attribute__((aligned(1)));
uint8_t gm_c7k2o7e3uv7[3] __attribute__((aligned(1)));
uint8_t gm_c84n42e55fh[14] __attribute__((aligned(1)));
uint8_t gm_c8lries3c2a[1442] __attribute__((aligned(1)));
uint8_t gm_c8tck9qi6pl[16] __attribute__((aligned(8)));
uint8_t gm_c90nlp6169j[1] __attribute__((aligned(1)));
uint8_t gm_c9smt58mcql[5] __attribute__((aligned(1)));
uint8_t gm_casq7h2b03a[12] __attribute__((aligned(1)));
uint8_t gm_cavtdnb9dpe[15] __attribute__((aligned(1)));
uint8_t gm_cdgbrlsjvrb[16] __attribute__((aligned(8)));
uint8_t gm_cdi7ltupqm6[10] __attribute__((aligned(1)));
uint8_t gm_ce4024rufik[15] __attribute__((aligned(1)));
uint8_t gm_cesm14gq4bu[7] __attribute__((aligned(1)));
uint8_t gm_cf1srcc1v0o[11] __attribute__((aligned(1)));
uint8_t gm_cfhcj0skuhf[18] __attribute__((aligned(1)));
uint8_t gm_cfj2cvp0vvo[14] __attribute__((aligned(1)));
uint8_t gm_cg2gm9ndqso[4] __attribute__((aligned(1)));
uint8_t gm_cg9qjv4r84d[12] __attribute__((aligned(1)));
uint8_t gm_cga37fdnf9v[11] __attribute__((aligned(1)));
uint8_t gm_cgelusdvikr[150] __attribute__((aligned(1)));
uint8_t gm_ch037afqg9e[8] __attribute__((aligned(1)));
uint8_t gm_ci3ce1h10ud[41] __attribute__((aligned(1)));
uint8_t gm_cj6ujl6k5d4[13] __attribute__((aligned(1)));
uint8_t gm_cjcr2ffc4p4[7] __attribute__((aligned(1)));
uint8_t gm_cjvio2kihdk[3] __attribute__((aligned(1)));
uint8_t gm_ck18kd1aefa[14] __attribute__((aligned(1)));
uint8_t gm_ckl5vopi62l[15] __attribute__((aligned(1)));
uint8_t gm_ckrhi684euh[87] __attribute__((aligned(1)));
uint8_t gm_cmr76t2omsb[10] __attribute__((aligned(1)));
uint8_t gm_cnpqnha93in[8] __attribute__((aligned(1)));
uint8_t gm_cnsqull7v4u[13] __attribute__((aligned(1)));
uint8_t gm_cqphq1ertk0[26] __attribute__((aligned(1)));
uint8_t gm_cr2tjd0cqon[16] __attribute__((aligned(1)));
uint8_t gm_crh91qok27k[45] __attribute__((aligned(1)));
uint8_t gm_crmb99jt6u6[11] __attribute__((aligned(1)));
uint8_t gm_crmiom2fklj[92] __attribute__((aligned(1)));
uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
uint8_t gm_cta94eldhtr[24] __attribute__((aligned(1)));
uint8_t gm_cugnnent2c3[11] __attribute__((aligned(1)));
uint8_t gm_cui00p62je5[7] __attribute__((aligned(1)));
uint8_t gm_cuk7s23am23[4] __attribute__((aligned(1)));
uint8_t gm_cvjv0vg9s4c[7] __attribute__((aligned(1)));
uint8_t gm_k0gn7sac5pt[40] __attribute__((aligned(8)));
uint8_t gm_k3ho3jie8mm[40] __attribute__((aligned(8)));
uint8_t gm_k3pvi109nh9[40] __attribute__((aligned(8)));
uint8_t gm_k4hgfv8dkha[40] __attribute__((aligned(8)));
uint8_t gm_kcu3lrib6j4[40] __attribute__((aligned(8)));
uint8_t gm_kdmasfkh2un[40] __attribute__((aligned(8)));
uint8_t gm_kdoa98a1tm0[40] __attribute__((aligned(8)));
uint8_t gm_khvdju4mt3n[40] __attribute__((aligned(8)));
uint8_t gm_kkpcsaqkj4k[40] __attribute__((aligned(8)));
uint8_t gm_kl5b02juaoa[40] __attribute__((aligned(8)));
uint8_t gm_klsha2ujn8u[40] __attribute__((aligned(8)));
uint8_t gm_kp550dhuma7[40] __attribute__((aligned(8)));
uint8_t gm_kr85ffi32os[24] __attribute__((aligned(8)));
uint8_t gm_krjmevl5bkc[40] __attribute__((aligned(8)));
uint8_t gm_krr4pbvd2tj[40] __attribute__((aligned(8)));
uint8_t gm_ksdcjqjfqsj[40] __attribute__((aligned(8)));
uint8_t gm_ksrlu8s6v53[40] __attribute__((aligned(8)));
uint8_t gm_ktbnd3fuoao[40] __attribute__((aligned(8)));
void *const gm_hlcpgr8cgvbp[] = {(void *)gm_f_args_6h3q549, (void *)gm_f_var_otupsib, (void *)gm_f_exit_jfn9nl4};
void *const gm_hlfkkokngdk1[] = {(void *)gm_f_for_body_l1ll8kh};
void *const gm_hlinnrcqopuj[] = {(void *)gm_f_for_body_kocuepv};
void *const gm_hln6al4vijjv[] = {(void *)gm_f_for_body_8edr2np};
void *const gm_hlnhfkgd9bs8[] = {(void *)gm_f_for_body_g88hj6k};

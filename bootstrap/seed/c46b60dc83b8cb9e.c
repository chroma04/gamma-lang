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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_taqu3436kubri;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_tekl8c9g34a9k;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint64_t f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_tm9oc6v1he136;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
typedef struct { gm_b16_8 f0; gm_tiilmfd16v3fc f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; gm_tnpifv20irnph f3; gm_b16_8 f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_t9qmk75lrp9p4;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
_Static_assert(sizeof(gm_taqu3436kubri) == 20 && _Alignof(gm_taqu3436kubri) == 4 && offsetof(gm_taqu3436kubri, f0) == 0 && offsetof(gm_taqu3436kubri, f1) == 4 && offsetof(gm_taqu3436kubri, f2) == 8, "layout of `HandlerBreak`");
typedef struct { uint64_t f0; void * f1; uint32_t f2; gm_t9qkh0rsooc8d f3; uint8_t p4[3]; gm_tiilmfd16v3fc f4; } gm_tbeui4bbnm52b;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tekl8c9g34a9k) == 32 && _Alignof(gm_tekl8c9g34a9k) == 8 && offsetof(gm_tekl8c9g34a9k, f0) == 0 && offsetof(gm_tekl8c9g34a9k, f1) == 16 && offsetof(gm_tekl8c9g34a9k, f2) == 20, "layout of `DeclWithCheck`");
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tm9oc6v1he136) == 32 && _Alignof(gm_tm9oc6v1he136) == 8 && offsetof(gm_tm9oc6v1he136, f0) == 0 && offsetof(gm_tm9oc6v1he136, f1) == 8 && offsetof(gm_tm9oc6v1he136, f2) == 24, "layout of `TemplateInstance`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_toceffb697p0u f0; uint8_t f1; uint8_t pe[3]; } gm_trj4m8pa99qp7;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_t9qmk75lrp9p4) == 96 && _Alignof(gm_t9qmk75lrp9p4) == 8 && offsetof(gm_t9qmk75lrp9p4, f0) == 0 && offsetof(gm_t9qmk75lrp9p4, f1) == 16 && offsetof(gm_t9qmk75lrp9p4, f2) == 40 && offsetof(gm_t9qmk75lrp9p4, f3) == 48 && offsetof(gm_t9qmk75lrp9p4, f4) == 64 && offsetof(gm_t9qmk75lrp9p4, f5) == 80, "layout of `HandlerTemplate`");
_Static_assert(sizeof(gm_tbeui4bbnm52b) == 48 && _Alignof(gm_tbeui4bbnm52b) == 8 && offsetof(gm_tbeui4bbnm52b, f0) == 0 && offsetof(gm_tbeui4bbnm52b, f1) == 8 && offsetof(gm_tbeui4bbnm52b, f2) == 16 && offsetof(gm_tbeui4bbnm52b, f3) == 20 && offsetof(gm_tbeui4bbnm52b, f4) == 24, "layout of `PendingClause`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { uint64_t f0; gm_tvc83pqnepc2e f1; } gm_tcth15utccicj;
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trj4m8pa99qp7) == 20 && _Alignof(gm_trj4m8pa99qp7) == 4 && offsetof(gm_trj4m8pa99qp7, f0) == 0 && offsetof(gm_trj4m8pa99qp7, f1) == 16, "layout of `?NeedsAt`");
typedef struct { uint64_t f0; gm_tbeui4bbnm52b f1; } gm_tt01bthgiqsjo;
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_thdmr9qicg35r f2; uint8_t pe[4]; } gm_tti6qnvf9jg1p;
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tcth15utccicj) == 72 && _Alignof(gm_tcth15utccicj) == 8 && offsetof(gm_tcth15utccicj, f0) == 0 && offsetof(gm_tcth15utccicj, f1) == 8, "layout of `(i: usize, x: HandlerDeclInfo)`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tt01bthgiqsjo) == 56 && _Alignof(gm_tt01bthgiqsjo) == 8 && offsetof(gm_tt01bthgiqsjo, f0) == 0 && offsetof(gm_tt01bthgiqsjo, f1) == 8, "layout of `(i: usize, x: PendingClause)`");
_Static_assert(sizeof(gm_tti6qnvf9jg1p) == 72 && _Alignof(gm_tti6qnvf9jg1p) == 8 && offsetof(gm_tti6qnvf9jg1p, f0) == 0 && offsetof(gm_tti6qnvf9jg1p, f1) == 40 && offsetof(gm_tti6qnvf9jg1p, f2) == 56, "layout of `InferTable`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
typedef struct { void * f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_tti6qnvf9jg1p f4; gm_t88rcucg1b7uk f5; uint8_t p6[4]; gm_tnpifv20irnph f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t p10[4]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_t9805qtonbsag f13; gm_trj4m8pa99qp7 f14; uint8_t p15[4]; gm_trqfvmjtt4sij f15; uint8_t f16; uint8_t p17[7]; gm_trqfvmjtt4sij f17; uint8_t f18; uint8_t p19[7]; uint64_t f19; gm_trqfvmjtt4sij f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; uint8_t f24; uint8_t p25[7]; gm_trqfvmjtt4sij f25; uint8_t f26; uint8_t p27[3]; gm_tkn42u7na3t81 f27; uint8_t p28[4]; gm_trqfvmjtt4sij f28; gm_t403eokj7ifg9 f29; gm_trqfvmjtt4sij f30; gm_trqfvmjtt4sij f31; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; uint8_t f38; uint8_t p39[7]; gm_trqfvmjtt4sij f39; uint8_t f40; uint8_t p41[7]; void * f41; } gm_tqgtfeqqnkjsa;
_Static_assert(sizeof(gm_tqgtfeqqnkjsa) == 1304 && _Alignof(gm_tqgtfeqqnkjsa) == 8 && offsetof(gm_tqgtfeqqnkjsa, f0) == 0 && offsetof(gm_tqgtfeqqnkjsa, f1) == 8 && offsetof(gm_tqgtfeqqnkjsa, f2) == 16 && offsetof(gm_tqgtfeqqnkjsa, f3) == 56 && offsetof(gm_tqgtfeqqnkjsa, f4) == 96 && offsetof(gm_tqgtfeqqnkjsa, f5) == 168 && offsetof(gm_tqgtfeqqnkjsa, f6) == 176 && offsetof(gm_tqgtfeqqnkjsa, f7) == 192 && offsetof(gm_tqgtfeqqnkjsa, f8) == 232 && offsetof(gm_tqgtfeqqnkjsa, f9) == 272 && offsetof(gm_tqgtfeqqnkjsa, f10) == 280 && offsetof(gm_tqgtfeqqnkjsa, f11) == 320 && offsetof(gm_tqgtfeqqnkjsa, f12) == 360 && offsetof(gm_tqgtfeqqnkjsa, f13) == 400 && offsetof(gm_tqgtfeqqnkjsa, f14) == 408 && offsetof(gm_tqgtfeqqnkjsa, f15) == 432 && offsetof(gm_tqgtfeqqnkjsa, f16) == 472 && offsetof(gm_tqgtfeqqnkjsa, f17) == 480 && offsetof(gm_tqgtfeqqnkjsa, f18) == 520 && offsetof(gm_tqgtfeqqnkjsa, f19) == 528 && offsetof(gm_tqgtfeqqnkjsa, f20) == 536 && offsetof(gm_tqgtfeqqnkjsa, f21) == 576 && offsetof(gm_tqgtfeqqnkjsa, f22) == 616 && offsetof(gm_tqgtfeqqnkjsa, f23) == 656 && offsetof(gm_tqgtfeqqnkjsa, f24) == 696 && offsetof(gm_tqgtfeqqnkjsa, f25) == 704 && offsetof(gm_tqgtfeqqnkjsa, f26) == 744 && offsetof(gm_tqgtfeqqnkjsa, f27) == 748 && offsetof(gm_tqgtfeqqnkjsa, f28) == 760 && offsetof(gm_tqgtfeqqnkjsa, f29) == 800 && offsetof(gm_tqgtfeqqnkjsa, f30) == 920 && offsetof(gm_tqgtfeqqnkjsa, f31) == 960 && offsetof(gm_tqgtfeqqnkjsa, f32) == 1000 && offsetof(gm_tqgtfeqqnkjsa, f33) == 1040 && offsetof(gm_tqgtfeqqnkjsa, f34) == 1080 && offsetof(gm_tqgtfeqqnkjsa, f35) == 1120 && offsetof(gm_tqgtfeqqnkjsa, f36) == 1160 && offsetof(gm_tqgtfeqqnkjsa, f37) == 1200 && offsetof(gm_tqgtfeqqnkjsa, f38) == 1240 && offsetof(gm_tqgtfeqqnkjsa, f39) == 1248 && offsetof(gm_tqgtfeqqnkjsa, f40) == 1288 && offsetof(gm_tqgtfeqqnkjsa, f41) == 1296, "layout of `FnCx`");
struct gm_fr_arg_may_unify_qmi580g {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* a */
    gm_toa2vhe3l4hfr l2; /* b */
    uint8_t ret;
};
struct gm_fr_array_kind_same_rl3epej {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_clause_1b0dqf0 {
    gm_frame h;
    gm_tbeui4bbnm52b l1; /* p */
    gm_tqqhoivahirr0 l2; /* sig */
    gm_td7pkamr64l0c l4; /* outer */
    gm_trqfvmjtt4sij l6; /* runtime */
    gm_t5v0c8ia6a5mo l9; /* q */
    gm_tqgtfeqqnkjsa l10; /* cx */
    gm_b0_1 ret;
};
struct gm_fr_check_decl_breaks_pk9cnmn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* reported */
    gm_t4gut2c9fj1kt l14; /* e */
    gm_b0_1 ret;
};
struct gm_fr_check_handler_clauses_n3lq2s1 {
    gm_frame h;
    gm_tbeui4bbnm52b l4; /* p */
    gm_b0_1 ret;
};
struct gm_fr_check_pending_clause_33df87a {
    gm_frame h;
    gm_tbeui4bbnm52b l5; /* p */
    uint8_t ret;
};
struct gm_fr_decl_clause_0i5kmeb {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_declare_handler_gd3gpcg {
    gm_frame h;
    void * l0; /* s */
    gm_trqfvmjtt4sij l3; /* params */
    gm_tf2orqcrphmek l7; /* p */
    gm_trqfvmjtt4sij l21; /* generics */
    gm_t5v0c8ia6a5mo l24; /* p */
    gm_trqfvmjtt4sij l26; /* seen */
    gm_t5v0c8ia6a5mo l29; /* p */
    gm_tammvh1iespi3 l31; /* info */
    gm_tvc83pqnepc2e l35; /* d */
    gm_t9qmk75lrp9p4 l39; /* probe */
    gm_tvc83pqnepc2e l42; /* d */
    gm_t9qmk75lrp9p4 l45; /* t */
    gm_toa2vhe3l4hfr l50; /* x */
    gm_t99vmhfllumbh l53; /* c */
    gm_tv0k2vg3054a6 l57; /* o */
    gm_trqfvmjtt4sij l58; /* clauses */
    gm_tv0k2vg3054a6 l61; /* o */
    gm_trqfvmjtt4sij l64; /* cparams */
    gm_b0_1 ret;
};
struct gm_fr_declare_handlers_c3ac89r {
    gm_frame h;
    gm_tp8h583g7s5hd l4; /* o */
    gm_b0_1 ret;
};
struct gm_fr_fn_51177of {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_handler_template_args_dlm07kb {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* binds */
    gm_trqfvmjtt4sij l4; /* args */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_handlers_of_type_5gareb4 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_tvc83pqnepc2e l6; /* d */
    gm_t9qmk75lrp9p4 l10; /* tpl */
    gm_trqfvmjtt4sij l27; /* clauses */
    gm_b16_8 ret;
};
struct gm_fr_ident_is_uo88ron {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_may_unify_0noeh56 {
    gm_frame h;
    gm_t53at80pfdus3 l3; /* ka */
    gm_t53at80pfdus3 l4; /* kb */
    gm_toa2vhe3l4hfr l54; /* p */
    uint8_t ret;
};
struct gm_fr_template_instance_is_ovv1pcj {
    gm_frame h;
    gm_t9qmk75lrp9p4 l4; /* tpl */
    uint8_t ret;
};
struct gm_fr_template_map_lfm02i0 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* map */
    gm_b16_8 ret;
};
struct gm_fr_template_probe_is_d1ep4c6 {
    gm_frame h;
    uint8_t ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g(void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g_c(void * a0, gm_toa2vhe3l4hfr a1, gm_toa2vhe3l4hfr a2);
__attribute__((aligned(4))) uint8_t gm_f_array_kind_same_rl3epej(gm_tnnjeerolos0a a0, gm_tnnjeerolos0a a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_clause_1b0dqf0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_decl_breaks_pk9cnmn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_handler_clauses_n3lq2s1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_check_pending_clause_33df87a(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_clause_signature_9v4rkv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_clause_value_tpeb80m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_key_c6mirrv(void * a0, void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_decl_clause_0i5kmeb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_declare_clause_params_7bqbscg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handler_gd3gpcg(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handlers_c3ac89r(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
__attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vi643oe(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void gm_f_format_74vh99o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8h6eueo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_klj2nqm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lveqqdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mgpfvh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tv3i6qj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_handler_template_args_dlm07kb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_may_unify_0noeh56(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_9gv7k4m(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_b9c0fdf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_mi6l2kj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_qkkb0em(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_remove_76216q8(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_eref_kf9o1q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_template_instance_is_ovv1pcj(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_template_map_lfm02i0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_template_probe_is_d1ep4c6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_sgjkgsqdq34nu[37];
extern const uint8_t gm_sho5nuaja1vvf[5];
extern const uint8_t gm_shqja2jbdo2o0[58];
extern const char gm_srcgr2mvgis129a[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g(void * a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arg_may_unify_qmi580g F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* y */
    gm_t428ue2u3hf5g gm_l5; /* x */
    gm_t428ue2u3hf5g gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 478u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t4 != ((uint8_t)0ULL)) goto mn3;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
        gm_l3 = t5;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t8 != ((uint8_t)0ULL)) goto mn7;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 8));
        gm_l4 = t9;
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l3;
        gm_t88rcucg1b7uk t12 = gm_l4;
        uint8_t t13 = gm_f_may_unify_0noeh56(t10, GM_CONV(gm_csguankko4lmhd, t11), GM_CONV(gm_csguankko4lmhd, t12));
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
        goto me6;
mn7:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn15;
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 8);
        gm_t88rcucg1b7uk t19 = gm_l3;
        gm_t53at80pfdus3 t20;
        gm_f_kind_jcnt4hm((void *)&t20, t18, GM_CONV(gm_csguankko4lmhd, t19));
        gm_t53at80pfdus3 t21 = t20;
        uint8_t t22;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
        if (t25 != ((uint8_t)19ULL)) goto mn24;
        t22 = ((uint8_t)1);
        goto me23;
mn24:;
        t22 = ((uint8_t)0);
        goto me23;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 227, 33);
me23:;
        uint8_t t27 = 0;
        F.ret = t22;
        goto gm_fret;
        goto me6;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 225, 21);
me6:;
        goto me2;
mn3:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t29 != ((uint8_t)1ULL)) goto mn28;
        gm_t428ue2u3hf5g t30 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)a1) + 8));
        gm_l5 = t30;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t33 != ((uint8_t)1ULL)) goto mn32;
        gm_t428ue2u3hf5g t34 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&F.l2) + 8));
        gm_l6 = t34;
        void * t35 = (void *)((uint8_t *)&gm_l5);
        void * t36 = (void *)((uint8_t *)&gm_l6);
        int32_t t37 = gm_f_cmp_key_c6mirrv(t35, t36);
        uint8_t t38 = ((t37 == ((int32_t)0LL)));
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
        goto me31;
mn32:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t41 != ((uint8_t)0ULL)) goto mn40;
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 8));
        gm_l7 = t42;
        void * t43 = gm_l0;
        void * t44 = (void *)(((uint8_t *)t43) + 8);
        gm_t88rcucg1b7uk t45 = gm_l7;
        gm_t53at80pfdus3 t46;
        gm_f_kind_jcnt4hm((void *)&t46, t44, GM_CONV(gm_csguankko4lmhd, t45));
        gm_t53at80pfdus3 t47 = t46;
        uint8_t t48;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
        if (t51 != ((uint8_t)19ULL)) goto mn50;
        t48 = ((uint8_t)1);
        goto me49;
mn50:;
        t48 = ((uint8_t)0);
        goto me49;
mn52:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 231, 32);
me49:;
        uint8_t t53 = 0;
        F.ret = t48;
        goto gm_fret;
        goto me31;
mn40:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 229, 22);
me31:;
        goto me2;
mn28:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 224, 5);
me2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g_c(void * a0, gm_toa2vhe3l4hfr a1, gm_toa2vhe3l4hfr a2) { return gm_f_arg_may_unify_qmi580g(a0, &a1, &a2); }
inline __attribute__((aligned(4))) uint8_t gm_f_array_kind_same_rl3epej(gm_tnnjeerolos0a a0, gm_tnnjeerolos0a a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_array_kind_same_rl3epej F;
    gm_tnnjeerolos0a gm_l0; /* a */
    gm_tnnjeerolos0a gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* y */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    uint8_t t11 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
    t1 = t11;
    goto me2;
mn9:;
ma4:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)1ULL)) goto mn12;
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l0) + 4));
    gm_l2 = t14;
    uint8_t t15;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)1ULL)) goto ie16;
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l1) + 4));
    gm_l3 = t18;
    gm_t88rcucg1b7uk t20 = gm_l2;
    gm_t88rcucg1b7uk t21 = gm_l3;
    gm_t88rcucg1b7uk t22 = t20;
    gm_t88rcucg1b7uk t23 = t21;
    uint8_t t24 = (gm_eq9m6mvtmgde(((uint8_t *)&t22), ((uint8_t *)&t23)));
    uint8_t t25 = t24;
    t15 = t25;
    goto ix16;
ie16:;
    uint8_t t27 = ((uint8_t)0);
    t15 = t27;
ix16:;
    t1 = t15;
    goto me2;
mn12:;
ma5:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t29 != ((uint8_t)2ULL)) goto mn28;
    uint8_t t30 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)2ULL)));
    t1 = t30;
    goto me2;
mn28:;
ma6:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)3ULL)) goto mn31;
    uint8_t t33 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)3ULL)));
    t1 = t33;
    goto me2;
mn31:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 216, 57);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_check_clause_1b0dqf0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_check_clause_1b0dqf0 F;
    void * gm_l0; /* s */
    void * gm_l3; /* c */
    gm_t66j1im9ip128 gm_l5; /* scope */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l11; /* ids */
    void * gm_l12; /* body */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* j */
    void * gm_l16; /* b */
    void * gm_l17; /* def */
    GM_STACK_CHECK(&F, 1579u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcgr2mvgis129a, 271, 12);
        gm_tqqhoivahirr0 t7 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136));
        F.l2 = t7;
    }
    {
        void * t8 = GM_LD(void *, (((uint8_t *)a1) + 8));
        gm_l3 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_f_enter_module_scope_011vo5a((void *)&F.l4, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = gm_l0;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 40));
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_f_scope_type_params_2di4dm5((void *)&gm_l5, (void *)NULL, cx, NULL, t10, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_af74s0q((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 40));
            gm_l7 = t15;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l8;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l8;
                    uint64_t t23 = (uintptr_t)t22;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t5v0c8ia6a5mo t27 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t23 * 56));
                    F.l9 = t27;
                    {
                        uint8_t t30;
                        if ((((((uint8_t *)&F.l9) + 40))[4] != 0)) goto mn32;
                        t30 = ((uint8_t)1);
                        goto me31;
mn32:;
                        t30 = ((uint8_t)0);
                        goto me31;
mn33:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 278, 12);
me31:;
                        if (!t30) goto ie29;
                        {
                            void * t35 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t36 = gm_f_push_o8u01sh(t35, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l9)));
                            gm_tftb1odc739t5 t37 = t36;
                            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcgr2mvgis129a, 278, 32);
                            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 277, 5);
me25:;
                }
lc16:;
                {
                    uint64_t t40 = gm_l8;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l8 = t43;
                }
            }
lb16:;
        }
    }
    {
        void * t44 = gm_l0;
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
        gm_tnpifv20irnph t47 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l2) + 64));
        gm_f_fn_cx_hgvq15e((void *)&F.l10, (void *)NULL, cx, NULL, t44, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        gm_t9805qtonbsag t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t49), t48);
        ((uint8_t *)&t49)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)&F.l10) + 400), t49);
    }
    {
        gm_tnqmlaad6uchl t50;
        memset((uint8_t *)&t50 + 1, 0, 3);
        memset((uint8_t *)&t50 + 28, 0, 4);
        gm_t9qkh0rsooc8d t51 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a1) + 20));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t50 + 0, t51);
        gm_t9805qtonbsag t52;
        memset(&t52, 0, sizeof t52);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t50 + 4, t52);
        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        gm_t9805qtonbsag t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t54), t53);
        ((uint8_t *)&t54)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t50 + 12, t54);
        gm_tkn42u7na3t81 t55;
        memset(&t55, 0, sizeof t55);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t50 + 20, t55);
        gm_tjvtgq0p0sehv t56;
        memset(&t56, 0, sizeof t56);
        GM_ST(gm_tjvtgq0p0sehv, (uint8_t *)&t50 + 32, t56);
        gm_tkn42u7na3t81 t57;
        memset(&t57, 0, sizeof t57);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t50 + 72, t57);
        gm_tiilmfd16v3fc t58 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)a1) + 24));
        gm_tqu3m2l3n8mnc t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t59), t58);
        ((uint8_t *)&t59)[24] = 1;
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t50 + 80, t59);
        gm_t403eokj7ifg9 t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 113, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 114, 0);
        GM_ST(uint32_t, (uint8_t *)&t60 + 116, 0);
        GM_ST(gm_tnqmlaad6uchl, ((uint8_t *)&t60), t50);
        ((uint8_t *)&t60)[112] = 1;
        GM_ST(gm_t403eokj7ifg9, (((uint8_t *)&F.l10) + 800), t60);
    }
    {
        void * t62 = gm_l3;
        uint64_t t63 = GM_LD(uintptr_t, (((uint8_t *)t62) + 32) + 8);
        uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t65 = (!(t63 == t64));
        if (!t65) goto ie61;
        {
            void * t67 = (void *)((uint8_t *)&F.l10);
            void * t68 = gm_l3;
            gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 56));
            gm_t3877pk7lv748 t70;
            void * t71 = gm_l3;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((((uint8_t *)t71) + 0) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t72);
            uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            GM_ST(uint64_t, (uint8_t *)&t70 + 16, t73);
            gm_tabb710d9mq2e t74;
            gm_f_format_lveqqdp((void *)&t74, (void *)NULL, cx, NULL, &t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t75 = t74;
            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srcgr2mvgis129a, 284, 26);
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
            gm_b16_8 t77 = t76;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_b0_1 t79 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t67, t69, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        void * t80 = (void *)((uint8_t *)&F.l10);
        void * t81 = gm_l3;
        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t83 = t82;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_b16_8 t85 = gm_f_declare_clause_params_7bqbscg((void *)NULL, cx, NULL, t80, t81, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t85;
    }
    {
        void * t86 = (void *)((uint8_t *)&F.l10);
        void * t87 = gm_l3;
        gm_t9qkh0rsooc8d t88 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a1) + 20));
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
        void * t90 = gm_f_clause_value_tpeb80m((void *)NULL, cx, NULL, t86, t87, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t90;
    }
    {
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            void * t92 = gm_l0;
            uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 720) + 0) + 8);
            gm_l13 = t93;
        }
        {
            for (;;) {
                uint64_t t95 = gm_l14;
                uint64_t t96 = gm_l13;
                uint8_t t97 = (t95 < t96);
                if (!t97) goto lb94;
                {
                    uint64_t t100 = gm_l14;
                    uint64_t t101 = t100;
                    uint64_t t104 = GM_LD(uint64_t, ((uint8_t *)&t101));
                    gm_l15 = t104;
                    {
                        uint64_t t106 = gm_l15;
                        uint64_t t107 = (uintptr_t)t106;
                        void * t108 = gm_l0;
                        uint64_t t109 = GM_LD(uintptr_t, ((((uint8_t *)t108) + 720) + 0) + 8);
                        if (t107 >= (uintptr_t)(t109)) gm_trap_index(t107, t109, gm_srcgr2mvgis129a, 290, 15);
                        void * t110 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t108) + 720) + 0)) + t107 * 20);
                        gm_l16 = t110;
                    }
                    {
                        void * t112 = gm_l16;
                        uint32_t t113 = GM_LD(uint32_t, (((uint8_t *)t112) + 0));
                        uint32_t t114 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
                        uint8_t t115 = ((t113 == t114));
                        if (!t115) goto ie111;
                        {
                            void * t117 = gm_l16;
                            uint8_t *t118 = (((uint8_t *)t117) + 4);
                            void * t119 = (void *)((uint8_t *)&F.l10);
                            void * t120 = gm_l16;
                            gm_t88rcucg1b7uk t121 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t120) + 4));
                            void * t122 = gm_l16;
                            gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 8));
                            gm_t88rcucg1b7uk t124 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t119, t121, t123);
                            if (gm_unw.kind) {
                                goto gm_ll98;
                            }
                            GM_ST(gm_t88rcucg1b7uk, t118, t124);
                        }
                        goto ix111;
ie111:;
ix111:;
                    }
                    goto me102;
mn103:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 289, 5);
me102:;
                }
lc94:;
                {
                    uint64_t t126 = gm_l14;
                    uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                    uint64_t t129 = r127;
                    gm_l14 = t129;
                }
            }
gm_ll98:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb94; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc94; }
            }
            goto gm_lad;
lb94:;
        }
    }
    {
        void * t130 = (void *)((uint8_t *)&F.l10);
        void * t131 = gm_l12;
        gm_b0_1 t132 = gm_f_finish_m8t0599((void *)NULL, cx, NULL, t130, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t133 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        uint64_t t134 = (uint64_t)t133;
        uint64_t t135 = (uintptr_t)t134;
        void * t136 = gm_l0;
        uint64_t t137 = GM_LD(uintptr_t, ((((uint8_t *)t136) + 384) + 0) + 8);
        if (t135 >= (uintptr_t)(t137)) gm_trap_index(t135, t137, gm_srcgr2mvgis129a, 294, 13);
        void * t138 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t136) + 384) + 0)) + t135 * 64);
        gm_l17 = t138;
    }
    {
        void * t139 = gm_l17;
        gm_b16_8 t140 = gm_l11;
        GM_ST(gm_b16_8, (((uint8_t *)t139) + 8), t140);
    }
    {
        void * t141 = gm_l17;
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l10) + 16) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)t141) + 24), t142);
    }
    {
        void * t143 = gm_l17;
        void * t144 = gm_l12;
        GM_ST(void *, (((uint8_t *)t143) + 40), t144);
    }
    {
        void * t145 = gm_l0;
        gm_f_unscope_type_params_bofhhep(t145, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l5)));
    }
    {
        void * t146 = gm_l0;
        gm_f_leave_module_scope_4hp5mc4(t146, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l4)));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_decl_breaks_pk9cnmn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_decl_breaks_pk9cnmn F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tekl8c9g34a9k gm_l4; /* w */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* d */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint32_t gm_l10; /* f */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_taqu3436kubri gm_l13; /* b */
    GM_STACK_CHECK(&F, 1089u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_7s4om10((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 760) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tekl8c9g34a9k t16 = GM_LD(gm_tekl8c9g34a9k, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        {
                            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                            gm_l5 = t19;
                        }
                        {
                            gm_l6 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t21 = gm_l6;
                                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                uint8_t t23 = (t21 < t22);
                                if (!t23) goto lb20;
                                {
                                    uint64_t t26 = gm_l6;
                                    uint64_t t27 = (uintptr_t)t26;
                                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                    uint32_t t29 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t27 * 4));
                                    uint32_t t30 = t29;
                                    uint32_t t33 = GM_LD(uint32_t, ((uint8_t *)&t30));
                                    gm_l7 = t33;
                                    {
                                        {
                                            uint32_t t36 = gm_l7;
                                            uint64_t t37 = (uint64_t)t36;
                                            uint64_t t38 = (uintptr_t)t37;
                                            void * t39 = gm_l0;
                                            uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)t39) + 544) + 0) + 8);
                                            if (t38 >= (uintptr_t)(t40)) gm_trap_index(t38, t40, gm_srcgr2mvgis129a, 308, 22);
                                            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t39) + 544) + 0)) + t38 * 64) + 48));
                                            gm_l8 = t41;
                                        }
                                        {
                                            gm_l9 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t43 = gm_l9;
                                                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                                uint8_t t45 = (t43 < t44);
                                                if (!t45) goto lb42;
                                                {
                                                    uint64_t t48 = gm_l9;
                                                    uint64_t t49 = (uintptr_t)t48;
                                                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                                    uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t49 * 4));
                                                    uint32_t t52 = t51;
                                                    uint32_t t55 = GM_LD(uint32_t, ((uint8_t *)&t52));
                                                    gm_l10 = t55;
                                                    {
                                                        {
                                                            void * t58 = gm_l0;
                                                            gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)t58) + 720) + 0));
                                                            gm_l11 = t59;
                                                        }
                                                        {
                                                            gm_l12 = ((uint64_t)0ULL);
                                                        }
                                                        {
                                                            for (;;) {
                                                                uint64_t t61 = gm_l12;
                                                                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                                uint8_t t63 = (t61 < t62);
                                                                if (!t63) goto lb60;
                                                                {
                                                                    uint64_t t66 = gm_l12;
                                                                    uint64_t t67 = (uintptr_t)t66;
                                                                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                                    gm_taqu3436kubri t71 = GM_LD(gm_taqu3436kubri, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t67 * 20));
                                                                    gm_l13 = t71;
                                                                    {
                                                                        uint8_t t74 = 0;
                                                                        uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 0));
                                                                        uint32_t t76 = gm_l10;
                                                                        uint8_t t77 = (!(t75 == t76));
                                                                        if (!t77) {
                                                                            gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                            gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                                                                            gm_t88rcucg1b7uk t80 = t78;
                                                                            gm_t88rcucg1b7uk t81 = t79;
                                                                            uint8_t t82 = (gm_eq9m6mvtmgde(((uint8_t *)&t80), ((uint8_t *)&t81)));
                                                                            t74 = t82;
                                                                        } else t74 = 1;
                                                                        if (!t74) goto ie73;
                                                                        {
                                                                            goto lc60;
                                                                        }
                                                                        goto ix73;
ie73:;
ix73:;
                                                                    }
                                                                    {
                                                                        uint8_t t85 = 0;
                                                                        uint8_t t86 = 0;
                                                                        void * t87 = gm_l0;
                                                                        void * t88 = (void *)(((uint8_t *)t87) + 8);
                                                                        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                        gm_t53at80pfdus3 t90;
                                                                        gm_f_kind_jcnt4hm((void *)&t90, t88, GM_CONV(gm_csguankko4lmhd, t89));
                                                                        gm_t53at80pfdus3 t91 = t90;
                                                                        uint8_t t92 = ((GM_LD(uint8_t, ((uint8_t *)&t91) + 0) == ((uint8_t)5ULL)));
                                                                        if (!t92) {
                                                                            void * t93 = gm_l0;
                                                                            void * t94 = (void *)(((uint8_t *)t93) + 8);
                                                                            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                            uint8_t t96 = gm_f_is_error_8k27gd1(t94, GM_CONV(gm_csguankko4lmhd, t95));
                                                                            t86 = t96;
                                                                        } else t86 = 1;
                                                                        if (!t86) {
                                                                            gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                                                                            gm_b16_8 t99 = t98;
                                                                            uint8_t t100 = 0;
                                                                            gm_tl874t2atnlh3 t101 = t97;
                                                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t99) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t99))) + i * 12, ((uint8_t *)&t101))) { t100 = 1; break; }
                                                                            uint8_t t102 = (uint8_t)(t100);
                                                                            t85 = t102;
                                                                        } else t85 = 1;
                                                                        if (!t85) goto ie84;
                                                                        {
                                                                            goto lc60;
                                                                        }
                                                                        goto ix84;
ie84:;
ix84:;
                                                                    }
                                                                    {
                                                                        void * t104 = (void *)((uint8_t *)&F.l1);
                                                                        gm_tl874t2atnlh3 t105 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                        gm_tftb1odc739t5 t106 = gm_f_push_0mk7udh(t104, GM_CONV(gm_cseahvg8v7p0vs, t105));
                                                                        gm_tftb1odc739t5 t107 = t106;
                                                                        if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcgr2mvgis129a, 312, 21);
                                                                        gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                                                                    }
                                                                    {
                                                                        gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                        gm_t6j6smj7qoctu t110;
                                                                        void * t111 = gm_l0;
                                                                        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                        gm_b16_8 t113 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t111, t112);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll64;
                                                                        }
                                                                        GM_ST(gm_b16_8, (uint8_t *)&t110 + 0, t113);
                                                                        void * t114 = gm_l0;
                                                                        gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                                                                        gm_b16_8 t116 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t114, t115);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll64;
                                                                        }
                                                                        GM_ST(gm_b16_8, (uint8_t *)&t110 + 16, t116);
                                                                        gm_tabb710d9mq2e t117;
                                                                        gm_f_format_klj2nqm((void *)&t117, (void *)NULL, cx, NULL, &t110);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll64;
                                                                        }
                                                                        gm_tabb710d9mq2e t118 = t117;
                                                                        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_srcgr2mvgis129a, 313, 45);
                                                                        gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                                                                        gm_b16_8 t120 = t119;
                                                                        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                                                        gm_f_error_s6hn871((void *)&F.l14, (void *)NULL, cx, NULL, t109, t121);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll64;
                                                                        }
                                                                    }
                                                                    {
                                                                        void * t122 = (void *)((uint8_t *)&F.l14);
                                                                        gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 20));
                                                                        gm_b16_8 t124;
                                                                        GM_ST(void *, (uint8_t *)&t124, (void *)(gm_sgjkgsqdq34nu));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(36));
                                                                        gm_f_add_label_50fj7vv(t122, GM_CONV(gm_cseahvg8v7p0vs, t123), t124);
                                                                    }
                                                                    {
                                                                        void * t125 = gm_l0;
                                                                        gm_b0_1 t126 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t125, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l14)));
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll64;
                                                                        }
                                                                    }
                                                                    goto me69;
mn70:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 309, 17);
me69:;
                                                                }
lc60:;
                                                                {
                                                                    uint64_t t128 = gm_l12;
                                                                    uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                                                                    uint64_t t131 = r129;
                                                                    gm_l12 = t131;
                                                                }
                                                            }
gm_ll64:;
                                                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                                                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
                                                            }
                                                            goto gm_ll46;
lb60:;
                                                        }
                                                    }
                                                    goto me53;
mn54:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 308, 13);
me53:;
                                                }
lc42:;
                                                {
                                                    uint64_t t133 = gm_l9;
                                                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                                                    uint64_t t136 = r134;
                                                    gm_l9 = t136;
                                                }
                                            }
gm_ll46:;
                                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb42; }
                                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc42; }
                                            }
                                            goto gm_ll24;
lb42:;
                                        }
                                    }
                                    goto me31;
mn32:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 307, 9);
me31:;
                                }
lc20:;
                                {
                                    uint64_t t138 = gm_l6;
                                    uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                                    uint64_t t141 = r139;
                                    gm_l6 = t141;
                                }
                            }
gm_ll24:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
                            }
                            goto gm_ll9;
lb20:;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 306, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t143 = gm_l3;
                    uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                    uint64_t t146 = r144;
                    gm_l3 = t146;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_handler_clauses_n3lq2s1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_handler_clauses_n3lq2s1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* taken */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 278u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 680) + 0));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        uint8_t *t5 = (((uint8_t *)t4) + 680);
        gm_f_list_4kh12ib((void *)&(*(gm_trqfvmjtt4sij *)t5), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t7 = gm_l1;
            gm_l2 = t7;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l3;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l3;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tbeui4bbnm52b t19 = GM_LD(gm_tbeui4bbnm52b, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 48));
                    F.l4 = t19;
                    {
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 0));
                        uint64_t t23 = (uintptr_t)t22;
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 200) + 0) + 8);
                        if (t23 >= (uintptr_t)(t26)) gm_trap_index(t23, t26, gm_srcgr2mvgis129a, 250, 12);
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 200) + 0)) + t23 * 184) + 112));
                        if (!t27) goto ie21;
                        {
                            void * t29 = gm_l0;
                            void * t30 = (void *)(((uint8_t *)t29) + 680);
                            gm_tftb1odc739t5 t31 = gm_f_push_qkkb0em(t30, &(*(gm_tbeui4bbnm52b *)((uint8_t *)&F.l4)));
                            gm_tftb1odc739t5 t32 = t31;
                            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcgr2mvgis129a, 251, 13);
                            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                        }
                        goto ix21;
ie21:;
                        {
                            void * t35 = gm_l0;
                            gm_b0_1 t36 = gm_f_check_clause_1b0dqf0((void *)NULL, cx, NULL, t35, &(*(gm_tbeui4bbnm52b *)((uint8_t *)&F.l4)));
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                        }
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 247, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l3 = t41;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_check_pending_clause_33df87a(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_check_pending_clause_33df87a F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 426u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 680) + 0));
            gm_b16_8 t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_l2 = t6;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l3;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    gm_tt01bthgiqsjo t13;
                    uint64_t t14 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
                    uint64_t t15 = gm_l3;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t13 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 48), 48);
                    gm_tt01bthgiqsjo t18 = t13;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l4 = t21;
                    gm_tbeui4bbnm52b t22 = GM_LD(gm_tbeui4bbnm52b, (((uint8_t *)&t18) + 8));
                    F.l5 = t22;
                    {
                        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 16));
                        uint32_t t26 = gm_l1;
                        uint8_t t27 = (!(t25 == t26));
                        if (!t27) goto ie24;
                        {
                            goto lc7;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    {
                        void * t29 = gm_l0;
                        void * t30 = gm_l0;
                        void * t31 = (void *)(((uint8_t *)t30) + 680);
                        uint64_t t32 = gm_l4;
                        gm_tbeui4bbnm52b t33;
                        gm_f_remove_76216q8((void *)&t33, t31, t32);
                        gm_b0_1 t34 = gm_f_check_clause_1b0dqf0((void *)NULL, cx, NULL, t29, &t33);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                    }
                    {
                        uint8_t t35 = 0;
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 261, 5);
me19:;
                }
lc7:;
                {
                    uint64_t t37 = gm_l3;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l3 = t40;
                }
            }
gm_ll11:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
            }
            goto gm_lad;
lb7:;
        }
    }
    {
        uint8_t t41 = 0;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_decl_clause_0i5kmeb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_clause_0i5kmeb F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 292u);
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
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 64) + 8);
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
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 64) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcgr2mvgis129a, 238, 12);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 64)) + t19 * 72) + 0) + 0));
                        gm_b16_8 t23 = gm_l1;
                        gm_b16_8 t24 = t22;
                        gm_b16_8 t25 = t23;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie17;
                        {
                            uint64_t t28 = gm_l4;
                            gm_tf5srh4mp4b3i t29;
                            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t29), t28);
                            ((uint8_t *)&t29)[8] = 1;
                            uint8_t t30 = 0;
                            F.ret = t29;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 237, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t36;
        memset(&t36, 0, sizeof t36);
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_handler_gd3gpcg(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_declare_handler_gd3gpcg F;
    uint64_t gm_l1; /* module */
    void * gm_l2; /* h */
    gm_b16_8 gm_l4; /* ps */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tnnjeerolos0a gm_l8; /* kind */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    void * gm_l10; /* t */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t08olq97rscn3 gm_l13; /* n */
    uint32_t gm_l14; /* pid */
    gm_t66j1im9ip128 gm_l15; /* saved */
    gm_tqu3m2l3n8mnc gm_l16; /* r */
    gm_t88rcucg1b7uk gm_l17; /* for_ty */
    gm_tnpifv20irnph gm_l18; /* effects */
    gm_b16_8 gm_l19; /* list_ */
    gm_tiilmfd16v3fc gm_l20; /* eref */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint32_t gm_l25; /* g */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint32_t gm_l30; /* g */
    uint8_t gm_l32; /* duplicate */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* k */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint8_t gm_l46; /* all */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    uint64_t gm_l49; /* i */
    gm_b16_8 gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint8_t gm_l54; /* known */
    gm_b16_8 gm_l55; /* <tmp> */
    uint64_t gm_l56; /* <tmp> */
    gm_b16_8 gm_l59; /* <tmp> */
    uint64_t gm_l60; /* <tmp> */
    uint64_t gm_l62; /* ci */
    gm_tep2drsl9408b gm_l63; /* sig */
    uint32_t gm_l65; /* fn_id */
    GM_STACK_CHECK(&F, 5161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_af74s0q((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l2;
        if (!(((((uint8_t *)t3) + 0))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_l4 = t4;
        {
            {
                gm_b16_8 t7 = gm_l4;
                gm_l5 = t7;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t9 = gm_l6;
                    uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t11 = (t9 < t10);
                    if (!t11) goto lb8;
                    {
                        uint64_t t14 = gm_l6;
                        uint64_t t15 = (uintptr_t)t14;
                        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_tf2orqcrphmek t19 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t15 * 56));
                        F.l7 = t19;
                        {
                            gm_tnnjeerolos0a t21;
                            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
                            gm_l8 = t21;
                        }
                        {
                            void * t22 = F.l0;
                            void * t23 = (void *)(((uint8_t *)t22) + 8);
                            gm_t88rcucg1b7uk t24 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t23);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            gm_l9 = t24;
                        }
                        {
                            void * t26 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
                            void * t27 = t26;
                            if (!(GM_LD(void *, ((uint8_t *)&t27)) != 0)) goto ie25;
                            void * t28 = GM_LD(void *, ((uint8_t *)&t27));
                            gm_l10 = t28;
                            {
                                void * t31 = gm_l10;
                                uint8_t t32 = gm_f_is_effects_kind_uutbf9u(t31);
                                if (!t32) goto ie30;
                                {
                                    gm_tnnjeerolos0a t34;
                                    GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1ULL));
                                    gm_l8 = t34;
                                }
                                goto ix30;
ie30:;
                                uint8_t t36 = 0;
                                void * t37 = gm_l10;
                                uint8_t t38;
                                uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t37) + 8) + 0));
                                if (t41 != ((uint8_t)6ULL)) goto mn40;
                                t38 = ((uint8_t)1);
                                goto me39;
mn40:;
                                t38 = ((uint8_t)0);
                                goto me39;
mn42:;
                                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 28, 32);
me39:;
                                if (t38) {
                                    void * t43 = gm_l10;
                                    gm_b16_8 t44;
                                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s2737i1iq57nf));
                                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
                                    uint8_t t45 = gm_f_ident_is_uo88ron(t43, t44);
                                    t36 = t45;
                                } else t36 = 0;
                                uint8_t t46 = (uint8_t)!t36;
                                if (!t46) goto ie35;
                                {
                                    void * t48 = F.l0;
                                    uint64_t t49 = gm_l1;
                                    void * t50 = gm_l10;
                                    gm_t88rcucg1b7uk t51 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t48, t49, t50);
                                    if (gm_unw.kind) {
                                        goto gm_ll12;
                                    }
                                    gm_l9 = t51;
                                }
                                {
                                    gm_t88rcucg1b7uk t52 = gm_l9;
                                    gm_tnnjeerolos0a t53;
                                    GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)2ULL));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t53 + 4), t52);
                                    gm_l8 = t53;
                                }
                                goto ix35;
ie35:;
ix35:;
ix30:;
                            }
                            goto ix25;
ie25:;
                            {
                                void * t55 = F.l0;
                                gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 40));
                                gm_b16_8 t57;
                                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_shqja2jbdo2o0));
                                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(57));
                                gm_b0_1 t58 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t55, t56, t57);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                            }
                            {
                                goto lc8;
                            }
ix25:;
                        }
                        {
                            {
                                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                                gm_l11 = t60;
                            }
                            {
                                gm_l12 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t62 = gm_l12;
                                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    uint8_t t64 = (t62 < t63);
                                    if (!t64) goto lb61;
                                    {
                                        uint64_t t67 = gm_l12;
                                        uint64_t t68 = (uintptr_t)t67;
                                        uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                        gm_t08olq97rscn3 t72 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t68 * 32));
                                        gm_l13 = t72;
                                        {
                                            void * t74 = F.l0;
                                            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                            gm_tnnjeerolos0a t76 = gm_l8;
                                            uint32_t t77 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t74, t75, t76);
                                            if (gm_unw.kind) {
                                                goto gm_ll65;
                                            }
                                            gm_l14 = t77;
                                        }
                                        {
                                            void * t78 = (void *)((uint8_t *)&F.l3);
                                            gm_t5v0c8ia6a5mo t79;
                                            memset((uint8_t *)&t79 + 49, 0, 7);
                                            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t79 + 0, t80);
                                            gm_t88rcucg1b7uk t81 = gm_l9;
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t79 + 16, t81);
                                            void * t82 = gm_l2;
                                            gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t82) + 80));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t79 + 20, t83);
                                            GM_ST(void *, (uint8_t *)&t79 + 32, ((void *)0));
                                            uint32_t t84 = gm_l14;
                                            gm_t9805qtonbsag t85;
                                            GM_ST(uint8_t, (uint8_t *)&t85 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t85 + 6, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t85), t84);
                                            ((uint8_t *)&t85)[4] = 1;
                                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t79 + 40, t85);
                                            GM_ST(uint8_t, (uint8_t *)&t79 + 48, ((uint8_t)0));
                                            gm_tftb1odc739t5 t86 = gm_f_push_o8u01sh(t78, &t79);
                                            gm_tftb1odc739t5 t87 = t86;
                                            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcgr2mvgis129a, 38, 17);
                                            gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                                        }
                                        goto me70;
mn71:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 36, 13);
me70:;
                                    }
lc61:;
                                    {
                                        uint64_t t90 = gm_l12;
                                        uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                                        uint64_t t93 = r91;
                                        gm_l12 = t93;
                                    }
                                }
gm_ll65:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb61; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc61; }
                                }
                                goto gm_ll12;
lb61:;
                            }
                        }
                        goto me17;
mn18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 22, 9);
me17:;
                    }
lc8:;
                    {
                        uint64_t t95 = gm_l6;
                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                        uint64_t t98 = r96;
                        gm_l6 = t98;
                    }
                }
gm_ll12:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
                }
                goto gm_lad;
lb8:;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t99 = F.l0;
        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t101 = t100;
        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
        gm_f_scope_type_params_2di4dm5((void *)&gm_l15, (void *)NULL, cx, NULL, t99, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t103 = F.l0;
        uint64_t t104 = gm_l1;
        void * t105 = gm_l2;
        void * t106 = GM_LD(void *, (((uint8_t *)t105) + 24));
        gm_f_resolve_effect_ref_bggirs2((void *)&gm_l16, (void *)NULL, cx, NULL, t103, t104, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t107 = F.l0;
        uint64_t t108 = gm_l1;
        void * t109 = gm_l2;
        void * t110 = GM_LD(void *, (((uint8_t *)t109) + 32));
        gm_t88rcucg1b7uk t111 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t107, t108, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t111;
    }
    {
        gm_tnpifv20irnph t112 = gm_f_empty_effects_r0iv3pv();
        gm_l18 = t112;
    }
    {
        void * t114 = gm_l2;
        if (!(((((uint8_t *)t114) + 40))[16] != 0)) goto ie113;
        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)t114) + 40));
        gm_l19 = t115;
        {
            void * t117 = F.l0;
            uint64_t t118 = gm_l1;
            gm_b16_8 t119 = gm_l19;
            gm_b16_8 t120 = t119;
            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
            gm_tnpifv20irnph t122 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t117, t118, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t122;
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        void * t123 = F.l0;
        gm_f_unscope_type_params_bofhhep(t123, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l15)));
    }
    {
        gm_tqu3m2l3n8mnc t124 = gm_l16;
        gm_tqu3m2l3n8mnc t125 = t124;
        gm_tiilmfd16v3fc t126;
        if (((((uint8_t *)&t125))[24] != 0)) {
            gm_tiilmfd16v3fc t127 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t125) + 0));
            t126 = t127;
        } else {
            gm_b0_1 t128;
            uint8_t t129 = 0;
            F.ret = t128;
            goto gm_fret;
        }
        gm_l20 = t126;
    }
    {
        void * t131 = F.l0;
        void * t132 = (void *)(((uint8_t *)t131) + 8);
        gm_t88rcucg1b7uk t133 = gm_l17;
        uint8_t t134 = gm_f_is_error_8k27gd1(t132, GM_CONV(gm_csguankko4lmhd, t133));
        if (!t134) goto ie130;
        {
            gm_b0_1 t136;
            uint8_t t137 = 0;
            F.ret = t136;
            goto gm_fret;
        }
        goto ix130;
ie130:;
ix130:;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l22 = t139;
        }
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t141 = gm_l23;
                uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                uint8_t t143 = (t141 < t142);
                if (!t143) goto lb140;
                {
                    uint64_t t146 = gm_l23;
                    uint64_t t147 = (uintptr_t)t146;
                    uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    gm_t5v0c8ia6a5mo t151 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t147 * 56));
                    F.l24 = t151;
                    {
                        if (!(((((uint8_t *)&F.l24) + 40))[4] != 0)) goto ie153;
                        uint32_t t154 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + 40));
                        gm_l25 = t154;
                        {
                            void * t156 = (void *)((uint8_t *)&F.l21);
                            uint32_t t157 = gm_l25;
                            gm_tftb1odc739t5 t158 = gm_f_push_im9d4ro(t156, t157);
                            gm_tftb1odc739t5 t159 = t158;
                            if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 1, gm_srcgr2mvgis129a, 52, 29);
                            gm_b0_1 t160 = GM_LD(gm_b0_1, (((uint8_t *)&t159) + 1));
                        }
                        goto ix153;
ie153:;
ix153:;
                    }
                    goto me149;
mn150:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 51, 5);
me149:;
                }
lc140:;
                {
                    uint64_t t162 = gm_l23;
                    uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                    uint64_t t165 = r163;
                    gm_l23 = t165;
                }
            }
lb140:;
        }
    }
    {
        uint64_t t167 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        uint8_t t168 = (t167 > ((uint64_t)0ULL));
        if (!t168) goto ie166;
        {
            gm_f_list_go2qg0b((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t170 = F.l0;
            gm_t88rcucg1b7uk t171 = gm_l17;
            gm_closure t172 = ((gm_closure){(void *)gm_f_fn_51177of, (void *)&F.h, cx});
            gm_f_walk_v858co7(t170, GM_CONV(gm_csguankko4lmhd, t171), t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_l27 = t174;
            }
            {
                gm_l28 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t176 = gm_l28;
                    uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t178 = (t176 < t177);
                    if (!t178) goto lb175;
                    {
                        uint64_t t181 = gm_l28;
                        uint64_t t182 = (uintptr_t)t181;
                        uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        gm_t5v0c8ia6a5mo t186 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t182 * 56));
                        F.l29 = t186;
                        {
                            gm_t9805qtonbsag t188 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l29) + 40));
                            gm_t9805qtonbsag t189 = t188;
                            uint32_t t190;
                            if (((((uint8_t *)&t189))[4] != 0)) {
                                uint32_t t191 = GM_LD(uint32_t, (((uint8_t *)&t189) + 0));
                                t190 = t191;
                            } else {
                                goto lc175;
                            }
                            gm_l30 = t190;
                        }
                        {
                            uint32_t t193 = gm_l30;
                            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                            gm_b16_8 t195 = t194;
                            uint8_t t196 = 0;
                            uint32_t t197 = t193;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t195) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t195))) + i * 4, ((uint8_t *)&t197))) { t196 = 1; break; }
                            uint8_t t198 = (uint8_t)(!t196);
                            if (!t198) goto ie192;
                            {
                                void * t200 = F.l0;
                                void * t201 = gm_l2;
                                gm_tl874t2atnlh3 t202 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t201) + 80));
                                gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                                gm_tnpifv20irnph t204;
                                GM_ST(gm_b16_8, ((uint8_t *)&t204 + 0), t203);
                                gm_tabb710d9mq2e t205;
                                gm_f_format_mgpfvh0((void *)&t205, (void *)NULL, cx, NULL, t204);
                                if (gm_unw.kind) {
                                    goto gm_ll179;
                                }
                                gm_tabb710d9mq2e t206 = t205;
                                if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 8, gm_srcgr2mvgis129a, 62, 33);
                                gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&t206) + 8));
                                gm_b16_8 t208 = t207;
                                gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                                gm_b0_1 t210 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t200, t202, t209);
                                if (gm_unw.kind) {
                                    goto gm_ll179;
                                }
                            }
                            {
                                gm_b0_1 t211;
                                uint8_t t212 = 0;
                                F.ret = t211;
                                goto gm_fret;
                            }
                            goto ix192;
ie192:;
ix192:;
                        }
                        goto me184;
mn185:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 59, 9);
me184:;
                    }
lc175:;
                    {
                        uint64_t t214 = gm_l28;
                        uint64_t r215; bool o216 = __builtin_add_overflow(t214, ((uint64_t)1ULL), &r215);
                        uint64_t t217 = r215;
                        gm_l28 = t217;
                    }
                }
gm_ll179:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb175; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc175; }
                }
                goto gm_lad;
lb175:;
            }
        }
        goto ix166;
ie166:;
ix166:;
    }
    {
        uint32_t t218 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 0));
        uint64_t t219 = (uint64_t)t218;
        uint64_t t220 = (uintptr_t)t219;
        void * t221 = F.l0;
        uint64_t t222 = GM_LD(uintptr_t, ((((uint8_t *)t221) + 1048) + 0) + 8);
        if (t220 >= (uintptr_t)(t222)) gm_trap_index(t220, t222, gm_srcgr2mvgis129a, 67, 13);
        gm_tammvh1iespi3 t223 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t221) + 1048) + 0)) + t220 * 56));
        F.l31 = t223;
    }
    {
        gm_l32 = ((uint8_t)0);
    }
    {
        uint64_t t225 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        uint8_t t226 = ((t225 == ((uint64_t)0ULL)));
        if (!t226) goto ie224;
        {
            {
                void * t229 = F.l0;
                gm_b16_8 t230 = GM_LD(gm_b16_8, ((((uint8_t *)t229) + 544) + 0));
                gm_l33 = t230;
            }
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t232 = gm_l34;
                    uint64_t t233 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint8_t t234 = (t232 < t233);
                    if (!t234) goto lb231;
                    {
                        uint64_t t237 = gm_l34;
                        uint64_t t238 = (uintptr_t)t237;
                        uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        gm_tvc83pqnepc2e t242 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t238 * 64));
                        F.l35 = t242;
                        {
                            void * t245 = (void *)(((uint8_t *)&F.l35) + 0);
                            void * t246 = (void *)((uint8_t *)&gm_l20);
                            int32_t t247 = gm_f_cmp_ref_8ntrqll(t245, t246);
                            uint8_t t248 = ((t247 == ((int32_t)0LL)));
                            if (!t248) goto ie244;
                            gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l35) + 24));
                            gm_t88rcucg1b7uk t250 = gm_l17;
                            gm_t88rcucg1b7uk t251 = t249;
                            gm_t88rcucg1b7uk t252 = t250;
                            uint8_t t253 = (gm_eq9m6mvtmgde(((uint8_t *)&t251), ((uint8_t *)&t252)));
                            if (!t253) goto ie244;
                            {
                                gm_l32 = ((uint8_t)1);
                            }
                            goto ix244;
ie244:;
ix244:;
                        }
                        goto me240;
mn241:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 72, 9);
me240:;
                    }
lc231:;
                    {
                        uint64_t t256 = gm_l34;
                        uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                        uint64_t t259 = r257;
                        gm_l34 = t259;
                    }
                }
lb231:;
            }
        }
        {
            uint8_t t261 = gm_l32;
            uint8_t t262 = (uint8_t)!t261;
            if (!t262) goto ie260;
            {
                {
                    gm_l37 = ((uint64_t)0ULL);
                }
                {
                    void * t265 = F.l0;
                    uint64_t t266 = GM_LD(uintptr_t, ((((uint8_t *)t265) + 592) + 0) + 8);
                    gm_l36 = t266;
                }
                {
                    for (;;) {
                        uint64_t t268 = gm_l37;
                        uint64_t t269 = gm_l36;
                        uint8_t t270 = (t268 < t269);
                        if (!t270) goto lb267;
                        {
                            uint64_t t273 = gm_l37;
                            uint64_t t274 = t273;
                            uint64_t t277 = GM_LD(uint64_t, ((uint8_t *)&t274));
                            gm_l38 = t277;
                            {
                                void * t280 = F.l0;
                                uint64_t t281 = gm_l38;
                                void * t282 = (void *)((uint8_t *)&gm_l20);
                                gm_t88rcucg1b7uk t283 = gm_l17;
                                uint8_t t284 = gm_f_template_instance_is_ovv1pcj((void *)NULL, cx, NULL, t280, t281, t282, t283);
                                if (gm_unw.kind) {
                                    goto gm_ll271;
                                }
                                if (!t284) goto ie279;
                                {
                                    gm_l32 = ((uint8_t)1);
                                }
                                goto ix279;
ie279:;
ix279:;
                            }
                            goto me275;
mn276:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 76, 13);
me275:;
                        }
lc267:;
                        {
                            uint64_t t287 = gm_l37;
                            uint64_t r288; bool o289 = __builtin_add_overflow(t287, ((uint64_t)1ULL), &r288);
                            uint64_t t290 = r288;
                            gm_l37 = t290;
                        }
                    }
gm_ll271:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb267; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc267; }
                    }
                    goto gm_lad;
lb267:;
                }
            }
            goto ix260;
ie260:;
ix260:;
        }
        goto ix224;
ie224:;
        {
            memset((uint8_t *)&F.l39 + 44, 0, 4);
            memset((uint8_t *)&F.l39 + 92, 0, 4);
            gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&F.l39 + 0, t292);
            memcpy((uint8_t *)&F.l39 + 16, ((uint8_t *)&gm_l20), 24);
            gm_t88rcucg1b7uk t293 = gm_l17;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l39 + 40, t293);
            gm_tnpifv20irnph t294 = gm_l18;
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&F.l39 + 48, t294);
            gm_b16_8 t295 = gm_f_empty_slice_d914i5u();
            GM_ST(gm_b16_8, (uint8_t *)&F.l39 + 64, t295);
            void * t296 = gm_l2;
            gm_tl874t2atnlh3 t297 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t296) + 80));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l39 + 80, t297);
        }
        {
            {
                void * t299 = F.l0;
                gm_b16_8 t300 = GM_LD(gm_b16_8, ((((uint8_t *)t299) + 544) + 0));
                gm_l40 = t300;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t302 = gm_l41;
                    uint64_t t303 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    uint8_t t304 = (t302 < t303);
                    if (!t304) goto lb301;
                    {
                        uint64_t t307 = gm_l41;
                        uint64_t t308 = (uintptr_t)t307;
                        uint64_t t309 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        gm_tvc83pqnepc2e t312 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t308 * 64));
                        F.l42 = t312;
                        {
                            void * t315 = F.l0;
                            void * t316 = (void *)((uint8_t *)&F.l39);
                            void * t317 = (void *)(((uint8_t *)&F.l42) + 0);
                            gm_t88rcucg1b7uk t318 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l42) + 24));
                            uint8_t t319 = gm_f_template_probe_is_d1ep4c6((void *)NULL, cx, NULL, t315, t316, t317, t318);
                            if (gm_unw.kind) {
                                goto gm_ll305;
                            }
                            if (!t319) goto ie314;
                            {
                                gm_l32 = ((uint8_t)1);
                            }
                            goto ix314;
ie314:;
ix314:;
                        }
                        goto me310;
mn311:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 82, 9);
me310:;
                    }
lc301:;
                    {
                        uint64_t t322 = gm_l41;
                        uint64_t r323; bool o324 = __builtin_add_overflow(t322, ((uint64_t)1ULL), &r323);
                        uint64_t t325 = r323;
                        gm_l41 = t325;
                    }
                }
gm_ll305:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb301; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc301; }
                }
                goto gm_lad;
lb301:;
            }
        }
        {
            {
                void * t327 = F.l0;
                gm_b16_8 t328 = GM_LD(gm_b16_8, ((((uint8_t *)t327) + 592) + 0));
                gm_l43 = t328;
            }
            {
                gm_l44 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t330 = gm_l44;
                    uint64_t t331 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                    uint8_t t332 = (t330 < t331);
                    if (!t332) goto lb329;
                    {
                        uint64_t t335 = gm_l44;
                        uint64_t t336 = (uintptr_t)t335;
                        uint64_t t337 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                        gm_t9qmk75lrp9p4 t340 = GM_LD(gm_t9qmk75lrp9p4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t336 * 96));
                        F.l45 = t340;
                        {
                            uint32_t t343 = GM_LD(uint32_t, ((((uint8_t *)&F.l45) + 16) + 0));
                            uint32_t t344 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 0));
                            uint8_t t345 = ((t343 == t344));
                            if (!t345) goto ie342;
                            void * t346 = F.l0;
                            gm_t88rcucg1b7uk t347 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l45) + 40));
                            gm_t88rcucg1b7uk t348 = gm_l17;
                            uint8_t t349 = gm_f_may_unify_0noeh56(t346, GM_CONV(gm_csguankko4lmhd, t347), GM_CONV(gm_csguankko4lmhd, t348));
                            if (!t349) goto ie342;
                            uint64_t t350 = GM_LD(uintptr_t, ((((uint8_t *)&F.l45) + 16) + 8) + 8);
                            uint64_t t351 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                            uint8_t t352 = ((t350 == t351));
                            if (!t352) goto ie342;
                            {
                                gm_l46 = ((uint8_t)1);
                            }
                            {
                                {
                                    gm_b16_8 t355 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l45) + 16) + 8));
                                    gm_l47 = t355;
                                }
                                {
                                    gm_l48 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t357 = gm_l48;
                                        uint64_t t358 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                        uint8_t t359 = (t357 < t358);
                                        if (!t359) goto lb356;
                                        {
                                            gm_tdr8bh2bm3fc8 t362;
                                            uint64_t t363 = gm_l48;
                                            GM_ST(uint64_t, (uint8_t *)&t362 + 0, t363);
                                            uint64_t t364 = gm_l48;
                                            uint64_t t365 = (uintptr_t)t364;
                                            uint64_t t366 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                            memcpy((uint8_t *)&t362 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t365 * 40), 40);
                                            gm_tdr8bh2bm3fc8 t367 = t362;
                                            uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&t367) + 0));
                                            gm_l49 = t370;
                                            gm_toa2vhe3l4hfr t371 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)&t367) + 8));
                                            F.l50 = t371;
                                            {
                                                void * t374 = F.l0;
                                                uint64_t t375 = gm_l49;
                                                uint64_t t376 = (uintptr_t)t375;
                                                uint64_t t377 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                                                if (t376 >= (uintptr_t)(t377)) gm_trap_index(t376, t377, gm_srcgr2mvgis129a, 89, 48);
                                                uint8_t t378 = gm_f_arg_may_unify_qmi580g(t374, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&F.l50)), &(*(gm_toa2vhe3l4hfr *)((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l20) + 8)) + t376 * 40)));
                                                uint8_t t379 = (uint8_t)!t378;
                                                if (!t379) goto ie373;
                                                {
                                                    gm_l46 = ((uint8_t)0);
                                                }
                                                goto ix373;
ie373:;
ix373:;
                                            }
                                            goto me368;
mn369:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 88, 17);
me368:;
                                        }
lc356:;
                                        {
                                            uint64_t t382 = gm_l48;
                                            uint64_t r383; bool o384 = __builtin_add_overflow(t382, ((uint64_t)1ULL), &r383);
                                            uint64_t t385 = r383;
                                            gm_l48 = t385;
                                        }
                                    }
lb356:;
                                }
                            }
                            {
                                uint8_t t387 = gm_l46;
                                if (!t387) goto ie386;
                                {
                                    gm_l32 = ((uint8_t)1);
                                }
                                goto ix386;
ie386:;
ix386:;
                            }
                            goto ix342;
ie342:;
ix342:;
                        }
                        goto me338;
mn339:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 85, 9);
me338:;
                    }
lc329:;
                    {
                        uint64_t t390 = gm_l44;
                        uint64_t r391; bool o392 = __builtin_add_overflow(t390, ((uint64_t)1ULL), &r391);
                        uint64_t t393 = r391;
                        gm_l44 = t393;
                    }
                }
lb329:;
            }
        }
ix224:;
    }
    {
        uint8_t t395 = gm_l32;
        if (!t395) goto ie394;
        {
            void * t397 = F.l0;
            void * t398 = gm_l2;
            gm_tl874t2atnlh3 t399 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t398) + 80));
            gm_t6j6smj7qoctu t400;
            gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t400 + 0, t401);
            void * t402 = F.l0;
            gm_t88rcucg1b7uk t403 = gm_l17;
            gm_b16_8 t404 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t402, t403);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t400 + 16, t404);
            gm_tabb710d9mq2e t405;
            gm_f_format_8h6eueo((void *)&t405, (void *)NULL, cx, NULL, &t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t406 = t405;
            if (((((uint8_t *)&t406))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t406) + 8, gm_srcgr2mvgis129a, 96, 25);
            gm_b16_8 t407 = GM_LD(gm_b16_8, (((uint8_t *)&t406) + 8));
            gm_b16_8 t408 = t407;
            gm_b16_8 t409 = GM_LD(gm_b16_8, ((uint8_t *)&t408));
            gm_b0_1 t410 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t397, t399, t409);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t411;
            uint8_t t412 = 0;
            F.ret = t411;
            goto gm_fret;
        }
        goto ix394;
ie394:;
ix394:;
    }
    {
        {
            void * t414 = gm_l2;
            gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)t414) + 64));
            gm_l51 = t415;
        }
        {
            gm_l52 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t417 = gm_l52;
                uint64_t t418 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                uint8_t t419 = (t417 < t418);
                if (!t419) goto lb416;
                {
                    uint64_t t422 = gm_l52;
                    uint64_t t423 = (uintptr_t)t422;
                    uint64_t t424 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                    gm_t99vmhfllumbh t427 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t423 * 72));
                    F.l53 = t427;
                    {
                        gm_l54 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 40));
                            gm_l55 = t430;
                        }
                        {
                            gm_l56 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t432 = gm_l56;
                                uint64_t t433 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                uint8_t t434 = (t432 < t433);
                                if (!t434) goto lb431;
                                {
                                    uint64_t t437 = gm_l56;
                                    uint64_t t438 = (uintptr_t)t437;
                                    uint64_t t439 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                    gm_tv0k2vg3054a6 t442 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t438 * 48));
                                    F.l57 = t442;
                                    {
                                        gm_b16_8 t445 = GM_LD(gm_b16_8, (((uint8_t *)&F.l57) + 0));
                                        gm_b16_8 t446 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l53) + 0) + 0));
                                        gm_b16_8 t447 = t445;
                                        gm_b16_8 t448 = t446;
                                        uint8_t t449 = (gm_eq0iqiubfm7e(((uint8_t *)&t447), ((uint8_t *)&t448)));
                                        if (!t449) goto ie444;
                                        {
                                            gm_l54 = ((uint8_t)1);
                                        }
                                        goto ix444;
ie444:;
ix444:;
                                    }
                                    goto me440;
mn441:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 101, 9);
me440:;
                                }
lc431:;
                                {
                                    uint64_t t452 = gm_l56;
                                    uint64_t r453; bool o454 = __builtin_add_overflow(t452, ((uint64_t)1ULL), &r453);
                                    uint64_t t455 = r453;
                                    gm_l56 = t455;
                                }
                            }
lb431:;
                        }
                    }
                    {
                        uint8_t t457 = gm_l54;
                        uint8_t t458 = (uint8_t)!t457;
                        if (!t458) goto ie456;
                        {
                            void * t460 = F.l0;
                            gm_tl874t2atnlh3 t461 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l53) + 0) + 16));
                            gm_t6j6smj7qoctu t462;
                            gm_b16_8 t463 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t462 + 0, t463);
                            gm_b16_8 t464 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l53) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t462 + 16, t464);
                            gm_tabb710d9mq2e t465;
                            gm_f_format_74vh99o((void *)&t465, (void *)NULL, cx, NULL, &t462);
                            if (gm_unw.kind) {
                                goto gm_ll420;
                            }
                            gm_tabb710d9mq2e t466 = t465;
                            if (((((uint8_t *)&t466))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t466) + 8, gm_srcgr2mvgis129a, 104, 45);
                            gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&t466) + 8));
                            gm_b16_8 t468 = t467;
                            gm_b16_8 t469 = GM_LD(gm_b16_8, ((uint8_t *)&t468));
                            gm_b0_1 t470 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t460, t461, t469);
                            if (gm_unw.kind) {
                                goto gm_ll420;
                            }
                        }
                        goto ix456;
ie456:;
ix456:;
                    }
                    goto me425;
mn426:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 99, 5);
me425:;
                }
lc416:;
                {
                    uint64_t t472 = gm_l52;
                    uint64_t r473; bool o474 = __builtin_add_overflow(t472, ((uint64_t)1ULL), &r473);
                    uint64_t t475 = r473;
                    gm_l52 = t475;
                }
            }
gm_ll420:;
            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb416; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc416; }
            }
            goto gm_lad;
lb416:;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t477 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 40));
            gm_l59 = t477;
        }
        {
            gm_l60 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t479 = gm_l60;
                uint64_t t480 = GM_LD(uintptr_t, ((uint8_t *)&gm_l59) + 8);
                uint8_t t481 = (t479 < t480);
                if (!t481) goto lb478;
                {
                    uint64_t t484 = gm_l60;
                    uint64_t t485 = (uintptr_t)t484;
                    uint64_t t486 = GM_LD(uintptr_t, ((uint8_t *)&gm_l59) + 8);
                    gm_tv0k2vg3054a6 t489 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l59)) + t485 * 48));
                    F.l61 = t489;
                    {
                        void * t491 = gm_l2;
                        gm_b16_8 t492 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                        gm_tf5srh4mp4b3i t493 = gm_f_decl_clause_0i5kmeb(t491, t492);
                        gm_tf5srh4mp4b3i t494 = t493;
                        uint64_t t495;
                        if (((((uint8_t *)&t494))[8] != 0)) {
                            uint64_t t496 = GM_LD(uint64_t, (((uint8_t *)&t494) + 0));
                            t495 = t496;
                        } else {
                            {
                                void * t498 = F.l0;
                                void * t499 = gm_l2;
                                gm_tl874t2atnlh3 t500 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t499) + 80));
                                gm_b16_8 t501 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                                gm_tnpifv20irnph t502;
                                GM_ST(gm_b16_8, ((uint8_t *)&t502 + 0), t501);
                                gm_tabb710d9mq2e t503;
                                gm_f_format_tv3i6qj((void *)&t503, (void *)NULL, cx, NULL, t502);
                                if (gm_unw.kind) {
                                    goto gm_ll482;
                                }
                                gm_tabb710d9mq2e t504 = t503;
                                if (((((uint8_t *)&t504))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t504) + 8, gm_srcgr2mvgis129a, 109, 29);
                                gm_b16_8 t505 = GM_LD(gm_b16_8, (((uint8_t *)&t504) + 8));
                                gm_b16_8 t506 = t505;
                                gm_b16_8 t507 = GM_LD(gm_b16_8, ((uint8_t *)&t506));
                                gm_b0_1 t508 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t498, t500, t507);
                                if (gm_unw.kind) {
                                    goto gm_ll482;
                                }
                            }
                            goto lc478;
                        }
                        gm_l62 = t495;
                    }
                    {
                        void * t509 = F.l0;
                        void * t510 = (void *)((uint8_t *)&gm_l20);
                        void * t511 = (void *)((uint8_t *)&F.l61);
                        gm_f_clause_signature_9v4rkv8((void *)&gm_l63, (void *)NULL, cx, NULL, t509, t510, t511);
                        if (gm_unw.kind) {
                            goto gm_ll482;
                        }
                    }
                    {
                        gm_f_list_af74s0q((void *)&F.l64, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll482;
                        }
                    }
                    {
                        void * t512 = (void *)((uint8_t *)&F.l64);
                        gm_b16_8 t513 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t514 = t513;
                        gm_b16_8 t515 = GM_LD(gm_b16_8, ((uint8_t *)&t514));
                        gm_tftb1odc739t5 t516 = gm_f_extend_vi643oe(t512, t515);
                        gm_tftb1odc739t5 t517 = t516;
                        if (((((uint8_t *)&t517))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t517) + 1, gm_srcgr2mvgis129a, 115, 9);
                        gm_b0_1 t518 = GM_LD(gm_b0_1, (((uint8_t *)&t517) + 1));
                    }
                    {
                        void * t519 = (void *)((uint8_t *)&F.l64);
                        gm_t5v0c8ia6a5mo t520;
                        memset((uint8_t *)&t520 + 49, 0, 7);
                        gm_b16_8 t521;
                        GM_ST(void *, (uint8_t *)&t521, (void *)(gm_sho5nuaja1vvf));
                        GM_ST(uintptr_t, (uint8_t *)&t521 + 8, (uintptr_t)(4));
                        GM_ST(gm_b16_8, (uint8_t *)&t520 + 0, t521);
                        gm_t88rcucg1b7uk t522 = gm_l17;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t520 + 16, t522);
                        void * t523 = gm_l2;
                        void * t524 = GM_LD(void *, (((uint8_t *)t523) + 32));
                        gm_tl874t2atnlh3 t525 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t524) + 120));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t520 + 20, t525);
                        GM_ST(void *, (uint8_t *)&t520 + 32, ((void *)0));
                        gm_t9805qtonbsag t526;
                        memset(&t526, 0, sizeof t526);
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t520 + 40, t526);
                        GM_ST(uint8_t, (uint8_t *)&t520 + 48, ((uint8_t)0));
                        gm_tftb1odc739t5 t527 = gm_f_push_o8u01sh(t519, &t520);
                        gm_tftb1odc739t5 t528 = t527;
                        if (((((uint8_t *)&t528))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t528) + 1, gm_srcgr2mvgis129a, 116, 9);
                        gm_b0_1 t529 = GM_LD(gm_b0_1, (((uint8_t *)&t528) + 1));
                    }
                    {
                        void * t530 = (void *)((uint8_t *)&F.l64);
                        gm_b16_8 t531 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 0));
                        gm_b16_8 t532 = t531;
                        gm_b16_8 t533 = GM_LD(gm_b16_8, ((uint8_t *)&t532));
                        gm_tftb1odc739t5 t534 = gm_f_extend_vi643oe(t530, t533);
                        gm_tftb1odc739t5 t535 = t534;
                        if (((((uint8_t *)&t535))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t535) + 1, gm_srcgr2mvgis129a, 117, 9);
                        gm_b0_1 t536 = GM_LD(gm_b0_1, (((uint8_t *)&t535) + 1));
                    }
                    {
                        void * t537 = F.l0;
                        gm_t9qkh0rsooc8d t538;
                        GM_ST(uint8_t, ((uint8_t *)&t538 + 0), ((uint8_t)2ULL));
                        gm_b16_8 t539 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                        uint64_t t540 = gm_l62;
                        uint64_t t541 = (uintptr_t)t540;
                        void * t542 = gm_l2;
                        uint64_t t543 = GM_LD(uintptr_t, (((uint8_t *)t542) + 64) + 8);
                        if (t541 >= (uintptr_t)(t543)) gm_trap_index(t541, t543, gm_srcgr2mvgis129a, 118, 51);
                        gm_tl874t2atnlh3 t544 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t542) + 64)) + t541 * 72) + 56));
                        uint64_t t545 = gm_l1;
                        gm_b16_8 t546 = GM_LD(gm_b16_8, (((uint8_t *)&F.l64) + 0));
                        gm_t88rcucg1b7uk t547 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l63) + 16));
                        gm_tnpifv20irnph t548 = gm_l18;
                        uint32_t t549 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t537, t538, t539, t544, t545, t546, t547, t548);
                        if (gm_unw.kind) {
                            goto gm_ll482;
                        }
                        gm_l65 = t549;
                    }
                    {
                        uint32_t t550 = gm_l65;
                        uint64_t t551 = (uint64_t)t550;
                        uint64_t t552 = (uintptr_t)t551;
                        void * t553 = F.l0;
                        uint64_t t554 = GM_LD(uintptr_t, ((((uint8_t *)t553) + 344) + 0) + 8);
                        if (t552 >= (uintptr_t)(t554)) gm_trap_index(t552, t554, gm_srcgr2mvgis129a, 120, 9);
                        gm_b16_8 t555 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t553) + 344) + 0)) + t552 * 136) + 80), t555);
                    }
                    {
                        void * t556 = (void *)((uint8_t *)&F.l58);
                        uint32_t t557 = gm_l65;
                        gm_tftb1odc739t5 t558 = gm_f_push_im9d4ro(t556, t557);
                        gm_tftb1odc739t5 t559 = t558;
                        if (((((uint8_t *)&t559))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t559) + 1, gm_srcgr2mvgis129a, 121, 9);
                        gm_b0_1 t560 = GM_LD(gm_b0_1, (((uint8_t *)&t559) + 1));
                    }
                    {
                        void * t561 = F.l0;
                        void * t562 = (void *)(((uint8_t *)t561) + 680);
                        gm_tbeui4bbnm52b t563;
                        memset((uint8_t *)&t563 + 21, 0, 3);
                        uint64_t t564 = gm_l1;
                        GM_ST(uint64_t, (uint8_t *)&t563 + 0, t564);
                        uint64_t t565 = gm_l62;
                        uint64_t t566 = (uintptr_t)t565;
                        void * t567 = gm_l2;
                        uint64_t t568 = GM_LD(uintptr_t, (((uint8_t *)t567) + 64) + 8);
                        if (t566 >= (uintptr_t)(t568)) gm_trap_index(t566, t568, gm_srcgr2mvgis129a, 122, 76);
                        void * t569 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t567) + 64)) + t566 * 72);
                        GM_ST(void *, (uint8_t *)&t563 + 8, t569);
                        uint32_t t570 = gm_l65;
                        GM_ST(uint32_t, (uint8_t *)&t563 + 16, t570);
                        gm_t9qkh0rsooc8d t571 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l61) + 16));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t563 + 20, t571);
                        memcpy((uint8_t *)&t563 + 24, ((uint8_t *)&gm_l20), 24);
                        gm_tftb1odc739t5 t572 = gm_f_push_qkkb0em(t562, &t563);
                        gm_tftb1odc739t5 t573 = t572;
                        if (((((uint8_t *)&t573))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t573) + 1, gm_srcgr2mvgis129a, 122, 9);
                        gm_b0_1 t574 = GM_LD(gm_b0_1, (((uint8_t *)&t573) + 1));
                    }
                    goto me487;
mn488:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 107, 5);
me487:;
                }
lc478:;
                {
                    uint64_t t576 = gm_l60;
                    uint64_t r577; bool o578 = __builtin_add_overflow(t576, ((uint64_t)1ULL), &r577);
                    uint64_t t579 = r577;
                    gm_l60 = t579;
                }
            }
gm_ll482:;
            if (gm_unw.frame == &F.h && gm_unw.label == 11) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb478; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc478; }
            }
            goto gm_lad;
lb478:;
        }
    }
    {
        uint64_t t581 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        uint8_t t582 = ((t581 == ((uint64_t)0ULL)));
        if (!t582) goto ie580;
        {
            void * t584 = F.l0;
            void * t585 = (void *)(((uint8_t *)t584) + 544);
            gm_tvc83pqnepc2e t586;
            memset((uint8_t *)&t586 + 28, 0, 4);
            memcpy((uint8_t *)&t586 + 0, ((uint8_t *)&gm_l20), 24);
            gm_t88rcucg1b7uk t587 = gm_l17;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t586 + 24, t587);
            gm_tnpifv20irnph t588 = gm_l18;
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&t586 + 32, t588);
            gm_b16_8 t589 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t586 + 48, t589);
            gm_tftb1odc739t5 t590 = gm_f_push_mi6l2kj(t585, &t586);
            gm_tftb1odc739t5 t591 = t590;
            if (((((uint8_t *)&t591))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t591) + 1, gm_srcgr2mvgis129a, 125, 9);
            gm_b0_1 t592 = GM_LD(gm_b0_1, (((uint8_t *)&t591) + 1));
        }
        goto ix580;
ie580:;
        {
            void * t594 = F.l0;
            void * t595 = (void *)(((uint8_t *)t594) + 592);
            gm_t9qmk75lrp9p4 t596;
            memset((uint8_t *)&t596 + 44, 0, 4);
            memset((uint8_t *)&t596 + 92, 0, 4);
            gm_b16_8 t597 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t596 + 0, t597);
            memcpy((uint8_t *)&t596 + 16, ((uint8_t *)&gm_l20), 24);
            gm_t88rcucg1b7uk t598 = gm_l17;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t596 + 40, t598);
            gm_tnpifv20irnph t599 = gm_l18;
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&t596 + 48, t599);
            gm_b16_8 t600 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t596 + 64, t600);
            void * t601 = gm_l2;
            gm_tl874t2atnlh3 t602 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t601) + 80));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t596 + 80, t602);
            gm_tftb1odc739t5 t603 = gm_f_push_9gv7k4m(t595, &t596);
            gm_tftb1odc739t5 t604 = t603;
            if (((((uint8_t *)&t604))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t604) + 1, gm_srcgr2mvgis129a, 127, 9);
            gm_b0_1 t605 = GM_LD(gm_b0_1, (((uint8_t *)&t604) + 1));
        }
ix580:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_declare_handlers_c3ac89r(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_declare_handlers_c3ac89r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    void * gm_l5; /* h */
    GM_STACK_CHECK(&F, 380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 584));
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
        GM_ST(uint8_t, (((uint8_t *)t8) + 584), ((uint8_t)1));
    }
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 280) + 0) + 8);
            gm_l1 = t12;
        }
        {
            for (;;) {
                uint64_t t14 = gm_l2;
                uint64_t t15 = gm_l1;
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l2;
                    uint64_t t20 = t19;
                    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                    gm_l3 = t23;
                    {
                        uint64_t t25 = gm_l3;
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 280) + 0) + 8);
                        if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_srcgr2mvgis129a, 12, 14);
                        gm_tp8h583g7s5hd t30 = GM_LD(gm_tp8h583g7s5hd, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 280) + 0)) + t26 * 168));
                        F.l4 = t30;
                    }
                    {
                        uint8_t t32 = GM_LD(uint8_t, (((((uint8_t *)&F.l4) + 8) + 64) + 0));
                        if (t32 != ((uint8_t)2ULL)) goto ie31;
                        void * t33 = GM_LD(void *, (((((uint8_t *)&F.l4) + 8) + 64) + 8));
                        gm_l5 = t33;
                        {
                            void * t35 = gm_l0;
                            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 0));
                            void * t37 = gm_l5;
                            gm_b0_1 t38 = gm_f_declare_handler_gd3gpcg((void *)NULL, cx, NULL, t35, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        goto ix31;
ie31:;
                        {
                            goto lc13;
                        }
ix31:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 11, 5);
me21:;
                }
lc13:;
                {
                    uint64_t t41 = gm_l2;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l2 = t44;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_51177of F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_declare_handler_gd3gpcg *)((&F.h)->up))->l0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l0;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)19ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 8));
        gm_l1 = t9;
        {
            void * t11 = (void *)((uint8_t *)&((struct gm_fr_declare_handler_gd3gpcg *)((&F.h)->up))->l26);
            uint32_t t12 = gm_l1;
            gm_tftb1odc739t5 t13 = gm_f_push_im9d4ro(t11, t12);
            gm_tftb1odc739t5 t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcgr2mvgis129a, 57, 45);
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
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
inline __attribute__((aligned(4))) void gm_f_handler_template_args_dlm07kb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_handler_template_args_dlm07kb F;
    void * gm_l0; /* s */
    void * gm_l1; /* tpl */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_oalq03d((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 40));
        gm_t88rcucg1b7uk t5 = gm_l2;
        void * t6 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t7 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t2, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t9 = gm_l1;
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 0));
            gm_l5 = t10;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l6;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l6;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t18 * 4));
                    uint32_t t21 = t20;
                    uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)&t21));
                    gm_l7 = t24;
                    {
                        void * t26 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t28 = t27;
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                        uint32_t t30 = gm_l7;
                        gm_tkn42u7na3t81 t31 = gm_f_bound_to_5c11t0h(t29, t30);
                        gm_tkn42u7na3t81 t32 = t31;
                        gm_t88rcucg1b7uk t33;
                        if (((((uint8_t *)&t32))[4] != 0)) {
                            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t32) + 0));
                            t33 = t34;
                        } else {
                            gm_th2lqbo77pvsc t35;
                            memset(&t35, 0, sizeof t35);
                            uint8_t t36 = 0;
                            F.ret = t35;
                            goto gm_fret;
                        }
                        gm_tftb1odc739t5 t37 = gm_f_push_k3l9nic(t26, GM_CONV(gm_csguankko4lmhd, t33));
                        gm_tftb1odc739t5 t38 = t37;
                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcgr2mvgis129a, 142, 29);
                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 142, 5);
me22:;
                }
lc11:;
                {
                    uint64_t t41 = gm_l6;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l6 = t44;
                }
            }
lb11:;
        }
    }
    {
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_th2lqbo77pvsc t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t46), t45);
        ((uint8_t *)&t46)[16] = 1;
        uint8_t t47 = 0;
        F.ret = t46;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_handlers_of_type_5gareb4 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    gm_b16_8 gm_l11; /* args */
    gm_b16_8 gm_l12; /* map */
    uint8_t gm_l13; /* repeated */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint32_t gm_l16; /* d */
    gm_t9805qtonbsag gm_l17; /* found */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tm9oc6v1he136 gm_l20; /* inst */
    uint8_t gm_l21; /* same */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_t88rcucg1b7uk gm_l25; /* a */
    uint32_t gm_l26; /* d */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint32_t gm_l30; /* c */
    uint32_t gm_l31; /* id */
    GM_STACK_CHECK(&F, 2241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_declare_handlers_c3ac89r((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t5 = gm_l0;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 544) + 0));
            gm_b16_8 t7 = t6;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            gm_l3 = t8;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l4;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    gm_tcth15utccicj t15;
                    uint64_t t16 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t15 + 0, t16);
                    uint64_t t17 = gm_l4;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    memcpy((uint8_t *)&t15 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t18 * 64), 64);
                    gm_tcth15utccicj t20 = t15;
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&t20) + 0));
                    gm_l5 = t23;
                    gm_tvc83pqnepc2e t24 = GM_LD(gm_tvc83pqnepc2e, (((uint8_t *)&t20) + 8));
                    F.l6 = t24;
                    {
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 24));
                        gm_t88rcucg1b7uk t28 = gm_l1;
                        gm_t88rcucg1b7uk t29 = t27;
                        gm_t88rcucg1b7uk t30 = t28;
                        uint8_t t31 = (gm_eq9m6mvtmgde(((uint8_t *)&t29), ((uint8_t *)&t30)));
                        if (!t31) goto ie26;
                        {
                            void * t33 = (void *)((uint8_t *)&F.l2);
                            uint64_t t34 = gm_l5;
                            uint32_t t35 = (uint32_t)t34;
                            gm_tftb1odc739t5 t36 = gm_f_push_im9d4ro(t33, t35);
                            gm_tftb1odc739t5 t37 = t36;
                            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcgr2mvgis129a, 328, 28);
                            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 327, 5);
me21:;
                }
lc9:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
                }
            }
lb9:;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t45 = gm_l0;
            uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 592) + 0) + 8);
            gm_l7 = t46;
        }
        {
            for (;;) {
                uint64_t t48 = gm_l8;
                uint64_t t49 = gm_l7;
                uint8_t t50 = (t48 < t49);
                if (!t50) goto lb47;
                {
                    uint64_t t53 = gm_l8;
                    uint64_t t54 = t53;
                    uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)&t54));
                    gm_l9 = t57;
                    {
                        uint64_t t59 = gm_l9;
                        uint64_t t60 = (uintptr_t)t59;
                        void * t61 = gm_l0;
                        uint64_t t62 = GM_LD(uintptr_t, ((((uint8_t *)t61) + 592) + 0) + 8);
                        if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_srcgr2mvgis129a, 331, 16);
                        gm_t9qmk75lrp9p4 t63 = GM_LD(gm_t9qmk75lrp9p4, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t61) + 592) + 0)) + t60 * 96));
                        F.l10 = t63;
                    }
                    {
                        void * t64 = gm_l0;
                        void * t65 = (void *)((uint8_t *)&F.l10);
                        gm_t88rcucg1b7uk t66 = gm_l1;
                        gm_th2lqbo77pvsc t67;
                        gm_f_handler_template_args_dlm07kb((void *)&t67, (void *)NULL, cx, NULL, t64, t65, t66);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        gm_th2lqbo77pvsc t68 = t67;
                        gm_b16_8 t69;
                        if (((((uint8_t *)&t68))[16] != 0)) {
                            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 0));
                            t69 = t70;
                        } else {
                            goto lc47;
                        }
                        gm_l11 = t69;
                    }
                    {
                        void * t71 = (void *)((uint8_t *)&F.l10);
                        gm_b16_8 t72 = gm_l11;
                        gm_b16_8 t73 = t72;
                        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                        gm_b16_8 t75 = gm_f_template_map_lfm02i0((void *)NULL, cx, NULL, t71, t74);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        gm_l12 = t75;
                    }
                    {
                        void * t77 = gm_l0;
                        gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 40));
                        gm_b16_8 t79 = gm_l12;
                        gm_b16_8 t80 = t79;
                        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                        gm_t88rcucg1b7uk t82 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t77, t78, t81);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        gm_t88rcucg1b7uk t83 = gm_l1;
                        gm_t88rcucg1b7uk t84 = t82;
                        gm_t88rcucg1b7uk t85 = t83;
                        uint8_t t86 = (!gm_eq9m6mvtmgde(((uint8_t *)&t84), ((uint8_t *)&t85)));
                        if (!t86) goto ie76;
                        {
                            goto lc47;
                        }
                        goto ix76;
ie76:;
ix76:;
                    }
                    {
                        gm_l13 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                            gm_l14 = t89;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t91 = gm_l15;
                                uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t93 = (t91 < t92);
                                if (!t93) goto lb90;
                                {
                                    uint64_t t96 = gm_l15;
                                    uint64_t t97 = (uintptr_t)t96;
                                    uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    uint32_t t99 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t97 * 4));
                                    uint32_t t100 = t99;
                                    uint32_t t103 = GM_LD(uint32_t, ((uint8_t *)&t100));
                                    gm_l16 = t103;
                                    {
                                        uint32_t t106 = gm_l16;
                                        uint64_t t107 = (uint64_t)t106;
                                        uint64_t t108 = (uintptr_t)t107;
                                        void * t109 = gm_l0;
                                        uint64_t t110 = GM_LD(uintptr_t, ((((uint8_t *)t109) + 544) + 0) + 8);
                                        if (t108 >= (uintptr_t)(t110)) gm_trap_index(t108, t110, gm_srcgr2mvgis129a, 338, 16);
                                        uint32_t t111 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t109) + 544) + 0)) + t108 * 64) + 0) + 0));
                                        uint32_t t112 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 16) + 0));
                                        uint8_t t113 = ((t111 == t112));
                                        if (!t113) goto ie105;
                                        {
                                            gm_l13 = ((uint8_t)1);
                                        }
                                        goto ix105;
ie105:;
ix105:;
                                    }
                                    goto me101;
mn102:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 337, 9);
me101:;
                                }
lc90:;
                                {
                                    uint64_t t116 = gm_l15;
                                    uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                                    uint64_t t119 = r117;
                                    gm_l15 = t119;
                                }
                            }
lb90:;
                        }
                    }
                    {
                        uint8_t t121 = gm_l13;
                        if (!t121) goto ie120;
                        {
                            goto lc47;
                        }
                        goto ix120;
ie120:;
ix120:;
                    }
                    {
                        gm_t9805qtonbsag t123;
                        memset(&t123, 0, sizeof t123);
                        gm_l17 = t123;
                    }
                    {
                        {
                            void * t125 = gm_l0;
                            gm_b16_8 t126 = GM_LD(gm_b16_8, ((((uint8_t *)t125) + 632) + 0));
                            gm_l18 = t126;
                        }
                        {
                            gm_l19 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t128 = gm_l19;
                                uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                uint8_t t130 = (t128 < t129);
                                if (!t130) goto lb127;
                                {
                                    uint64_t t133 = gm_l19;
                                    uint64_t t134 = (uintptr_t)t133;
                                    uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    gm_tm9oc6v1he136 t138 = GM_LD(gm_tm9oc6v1he136, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t134 * 32));
                                    gm_l20 = t138;
                                    {
                                        uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 0));
                                        uint64_t t142 = gm_l9;
                                        uint8_t t143 = ((t141 == t142));
                                        if (!t143) goto ie140;
                                        uint64_t t144 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                                        uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                        uint8_t t146 = ((t144 == t145));
                                        if (!t146) goto ie140;
                                        {
                                            gm_l21 = ((uint8_t)1);
                                        }
                                        {
                                            {
                                                gm_b16_8 t149 = gm_l11;
                                                gm_b16_8 t150 = t149;
                                                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                                                gm_l22 = t151;
                                            }
                                            {
                                                gm_l23 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t153 = gm_l23;
                                                    uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                                    uint8_t t155 = (t153 < t154);
                                                    if (!t155) goto lb152;
                                                    {
                                                        uint64_t t158 = gm_l23;
                                                        uint64_t t159 = gm_l23;
                                                        uint64_t t160 = (uintptr_t)t159;
                                                        uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                                        gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t160 * 4));
                                                        gm_tlcm4lvu3g1a8 t163;
                                                        GM_ST(uint32_t, (uint8_t *)&t163 + 12, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&t163 + 0), t158);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t163 + 8), t162);
                                                        gm_tlcm4lvu3g1a8 t164 = t163;
                                                        uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&t164) + 0));
                                                        gm_l24 = t167;
                                                        gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t164) + 8));
                                                        gm_l25 = t168;
                                                        {
                                                            uint64_t t171 = gm_l24;
                                                            uint64_t t172 = (uintptr_t)t171;
                                                            uint64_t t173 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                                                            if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_srcgr2mvgis129a, 346, 24);
                                                            gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l20) + 8)) + t172 * 4));
                                                            gm_t88rcucg1b7uk t175 = gm_l25;
                                                            gm_t88rcucg1b7uk t176 = t174;
                                                            gm_t88rcucg1b7uk t177 = t175;
                                                            uint8_t t178 = (!gm_eq9m6mvtmgde(((uint8_t *)&t176), ((uint8_t *)&t177)));
                                                            if (!t178) goto ie170;
                                                            {
                                                                gm_l21 = ((uint8_t)0);
                                                            }
                                                            goto ix170;
ie170:;
ix170:;
                                                        }
                                                        goto me165;
mn166:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 345, 17);
me165:;
                                                    }
lc152:;
                                                    {
                                                        uint64_t t181 = gm_l23;
                                                        uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                                                        uint64_t t184 = r182;
                                                        gm_l23 = t184;
                                                    }
                                                }
lb152:;
                                            }
                                        }
                                        {
                                            uint8_t t186 = gm_l21;
                                            if (!t186) goto ie185;
                                            {
                                                uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 24));
                                                gm_t9805qtonbsag t189;
                                                GM_ST(uint8_t, (uint8_t *)&t189 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t189 + 6, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t189), t188);
                                                ((uint8_t *)&t189)[4] = 1;
                                                gm_l17 = t189;
                                            }
                                            goto ix185;
ie185:;
ix185:;
                                        }
                                        goto ix140;
ie140:;
ix140:;
                                    }
                                    goto me136;
mn137:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 342, 9);
me136:;
                                }
lc127:;
                                {
                                    uint64_t t191 = gm_l19;
                                    uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                                    uint64_t t194 = r192;
                                    gm_l19 = t194;
                                }
                            }
lb127:;
                        }
                    }
                    {
                        if (!((((uint8_t *)&gm_l17))[4] != 0)) goto ie195;
                        uint32_t t196 = GM_LD(uint32_t, ((uint8_t *)&gm_l17));
                        gm_l26 = t196;
                        {
                            void * t198 = (void *)((uint8_t *)&F.l2);
                            uint32_t t199 = gm_l26;
                            gm_tftb1odc739t5 t200 = gm_f_push_im9d4ro(t198, t199);
                            gm_tftb1odc739t5 t201 = t200;
                            if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srcgr2mvgis129a, 352, 13);
                            gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
                        }
                        {
                            goto lc47;
                        }
                        goto ix195;
ie195:;
ix195:;
                    }
                    {
                        gm_f_list_go2qg0b((void *)&F.l27, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                    }
                    {
                        {
                            gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 64));
                            gm_l28 = t204;
                        }
                        {
                            gm_l29 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t206 = gm_l29;
                                uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                uint8_t t208 = (t206 < t207);
                                if (!t208) goto lb205;
                                {
                                    uint64_t t211 = gm_l29;
                                    uint64_t t212 = (uintptr_t)t211;
                                    uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                    uint32_t t214 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t212 * 4));
                                    uint32_t t215 = t214;
                                    uint32_t t218 = GM_LD(uint32_t, ((uint8_t *)&t215));
                                    gm_l30 = t218;
                                    {
                                        void * t220 = (void *)((uint8_t *)&F.l27);
                                        void * t221 = gm_l0;
                                        uint32_t t222 = gm_l30;
                                        gm_b16_8 t223 = gm_l11;
                                        gm_b16_8 t224 = t223;
                                        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
                                        gm_tl874t2atnlh3 t226 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 80));
                                        uint32_t t227 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t221, t222, t225, t226);
                                        if (gm_unw.kind) {
                                            goto gm_ll209;
                                        }
                                        gm_tftb1odc739t5 t228 = gm_f_push_im9d4ro(t220, t227);
                                        gm_tftb1odc739t5 t229 = t228;
                                        if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srcgr2mvgis129a, 356, 32);
                                        gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
                                    }
                                    goto me216;
mn217:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 356, 9);
me216:;
                                }
lc205:;
                                {
                                    uint64_t t232 = gm_l29;
                                    uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                                    uint64_t t235 = r233;
                                    gm_l29 = t235;
                                }
                            }
gm_ll209:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb205; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc205; }
                            }
                            goto gm_ll51;
lb205:;
                        }
                    }
                    {
                        void * t236 = gm_l0;
                        uint64_t t237 = GM_LD(uintptr_t, ((((uint8_t *)t236) + 544) + 0) + 8);
                        uint32_t t238 = (uint32_t)t237;
                        gm_l31 = t238;
                    }
                    {
                        void * t239 = gm_l0;
                        void * t240 = (void *)(((uint8_t *)t239) + 544);
                        gm_tvc83pqnepc2e t241;
                        memset((uint8_t *)&t241 + 28, 0, 4);
                        void * t242 = gm_l0;
                        void * t243 = (void *)(((uint8_t *)&F.l10) + 16);
                        gm_b16_8 t244 = gm_l12;
                        gm_b16_8 t245 = t244;
                        gm_b16_8 t246 = GM_LD(gm_b16_8, ((uint8_t *)&t245));
                        gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t241 + 0)), (void *)NULL, cx, NULL, t242, t243, t246);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        gm_t88rcucg1b7uk t247 = gm_l1;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t241 + 24, t247);
                        void * t248 = gm_l0;
                        void * t249 = (void *)(((uint8_t *)&F.l10) + 48);
                        gm_b16_8 t250 = gm_l12;
                        gm_b16_8 t251 = t250;
                        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                        gm_tnpifv20irnph t253 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t248, t249, t252);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t241 + 32, t253);
                        gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t241 + 48, t254);
                        gm_tftb1odc739t5 t255 = gm_f_push_mi6l2kj(t240, &t241);
                        gm_tftb1odc739t5 t256 = t255;
                        if (((((uint8_t *)&t256))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t256) + 1, gm_srcgr2mvgis129a, 358, 9);
                        gm_b0_1 t257 = GM_LD(gm_b0_1, (((uint8_t *)&t256) + 1));
                    }
                    {
                        void * t258 = gm_l0;
                        void * t259 = (void *)(((uint8_t *)t258) + 632);
                        gm_tm9oc6v1he136 t260;
                        memset((uint8_t *)&t260 + 28, 0, 4);
                        uint64_t t261 = gm_l9;
                        GM_ST(uint64_t, (uint8_t *)&t260 + 0, t261);
                        gm_b16_8 t262 = gm_l11;
                        gm_b16_8 t263 = t262;
                        gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&t263));
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 8, t264);
                        uint32_t t265 = gm_l31;
                        GM_ST(uint32_t, (uint8_t *)&t260 + 24, t265);
                        gm_tftb1odc739t5 t266 = gm_f_push_b9c0fdf(t259, &t260);
                        gm_tftb1odc739t5 t267 = t266;
                        if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 1, gm_srcgr2mvgis129a, 359, 9);
                        gm_b0_1 t268 = GM_LD(gm_b0_1, (((uint8_t *)&t267) + 1));
                    }
                    {
                        void * t269 = (void *)((uint8_t *)&F.l2);
                        uint32_t t270 = gm_l31;
                        gm_tftb1odc739t5 t271 = gm_f_push_im9d4ro(t269, t270);
                        gm_tftb1odc739t5 t272 = t271;
                        if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 1, gm_srcgr2mvgis129a, 360, 9);
                        gm_b0_1 t273 = GM_LD(gm_b0_1, (((uint8_t *)&t272) + 1));
                    }
                    goto me55;
mn56:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 330, 5);
me55:;
                }
lc47:;
                {
                    uint64_t t275 = gm_l8;
                    uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                    uint64_t t278 = r276;
                    gm_l8 = t278;
                }
            }
gm_ll51:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
            }
            goto gm_lad;
lb47:;
        }
    }
    {
        gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t280 = 0;
        F.ret = t279;
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
inline __attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_is_uo88ron F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* name */
    gm_t08olq97rscn3 gm_l2; /* i */
    GM_STACK_CHECK(&F, 166u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_b16_8 t8 = gm_l1;
            gm_b16_8 t9 = t7;
            gm_b16_8 t10 = t8;
            uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_may_unify_0noeh56(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_may_unify_0noeh56 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* y */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* x */
    gm_tnnjeerolos0a gm_l10; /* i */
    gm_t88rcucg1b7uk gm_l11; /* y */
    uint64_t gm_l12; /* n */
    gm_t88rcucg1b7uk gm_l13; /* y */
    gm_tnnjeerolos0a gm_l14; /* j */
    uint8_t gm_l15; /* c */
    gm_t88rcucg1b7uk gm_l16; /* x */
    uint8_t gm_l17; /* d */
    gm_t88rcucg1b7uk gm_l18; /* y */
    uint8_t gm_l19; /* c */
    gm_t88rcucg1b7uk gm_l20; /* x */
    uint8_t gm_l21; /* d */
    gm_t88rcucg1b7uk gm_l22; /* y */
    uint8_t gm_l23; /* c */
    gm_t88rcucg1b7uk gm_l24; /* x */
    uint8_t gm_l25; /* d */
    gm_t88rcucg1b7uk gm_l26; /* y */
    gm_t88rcucg1b7uk gm_l27; /* x */
    gm_t88rcucg1b7uk gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* x */
    gm_t88rcucg1b7uk gm_l30; /* y */
    gm_t88rcucg1b7uk gm_l31; /* e1 */
    gm_t88rcucg1b7uk gm_l32; /* o1 */
    gm_t88rcucg1b7uk gm_l33; /* e2 */
    gm_t88rcucg1b7uk gm_l34; /* o2 */
    gm_b16_8 gm_l35; /* x */
    gm_b16_8 gm_l36; /* n */
    gm_b16_8 gm_l37; /* y */
    gm_b16_8 gm_l38; /* m */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_b16_8 gm_l42; /* nm */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* i */
    gm_t88rcucg1b7uk gm_l46; /* p */
    uint32_t gm_l47; /* x */
    gm_b16_8 gm_l48; /* xs */
    uint32_t gm_l49; /* y */
    gm_b16_8 gm_l50; /* ys */
    gm_b16_8 gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* i */
    GM_STACK_CHECK(&F, 1702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = t3;
        gm_t88rcucg1b7uk t6 = t4;
        uint8_t t7 = (gm_eq9m6mvtmgde(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        gm_t88rcucg1b7uk t12 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t11, GM_CONV(gm_csguankko4lmhd, t12));
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        gm_t88rcucg1b7uk t15 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l4, t14, GM_CONV(gm_csguankko4lmhd, t15));
    }
    {
        uint8_t t17 = 0;
        uint8_t t18;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t21 != ((uint8_t)19ULL)) goto mn20;
        t18 = ((uint8_t)1);
        goto me19;
mn20:;
        t18 = ((uint8_t)0);
        goto me19;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 174, 8);
me19:;
        if (!t18) {
            uint8_t t23;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t26 != ((uint8_t)19ULL)) goto mn25;
            t23 = ((uint8_t)1);
            goto me24;
mn25:;
            t23 = ((uint8_t)0);
            goto me24;
mn27:;
            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 174, 27);
me24:;
            t17 = t23;
        } else t17 = 1;
        if (!t17) goto ie16;
        {
            uint8_t t29 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        switch (t45) { case ((uint8_t)0ULL): goto ma43; case ((uint8_t)1ULL): goto ma43; case ((uint8_t)2ULL): goto ma43; case ((uint8_t)3ULL): goto ma43; case ((uint8_t)4ULL): goto ma43; case ((uint8_t)5ULL): goto ma43; case ((uint8_t)6ULL): goto ma43; case ((uint8_t)7ULL): goto ma43; case ((uint8_t)8ULL): goto ma33; case ((uint8_t)9ULL): goto ma35; case ((uint8_t)10ULL): goto ma34; case ((uint8_t)11ULL): goto ma32; case ((uint8_t)12ULL): goto ma39; case ((uint8_t)13ULL): goto ma36; case ((uint8_t)14ULL): goto ma38; case ((uint8_t)15ULL): goto ma41; case ((uint8_t)16ULL): goto ma42; case ((uint8_t)17ULL): goto ma40; case ((uint8_t)18ULL): goto ma37; case ((uint8_t)19ULL): goto ma43; case ((uint8_t)20ULL): goto ma31; case ((uint8_t)21ULL): goto ma43; case ((uint8_t)22ULL): goto ma43; case ((uint8_t)23ULL): goto ma43; case ((uint8_t)24ULL): goto ma43; case ((uint8_t)25ULL): goto ma43; case ((uint8_t)26ULL): goto ma43; default: goto ma43; }
ma31:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t47 != ((uint8_t)20ULL)) goto mn46;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l5 = t48;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t53 != ((uint8_t)20ULL)) goto pa52;
        gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l6 = t54;
        goto po51;
pa52:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t56 != ((uint8_t)11ULL)) goto pa55;
        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l6 = t57;
        goto po51;
pa55:;
        goto mn50;
po51:;
        void * t58 = gm_l0;
        gm_t88rcucg1b7uk t59 = gm_l5;
        gm_t88rcucg1b7uk t60 = gm_l6;
        uint8_t t61 = gm_f_may_unify_0noeh56(t58, GM_CONV(gm_csguankko4lmhd, t59), GM_CONV(gm_csguankko4lmhd, t60));
        uint8_t t62 = 0;
        F.ret = t61;
        goto gm_fret;
        goto me49;
mn50:;
        uint8_t t64 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me49;
mn63:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 176, 30);
me49:;
        goto me30;
mn46:;
ma32:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t66 != ((uint8_t)11ULL)) goto mn65;
        uint64_t t67 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l8 = t67;
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 8));
        gm_l9 = t68;
        gm_tnnjeerolos0a t69 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l3) + 8) + 12));
        gm_l10 = t69;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        switch (t75) { case ((uint8_t)0ULL): goto ma73; case ((uint8_t)1ULL): goto ma73; case ((uint8_t)2ULL): goto ma73; case ((uint8_t)3ULL): goto ma73; case ((uint8_t)4ULL): goto ma73; case ((uint8_t)5ULL): goto ma73; case ((uint8_t)6ULL): goto ma73; case ((uint8_t)7ULL): goto ma73; case ((uint8_t)8ULL): goto ma73; case ((uint8_t)9ULL): goto ma73; case ((uint8_t)10ULL): goto ma73; case ((uint8_t)11ULL): goto ma72; case ((uint8_t)12ULL): goto ma73; case ((uint8_t)13ULL): goto ma73; case ((uint8_t)14ULL): goto ma73; case ((uint8_t)15ULL): goto ma73; case ((uint8_t)16ULL): goto ma73; case ((uint8_t)17ULL): goto ma73; case ((uint8_t)18ULL): goto ma73; case ((uint8_t)19ULL): goto ma73; case ((uint8_t)20ULL): goto ma71; case ((uint8_t)21ULL): goto ma73; case ((uint8_t)22ULL): goto ma73; case ((uint8_t)23ULL): goto ma73; case ((uint8_t)24ULL): goto ma73; case ((uint8_t)25ULL): goto ma73; case ((uint8_t)26ULL): goto ma73; default: goto ma73; }
ma71:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t77 != ((uint8_t)20ULL)) goto mn76;
        gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l11 = t78;
        void * t79 = gm_l0;
        gm_t88rcucg1b7uk t80 = gm_l9;
        gm_t88rcucg1b7uk t81 = gm_l11;
        uint8_t t82 = gm_f_may_unify_0noeh56(t79, GM_CONV(gm_csguankko4lmhd, t80), GM_CONV(gm_csguankko4lmhd, t81));
        uint8_t t83 = 0;
        F.ret = t82;
        goto gm_fret;
        goto me70;
mn76:;
ma72:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t85 != ((uint8_t)11ULL)) goto mn84;
        uint64_t t86 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l12 = t86;
        gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l13 = t87;
        gm_tnnjeerolos0a t88 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l4) + 8) + 12));
        gm_l14 = t88;
        uint8_t t89 = 0;
        uint8_t t90 = 0;
        uint64_t t91 = gm_l8;
        uint64_t t92 = gm_l12;
        uint8_t t93 = ((t91 == t92));
        if (t93) {
            gm_tnnjeerolos0a t94 = gm_l10;
            gm_tnnjeerolos0a t95 = gm_l14;
            uint8_t t96 = gm_f_array_kind_same_rl3epej(t94, t95);
            t90 = t96;
        } else t90 = 0;
        if (t90) {
            void * t97 = gm_l0;
            gm_t88rcucg1b7uk t98 = gm_l9;
            gm_t88rcucg1b7uk t99 = gm_l13;
            uint8_t t100 = gm_f_may_unify_0noeh56(t97, GM_CONV(gm_csguankko4lmhd, t98), GM_CONV(gm_csguankko4lmhd, t99));
            t89 = t100;
        } else t89 = 0;
        uint8_t t101 = 0;
        F.ret = t89;
        goto gm_fret;
        goto me70;
mn84:;
ma73:;
        uint8_t t103 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me70;
mn102:;
ma74:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 180, 28);
me70:;
        goto me30;
mn65:;
ma33:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t105 != ((uint8_t)8ULL)) goto mn104;
        uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l15 = t106;
        gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l16 = t107;
        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t109 != ((uint8_t)8ULL)) goto ie108;
        uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l17 = t110;
        gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l18 = t111;
        {
            uint8_t t113 = 0;
            uint8_t t114 = gm_l15;
            uint8_t t115 = gm_l17;
            uint8_t t116 = ((t114 == t115));
            if (t116) {
                void * t117 = gm_l0;
                gm_t88rcucg1b7uk t118 = gm_l16;
                gm_t88rcucg1b7uk t119 = gm_l18;
                uint8_t t120 = gm_f_may_unify_0noeh56(t117, GM_CONV(gm_csguankko4lmhd, t118), GM_CONV(gm_csguankko4lmhd, t119));
                t113 = t120;
            } else t113 = 0;
            uint8_t t121 = 0;
            F.ret = t113;
            goto gm_fret;
        }
        goto ix108;
ie108:;
ix108:;
        goto me30;
mn104:;
ma34:;
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t123 != ((uint8_t)10ULL)) goto mn122;
        uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l19 = t124;
        gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l20 = t125;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t127 != ((uint8_t)10ULL)) goto ie126;
        uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l21 = t128;
        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l22 = t129;
        {
            uint8_t t131 = 0;
            uint8_t t132 = gm_l19;
            uint8_t t133 = gm_l21;
            uint8_t t134 = ((t132 == t133));
            if (t134) {
                void * t135 = gm_l0;
                gm_t88rcucg1b7uk t136 = gm_l20;
                gm_t88rcucg1b7uk t137 = gm_l22;
                uint8_t t138 = gm_f_may_unify_0noeh56(t135, GM_CONV(gm_csguankko4lmhd, t136), GM_CONV(gm_csguankko4lmhd, t137));
                t131 = t138;
            } else t131 = 0;
            uint8_t t139 = 0;
            F.ret = t131;
            goto gm_fret;
        }
        goto ix126;
ie126:;
ix126:;
        goto me30;
mn122:;
ma35:;
        uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t141 != ((uint8_t)9ULL)) goto mn140;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l23 = t142;
        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l24 = t143;
        uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t145 != ((uint8_t)9ULL)) goto ie144;
        uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l25 = t146;
        gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l26 = t147;
        {
            uint8_t t149 = 0;
            uint8_t t150 = gm_l23;
            uint8_t t151 = gm_l25;
            uint8_t t152 = ((t150 == t151));
            if (t152) {
                void * t153 = gm_l0;
                gm_t88rcucg1b7uk t154 = gm_l24;
                gm_t88rcucg1b7uk t155 = gm_l26;
                uint8_t t156 = gm_f_may_unify_0noeh56(t153, GM_CONV(gm_csguankko4lmhd, t154), GM_CONV(gm_csguankko4lmhd, t155));
                t149 = t156;
            } else t149 = 0;
            uint8_t t157 = 0;
            F.ret = t149;
            goto gm_fret;
        }
        goto ix144;
ie144:;
ix144:;
        goto me30;
mn140:;
ma36:;
        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t159 != ((uint8_t)13ULL)) goto mn158;
        gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
        gm_l27 = t160;
        uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t162 != ((uint8_t)13ULL)) goto ie161;
        gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l28 = t163;
        {
            void * t165 = gm_l0;
            gm_t88rcucg1b7uk t166 = gm_l27;
            gm_t88rcucg1b7uk t167 = gm_l28;
            uint8_t t168 = gm_f_may_unify_0noeh56(t165, GM_CONV(gm_csguankko4lmhd, t166), GM_CONV(gm_csguankko4lmhd, t167));
            uint8_t t169 = 0;
            F.ret = t168;
            goto gm_fret;
        }
        goto ix161;
ie161:;
ix161:;
        goto me30;
mn158:;
ma37:;
        uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t171 != ((uint8_t)18ULL)) goto mn170;
        gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
        gm_l29 = t172;
        uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t174 != ((uint8_t)18ULL)) goto ie173;
        gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l30 = t175;
        {
            void * t177 = gm_l0;
            gm_t88rcucg1b7uk t178 = gm_l29;
            gm_t88rcucg1b7uk t179 = gm_l30;
            uint8_t t180 = gm_f_may_unify_0noeh56(t177, GM_CONV(gm_csguankko4lmhd, t178), GM_CONV(gm_csguankko4lmhd, t179));
            uint8_t t181 = 0;
            F.ret = t180;
            goto gm_fret;
        }
        goto ix173;
ie173:;
ix173:;
        goto me30;
mn170:;
ma38:;
        uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t183 != ((uint8_t)14ULL)) goto mn182;
        gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l31 = t184;
        gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l32 = t185;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t187 != ((uint8_t)14ULL)) goto ie186;
        gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l33 = t188;
        gm_t88rcucg1b7uk t189 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l34 = t189;
        {
            uint8_t t191 = 0;
            void * t192 = gm_l0;
            gm_t88rcucg1b7uk t193 = gm_l31;
            gm_t88rcucg1b7uk t194 = gm_l33;
            uint8_t t195 = gm_f_may_unify_0noeh56(t192, GM_CONV(gm_csguankko4lmhd, t193), GM_CONV(gm_csguankko4lmhd, t194));
            if (t195) {
                void * t196 = gm_l0;
                gm_t88rcucg1b7uk t197 = gm_l32;
                gm_t88rcucg1b7uk t198 = gm_l34;
                uint8_t t199 = gm_f_may_unify_0noeh56(t196, GM_CONV(gm_csguankko4lmhd, t197), GM_CONV(gm_csguankko4lmhd, t198));
                t191 = t199;
            } else t191 = 0;
            uint8_t t200 = 0;
            F.ret = t191;
            goto gm_fret;
        }
        goto ix186;
ie186:;
ix186:;
        goto me30;
mn182:;
ma39:;
        uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t202 != ((uint8_t)12ULL)) goto mn201;
        gm_b16_8 t203 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l35 = t203;
        gm_b16_8 t204 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 16));
        gm_l36 = t204;
        uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t206 != ((uint8_t)12ULL)) goto ie205;
        gm_b16_8 t207 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l37 = t207;
        gm_b16_8 t208 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 16));
        gm_l38 = t208;
        {
            uint8_t t211 = 0;
            uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
            uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
            uint8_t t214 = (!(t212 == t213));
            if (!t214) {
                uint64_t t215 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                uint8_t t217 = (!(t215 == t216));
                t211 = t217;
            } else t211 = 1;
            if (!t211) goto ie210;
            {
                uint8_t t219 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix210;
ie210:;
ix210:;
        }
        {
            {
                gm_b16_8 t221 = gm_l36;
                gm_l39 = t221;
            }
            {
                gm_l40 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t223 = gm_l40;
                    uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                    uint8_t t225 = (t223 < t224);
                    if (!t225) goto lb222;
                    {
                        gm_tf1bjg81u0glv t228;
                        uint64_t t229 = gm_l40;
                        GM_ST(uint64_t, (uint8_t *)&t228 + 0, t229);
                        uint64_t t230 = gm_l40;
                        uint64_t t231 = (uintptr_t)t230;
                        uint64_t t232 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                        gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t231 * 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t228 + 8, t233);
                        gm_tf1bjg81u0glv t234 = t228;
                        uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)&t234) + 0));
                        gm_l41 = t237;
                        gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
                        gm_l42 = t238;
                        {
                            gm_b16_8 t241 = gm_l42;
                            uint64_t t242 = gm_l41;
                            uint64_t t243 = (uintptr_t)t242;
                            uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                            if (t243 >= (uintptr_t)(t244)) gm_trap_index(t243, t244, gm_srcgr2mvgis129a, 194, 30);
                            gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t243 * 16));
                            gm_b16_8 t246 = t241;
                            gm_b16_8 t247 = t245;
                            uint8_t t248 = (!gm_eq0iqiubfm7e(((uint8_t *)&t246), ((uint8_t *)&t247)));
                            if (!t248) goto ie240;
                            {
                                uint8_t t250 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix240;
ie240:;
ix240:;
                        }
                        goto me235;
mn236:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 193, 13);
me235:;
                    }
lc222:;
                    {
                        uint64_t t252 = gm_l40;
                        uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                        uint64_t t255 = r253;
                        gm_l40 = t255;
                    }
                }
lb222:;
            }
        }
        {
            {
                gm_b16_8 t257 = gm_l35;
                gm_l43 = t257;
            }
            {
                gm_l44 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t259 = gm_l44;
                    uint64_t t260 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                    uint8_t t261 = (t259 < t260);
                    if (!t261) goto lb258;
                    {
                        uint64_t t264 = gm_l44;
                        uint64_t t265 = gm_l44;
                        uint64_t t266 = (uintptr_t)t265;
                        uint64_t t267 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                        gm_t88rcucg1b7uk t268 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t266 * 4));
                        gm_tlcm4lvu3g1a8 t269;
                        GM_ST(uint32_t, (uint8_t *)&t269 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t269 + 0), t264);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t269 + 8), t268);
                        gm_tlcm4lvu3g1a8 t270 = t269;
                        uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&t270) + 0));
                        gm_l45 = t273;
                        gm_t88rcucg1b7uk t274 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t270) + 8));
                        gm_l46 = t274;
                        {
                            void * t277 = gm_l0;
                            gm_t88rcucg1b7uk t278 = gm_l46;
                            uint64_t t279 = gm_l45;
                            uint64_t t280 = (uintptr_t)t279;
                            uint64_t t281 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                            if (t280 >= (uintptr_t)(t281)) gm_trap_index(t280, t281, gm_srcgr2mvgis129a, 197, 40);
                            gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t280 * 4));
                            uint8_t t283 = gm_f_may_unify_0noeh56(t277, GM_CONV(gm_csguankko4lmhd, t278), GM_CONV(gm_csguankko4lmhd, t282));
                            uint8_t t284 = (uint8_t)!t283;
                            if (!t284) goto ie276;
                            {
                                uint8_t t286 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix276;
ie276:;
ix276:;
                        }
                        goto me271;
mn272:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 196, 13);
me271:;
                    }
lc258:;
                    {
                        uint64_t t288 = gm_l44;
                        uint64_t r289; bool o290 = __builtin_add_overflow(t288, ((uint64_t)1ULL), &r289);
                        uint64_t t291 = r289;
                        gm_l44 = t291;
                    }
                }
lb258:;
            }
        }
        {
            uint8_t t292 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix205;
ie205:;
ix205:;
        goto me30;
mn201:;
ma40:;
        uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t294 != ((uint8_t)17ULL)) goto mn293;
        uint32_t t295 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l47 = t295;
        gm_b16_8 t296 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 8));
        gm_l48 = t296;
        uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t298 != ((uint8_t)17ULL)) goto ie297;
        uint32_t t299 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l49 = t299;
        gm_b16_8 t300 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l50 = t300;
        {
            uint8_t t303 = 0;
            void * t304 = gm_l0;
            void * t305 = (void *)(((uint8_t *)t304) + 8);
            uint32_t t306 = gm_l47;
            uint32_t t307 = gm_f_template_9o92cvv(t305, t306);
            void * t308 = gm_l0;
            void * t309 = (void *)(((uint8_t *)t308) + 8);
            uint32_t t310 = gm_l49;
            uint32_t t311 = gm_f_template_9o92cvv(t309, t310);
            uint8_t t312 = (!(t307 == t311));
            if (!t312) {
                uint64_t t313 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                uint64_t t314 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                uint8_t t315 = (!(t313 == t314));
                t303 = t315;
            } else t303 = 1;
            if (!t303) goto ie302;
            {
                uint8_t t317 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix302;
ie302:;
ix302:;
        }
        {
            {
                gm_b16_8 t319 = gm_l48;
                gm_l51 = t319;
            }
            {
                gm_l52 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t321 = gm_l52;
                    uint64_t t322 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                    uint8_t t323 = (t321 < t322);
                    if (!t323) goto lb320;
                    {
                        gm_tdr8bh2bm3fc8 t326;
                        uint64_t t327 = gm_l52;
                        GM_ST(uint64_t, (uint8_t *)&t326 + 0, t327);
                        uint64_t t328 = gm_l52;
                        uint64_t t329 = (uintptr_t)t328;
                        uint64_t t330 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                        memcpy((uint8_t *)&t326 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t329 * 40), 40);
                        gm_tdr8bh2bm3fc8 t331 = t326;
                        uint64_t t334 = GM_LD(uint64_t, (((uint8_t *)&t331) + 0));
                        gm_l53 = t334;
                        gm_toa2vhe3l4hfr t335 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)&t331) + 8));
                        F.l54 = t335;
                        {
                            void * t338 = gm_l0;
                            uint64_t t339 = gm_l53;
                            uint64_t t340 = (uintptr_t)t339;
                            uint64_t t341 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                            if (t340 >= (uintptr_t)(t341)) gm_trap_index(t340, t341, gm_srcgr2mvgis129a, 204, 44);
                            uint8_t t342 = gm_f_arg_may_unify_qmi580g(t338, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&F.l54)), &(*(gm_toa2vhe3l4hfr *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t340 * 40)));
                            uint8_t t343 = (uint8_t)!t342;
                            if (!t343) goto ie337;
                            {
                                uint8_t t345 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix337;
ie337:;
ix337:;
                        }
                        goto me332;
mn333:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 203, 13);
me332:;
                    }
lc320:;
                    {
                        uint64_t t347 = gm_l52;
                        uint64_t r348; bool o349 = __builtin_add_overflow(t347, ((uint64_t)1ULL), &r348);
                        uint64_t t350 = r348;
                        gm_l52 = t350;
                    }
                }
lb320:;
            }
        }
        {
            uint8_t t351 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix297;
ie297:;
ix297:;
        goto me30;
mn293:;
ma41:;
        uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t353 != ((uint8_t)15ULL)) goto mn352;
        uint8_t t354;
        uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t357 != ((uint8_t)15ULL)) goto mn356;
        t354 = ((uint8_t)1);
        goto me355;
mn356:;
        t354 = ((uint8_t)0);
        goto me355;
mn358:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 209, 26);
me355:;
        uint8_t t359 = 0;
        F.ret = t354;
        goto gm_fret;
        goto me30;
mn352:;
ma42:;
        uint8_t t361 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t361 != ((uint8_t)16ULL)) goto mn360;
        uint8_t t362;
        uint8_t t365 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t365 != ((uint8_t)16ULL)) goto mn364;
        t362 = ((uint8_t)1);
        goto me363;
mn364:;
        t362 = ((uint8_t)0);
        goto me363;
mn366:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 210, 29);
me363:;
        uint8_t t367 = 0;
        F.ret = t362;
        goto gm_fret;
        goto me30;
mn360:;
ma43:;
        goto me30;
mn368:;
ma44:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 175, 5);
me30:;
    }
    {
        uint8_t t370 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_template_instance_is_ovv1pcj(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_template_instance_is_ovv1pcj F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* k */
    void * gm_l2; /* eref */
    gm_t88rcucg1b7uk gm_l3; /* t */
    GM_STACK_CHECK(&F, 188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 592) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcgr2mvgis129a, 154, 12);
        gm_t9qmk75lrp9p4 t6 = GM_LD(gm_t9qmk75lrp9p4, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 592) + 0)) + t3 * 96));
        F.l4 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = (void *)((uint8_t *)&F.l4);
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = gm_l3;
        uint8_t t11 = gm_f_template_probe_is_d1ep4c6((void *)NULL, cx, NULL, t7, t8, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_template_map_lfm02i0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_template_map_lfm02i0 F;
    void * gm_l0; /* tpl */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* p */
    GM_STACK_CHECK(&F, 330u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_oalq03d((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_b16_8 t5 = t4;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_l3 = t6;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l4;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l4;
                    uint64_t t14 = gm_l4;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 4));
                    gm_tn68el8d14vku t18;
                    GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t13);
                    GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
                    gm_tn68el8d14vku t19 = t18;
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
                    gm_l5 = t22;
                    uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&t19) + 8));
                    gm_l6 = t23;
                    {
                        void * t25 = (void *)((uint8_t *)&F.l2);
                        uint32_t t26 = gm_l6;
                        uint64_t t27 = gm_l5;
                        uint64_t t28 = (uintptr_t)t27;
                        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srcgr2mvgis129a, 148, 77);
                        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t28 * 4));
                        gm_tplpbqf3duvn3 t31;
                        GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t26);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t31 + 4), t30);
                        gm_tftb1odc739t5 t32 = gm_f_push_h69n5fi(t25, GM_CONV(gm_cs7e9ppsd384bj, t31));
                        gm_tftb1odc739t5 t33 = t32;
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcgr2mvgis129a, 148, 43);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 148, 5);
me20:;
                }
lc7:;
                {
                    uint64_t t36 = gm_l4;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l4 = t39;
                }
            }
lb7:;
        }
    }
    {
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t41 = 0;
        F.ret = t40;
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
inline __attribute__((aligned(4))) uint8_t gm_f_template_probe_is_d1ep4c6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_template_probe_is_d1ep4c6 F;
    void * gm_l0; /* s */
    void * gm_l1; /* tpl */
    void * gm_l2; /* eref */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* map */
    gm_tiilmfd16v3fc gm_l6; /* inst */
    GM_STACK_CHECK(&F, 465u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 16) + 0));
        void * t5 = gm_l2;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 0));
        uint8_t t7 = (!(t4 == t6));
        if (!t7) goto ie2;
        {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l1;
        gm_t88rcucg1b7uk t12 = gm_l3;
        gm_th2lqbo77pvsc t13;
        gm_f_handler_template_args_dlm07kb((void *)&t13, (void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t14 = t13;
        gm_b16_8 t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t15;
    }
    {
        void * t18 = gm_l1;
        gm_b16_8 t19 = gm_l4;
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        gm_b16_8 t22 = gm_f_template_map_lfm02i0((void *)NULL, cx, NULL, t18, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t22;
    }
    {
        void * t24 = gm_l0;
        void * t25 = gm_l1;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t25) + 40));
        gm_b16_8 t27 = gm_l5;
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_t88rcucg1b7uk t30 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t24, t26, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t31 = gm_l3;
        gm_t88rcucg1b7uk t32 = t30;
        gm_t88rcucg1b7uk t33 = t31;
        uint8_t t34 = (!gm_eq9m6mvtmgde(((uint8_t *)&t32), ((uint8_t *)&t33)));
        if (!t34) goto ie23;
        {
            uint8_t t36 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = gm_l1;
        void * t39 = (void *)(((uint8_t *)t38) + 16);
        gm_b16_8 t40 = gm_l5;
        gm_b16_8 t41 = t40;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        gm_f_subst_eref_kf9o1q5((void *)&gm_l6, (void *)NULL, cx, NULL, t37, t39, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t43 = (void *)((uint8_t *)&gm_l6);
        void * t44 = gm_l2;
        int32_t t45 = gm_f_cmp_ref_8ntrqll(t43, t44);
        uint8_t t46 = ((t45 == ((int32_t)0LL)));
        uint8_t t47 = 0;
        F.ret = t46;
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

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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint32_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; gm_t9qkh0rsooc8d f4; gm_tkn42u7na3t81 f5; gm_toceffb697p0u f6; gm_toceffb697p0u f7; } gm_t4lq9ph3amfvg;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t2i8ne5ibd7v4 f0; uint8_t f1; uint8_t pe[3]; } gm_t8uadrbfpr7kk;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lq9ph3amfvg) == 48 && _Alignof(gm_t4lq9ph3amfvg) == 4 && offsetof(gm_t4lq9ph3amfvg, f0) == 0 && offsetof(gm_t4lq9ph3amfvg, f1) == 4 && offsetof(gm_t4lq9ph3amfvg, f2) == 5 && offsetof(gm_t4lq9ph3amfvg, f3) == 6 && offsetof(gm_t4lq9ph3amfvg, f4) == 7 && offsetof(gm_t4lq9ph3amfvg, f5) == 8 && offsetof(gm_t4lq9ph3amfvg, f6) == 16 && offsetof(gm_t4lq9ph3amfvg, f7) == 32, "layout of `Var`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8uadrbfpr7kk) == 20 && _Alignof(gm_t8uadrbfpr7kk) == 4 && offsetof(gm_t8uadrbfpr7kk, f0) == 0 && offsetof(gm_t8uadrbfpr7kk, f1) == 16, "layout of `?(ty: Ty, at: Span)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_thdmr9qicg35r f2; uint8_t pe[4]; } gm_tti6qnvf9jg1p;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tti6qnvf9jg1p) == 72 && _Alignof(gm_tti6qnvf9jg1p) == 8 && offsetof(gm_tti6qnvf9jg1p, f0) == 0 && offsetof(gm_tti6qnvf9jg1p, f1) == 40 && offsetof(gm_tti6qnvf9jg1p, f2) == 56, "layout of `InferTable`");
struct gm_fr_apply_fallbacks_gcbql0t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bind_btu6lfa {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_compatible_rirr7pa {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oldiiq9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fallback_max_vinhf97 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_fallback_type_8qqovjs {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fixed_by_afiovc1 {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_fn_4b1mi3j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_infer_table_6o0km8j {
    gm_frame h;
    gm_tti6qnvf9jg1p ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_complex_81cs4ab {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_e6ie1fq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_new_var_up2m1nq {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_note_fixed_by_ev84og7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_require_integer_66qj2qi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_require_unsigned_0fhlknt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_resolve_args_i7jdhok {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_toa2vhe3l4hfr l6; /* a */
    gm_b16_8 ret;
};
struct gm_fr_resolve_effects_j374dhk {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tnpifv20irnph ret;
};
struct gm_fr_resolve_sig_ahumdsh {
    gm_frame h;
    gm_tddtjfggdat0d l2; /* sig */
    gm_trqfvmjtt4sij l3; /* params */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_resolve_t2kal61 {
    gm_frame h;
    gm_trqfvmjtt4sij l23; /* out */
    gm_tddtjfggdat0d l27; /* sig */
    gm_tddtjfggdat0d l28; /* sig */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_root_jbs2avl {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_set_fallback_s66n99d {
    gm_frame h;
    gm_t8uadrbfpr7kk ret;
};
struct gm_fr_settle_vars_vpp9ov6 {
    gm_frame h;
    void * l1; /* s */
    gm_trqfvmjtt4sij l5; /* vars */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_shallow_8ur803a {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_unify_structural_a96lmna {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_unify_t2qjv2p {
    gm_frame h;
    gm_t53at80pfdus3 l6; /* ka */
    gm_t53at80pfdus3 l7; /* kb */
    gm_t4lq9ph3amfvg l12; /* vx */
    gm_t4lq9ph3amfvg l13; /* vy */
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_var_kind_1sl5sig {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_fallbacks_gcbql0t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_bind_btu6lfa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) uint8_t gm_f_compatible_rirr7pa(void * a0, void * a1, void * a2, gm_csguankko4lmhd a3);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fallback_max_vinhf97(gm_t9qkh0rsooc8d a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_fallback_type_8qqovjs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_fixed_by_afiovc1(void * a0, void * a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4b1mi3j(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_infer_table_6o0km8j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_complex_81cs4ab(void * a0, void * a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_new_var_up2m1nq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_note_fixed_by_ev84og7(void * a0, void * a1, gm_csguankko4lmhd a2, gm_cseahvg8v7p0vs a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t6e85n8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_integer_66qj2qi(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_resolve_args_i7jdhok(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effects_j374dhk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tnpifv20irnph a2);
__attribute__((aligned(4))) void gm_f_resolve_sig_ahumdsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint32_t gm_f_root_jbs2avl(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_set_fallback_s66n99d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_vars_vpp9ov6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_shallow_8ur803a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_structural_a96lmna(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_sq5rlbq4gbs5v[27];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclu7allsekqhf[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)1) return gm_eq9m6mvtmgde(a + 4, b + 4);
    return true;
}
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    for (uintptr_t i = 0; i < n; i++) if (!gm_eq0iqiubfm7e(pa + i * 16, pb + i * 16)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oldiiq9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oldiiq9 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fallback_max_vinhf97(gm_t9qkh0rsooc8d a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fallback_max_vinhf97 F;
    gm_t9qkh0rsooc8d gm_l0; /* a */
    gm_t9qkh0rsooc8d gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9qkh0rsooc8d t1;
    uint8_t t3 = 0;
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)1ULL)));
    if (!t4) {
        uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
        t3 = t5;
    } else t3 = 1;
    if (!t3) goto ie2;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t11 = t10;
    t1 = t11;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4b1mi3j(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_4b1mi3j F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* v */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_settle_vars_vpp9ov6 *)((&F.h)->up))->l1));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l0;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)23ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l1 = t8;
        {
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_settle_vars_vpp9ov6 *)((&F.h)->up))->l5);
            uint32_t t11 = gm_l1;
            gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t10, t11);
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srclu7allsekqhf, 295, 41);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
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
inline __attribute__((aligned(4))) void gm_f_infer_table_6o0km8j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_infer_table_6o0km8j F;
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 68, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_e6ie1fq((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
    gm_thdmr9qicg35r t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 0, t1);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 40, t2);
    GM_ST(gm_thdmr9qicg35r, (uint8_t *)&(*(gm_tti6qnvf9jg1p *)gm_out) + 56, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
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
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_e6ie1fq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_e6ie1fq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oldiiq9();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
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
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 176) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 176) + 0)) + t3 * 4));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = GM_LD(gm_b16_8, gm_c3t36dlh4g5);
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_e79cvtl((void *)&t7, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srclv0dcbbfhves, 635, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_apply_fallbacks_gcbql0t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_apply_fallbacks_gcbql0t F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* r */
    GM_STACK_CHECK(&F, 286u);
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
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
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
                        void * t16 = gm_l0;
                        uint64_t t17 = gm_l4;
                        uint32_t t18 = (uint32_t)t17;
                        uint32_t t19 = gm_f_root_jbs2avl(t16, t18);
                        uint64_t t20 = (uint64_t)t19;
                        gm_l5 = t20;
                    }
                    {
                        uint64_t t22 = gm_l5;
                        uint64_t t23 = (uintptr_t)t22;
                        void * t24 = gm_l0;
                        uint64_t t25 = GM_LD(uintptr_t, ((((uint8_t *)t24) + 0) + 0) + 8);
                        if (t23 >= (uintptr_t)(t25)) gm_trap_index(t23, t25, gm_srclu7allsekqhf, 311, 12);
                        uint8_t t26;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t24) + 0) + 0)) + t23 * 48) + 8))[4] != 0)) goto mn28;
                        t26 = ((uint8_t)1);
                        goto me27;
mn28:;
                        t26 = ((uint8_t)0);
                        goto me27;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 311, 12);
me27:;
                        if (!t26) goto ie21;
                        {
                            uint64_t t31 = gm_l5;
                            uint64_t t32 = (uintptr_t)t31;
                            void * t33 = gm_l0;
                            uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 0) + 0) + 8);
                            if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srclu7allsekqhf, 312, 13);
                            uint8_t *t35 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 0) + 0)) + t32 * 48) + 8);
                            void * t36 = gm_l0;
                            void * t37 = gm_l1;
                            uint64_t t38 = gm_l5;
                            uint64_t t39 = (uintptr_t)t38;
                            void * t40 = gm_l0;
                            uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 0) + 0) + 8);
                            if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_srclu7allsekqhf, 312, 63);
                            void * t42 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t40) + 0) + 0)) + t39 * 48);
                            gm_t88rcucg1b7uk t43 = gm_f_fallback_type_8qqovjs((void *)NULL, cx, NULL, t36, t37, t42);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tkn42u7na3t81 t44;
                            GM_ST(uint8_t, (uint8_t *)&t44 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t44 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44), t43);
                            ((uint8_t *)&t44)[4] = 1;
                            GM_ST(gm_tkn42u7na3t81, t35, t44);
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 309, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t46 = gm_l3;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l3 = t49;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_bind_btu6lfa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_bind_btu6lfa F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    uint32_t gm_l2; /* v */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    uint64_t gm_l4; /* r */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l2;
        uint32_t t4 = gm_f_root_jbs2avl(t2, t3);
        uint64_t t5 = (uint64_t)t4;
        gm_l4 = t5;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l1;
        uint64_t t9 = gm_l4;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 0) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srclu7allsekqhf, 251, 31);
        void * t13 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 0) + 0)) + t10 * 48);
        gm_t88rcucg1b7uk t14 = gm_l3;
        uint8_t t15 = gm_f_compatible_rirr7pa(t7, t8, t13, GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie6;
        {
            gm_b0_1 t18;
            gm_t9qkh0rsooc8d t19;
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t19 + 1), t18);
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t19;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint64_t t21 = gm_l4;
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 0) + 0) + 8);
        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_srclu7allsekqhf, 252, 5);
        gm_t88rcucg1b7uk t25 = gm_l3;
        gm_tkn42u7na3t81 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[4] = 1;
        GM_ST(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t23) + 0) + 0)) + t22 * 48) + 8), t26);
    }
    {
        gm_b0_1 t27;
        gm_t9qkh0rsooc8d t28;
        ((uint8_t *)&t28)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t28 + 1), t27);
        uint8_t t29 = ((((uint8_t *)&t28))[0] != 0);
        F.ret = t28;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_compatible_rirr7pa(void * a0, void * a1, void * a2, gm_csguankko4lmhd a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_compatible_rirr7pa F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    void * gm_l2; /* v */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    uint32_t gm_l4; /* p */
    uint8_t gm_l5; /* is_float */
    GM_STACK_CHECK_SHALLOW(&F, 339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = GM_CONV(gm_t88rcucg1b7uk, a3);
    {
        void * t3 = gm_l2;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 4) + 0) == ((uint8_t)2ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            void * t7 = gm_l1;
            gm_t88rcucg1b7uk t8 = gm_l3;
            uint8_t t9 = gm_f_is_complex_81cs4ab(t6, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l1;
        gm_t88rcucg1b7uk t13 = gm_l3;
        gm_t53at80pfdus3 t14;
        gm_f_kind_jcnt4hm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t15 != ((uint8_t)19ULL)) goto ie11;
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&t14) + 8));
        gm_l4 = t16;
        {
            uint8_t t18 = 0;
            void * t19 = gm_l2;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t19) + 5));
            uint8_t t21 = (uint8_t)!t20;
            if (t21) {
                uint32_t t22 = gm_l4;
                void * t23 = gm_l0;
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 40));
                uint8_t t25 = 0;
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t24) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t24))) + i * 4, ((uint8_t *)&t22))) { t25 = 1; break; }
                uint8_t t26 = (uint8_t)(t25);
                t18 = t26;
            } else t18 = 0;
            uint8_t t27 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t28 = gm_l1;
        gm_t88rcucg1b7uk t29 = gm_l3;
        gm_t53at80pfdus3 t30;
        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
        uint8_t t31;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t34 != ((uint8_t)3ULL)) goto mn33;
        t31 = ((uint8_t)1);
        goto me32;
mn33:;
        t31 = ((uint8_t)0);
        goto me32;
mn35:;
        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 203, 17);
me32:;
        gm_l5 = t31;
    }
    {
        void * t37 = gm_l2;
        uint8_t t38 = ((GM_LD(uint8_t, (((uint8_t *)t37) + 4) + 0) == ((uint8_t)1ULL)));
        if (!t38) goto ie36;
        {
            uint8_t t40 = gm_l5;
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        void * t43 = gm_l2;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t43) + 5));
        if (!t44) goto ie42;
        {
            uint8_t t46 = 0;
            void * t47 = gm_l1;
            gm_t88rcucg1b7uk t48 = gm_l3;
            uint8_t t49 = gm_f_is_integer_8d4f9ek(t47, GM_CONV(gm_csguankko4lmhd, t48));
            if (t49) {
                void * t50 = gm_l1;
                gm_t88rcucg1b7uk t51 = gm_l3;
                uint8_t t52 = gm_f_is_signed_integer_s66djm7(t50, GM_CONV(gm_csguankko4lmhd, t51));
                uint8_t t53 = (uint8_t)!t52;
                t46 = t53;
            } else t46 = 0;
            uint8_t t54 = 0;
            F.ret = t46;
            goto gm_fret;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        void * t56 = gm_l2;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t56) + 6));
        if (!t57) goto ie55;
        {
            void * t59 = gm_l1;
            gm_t88rcucg1b7uk t60 = gm_l3;
            uint8_t t61 = gm_f_is_integer_8d4f9ek(t59, GM_CONV(gm_csguankko4lmhd, t60));
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        uint8_t t63 = 0;
        void * t64 = gm_l1;
        gm_t88rcucg1b7uk t65 = gm_l3;
        uint8_t t66 = gm_f_is_integer_8d4f9ek(t64, GM_CONV(gm_csguankko4lmhd, t65));
        if (!t66) {
            uint8_t t67 = gm_l5;
            t63 = t67;
        } else t63 = 1;
        uint8_t t68 = 0;
        F.ret = t63;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_fallback_max_vinhf97(gm_t9qkh0rsooc8d a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_fallback_type_8qqovjs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_fallback_type_8qqovjs F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    void * gm_l2; /* v */
    gm_tplpbqf3duvn3 gm_l3; /* c */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 4) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; default: goto ma7; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 4) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        void * t11 = gm_l1;
        gm_t53at80pfdus3 t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)3ULL));
        GM_ST(uint16_t, ((uint8_t *)&t12 + 8), ((uint16_t)64ULL));
        gm_t88rcucg1b7uk t13 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t11, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
        goto me3;
mn9:;
ma5:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 4) + 0));
        if (t16 != ((uint8_t)2ULL)) goto mn15;
        gm_t88rcucg1b7uk t17;
        void * t19 = gm_l0;
        if (!(((((uint8_t *)t19) + 56))[8] != 0)) goto ie18;
        gm_tplpbqf3duvn3 t20 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t19) + 56));
        gm_l3 = t20;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 4));
        gm_t88rcucg1b7uk t23 = t22;
        t17 = t23;
        goto ix18;
ie18:;
        void * t25 = gm_l1;
        gm_t88rcucg1b7uk t26 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t27 = t26;
        t17 = t27;
ix18:;
        uint8_t t28 = 0;
        F.ret = t17;
        goto gm_fret;
        goto me3;
mn15:;
ma6:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 4) + 0));
        if (t30 != ((uint8_t)0ULL)) goto mn29;
        {
            void * t33 = gm_l2;
            uint8_t t34 = ((GM_LD(uint8_t, (((uint8_t *)t33) + 7) + 0) == ((uint8_t)1ULL)));
            if (!t34) goto ie32;
            {
                void * t36 = gm_l1;
                gm_t88rcucg1b7uk t37 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t38 = 0;
                F.ret = t37;
                goto gm_fret;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            gm_t88rcucg1b7uk t39;
            void * t41 = gm_l2;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 5));
            if (!t42) goto ie40;
            void * t44 = gm_l1;
            gm_t88rcucg1b7uk t45 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t44, ((uint8_t)0), ((uint16_t)32ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t46 = t45;
            t39 = t46;
            goto ix40;
ie40:;
            void * t48 = gm_l1;
            gm_t88rcucg1b7uk t49 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t48, ((uint8_t)1), ((uint16_t)32ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t50 = t49;
            t39 = t50;
ix40:;
            uint8_t t51 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto me3;
mn29:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 280, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_fixed_by_afiovc1(void * a0, void * a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_by_afiovc1 F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l3; /* v */
    GM_STACK_CHECK(&F, 227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)23ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l3 = t7;
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 0) + 0));
            gm_b16_8 t11 = t10;
            void * t12 = gm_l0;
            uint32_t t13 = gm_l3;
            uint32_t t14 = gm_f_root_jbs2avl(t12, t13);
            uint64_t t15 = (uint64_t)t14;
            uint64_t t16 = (uintptr_t)t15;
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&t11) + 8);
            if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srclu7allsekqhf, 72, 12);
            gm_toceffb697p0u t18 = GM_LD(gm_toceffb697p0u, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t11)) + t16 * 48) + 32));
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_toceffb697p0u t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4b1mi3j(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) void gm_f_infer_table_6o0km8j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_is_complex_81cs4ab(void * a0, void * a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_complex_81cs4ab F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l3; /* id */
    gm_tplpbqf3duvn3 gm_l4; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l3 = t7;
        {
            void * t9 = gm_l0;
            gm_thdmr9qicg35r t10 = GM_LD(gm_thdmr9qicg35r, (((uint8_t *)t9) + 56));
            gm_tplpbqf3duvn3 t11;
            if (((((uint8_t *)&t10))[8] != 0)) {
                gm_tplpbqf3duvn3 t12 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)&t10) + 0));
                t11 = t12;
            } else {
                uint8_t t13 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l4 = t11;
        }
        {
            void * t14 = gm_l1;
            uint32_t t15 = gm_l3;
            uint32_t t16 = gm_f_template_9o92cvv(t14, t15);
            uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t18 = ((t16 == t17));
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t21 = 0;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_new_var_up2m1nq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_new_var_up2m1nq F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint32_t gm_l3; /* v */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 0) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 0);
        gm_t4lq9ph3amfvg t7;
        uint32_t t8 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t7 + 0, t8);
        gm_t9qkh0rsooc8d t9 = gm_l2;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t7 + 4, t9);
        GM_ST(uint8_t, (uint8_t *)&t7 + 5, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&t7 + 6, ((uint8_t)0));
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t7 + 7, t10);
        gm_tkn42u7na3t81 t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t7 + 8, t11);
        gm_toceffb697p0u t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(gm_toceffb697p0u, (uint8_t *)&t7 + 16, t12);
        gm_toceffb697p0u t13;
        memset(&t13, 0, sizeof t13);
        GM_ST(gm_toceffb697p0u, (uint8_t *)&t7 + 32, t13);
        gm_tftb1odc739t5 t14 = gm_f_push_t6e85n8(t6, &t7);
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srclu7allsekqhf, 50, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
        void * t16 = gm_l1;
        uint32_t t17 = gm_l3;
        gm_t53at80pfdus3 t18;
        memset(&t18, 0, sizeof t18);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)23ULL));
        GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
        gm_t88rcucg1b7uk t19 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t16, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = 0;
        F.ret = t19;
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
inline __attribute__((aligned(4))) void gm_f_note_fixed_by_ev84og7(void * a0, void * a1, gm_csguankko4lmhd a2, gm_cseahvg8v7p0vs a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_note_fixed_by_ev84og7 F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* v */
    uint64_t gm_l5; /* r */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_l3 = GM_CONV(gm_tl874t2atnlh3, a3);
    {
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)23ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l4 = t7;
        {
            void * t9 = gm_l0;
            uint32_t t10 = gm_l4;
            uint32_t t11 = gm_f_root_jbs2avl(t9, t10);
            uint64_t t12 = (uint64_t)t11;
            gm_l5 = t12;
        }
        {
            uint64_t t14 = gm_l5;
            uint64_t t15 = (uintptr_t)t14;
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 0) + 0) + 8);
            if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srclu7allsekqhf, 66, 8);
            uint8_t t18;
            if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 0) + 0)) + t15 * 48) + 32))[12] != 0)) goto mn20;
            t18 = ((uint8_t)1);
            goto me19;
mn20:;
            t18 = ((uint8_t)0);
            goto me19;
mn21:;
            gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 66, 8);
me19:;
            if (!t18) goto ie13;
            {
                uint64_t t23 = gm_l5;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 0) + 0) + 8);
                if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srclu7allsekqhf, 66, 43);
                gm_tl874t2atnlh3 t27 = gm_l3;
                gm_toceffb697p0u t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t28), t27);
                ((uint8_t *)&t28)[12] = 1;
                GM_ST(gm_toceffb697p0u, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 0) + 0)) + t24 * 48) + 32), t28);
            }
            goto ix13;
ie13:;
ix13:;
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t30;
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_require_integer_66qj2qi(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_require_integer_66qj2qi F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_t88rcucg1b7uk gm_l3; /* st */
    uint32_t gm_l4; /* v */
    uint64_t gm_l5; /* r */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_t53at80pfdus3 t9;
        gm_f_kind_jcnt4hm((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t10 != ((uint8_t)23ULL)) goto ie6;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&t9) + 8));
        gm_l4 = t11;
        {
            void * t13 = gm_l0;
            uint32_t t14 = gm_l4;
            uint32_t t15 = gm_f_root_jbs2avl(t13, t14);
            uint64_t t16 = (uint64_t)t15;
            gm_l5 = t16;
        }
        {
            uint64_t t18 = gm_l5;
            uint64_t t19 = (uintptr_t)t18;
            void * t20 = gm_l0;
            uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 0) + 0) + 8);
            if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srclu7allsekqhf, 151, 12);
            uint8_t t22 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 0) + 0)) + t19 * 48) + 4) + 0) == ((uint8_t)0ULL)));
            if (!t22) goto ie17;
            {
                uint8_t t24 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            uint64_t t25 = gm_l5;
            uint64_t t26 = (uintptr_t)t25;
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 0) + 0) + 8);
            if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srclu7allsekqhf, 152, 9);
            GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 0) + 0)) + t26 * 48) + 6), ((uint8_t)1));
        }
        {
            uint8_t t29 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t30 = gm_l1;
        gm_t88rcucg1b7uk t31 = gm_l3;
        uint8_t t32 = gm_f_is_integer_8d4f9ek(t30, GM_CONV(gm_csguankko4lmhd, t31));
        uint8_t t33 = 0;
        F.ret = t32;
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
inline __attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3) {
    struct gm_fr_require_unsigned_0fhlknt F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_t9qkh0rsooc8d gm_l3; /* fallback */
    gm_t88rcucg1b7uk gm_l4; /* st */
    uint32_t gm_l5; /* v */
    uint64_t gm_l6; /* r */
    GM_STACK_CHECK(&F, 331u);
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
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = gm_l4;
        gm_t53at80pfdus3 t9;
        gm_f_kind_jcnt4hm((void *)&t9, t7, GM_CONV(gm_csguankko4lmhd, t8));
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t10 != ((uint8_t)23ULL)) goto ie6;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&t9) + 8));
        gm_l5 = t11;
        {
            void * t13 = gm_l0;
            uint32_t t14 = gm_l5;
            uint32_t t15 = gm_f_root_jbs2avl(t13, t14);
            uint64_t t16 = (uint64_t)t15;
            gm_l6 = t16;
        }
        {
            uint64_t t18 = gm_l6;
            uint64_t t19 = (uintptr_t)t18;
            void * t20 = gm_l0;
            uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 0) + 0) + 8);
            if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srclu7allsekqhf, 164, 12);
            uint8_t t22 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 0) + 0)) + t19 * 48) + 4) + 0) == ((uint8_t)0ULL)));
            if (!t22) goto ie17;
            {
                uint8_t t24 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            uint64_t t25 = gm_l6;
            uint64_t t26 = (uintptr_t)t25;
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 0) + 0) + 8);
            if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_srclu7allsekqhf, 165, 9);
            GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 0) + 0)) + t26 * 48) + 5), ((uint8_t)1));
        }
        {
            uint64_t t29 = gm_l6;
            uint64_t t30 = (uintptr_t)t29;
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 0) + 0) + 8);
            if (t30 >= (uintptr_t)(t32)) gm_trap_index(t30, t32, gm_srclu7allsekqhf, 166, 9);
            uint8_t *t33 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 0) + 0)) + t30 * 48) + 7);
            uint64_t t34 = gm_l6;
            uint64_t t35 = (uintptr_t)t34;
            void * t36 = gm_l0;
            uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 0) + 0) + 8);
            if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srclu7allsekqhf, 166, 49);
            gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 0) + 0)) + t35 * 48) + 7));
            gm_t9qkh0rsooc8d t39 = gm_l3;
            gm_t9qkh0rsooc8d t40 = gm_f_fallback_max_vinhf97(t38, t39);
            GM_ST(gm_t9qkh0rsooc8d, t33, t40);
        }
        {
            uint8_t t41 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint8_t t42 = 0;
        void * t43 = gm_l1;
        gm_t88rcucg1b7uk t44 = gm_l4;
        uint8_t t45 = gm_f_is_integer_8d4f9ek(t43, GM_CONV(gm_csguankko4lmhd, t44));
        if (t45) {
            void * t46 = gm_l1;
            gm_t88rcucg1b7uk t47 = gm_l4;
            uint8_t t48 = gm_f_is_signed_integer_s66djm7(t46, GM_CONV(gm_csguankko4lmhd, t47));
            uint8_t t49 = (uint8_t)!t48;
            t42 = t49;
        } else t42 = 0;
        uint8_t t50 = 0;
        F.ret = t42;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_resolve_args_i7jdhok(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_resolve_args_i7jdhok F;
    void * gm_l0; /* t */
    void * gm_l1; /* s */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* x */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_vmvl81b((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_toa2vhe3l4hfr t15 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 40));
                    F.l6 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t18 != ((uint8_t)0ULL)) goto ie17;
                        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                        gm_l7 = t19;
                        {
                            void * t21 = (void *)((uint8_t *)&F.l3);
                            void * t22 = gm_l0;
                            void * t23 = gm_l1;
                            gm_t88rcucg1b7uk t24 = gm_l7;
                            gm_t88rcucg1b7uk t25 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t22, t23, t24);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_toa2vhe3l4hfr t26;
                            memset(&t26, 0, sizeof t26);
                            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t26 + 8), t25);
                            gm_tftb1odc739t5 t27 = gm_f_push_s5vr2a2(t21, &t26);
                            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srclu7allsekqhf, 122, 28);
                            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        }
                        goto ix17;
ie17:;
                        {
                            void * t30 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t31 = gm_f_push_s5vr2a2(t30, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&F.l6)));
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srclu7allsekqhf, 122, 71);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 121, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t34 = gm_l5;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l5 = t37;
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
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effects_j374dhk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tnpifv20irnph a2) {
    struct gm_fr_resolve_effects_j374dhk F;
    void * gm_l0; /* t */
    void * gm_l1; /* s */
    gm_tnpifv20irnph gm_l2; /* set */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_084fcd6((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 24));
                    gm_l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        gm_tiilmfd16v3fc t18;
                        memset((uint8_t *)&t18 + 4, 0, 4);
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t18 + 0, t19);
                        void * t20 = gm_l0;
                        void * t21 = gm_l1;
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                        gm_b16_8 t23 = gm_f_resolve_args_i7jdhok((void *)NULL, cx, NULL, t20, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 8, t23);
                        gm_tftb1odc739t5 t24 = gm_f_push_d1r4h19(t17, &t18);
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srclu7allsekqhf, 129, 28);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 129, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t27 = gm_l5;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l5 = t30;
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
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_tnpifv20irnph t33 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_resolve_sig_ahumdsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_resolve_sig_ahumdsh F;
    void * gm_l0; /* t */
    void * gm_l1; /* s */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* p */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_nof7vfj((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 4));
                    gm_l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        void * t18 = gm_l0;
                        void * t19 = gm_l1;
                        gm_t88rcucg1b7uk t20 = gm_l6;
                        gm_t88rcucg1b7uk t21 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t19, t20);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t22 = gm_f_push_k3l9nic(t17, GM_CONV(gm_csguankko4lmhd, t21));
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srclu7allsekqhf, 135, 27);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 135, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l5;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l5 = t28;
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
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0, 4);
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0, 7);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 16));
        gm_t88rcucg1b7uk t34 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t35 = gm_l0;
        void * t36 = gm_l1;
        gm_tnpifv20irnph t37 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)a2) + 24));
        gm_tnpifv20irnph t38 = gm_f_resolve_effects_j374dhk((void *)NULL, cx, NULL, t35, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a2) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0, t30);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16, t34);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24, t38);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40, t39);
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_resolve_t2kal61 F;
    void * gm_l0; /* t */
    void * gm_l1; /* s */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_t88rcucg1b7uk gm_l3; /* r */
    uint8_t gm_l4; /* c */
    gm_t88rcucg1b7uk gm_l5; /* x */
    uint8_t gm_l6; /* c */
    gm_t88rcucg1b7uk gm_l7; /* x */
    uint8_t gm_l8; /* c */
    gm_t88rcucg1b7uk gm_l9; /* x */
    uint64_t gm_l10; /* len */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_tnnjeerolos0a gm_l12; /* kind */
    gm_t88rcucg1b7uk gm_l13; /* len */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    gm_t88rcucg1b7uk gm_l15; /* x */
    gm_t88rcucg1b7uk gm_l16; /* x */
    gm_t88rcucg1b7uk gm_l17; /* e */
    gm_t88rcucg1b7uk gm_l18; /* o */
    gm_t88rcucg1b7uk gm_l19; /* re */
    gm_t88rcucg1b7uk gm_l20; /* ro */
    gm_b16_8 gm_l21; /* elems */
    gm_b16_8 gm_l22; /* names */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_t88rcucg1b7uk gm_l26; /* x */
    uint32_t gm_l29; /* id */
    gm_b16_8 gm_l30; /* args */
    gm_tnpifv20irnph gm_l31; /* set */
    GM_STACK_CHECK(&F, 2018u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l2;
        uint8_t t6 = gm_f_has_infer_3o82a0b(t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) goto ie2;
        {
            gm_t88rcucg1b7uk t9 = gm_l2;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l1;
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_t88rcucg1b7uk t13 = gm_l2;
        gm_t53at80pfdus3 t14;
        gm_f_kind_jcnt4hm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma20; case ((uint8_t)12ULL): goto ma25; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma28; case ((uint8_t)18ULL): goto ma23; case ((uint8_t)19ULL): goto ma30; case ((uint8_t)20ULL): goto ma21; case ((uint8_t)21ULL): goto ma30; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma16; case ((uint8_t)24ULL): goto ma29; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma30; default: goto ma30; }
ma16:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t34 != ((uint8_t)23ULL)) goto mn33;
        {
            void * t36 = gm_l0;
            void * t37 = gm_l1;
            void * t38 = (void *)(((uint8_t *)t37) + 8);
            gm_t88rcucg1b7uk t39 = gm_l2;
            gm_t88rcucg1b7uk t40 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t36, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t40;
        }
        {
            uint8_t t42 = 0;
            gm_t88rcucg1b7uk t43 = gm_l3;
            gm_t88rcucg1b7uk t44 = gm_l2;
            uint8_t t45 = (gm_eq9m6mvtmgde(((uint8_t *)&t43), ((uint8_t *)&t44)));
            if (!t45) {
                void * t46 = gm_l1;
                void * t47 = (void *)(((uint8_t *)t46) + 8);
                gm_t88rcucg1b7uk t48 = gm_l3;
                uint8_t t49 = gm_f_has_infer_3o82a0b(t47, GM_CONV(gm_csguankko4lmhd, t48));
                uint8_t t50 = (uint8_t)!t49;
                t42 = t50;
            } else t42 = 1;
            if (!t42) goto ie41;
            {
                gm_t88rcucg1b7uk t52 = gm_l3;
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto ix41;
ie41:;
ix41:;
        }
        {
            void * t54 = gm_l0;
            void * t55 = gm_l1;
            gm_t88rcucg1b7uk t56 = gm_l3;
            gm_t88rcucg1b7uk t57 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto me15;
mn33:;
ma17:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t60 != ((uint8_t)8ULL)) goto mn59;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l4 = t61;
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 4));
        gm_l5 = t62;
        void * t63 = gm_l1;
        void * t64 = (void *)(((uint8_t *)t63) + 8);
        uint8_t t65 = gm_l4;
        void * t66 = gm_l0;
        void * t67 = gm_l1;
        gm_t88rcucg1b7uk t68 = gm_l5;
        gm_t88rcucg1b7uk t69 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t66, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t70 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t64, t65, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t71 = 0;
        F.ret = t70;
        goto gm_fret;
        goto me15;
mn59:;
ma18:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t73 != ((uint8_t)9ULL)) goto mn72;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l6 = t74;
        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 4));
        gm_l7 = t75;
        void * t76 = gm_l1;
        void * t77 = (void *)(((uint8_t *)t76) + 8);
        uint8_t t78 = gm_l6;
        void * t79 = gm_l0;
        void * t80 = gm_l1;
        gm_t88rcucg1b7uk t81 = gm_l7;
        gm_t88rcucg1b7uk t82 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t79, t80, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t83 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t77, t78, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t84 = 0;
        F.ret = t83;
        goto gm_fret;
        goto me15;
mn72:;
ma19:;
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t86 != ((uint8_t)10ULL)) goto mn85;
        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l8 = t87;
        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 4));
        gm_l9 = t88;
        void * t89 = gm_l1;
        void * t90 = (void *)(((uint8_t *)t89) + 8);
        uint8_t t91 = gm_l8;
        void * t92 = gm_l0;
        void * t93 = gm_l1;
        gm_t88rcucg1b7uk t94 = gm_l9;
        gm_t88rcucg1b7uk t95 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t92, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t96 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t90, t91, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t97 = 0;
        F.ret = t96;
        goto gm_fret;
        goto me15;
mn85:;
ma20:;
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t99 != ((uint8_t)11ULL)) goto mn98;
        uint64_t t100 = GM_LD(uint64_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l10 = t100;
        gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 8));
        gm_l11 = t101;
        gm_tnnjeerolos0a t102 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t14) + 8) + 12));
        gm_l12 = t102;
        void * t103 = gm_l1;
        void * t104 = (void *)(((uint8_t *)t103) + 8);
        gm_t8nckeqfenl8a t105;
        memset((uint8_t *)&t105 + 20, 0, 4);
        uint64_t t106 = gm_l10;
        GM_ST(uint64_t, (uint8_t *)&t105 + 0, t106);
        void * t107 = gm_l0;
        void * t108 = gm_l1;
        gm_t88rcucg1b7uk t109 = gm_l11;
        gm_t88rcucg1b7uk t110 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t107, t108, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t105 + 8, t110);
        gm_tnnjeerolos0a t111 = gm_l12;
        GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t105 + 12, t111);
        gm_t53at80pfdus3 t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t112 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t112 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t112 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)11ULL));
        GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t112 + 8), t105);
        gm_t88rcucg1b7uk t113 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t104, &t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t114 = 0;
        F.ret = t113;
        goto gm_fret;
        goto me15;
mn98:;
ma21:;
        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t116 != ((uint8_t)20ULL)) goto mn115;
        gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 0));
        gm_l13 = t117;
        gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 4));
        gm_l14 = t118;
        void * t119 = gm_l1;
        void * t120 = (void *)(((uint8_t *)t119) + 8);
        gm_t88rcucg1b7uk t121 = gm_l13;
        void * t122 = gm_l0;
        void * t123 = gm_l1;
        gm_t88rcucg1b7uk t124 = gm_l14;
        gm_t88rcucg1b7uk t125 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t122, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgglga8ro3isc t126;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t126 + 0), t121);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t126 + 4), t125);
        gm_t53at80pfdus3 t127;
        memset(&t127, 0, sizeof t127);
        GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t127 + 8), t126);
        gm_t88rcucg1b7uk t128 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t120, &t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t129 = 0;
        F.ret = t128;
        goto gm_fret;
        goto me15;
mn115:;
ma22:;
        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t131 != ((uint8_t)13ULL)) goto mn130;
        gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t14) + 8));
        gm_l15 = t132;
        void * t133 = gm_l1;
        void * t134 = (void *)(((uint8_t *)t133) + 8);
        void * t135 = gm_l0;
        void * t136 = gm_l1;
        gm_t88rcucg1b7uk t137 = gm_l15;
        gm_t88rcucg1b7uk t138 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t135, t136, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t139 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t134, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t140 = 0;
        F.ret = t139;
        goto gm_fret;
        goto me15;
mn130:;
ma23:;
        uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t142 != ((uint8_t)18ULL)) goto mn141;
        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t14) + 8));
        gm_l16 = t143;
        void * t144 = gm_l1;
        void * t145 = (void *)(((uint8_t *)t144) + 8);
        void * t146 = gm_l0;
        void * t147 = gm_l1;
        gm_t88rcucg1b7uk t148 = gm_l16;
        gm_t88rcucg1b7uk t149 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t146, t147, t148);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t150;
        memset(&t150, 0, sizeof t150);
        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t150 + 8), t149);
        gm_t88rcucg1b7uk t151 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t145, &t150);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t152 = 0;
        F.ret = t151;
        goto gm_fret;
        goto me15;
mn141:;
ma24:;
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t154 != ((uint8_t)14ULL)) goto mn153;
        gm_t88rcucg1b7uk t155 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 0));
        gm_l17 = t155;
        gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 4));
        gm_l18 = t156;
        {
            void * t158 = gm_l0;
            void * t159 = gm_l1;
            gm_t88rcucg1b7uk t160 = gm_l17;
            gm_t88rcucg1b7uk t161 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t158, t159, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t161;
        }
        {
            void * t162 = gm_l0;
            void * t163 = gm_l1;
            gm_t88rcucg1b7uk t164 = gm_l18;
            gm_t88rcucg1b7uk t165 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t162, t163, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t165;
        }
        {
            void * t166 = gm_l1;
            void * t167 = (void *)(((uint8_t *)t166) + 8);
            gm_t88rcucg1b7uk t168 = gm_l19;
            gm_t88rcucg1b7uk t169 = gm_l20;
            gm_t88rcucg1b7uk t170 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t167, t168, t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t171 = 0;
            F.ret = t170;
            goto gm_fret;
        }
        goto me15;
mn153:;
ma25:;
        uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t173 != ((uint8_t)12ULL)) goto mn172;
        gm_b16_8 t174 = GM_LD(gm_b16_8, ((((uint8_t *)&t14) + 8) + 0));
        gm_l21 = t174;
        gm_b16_8 t175 = GM_LD(gm_b16_8, ((((uint8_t *)&t14) + 8) + 16));
        gm_l22 = t175;
        {
            gm_f_list_nof7vfj((void *)&F.l23, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t178 = gm_l21;
                gm_l24 = t178;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t180 = gm_l25;
                    uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t182 = (t180 < t181);
                    if (!t182) goto lb179;
                    {
                        uint64_t t185 = gm_l25;
                        uint64_t t186 = (uintptr_t)t185;
                        uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t186 * 4));
                        gm_l26 = t190;
                        {
                            void * t192 = (void *)((uint8_t *)&F.l23);
                            void * t193 = gm_l0;
                            void * t194 = gm_l1;
                            gm_t88rcucg1b7uk t195 = gm_l26;
                            gm_t88rcucg1b7uk t196 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t193, t194, t195);
                            if (gm_unw.kind) {
                                goto gm_ll183;
                            }
                            gm_tftb1odc739t5 t197 = gm_f_push_k3l9nic(t192, GM_CONV(gm_csguankko4lmhd, t196));
                            if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srclu7allsekqhf, 105, 30);
                            gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
                        }
                        goto me188;
mn189:;
                        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 105, 13);
me188:;
                    }
lc179:;
                    {
                        uint64_t t200 = gm_l25;
                        uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                        uint64_t t203 = r201;
                        gm_l25 = t203;
                    }
                }
gm_ll183:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb179; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc179; }
                }
                goto gm_lad;
lb179:;
            }
        }
        {
            void * t204 = gm_l1;
            void * t205 = (void *)(((uint8_t *)t204) + 8);
            gm_t6j6smj7qoctu t206;
            gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
            gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 0, t208);
            gm_b16_8 t209 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t206 + 16, t209);
            gm_t53at80pfdus3 t210;
            GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t210 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t210 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t210 + 8), t206);
            gm_t88rcucg1b7uk t211 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t205, &t210);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t212 = 0;
            F.ret = t211;
            goto gm_fret;
        }
        goto me15;
mn172:;
ma26:;
        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t214 != ((uint8_t)15ULL)) goto mn213;
        F.l27 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t14) + 8));
        void * t215 = gm_l1;
        void * t216 = (void *)(((uint8_t *)t215) + 8);
        void * t217 = gm_l0;
        void * t218 = gm_l1;
        gm_tddtjfggdat0d t219;
        gm_f_resolve_sig_ahumdsh((void *)&t219, (void *)NULL, cx, NULL, t217, t218, &(*(gm_tddtjfggdat0d *)((uint8_t *)&F.l27)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t220;
        GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t220 + 8), t219);
        gm_t88rcucg1b7uk t221 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t216, &t220);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t222 = 0;
        F.ret = t221;
        goto gm_fret;
        goto me15;
mn213:;
ma27:;
        uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t224 != ((uint8_t)16ULL)) goto mn223;
        F.l28 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t14) + 8));
        void * t225 = gm_l1;
        void * t226 = (void *)(((uint8_t *)t225) + 8);
        void * t227 = gm_l0;
        void * t228 = gm_l1;
        gm_tddtjfggdat0d t229;
        gm_f_resolve_sig_ahumdsh((void *)&t229, (void *)NULL, cx, NULL, t227, t228, &(*(gm_tddtjfggdat0d *)((uint8_t *)&F.l28)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t230;
        GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)16ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t230 + 8), t229);
        gm_t88rcucg1b7uk t231 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t226, &t230);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t232 = 0;
        F.ret = t231;
        goto gm_fret;
        goto me15;
mn223:;
ma28:;
        uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t234 != ((uint8_t)17ULL)) goto mn233;
        uint32_t t235 = GM_LD(uint32_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l29 = t235;
        gm_b16_8 t236 = GM_LD(gm_b16_8, ((((uint8_t *)&t14) + 8) + 8));
        gm_l30 = t236;
        void * t237 = gm_l1;
        void * t238 = (void *)(((uint8_t *)t237) + 8);
        gm_tiilmfd16v3fc t239;
        memset((uint8_t *)&t239 + 4, 0, 4);
        uint32_t t240 = gm_l29;
        GM_ST(uint32_t, (uint8_t *)&t239 + 0, t240);
        void * t241 = gm_l0;
        void * t242 = gm_l1;
        gm_b16_8 t243 = gm_l30;
        gm_b16_8 t244 = gm_f_resolve_args_i7jdhok((void *)NULL, cx, NULL, t241, t242, t243);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t239 + 8, t244);
        gm_t53at80pfdus3 t245;
        GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)17ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t245 + 8), t239);
        gm_t88rcucg1b7uk t246 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t238, &t245);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t247 = 0;
        F.ret = t246;
        goto gm_fret;
        goto me15;
mn233:;
ma29:;
        uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t249 != ((uint8_t)24ULL)) goto mn248;
        gm_tnpifv20irnph t250 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t14) + 8));
        gm_l31 = t250;
        void * t251 = gm_l1;
        void * t252 = (void *)(((uint8_t *)t251) + 8);
        void * t253 = gm_l0;
        void * t254 = gm_l1;
        gm_tnpifv20irnph t255 = gm_l31;
        gm_tnpifv20irnph t256 = gm_f_resolve_effects_j374dhk((void *)NULL, cx, NULL, t253, t254, t255);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t257;
        memset(&t257, 0, sizeof t257);
        GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)24ULL));
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t257 + 8), t256);
        gm_t88rcucg1b7uk t258 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t252, &t257);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t259 = 0;
        F.ret = t258;
        goto gm_fret;
        goto me15;
mn248:;
ma30:;
        {
            void * t262 = gm_l1;
            gm_tl874t2atnlh3 t263 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t264;
            GM_ST(void *, (uint8_t *)&t264, (void *)(gm_sq5rlbq4gbs5v));
            GM_ST(uintptr_t, (uint8_t *)&t264 + 8, (uintptr_t)(26));
            gm_b0_1 t265 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t262, t263, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t88rcucg1b7uk t266 = gm_l2;
            uint8_t t267 = 0;
            F.ret = t266;
            goto gm_fret;
        }
        goto me15;
mn260:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 85, 5);
me15:;
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
inline __attribute__((aligned(4))) uint32_t gm_f_root_jbs2avl(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_root_jbs2avl F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* v */
    uint32_t gm_l2; /* p */
    uint32_t gm_l3; /* r */
    GM_STACK_CHECK(&F, 143u);
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
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 0) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srclu7allsekqhf, 55, 10);
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 0) + 0)) + t4 * 48) + 0));
        gm_l2 = t7;
    }
    {
        uint32_t t9 = gm_l2;
        uint32_t t10 = gm_l1;
        uint8_t t11 = ((t9 == t10));
        if (!t11) goto ie8;
        {
            uint32_t t13 = gm_l1;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t15 = gm_l0;
        uint32_t t16 = gm_l2;
        uint32_t t17 = gm_f_root_jbs2avl(t15, t16);
        gm_l3 = t17;
    }
    {
        uint32_t t18 = gm_l1;
        uint64_t t19 = (uint64_t)t18;
        uint64_t t20 = (uintptr_t)t19;
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 0) + 0) + 8);
        if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_srclu7allsekqhf, 58, 5);
        uint32_t t23 = gm_l3;
        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 0) + 0)) + t20 * 48) + 0), t23);
    }
    {
        uint32_t t24 = gm_l3;
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_set_fallback_s66n99d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3) {
    struct gm_fr_set_fallback_s66n99d F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_t9qkh0rsooc8d gm_l3; /* fallback */
    uint32_t gm_l4; /* v */
    uint64_t gm_l5; /* r */
    gm_t88rcucg1b7uk gm_l6; /* x */
    gm_tl874t2atnlh3 gm_l7; /* at */
    uint32_t gm_l8; /* w */
    uint64_t gm_l9; /* r */
    GM_STACK_CHECK(&F, 664u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)23ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l4 = t7;
        {
            void * t9 = gm_l0;
            uint32_t t10 = gm_l4;
            uint32_t t11 = gm_f_root_jbs2avl(t9, t10);
            uint64_t t12 = (uint64_t)t11;
            gm_l5 = t12;
        }
        {
            uint64_t t14 = gm_l5;
            uint64_t t15 = (uintptr_t)t14;
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 0) + 0) + 8);
            if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_srclu7allsekqhf, 183, 13);
            if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 0) + 0)) + t15 * 48) + 8))[4] != 0)) goto ie13;
            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 0) + 0)) + t15 * 48) + 8));
            gm_l6 = t18;
            uint64_t t19 = gm_l5;
            uint64_t t20 = (uintptr_t)t19;
            void * t21 = gm_l0;
            uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 0) + 0) + 8);
            if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_srclu7allsekqhf, 183, 48);
            if (!(((((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 0) + 0)) + t20 * 48) + 16))[12] != 0)) goto ie13;
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 0) + 0)) + t20 * 48) + 16));
            gm_l7 = t23;
            {
                uint8_t t26 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)1ULL)));
                if (!t26) goto ie25;
                gm_t88rcucg1b7uk t27 = gm_l6;
                void * t28 = gm_l1;
                gm_t88rcucg1b7uk t29 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t30 = (!gm_eq9m6mvtmgde(((uint8_t *)&t27), ((uint8_t *)&t29)));
                if (!t30) goto ie25;
                void * t31 = gm_l1;
                gm_t88rcucg1b7uk t32 = gm_l6;
                uint8_t t33 = gm_f_is_integer_8d4f9ek(t31, GM_CONV(gm_csguankko4lmhd, t32));
                if (!t33) goto ie25;
                {
                    gm_t88rcucg1b7uk t35 = gm_l6;
                    gm_tl874t2atnlh3 t36 = gm_l7;
                    gm_t2i8ne5ibd7v4 t37;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37 + 0), t35);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t37 + 4), t36);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t8uadrbfpr7kk *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t8uadrbfpr7kk *)gm_out) + 18, 0);
                    ((uint8_t *)&(*(gm_t8uadrbfpr7kk *)gm_out))[16] = 1;
                    GM_ST(gm_t2i8ne5ibd7v4, (uint8_t *)&(*(gm_t8uadrbfpr7kk *)gm_out) + 0, t37);
                    goto gm_fdone;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                gm_t8uadrbfpr7kk t38;
                memset(&t38, 0, sizeof t38);
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            uint64_t t41 = gm_l5;
            uint64_t t42 = (uintptr_t)t41;
            void * t43 = gm_l0;
            uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 0) + 0) + 8);
            if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_srclu7allsekqhf, 187, 8);
            uint8_t t45;
            if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t43) + 0) + 0)) + t42 * 48) + 8))[4] != 0)) goto mn47;
            t45 = ((uint8_t)1);
            goto me46;
mn47:;
            t45 = ((uint8_t)0);
            goto me46;
mn48:;
            gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 187, 8);
me46:;
            if (!t45) goto ie40;
            {
                uint64_t t50 = gm_l5;
                uint64_t t51 = (uintptr_t)t50;
                void * t52 = gm_l0;
                uint64_t t53 = GM_LD(uintptr_t, ((((uint8_t *)t52) + 0) + 0) + 8);
                if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_srclu7allsekqhf, 188, 9);
                uint8_t *t54 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t52) + 0) + 0)) + t51 * 48) + 7);
                uint64_t t55 = gm_l5;
                uint64_t t56 = (uintptr_t)t55;
                void * t57 = gm_l0;
                uint64_t t58 = GM_LD(uintptr_t, ((((uint8_t *)t57) + 0) + 0) + 8);
                if (t56 >= (uintptr_t)(t58)) gm_trap_index(t56, t58, gm_srclu7allsekqhf, 188, 49);
                gm_t9qkh0rsooc8d t59 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t57) + 0) + 0)) + t56 * 48) + 7));
                gm_t9qkh0rsooc8d t60 = gm_l3;
                gm_t9qkh0rsooc8d t61 = gm_f_fallback_max_vinhf97(t59, t60);
                GM_ST(gm_t9qkh0rsooc8d, t54, t61);
            }
            goto ix40;
ie40:;
ix40:;
        }
        {
            gm_t8uadrbfpr7kk t62;
            memset(&t62, 0, sizeof t62);
            uint8_t t63 = 0;
            F.ret = t62;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            {
                void * t67 = gm_l1;
                void * t68 = gm_l0;
                void * t69 = gm_l1;
                gm_t88rcucg1b7uk t70 = gm_l2;
                gm_t88rcucg1b7uk t71 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t68, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t53at80pfdus3 t72;
                gm_f_kind_jcnt4hm((void *)&t72, t67, GM_CONV(gm_csguankko4lmhd, t71));
                uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t72) + 0));
                if (t73 != ((uint8_t)23ULL)) goto ie66;
                uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&t72) + 8));
                gm_l8 = t74;
                {
                    void * t76 = gm_l0;
                    uint32_t t77 = gm_l8;
                    uint32_t t78 = gm_f_root_jbs2avl(t76, t77);
                    uint64_t t79 = (uint64_t)t78;
                    gm_l9 = t79;
                }
                {
                    uint64_t t80 = gm_l9;
                    uint64_t t81 = (uintptr_t)t80;
                    void * t82 = gm_l0;
                    uint64_t t83 = GM_LD(uintptr_t, ((((uint8_t *)t82) + 0) + 0) + 8);
                    if (t81 >= (uintptr_t)(t83)) gm_trap_index(t81, t83, gm_srclu7allsekqhf, 178, 13);
                    uint8_t *t84 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t82) + 0) + 0)) + t81 * 48) + 7);
                    uint64_t t85 = gm_l9;
                    uint64_t t86 = (uintptr_t)t85;
                    void * t87 = gm_l0;
                    uint64_t t88 = GM_LD(uintptr_t, ((((uint8_t *)t87) + 0) + 0) + 8);
                    if (t86 >= (uintptr_t)(t88)) gm_trap_index(t86, t88, gm_srclu7allsekqhf, 178, 53);
                    gm_t9qkh0rsooc8d t89 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t87) + 0) + 0)) + t86 * 48) + 7));
                    gm_t9qkh0rsooc8d t90 = gm_l3;
                    gm_t9qkh0rsooc8d t91 = gm_f_fallback_max_vinhf97(t89, t90);
                    GM_ST(gm_t9qkh0rsooc8d, t84, t91);
                }
                goto ix66;
ie66:;
ix66:;
            }
            {
                gm_t8uadrbfpr7kk t92;
                memset(&t92, 0, sizeof t92);
                uint8_t t93 = 0;
                F.ret = t92;
                goto gm_fret;
            }
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_vars_vpp9ov6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_settle_vars_vpp9ov6 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_t88rcucg1b7uk gm_l4; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* v */
    uint64_t gm_l9; /* root_ */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = F.l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = F.l1;
        gm_t88rcucg1b7uk t7 = gm_l4;
        gm_closure t8 = ((gm_closure){(void *)gm_f_fn_4b1mi3j, (void *)&F.h, cx});
        gm_f_walk_v858co7(t6, GM_CONV(gm_csguankko4lmhd, t7), t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l6 = t10;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l7;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l7;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t18 * 4));
                    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&t20));
                    gm_l8 = t23;
                    {
                        void * t25 = gm_l0;
                        uint32_t t26 = gm_l8;
                        uint32_t t27 = gm_f_root_jbs2avl(t25, t26);
                        uint64_t t28 = (uint64_t)t27;
                        gm_l9 = t28;
                    }
                    {
                        uint64_t t30 = gm_l9;
                        uint64_t t31 = (uintptr_t)t30;
                        void * t32 = gm_l0;
                        uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 0) + 0) + 8);
                        if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_srclu7allsekqhf, 299, 12);
                        uint8_t t34;
                        if ((((((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 0) + 0)) + t31 * 48) + 8))[4] != 0)) goto mn36;
                        t34 = ((uint8_t)1);
                        goto me35;
mn36:;
                        t34 = ((uint8_t)0);
                        goto me35;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 299, 12);
me35:;
                        if (!t34) goto ie29;
                        {
                            uint64_t t39 = gm_l9;
                            uint64_t t40 = (uintptr_t)t39;
                            void * t41 = gm_l0;
                            uint64_t t42 = GM_LD(uintptr_t, ((((uint8_t *)t41) + 0) + 0) + 8);
                            if (t40 >= (uintptr_t)(t42)) gm_trap_index(t40, t42, gm_srclu7allsekqhf, 300, 13);
                            uint8_t *t43 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t41) + 0) + 0)) + t40 * 48) + 8);
                            void * t44 = gm_l0;
                            void * t45 = F.l1;
                            void * t46 = (void *)(((uint8_t *)t45) + 8);
                            uint64_t t47 = gm_l9;
                            uint64_t t48 = (uintptr_t)t47;
                            void * t49 = gm_l0;
                            uint64_t t50 = GM_LD(uintptr_t, ((((uint8_t *)t49) + 0) + 0) + 8);
                            if (t48 >= (uintptr_t)(t50)) gm_trap_index(t48, t50, gm_srclu7allsekqhf, 300, 70);
                            void * t51 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t49) + 0) + 0)) + t48 * 48);
                            gm_t88rcucg1b7uk t52 = gm_f_fallback_type_8qqovjs((void *)NULL, cx, NULL, t44, t46, t51);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            gm_tkn42u7na3t81 t53;
                            GM_ST(uint8_t, (uint8_t *)&t53 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t53 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t53), t52);
                            ((uint8_t *)&t53)[4] = 1;
                            GM_ST(gm_tkn42u7na3t81, t43, t53);
                        }
                        {
                            uint64_t t54 = gm_l9;
                            uint64_t t55 = (uintptr_t)t54;
                            void * t56 = gm_l0;
                            uint64_t t57 = GM_LD(uintptr_t, ((((uint8_t *)t56) + 0) + 0) + 8);
                            if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_srclu7allsekqhf, 301, 13);
                            gm_tl874t2atnlh3 t58 = gm_l3;
                            gm_toceffb697p0u t59;
                            GM_ST(uint8_t, (uint8_t *)&t59 + 13, 0);
                            GM_ST(uint16_t, (uint8_t *)&t59 + 14, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t59), t58);
                            ((uint8_t *)&t59)[12] = 1;
                            GM_ST(gm_toceffb697p0u, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t56) + 0) + 0)) + t55 * 48) + 16), t59);
                        }
                        goto ix29;
ie29:;
ix29:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 297, 5);
me21:;
                }
lc11:;
                {
                    uint64_t t61 = gm_l7;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l7 = t64;
                }
            }
gm_ll15:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
            }
            goto gm_lad;
lb11:;
        }
    }
    {
        void * t65 = gm_l0;
        void * t66 = F.l1;
        gm_t88rcucg1b7uk t67 = gm_l2;
        gm_t88rcucg1b7uk t68 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t65, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_shallow_8ur803a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_shallow_8ur803a F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l3; /* v */
    uint32_t gm_l4; /* r */
    GM_STACK_CHECK(&F, 255u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)23ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l3 = t7;
        {
            void * t9 = gm_l0;
            uint32_t t10 = gm_l3;
            uint32_t t11 = gm_f_root_jbs2avl(t9, t10);
            gm_l4 = t11;
        }
        {
            uint32_t t12 = gm_l4;
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 0) + 0) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srclu7allsekqhf, 79, 12);
            gm_tkn42u7na3t81 t17 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 0) + 0)) + t14 * 48) + 8));
            gm_t88rcucg1b7uk t18;
            if (((((uint8_t *)&t17))[4] != 0)) {
                gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t17) + 0));
                t18 = t19;
            } else {
                void * t20 = gm_l1;
                uint32_t t21 = gm_l4;
                gm_t53at80pfdus3 t22;
                memset(&t22, 0, sizeof t22);
                GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)23ULL));
                GM_ST(uint32_t, ((uint8_t *)&t22 + 8), t21);
                gm_t88rcucg1b7uk t23 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t20, &t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t18 = t23;
            }
            uint8_t t24 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t88rcucg1b7uk t26 = gm_l2;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
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
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_structural_a96lmna(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_unify_structural_a96lmna F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* a */
    gm_t88rcucg1b7uk gm_l3; /* b */
    uint8_t gm_l4; /* c1 */
    gm_t88rcucg1b7uk gm_l5; /* p1 */
    uint8_t gm_l6; /* c2 */
    gm_t88rcucg1b7uk gm_l7; /* p2 */
    uint8_t gm_l8; /* c1 */
    gm_t88rcucg1b7uk gm_l9; /* e1 */
    uint8_t gm_l10; /* c2 */
    gm_t88rcucg1b7uk gm_l11; /* e2 */
    uint8_t gm_l12; /* c1 */
    gm_t88rcucg1b7uk gm_l13; /* e1 */
    uint8_t gm_l14; /* c2 */
    gm_t88rcucg1b7uk gm_l15; /* e2 */
    uint64_t gm_l16; /* l1 */
    gm_t88rcucg1b7uk gm_l17; /* e1 */
    gm_tnnjeerolos0a gm_l18; /* k1 */
    uint64_t gm_l19; /* l2 */
    gm_t88rcucg1b7uk gm_l20; /* e2 */
    gm_tnnjeerolos0a gm_l21; /* k2 */
    gm_t88rcucg1b7uk gm_l22; /* l1 */
    gm_t88rcucg1b7uk gm_l23; /* e1 */
    gm_t88rcucg1b7uk gm_l24; /* l2 */
    gm_t88rcucg1b7uk gm_l25; /* e2 */
    gm_t88rcucg1b7uk gm_l26; /* x */
    gm_t88rcucg1b7uk gm_l27; /* y */
    gm_t88rcucg1b7uk gm_l28; /* x */
    gm_t88rcucg1b7uk gm_l29; /* y */
    gm_t88rcucg1b7uk gm_l30; /* e1 */
    gm_t88rcucg1b7uk gm_l31; /* o1 */
    gm_t88rcucg1b7uk gm_l32; /* e2 */
    gm_t88rcucg1b7uk gm_l33; /* o2 */
    gm_b16_8 gm_l34; /* x */
    gm_b16_8 gm_l35; /* n1 */
    gm_b16_8 gm_l36; /* y */
    gm_b16_8 gm_l37; /* n2 */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* i */
    GM_STACK_CHECK(&F, 1584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t53at80pfdus3 t4;
        gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; default: goto ma15; }
ma6:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t19 != ((uint8_t)8ULL)) goto mn18;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l4 = t20;
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l5 = t21;
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = gm_l3;
        gm_t53at80pfdus3 t25;
        gm_f_kind_jcnt4hm((void *)&t25, t23, GM_CONV(gm_csguankko4lmhd, t24));
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t26 != ((uint8_t)8ULL)) goto ie22;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)&t25) + 8) + 0));
        gm_l6 = t27;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t25) + 8) + 4));
        gm_l7 = t28;
        uint8_t t29 = gm_l4;
        uint8_t t30 = gm_l6;
        uint8_t t31 = ((t29 == t30));
        if (!t31) goto ie22;
        {
            void * t33 = gm_l0;
            void * t34 = gm_l1;
            gm_t88rcucg1b7uk t35 = gm_l5;
            gm_t88rcucg1b7uk t36 = gm_l7;
            gm_t9qkh0rsooc8d t37 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t33, t34, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
            F.ret = t37;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
        goto me5;
mn18:;
ma7:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t40 != ((uint8_t)9ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l8 = t41;
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l9 = t42;
        void * t44 = gm_l1;
        gm_t88rcucg1b7uk t45 = gm_l3;
        gm_t53at80pfdus3 t46;
        gm_f_kind_jcnt4hm((void *)&t46, t44, GM_CONV(gm_csguankko4lmhd, t45));
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t46) + 0));
        if (t47 != ((uint8_t)9ULL)) goto ie43;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)&t46) + 8) + 0));
        gm_l10 = t48;
        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t46) + 8) + 4));
        gm_l11 = t49;
        uint8_t t50 = gm_l8;
        uint8_t t51 = gm_l10;
        uint8_t t52 = ((t50 == t51));
        if (!t52) goto ie43;
        {
            void * t54 = gm_l0;
            void * t55 = gm_l1;
            gm_t88rcucg1b7uk t56 = gm_l9;
            gm_t88rcucg1b7uk t57 = gm_l11;
            gm_t9qkh0rsooc8d t58 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t54, t55, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
            F.ret = t58;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
        goto me5;
mn39:;
ma8:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t61 != ((uint8_t)10ULL)) goto mn60;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l12 = t62;
        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l13 = t63;
        void * t65 = gm_l1;
        gm_t88rcucg1b7uk t66 = gm_l3;
        gm_t53at80pfdus3 t67;
        gm_f_kind_jcnt4hm((void *)&t67, t65, GM_CONV(gm_csguankko4lmhd, t66));
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
        if (t68 != ((uint8_t)10ULL)) goto ie64;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)&t67) + 8) + 0));
        gm_l14 = t69;
        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t67) + 8) + 4));
        gm_l15 = t70;
        uint8_t t71 = gm_l12;
        uint8_t t72 = gm_l14;
        uint8_t t73 = ((t71 == t72));
        if (!t73) goto ie64;
        {
            void * t75 = gm_l0;
            void * t76 = gm_l1;
            gm_t88rcucg1b7uk t77 = gm_l13;
            gm_t88rcucg1b7uk t78 = gm_l15;
            gm_t9qkh0rsooc8d t79 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t75, t76, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
            F.ret = t79;
            goto gm_fret;
        }
        goto ix64;
ie64:;
ix64:;
        goto me5;
mn60:;
ma9:;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t82 != ((uint8_t)11ULL)) goto mn81;
        uint64_t t83 = GM_LD(uint64_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l16 = t83;
        gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 8));
        gm_l17 = t84;
        gm_tnnjeerolos0a t85 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t4) + 8) + 12));
        gm_l18 = t85;
        void * t87 = gm_l1;
        gm_t88rcucg1b7uk t88 = gm_l3;
        gm_t53at80pfdus3 t89;
        gm_f_kind_jcnt4hm((void *)&t89, t87, GM_CONV(gm_csguankko4lmhd, t88));
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t89) + 0));
        if (t90 != ((uint8_t)11ULL)) goto ie86;
        uint64_t t91 = GM_LD(uint64_t, ((((uint8_t *)&t89) + 8) + 0));
        gm_l19 = t91;
        gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t89) + 8) + 8));
        gm_l20 = t92;
        gm_tnnjeerolos0a t93 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t89) + 8) + 12));
        gm_l21 = t93;
        uint64_t t94 = gm_l16;
        uint64_t t95 = gm_l19;
        uint8_t t96 = ((t94 == t95));
        if (!t96) goto ie86;
        gm_tnnjeerolos0a t97 = gm_l18;
        gm_tnnjeerolos0a t98 = gm_l21;
        uint8_t t99 = (gm_eqb2ul45tidi(((uint8_t *)&t97), ((uint8_t *)&t98)));
        if (!t99) goto ie86;
        {
            void * t101 = gm_l0;
            void * t102 = gm_l1;
            gm_t88rcucg1b7uk t103 = gm_l17;
            gm_t88rcucg1b7uk t104 = gm_l20;
            gm_t9qkh0rsooc8d t105 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t101, t102, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t106 = ((((uint8_t *)&t105))[0] != 0);
            F.ret = t105;
            goto gm_fret;
        }
        goto ix86;
ie86:;
ix86:;
        goto me5;
mn81:;
ma10:;
        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t108 != ((uint8_t)20ULL)) goto mn107;
        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 0));
        gm_l22 = t109;
        gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l23 = t110;
        void * t112 = gm_l1;
        gm_t88rcucg1b7uk t113 = gm_l3;
        gm_t53at80pfdus3 t114;
        gm_f_kind_jcnt4hm((void *)&t114, t112, GM_CONV(gm_csguankko4lmhd, t113));
        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t114) + 0));
        if (t115 != ((uint8_t)20ULL)) goto ie111;
        gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t114) + 8) + 0));
        gm_l24 = t116;
        gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t114) + 8) + 4));
        gm_l25 = t117;
        gm_t88rcucg1b7uk t118 = gm_l22;
        gm_t88rcucg1b7uk t119 = gm_l24;
        uint8_t t120 = (gm_eq9m6mvtmgde(((uint8_t *)&t118), ((uint8_t *)&t119)));
        if (!t120) goto ie111;
        {
            void * t122 = gm_l0;
            void * t123 = gm_l1;
            gm_t88rcucg1b7uk t124 = gm_l23;
            gm_t88rcucg1b7uk t125 = gm_l25;
            gm_t9qkh0rsooc8d t126 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t122, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t127 = ((((uint8_t *)&t126))[0] != 0);
            F.ret = t126;
            goto gm_fret;
        }
        goto ix111;
ie111:;
ix111:;
        goto me5;
mn107:;
ma11:;
        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t129 != ((uint8_t)13ULL)) goto mn128;
        gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t4) + 8));
        gm_l26 = t130;
        void * t132 = gm_l1;
        gm_t88rcucg1b7uk t133 = gm_l3;
        gm_t53at80pfdus3 t134;
        gm_f_kind_jcnt4hm((void *)&t134, t132, GM_CONV(gm_csguankko4lmhd, t133));
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&t134) + 0));
        if (t135 != ((uint8_t)13ULL)) goto ie131;
        gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t134) + 8));
        gm_l27 = t136;
        {
            void * t138 = gm_l0;
            void * t139 = gm_l1;
            gm_t88rcucg1b7uk t140 = gm_l26;
            gm_t88rcucg1b7uk t141 = gm_l27;
            gm_t9qkh0rsooc8d t142 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t138, t139, t140, t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t143 = ((((uint8_t *)&t142))[0] != 0);
            F.ret = t142;
            goto gm_fret;
        }
        goto ix131;
ie131:;
ix131:;
        goto me5;
mn128:;
ma12:;
        uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t145 != ((uint8_t)18ULL)) goto mn144;
        gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t4) + 8));
        gm_l28 = t146;
        void * t148 = gm_l1;
        gm_t88rcucg1b7uk t149 = gm_l3;
        gm_t53at80pfdus3 t150;
        gm_f_kind_jcnt4hm((void *)&t150, t148, GM_CONV(gm_csguankko4lmhd, t149));
        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&t150) + 0));
        if (t151 != ((uint8_t)18ULL)) goto ie147;
        gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t150) + 8));
        gm_l29 = t152;
        {
            void * t154 = gm_l0;
            void * t155 = gm_l1;
            gm_t88rcucg1b7uk t156 = gm_l28;
            gm_t88rcucg1b7uk t157 = gm_l29;
            gm_t9qkh0rsooc8d t158 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t154, t155, t156, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t159 = ((((uint8_t *)&t158))[0] != 0);
            F.ret = t158;
            goto gm_fret;
        }
        goto ix147;
ie147:;
ix147:;
        goto me5;
mn144:;
ma13:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t161 != ((uint8_t)14ULL)) goto mn160;
        gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 0));
        gm_l30 = t162;
        gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l31 = t163;
        void * t165 = gm_l1;
        gm_t88rcucg1b7uk t166 = gm_l3;
        gm_t53at80pfdus3 t167;
        gm_f_kind_jcnt4hm((void *)&t167, t165, GM_CONV(gm_csguankko4lmhd, t166));
        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&t167) + 0));
        if (t168 != ((uint8_t)14ULL)) goto ie164;
        gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t167) + 8) + 0));
        gm_l32 = t169;
        gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t167) + 8) + 4));
        gm_l33 = t170;
        {
            void * t172 = gm_l0;
            void * t173 = gm_l1;
            gm_t88rcucg1b7uk t174 = gm_l30;
            gm_t88rcucg1b7uk t175 = gm_l32;
            gm_t9qkh0rsooc8d t176 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t172, t173, t174, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t176))[0] != 0)) {
                gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
                gm_t9qkh0rsooc8d t178;
                ((uint8_t *)&t178)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t178 + 1), t177);
                F.ret = t178;
                goto gm_fret;
            }
            gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
        }
        {
            void * t180 = gm_l0;
            void * t181 = gm_l1;
            gm_t88rcucg1b7uk t182 = gm_l31;
            gm_t88rcucg1b7uk t183 = gm_l33;
            gm_t9qkh0rsooc8d t184 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t180, t181, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t185 = ((((uint8_t *)&t184))[0] != 0);
            F.ret = t184;
            goto gm_fret;
        }
        goto ix164;
ie164:;
ix164:;
        goto me5;
mn160:;
ma14:;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t187 != ((uint8_t)12ULL)) goto mn186;
        gm_b16_8 t188 = GM_LD(gm_b16_8, ((((uint8_t *)&t4) + 8) + 0));
        gm_l34 = t188;
        gm_b16_8 t189 = GM_LD(gm_b16_8, ((((uint8_t *)&t4) + 8) + 16));
        gm_l35 = t189;
        void * t191 = gm_l1;
        gm_t88rcucg1b7uk t192 = gm_l3;
        gm_t53at80pfdus3 t193;
        gm_f_kind_jcnt4hm((void *)&t193, t191, GM_CONV(gm_csguankko4lmhd, t192));
        uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&t193) + 0));
        if (t194 != ((uint8_t)12ULL)) goto ie190;
        gm_b16_8 t195 = GM_LD(gm_b16_8, ((((uint8_t *)&t193) + 8) + 0));
        gm_l36 = t195;
        gm_b16_8 t196 = GM_LD(gm_b16_8, ((((uint8_t *)&t193) + 8) + 16));
        gm_l37 = t196;
        uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
        uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
        uint8_t t199 = ((t197 == t198));
        if (!t199) goto ie190;
        gm_b16_8 t200 = gm_l35;
        gm_b16_8 t201 = gm_l37;
        uint8_t t202 = (gm_eqq39g9duc08(((uint8_t *)&t200), ((uint8_t *)&t201)));
        if (!t202) goto ie190;
        {
            {
                gm_l39 = ((uint64_t)0ULL);
            }
            {
                uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                gm_l38 = t205;
            }
            {
                for (;;) {
                    uint64_t t207 = gm_l39;
                    uint64_t t208 = gm_l38;
                    uint8_t t209 = (t207 < t208);
                    if (!t209) goto lb206;
                    {
                        uint64_t t212 = gm_l39;
                        uint64_t t215 = GM_LD(uint64_t, ((uint8_t *)&t212));
                        gm_l40 = t215;
                        {
                            void * t217 = gm_l0;
                            void * t218 = gm_l1;
                            uint64_t t219 = gm_l40;
                            uint64_t t220 = (uintptr_t)t219;
                            uint64_t t221 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            if (t220 >= (uintptr_t)(t221)) gm_trap_index(t220, t221, gm_srclu7allsekqhf, 270, 50);
                            gm_t88rcucg1b7uk t222 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t220 * 4));
                            uint64_t t223 = gm_l40;
                            uint64_t t224 = (uintptr_t)t223;
                            uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                            if (t224 >= (uintptr_t)(t225)) gm_trap_index(t224, t225, gm_srclu7allsekqhf, 270, 56);
                            gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t224 * 4));
                            gm_t9qkh0rsooc8d t227 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t217, t218, t222, t226);
                            if (gm_unw.kind) {
                                goto gm_ll210;
                            }
                            if (((((uint8_t *)&t227))[0] != 0)) {
                                gm_b0_1 t228 = GM_LD(gm_b0_1, (((uint8_t *)&t227) + 1));
                                gm_t9qkh0rsooc8d t229;
                                ((uint8_t *)&t229)[0] = 1;
                                GM_ST(gm_b0_1, ((uint8_t *)&t229 + 1), t228);
                                F.ret = t229;
                                goto gm_fret;
                            }
                            gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t227) + 1));
                        }
                        goto me213;
mn214:;
                        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 270, 13);
me213:;
                    }
lc206:;
                    {
                        uint64_t t232 = gm_l39;
                        uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                        uint64_t t235 = r233;
                        gm_l39 = t235;
                    }
                }
gm_ll210:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb206; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc206; }
                }
                goto gm_lad;
lb206:;
            }
        }
        {
            gm_b0_1 t236;
            gm_t9qkh0rsooc8d t237;
            ((uint8_t *)&t237)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t237 + 1), t236);
            uint8_t t238 = ((((uint8_t *)&t237))[0] != 0);
            F.ret = t237;
            goto gm_fret;
        }
        goto ix190;
ie190:;
ix190:;
        goto me5;
mn186:;
ma15:;
        goto me5;
mn239:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srclu7allsekqhf, 257, 5);
me5:;
    }
    {
        gm_b0_1 t241;
        gm_t9qkh0rsooc8d t242;
        ((uint8_t *)&t242)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t242 + 1), t241);
        uint8_t t243 = ((((uint8_t *)&t242))[0] != 0);
        F.ret = t242;
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
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_unify_t2qjv2p F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* a0 */
    gm_t88rcucg1b7uk gm_l3; /* b0 */
    gm_t88rcucg1b7uk gm_l4; /* a */
    gm_t88rcucg1b7uk gm_l5; /* b */
    uint32_t gm_l8; /* x */
    uint32_t gm_l9; /* y */
    uint32_t gm_l10; /* rx */
    uint32_t gm_l11; /* ry */
    gm_t9qkh0rsooc8d gm_l14; /* kind */
    uint8_t gm_l15; /* unsigned */
    uint8_t gm_l16; /* integer */
    void * gm_l17; /* v */
    uint32_t gm_l18; /* y */
    GM_STACK_CHECK(&F, 659u);
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
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_t88rcucg1b7uk t9 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t9;
    }
    {
        uint8_t t11 = 0;
        uint8_t t12 = 0;
        gm_t88rcucg1b7uk t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = gm_l5;
        uint8_t t15 = (gm_eq9m6mvtmgde(((uint8_t *)&t13), ((uint8_t *)&t14)));
        if (!t15) {
            void * t16 = gm_l1;
            gm_t88rcucg1b7uk t17 = gm_l4;
            uint8_t t18 = gm_f_is_error_8k27gd1(t16, GM_CONV(gm_csguankko4lmhd, t17));
            t12 = t18;
        } else t12 = 1;
        if (!t12) {
            void * t19 = gm_l1;
            gm_t88rcucg1b7uk t20 = gm_l5;
            uint8_t t21 = gm_f_is_error_8k27gd1(t19, GM_CONV(gm_csguankko4lmhd, t20));
            t11 = t21;
        } else t11 = 1;
        if (!t11) goto ie10;
        {
            gm_b0_1 t23;
            gm_t9qkh0rsooc8d t24;
            ((uint8_t *)&t24)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t24 + 1), t23);
            uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t26 = gm_l1;
        gm_t88rcucg1b7uk t27 = gm_l4;
        gm_f_kind_jcnt4hm((void *)&F.l6, t26, GM_CONV(gm_csguankko4lmhd, t27));
    }
    {
        void * t28 = gm_l1;
        gm_t88rcucg1b7uk t29 = gm_l5;
        gm_f_kind_jcnt4hm((void *)&F.l7, t28, GM_CONV(gm_csguankko4lmhd, t29));
    }
    {
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
        if (t31 != ((uint8_t)23ULL)) goto ie30;
        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
        gm_l8 = t32;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
            if (t35 != ((uint8_t)23ULL)) goto ie34;
            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 8));
            gm_l9 = t36;
            {
                void * t38 = gm_l0;
                uint32_t t39 = gm_l8;
                uint32_t t40 = gm_f_root_jbs2avl(t38, t39);
                gm_l10 = t40;
            }
            {
                void * t41 = gm_l0;
                uint32_t t42 = gm_l9;
                uint32_t t43 = gm_f_root_jbs2avl(t41, t42);
                gm_l11 = t43;
            }
            {
                uint32_t t44 = gm_l10;
                uint64_t t45 = (uint64_t)t44;
                uint64_t t46 = (uintptr_t)t45;
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 0) + 0) + 8);
                if (t46 >= (uintptr_t)(t48)) gm_trap_index(t46, t48, gm_srclu7allsekqhf, 221, 19);
                gm_t4lq9ph3amfvg t49 = GM_LD(gm_t4lq9ph3amfvg, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t47) + 0) + 0)) + t46 * 48));
                F.l12 = t49;
            }
            {
                uint32_t t50 = gm_l11;
                uint64_t t51 = (uint64_t)t50;
                uint64_t t52 = (uintptr_t)t51;
                void * t53 = gm_l0;
                uint64_t t54 = GM_LD(uintptr_t, ((((uint8_t *)t53) + 0) + 0) + 8);
                if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srclu7allsekqhf, 222, 19);
                gm_t4lq9ph3amfvg t55 = GM_LD(gm_t4lq9ph3amfvg, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t53) + 0) + 0)) + t52 * 48));
                F.l13 = t55;
            }
            {
                uint8_t t57 = ((GM_LD(uint8_t, (((uint8_t *)&F.l12) + 4) + 0) == ((uint8_t)2ULL)));
                uint8_t t58 = ((GM_LD(uint8_t, (((uint8_t *)&F.l13) + 4) + 0) == ((uint8_t)2ULL)));
                uint8_t t59 = (!(t57 == t58));
                if (!t59) goto ie56;
                {
                    gm_b0_1 t61;
                    gm_t9qkh0rsooc8d t62;
                    ((uint8_t *)&t62)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t62 + 1), t61);
                    uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
                    F.ret = t62;
                    goto gm_fret;
                }
                goto ix56;
ie56:;
ix56:;
            }
            {
                gm_t9qkh0rsooc8d t64;
                uint8_t t66 = ((GM_LD(uint8_t, (((uint8_t *)&F.l12) + 4) + 0) == ((uint8_t)2ULL)));
                if (!t66) goto ie65;
                gm_t9qkh0rsooc8d t68;
                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
                gm_t9qkh0rsooc8d t69 = t68;
                t64 = t69;
                goto ix65;
ie65:;
                gm_t9qkh0rsooc8d t70;
                uint8_t t72 = 0;
                uint8_t t73 = ((GM_LD(uint8_t, (((uint8_t *)&F.l12) + 4) + 0) == ((uint8_t)1ULL)));
                if (!t73) {
                    uint8_t t74 = ((GM_LD(uint8_t, (((uint8_t *)&F.l13) + 4) + 0) == ((uint8_t)1ULL)));
                    t72 = t74;
                } else t72 = 1;
                if (!t72) goto ie71;
                gm_t9qkh0rsooc8d t76;
                GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)1ULL));
                gm_t9qkh0rsooc8d t77 = t76;
                t70 = t77;
                goto ix71;
ie71:;
                gm_t9qkh0rsooc8d t79;
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)0ULL));
                gm_t9qkh0rsooc8d t80 = t79;
                t70 = t80;
ix71:;
                t64 = t70;
ix65:;
                gm_l14 = t64;
            }
            {
                uint8_t t81 = 0;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 5));
                if (!t82) {
                    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 5));
                    t81 = t83;
                } else t81 = 1;
                gm_l15 = t81;
            }
            {
                uint8_t t84 = 0;
                uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 6));
                if (!t85) {
                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 6));
                    t84 = t86;
                } else t84 = 1;
                gm_l16 = t84;
            }
            {
                uint8_t t88 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l14) + 0) == ((uint8_t)1ULL)));
                if (!t88) goto ie87;
                uint8_t t89 = 0;
                uint8_t t90 = gm_l15;
                if (!t90) {
                    uint8_t t91 = gm_l16;
                    t89 = t91;
                } else t89 = 1;
                if (!t89) goto ie87;
                {
                    gm_b0_1 t93;
                    gm_t9qkh0rsooc8d t94;
                    ((uint8_t *)&t94)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t94 + 1), t93);
                    uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
                    F.ret = t94;
                    goto gm_fret;
                }
                goto ix87;
ie87:;
ix87:;
            }
            {
                uint32_t t96 = gm_l10;
                uint64_t t97 = (uint64_t)t96;
                uint64_t t98 = (uintptr_t)t97;
                void * t99 = gm_l0;
                uint64_t t100 = GM_LD(uintptr_t, ((((uint8_t *)t99) + 0) + 0) + 8);
                if (t98 >= (uintptr_t)(t100)) gm_trap_index(t98, t100, gm_srclu7allsekqhf, 234, 13);
                uint32_t t101 = gm_l11;
                GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t99) + 0) + 0)) + t98 * 48) + 0), t101);
            }
            {
                uint32_t t102 = gm_l11;
                uint64_t t103 = (uint64_t)t102;
                uint64_t t104 = (uintptr_t)t103;
                void * t105 = gm_l0;
                uint64_t t106 = GM_LD(uintptr_t, ((((uint8_t *)t105) + 0) + 0) + 8);
                if (t104 >= (uintptr_t)(t106)) gm_trap_index(t104, t106, gm_srclu7allsekqhf, 235, 19);
                void * t107 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t105) + 0) + 0)) + t104 * 48);
                gm_l17 = t107;
            }
            {
                void * t108 = gm_l17;
                gm_t9qkh0rsooc8d t109 = gm_l14;
                GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t108) + 4), t109);
            }
            {
                void * t110 = gm_l17;
                uint8_t t111 = gm_l15;
                GM_ST(uint8_t, (((uint8_t *)t110) + 5), t111);
            }
            {
                void * t112 = gm_l17;
                uint8_t t113 = gm_l16;
                GM_ST(uint8_t, (((uint8_t *)t112) + 6), t113);
            }
            {
                void * t114 = gm_l17;
                uint8_t *t115 = (((uint8_t *)t114) + 7);
                gm_t9qkh0rsooc8d t116 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l12) + 7));
                gm_t9qkh0rsooc8d t117 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l13) + 7));
                gm_t9qkh0rsooc8d t118 = gm_f_fallback_max_vinhf97(t116, t117);
                GM_ST(gm_t9qkh0rsooc8d, t115, t118);
            }
            {
                gm_b0_1 t119;
                gm_t9qkh0rsooc8d t120;
                ((uint8_t *)&t120)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t120 + 1), t119);
                uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
                F.ret = t120;
                goto gm_fret;
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            void * t122 = gm_l0;
            void * t123 = gm_l1;
            uint32_t t124 = gm_l8;
            gm_t88rcucg1b7uk t125 = gm_l5;
            gm_t9qkh0rsooc8d t126 = gm_f_bind_btu6lfa((void *)NULL, cx, NULL, t122, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t127 = ((((uint8_t *)&t126))[0] != 0);
            F.ret = t126;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
        if (t129 != ((uint8_t)23ULL)) goto ie128;
        uint32_t t130 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 8));
        gm_l18 = t130;
        {
            void * t132 = gm_l0;
            void * t133 = gm_l1;
            uint32_t t134 = gm_l18;
            gm_t88rcucg1b7uk t135 = gm_l4;
            gm_t9qkh0rsooc8d t136 = gm_f_bind_btu6lfa((void *)NULL, cx, NULL, t132, t133, t134, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t137 = ((((uint8_t *)&t136))[0] != 0);
            F.ret = t136;
            goto gm_fret;
        }
        goto ix128;
ie128:;
ix128:;
    }
    {
        uint8_t t139 = 0;
        void * t140 = gm_l1;
        gm_t88rcucg1b7uk t141 = gm_l4;
        uint8_t t142 = gm_f_has_infer_3o82a0b(t140, GM_CONV(gm_csguankko4lmhd, t141));
        if (!t142) {
            void * t143 = gm_l1;
            gm_t88rcucg1b7uk t144 = gm_l5;
            uint8_t t145 = gm_f_has_infer_3o82a0b(t143, GM_CONV(gm_csguankko4lmhd, t144));
            t139 = t145;
        } else t139 = 1;
        if (!t139) goto ie138;
        {
            void * t147 = gm_l0;
            void * t148 = gm_l1;
            gm_t88rcucg1b7uk t149 = gm_l4;
            gm_t88rcucg1b7uk t150 = gm_l5;
            gm_t9qkh0rsooc8d t151 = gm_f_unify_structural_a96lmna((void *)NULL, cx, NULL, t147, t148, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t152 = ((((uint8_t *)&t151))[0] != 0);
            F.ret = t151;
            goto gm_fret;
        }
        goto ix138;
ie138:;
ix138:;
    }
    {
        gm_b0_1 t153;
        gm_t9qkh0rsooc8d t154;
        ((uint8_t *)&t154)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t154 + 1), t153);
        uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
        F.ret = t154;
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_var_kind_1sl5sig F;
    void * gm_l0; /* t */
    void * gm_l1; /* tys */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l3; /* v */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        void * t4 = gm_l0;
        void * t5 = gm_l1;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t88rcucg1b7uk t7 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t3, GM_CONV(gm_csguankko4lmhd, t7));
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)23ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 8));
        gm_l3 = t10;
        {
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 0) + 0));
            gm_b16_8 t14 = t13;
            void * t15 = gm_l0;
            uint32_t t16 = gm_l3;
            uint32_t t17 = gm_f_root_jbs2avl(t15, t16);
            uint64_t t18 = (uint64_t)t17;
            uint64_t t19 = (uintptr_t)t18;
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&t14) + 8);
            if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srclu7allsekqhf, 142, 12);
            gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t14)) + t19 * 48) + 4));
            gm_taari1s0m51a7 t22;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[1] = 1;
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_taari1s0m51a7 t25;
            memset(&t25, 0, sizeof t25);
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
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

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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[2]; } gm_t5liq177ejcou;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; uint32_t f2; uint8_t pe[4]; } gm_tk20dunvmmkbp;
typedef struct { uint64_t a[8]; } gm_tl4e4vqe807ge;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; void * v1; } f1; } gm_tljvbvj59adij;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
_Static_assert(sizeof(gm_t5liq177ejcou) == 2 && _Alignof(gm_t5liq177ejcou) == 1 && offsetof(gm_t5liq177ejcou, a) == 0, "layout of `[2]u8`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_td7tfovebirdn;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tk20dunvmmkbp) == 24 && _Alignof(gm_tk20dunvmmkbp) == 8 && offsetof(gm_tk20dunvmmkbp, f0) == 0 && offsetof(gm_tk20dunvmmkbp, f1) == 8 && offsetof(gm_tk20dunvmmkbp, f2) == 16, "layout of `(id: u32, env: ?^Frame, cx: u32)`");
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
_Static_assert(sizeof(gm_tl4e4vqe807ge) == 64 && _Alignof(gm_tl4e4vqe807ge) == 8 && offsetof(gm_tl4e4vqe807ge, a) == 0, "layout of `[8]usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tljvbvj59adij) == 16 && _Alignof(gm_tljvbvj59adij) == 8 && offsetof(gm_tljvbvj59adij, f0) == 0 && offsetof(gm_tljvbvj59adij, f1) == 8, "layout of `Outcome`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tnsjse4su5jue;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { gm_t76123gfet73g f0; uint8_t f1; uint8_t pe[7]; } gm_t07lf02ct4q61;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; } gm_t16gj5so0pj6j;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t0rfe1ou14h21 f0; void * f1; } gm_t4grvdhp6dkf1;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; gm_tl4e4vqe807ge f3; gm_trqfvmjtt4sij f4; uint64_t f5; uint64_t f6; } gm_t4m55nu62fs91;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td7tfovebirdn) == 40 && _Alignof(gm_td7tfovebirdn) == 8 && offsetof(gm_td7tfovebirdn, f0) == 0 && offsetof(gm_td7tfovebirdn, f1) == 16 && offsetof(gm_td7tfovebirdn, f2) == 32, "layout of `([]u8, string, u32)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnsjse4su5jue) == 48 && _Alignof(gm_tnsjse4su5jue) == 8 && offsetof(gm_tnsjse4su5jue, f0) == 0 && offsetof(gm_tnsjse4su5jue, f1) == 4 && offsetof(gm_tnsjse4su5jue, f2) == 8 && offsetof(gm_tnsjse4su5jue, f3) == 16 && offsetof(gm_tnsjse4su5jue, f4) == 32, "layout of `(fiber: u32, node: u32, op: u32, args: []const []u8, span: Span)`");
typedef struct { gm_trqfvmjtt4sij f0; uint32_t f1; uint32_t f2; uint64_t f3; void * f4; gm_tqs5p4n2tq19v f5; uint8_t pe[6]; } gm_tobnqifq2n0g5;
typedef struct { gm_t9d4sf0vqn7dt f0; uint8_t f1; uint8_t pe[3]; } gm_tok0o3qpepqqf;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; } gm_tolm6p7jp1hb3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
typedef struct { gm_trqfvmjtt4sij a[2]; } gm_tpr5fhtm8rpui;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tk20dunvmmkbp v0; gm_td7tfovebirdn v1; gm_t9qkh0rsooc8d v2; } f1; } gm_tte9vgf6noic9;
typedef struct { gm_t4c7ipjj9s3fp f0; gm_t4c7ipjj9s3fp f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; int64_t f5; gm_tf5srh4mp4b3i f6; } gm_ttse10gjbhca2;
_Static_assert(sizeof(gm_t07lf02ct4q61) == 32 && _Alignof(gm_t07lf02ct4q61) == 8 && offsetof(gm_t07lf02ct4q61, f0) == 0 && offsetof(gm_t07lf02ct4q61, f1) == 24, "layout of `?Flow`");
_Static_assert(sizeof(gm_t16gj5so0pj6j) == 56 && _Alignof(gm_t16gj5so0pj6j) == 8 && offsetof(gm_t16gj5so0pj6j, f0) == 0 && offsetof(gm_t16gj5so0pj6j, f1) == 40 && offsetof(gm_t16gj5so0pj6j, f2) == 48, "layout of `Scratch`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4grvdhp6dkf1) == 72 && _Alignof(gm_t4grvdhp6dkf1) == 8 && offsetof(gm_t4grvdhp6dkf1, f0) == 0 && offsetof(gm_t4grvdhp6dkf1, f1) == 64, "layout of `Layouts`");
_Static_assert(sizeof(gm_t4m55nu62fs91) == 176 && _Alignof(gm_t4m55nu62fs91) == 8 && offsetof(gm_t4m55nu62fs91, f0) == 0 && offsetof(gm_t4m55nu62fs91, f1) == 40 && offsetof(gm_t4m55nu62fs91, f2) == 48 && offsetof(gm_t4m55nu62fs91, f3) == 56 && offsetof(gm_t4m55nu62fs91, f4) == 120 && offsetof(gm_t4m55nu62fs91, f5) == 160 && offsetof(gm_t4m55nu62fs91, f6) == 168, "layout of `Memory`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_b16_8 f0; gm_t5liq177ejcou f1; uint8_t p2[6]; gm_tpr5fhtm8rpui f2; } gm_te2j9rtup22nm;
_Static_assert(sizeof(gm_tobnqifq2n0g5) == 72 && _Alignof(gm_tobnqifq2n0g5) == 8 && offsetof(gm_tobnqifq2n0g5, f0) == 0 && offsetof(gm_tobnqifq2n0g5, f1) == 40 && offsetof(gm_tobnqifq2n0g5, f2) == 44 && offsetof(gm_tobnqifq2n0g5, f3) == 48 && offsetof(gm_tobnqifq2n0g5, f4) == 56 && offsetof(gm_tobnqifq2n0g5, f5) == 64, "layout of `Threads`");
_Static_assert(sizeof(gm_tok0o3qpepqqf) == 20 && _Alignof(gm_tok0o3qpepqqf) == 4 && offsetof(gm_tok0o3qpepqqf, f0) == 0 && offsetof(gm_tok0o3qpepqqf, f1) == 16, "layout of `?TraceCall`");
_Static_assert(sizeof(gm_tolm6p7jp1hb3) == 72 && _Alignof(gm_tolm6p7jp1hb3) == 8 && offsetof(gm_tolm6p7jp1hb3, f0) == 0 && offsetof(gm_tolm6p7jp1hb3, f1) == 16 && offsetof(gm_tolm6p7jp1hb3, f2) == 32, "layout of `EvalTrap`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tpr5fhtm8rpui) == 80 && _Alignof(gm_tpr5fhtm8rpui) == 8 && offsetof(gm_tpr5fhtm8rpui, a) == 0, "layout of `[2]List(u8)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t07lf02ct4q61 f1; } gm_tslj4ghflekdk;
_Static_assert(sizeof(gm_tte9vgf6noic9) == 48 && _Alignof(gm_tte9vgf6noic9) == 8 && offsetof(gm_tte9vgf6noic9, f0) == 0 && offsetof(gm_tte9vgf6noic9, f1) == 8, "layout of `Installed`");
_Static_assert(sizeof(gm_ttse10gjbhca2) == 304 && _Alignof(gm_ttse10gjbhca2) == 8 && offsetof(gm_ttse10gjbhca2, f0) == 0 && offsetof(gm_ttse10gjbhca2, f1) == 56 && offsetof(gm_ttse10gjbhca2, f2) == 112 && offsetof(gm_ttse10gjbhca2, f3) == 168 && offsetof(gm_ttse10gjbhca2, f4) == 224 && offsetof(gm_ttse10gjbhca2, f5) == 280 && offsetof(gm_ttse10gjbhca2, f6) == 288, "layout of `SysState`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tiilmfd16v3fc f1; gm_tte9vgf6noic9 f2; uint64_t f3; uint32_t f4; uint8_t p5[4]; void * f5; } gm_tv2ad0oo3igpi;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tnsjse4su5jue v1; gm_tslj4ghflekdk v2; } f1; } gm_t9hl37efuvhj8;
_Static_assert(sizeof(gm_te2j9rtup22nm) == 104 && _Alignof(gm_te2j9rtup22nm) == 8 && offsetof(gm_te2j9rtup22nm, f0) == 0 && offsetof(gm_te2j9rtup22nm, f1) == 16 && offsetof(gm_te2j9rtup22nm, f2) == 24, "layout of `Filter`");
_Static_assert(sizeof(gm_tslj4ghflekdk) == 40 && _Alignof(gm_tslj4ghflekdk) == 8 && offsetof(gm_tslj4ghflekdk, f0) == 0 && offsetof(gm_tslj4ghflekdk, f1) == 8, "layout of `(fiber: u32, flow: ?Flow)`");
_Static_assert(sizeof(gm_tv2ad0oo3igpi) == 104 && _Alignof(gm_tv2ad0oo3igpi) == 8 && offsetof(gm_tv2ad0oo3igpi, f0) == 0 && offsetof(gm_tv2ad0oo3igpi, f1) == 8 && offsetof(gm_tv2ad0oo3igpi, f2) == 32 && offsetof(gm_tv2ad0oo3igpi, f3) == 80 && offsetof(gm_tv2ad0oo3igpi, f4) == 88 && offsetof(gm_tv2ad0oo3igpi, f5) == 96, "layout of `CtxNode`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_t9hl37efuvhj8 f3; gm_b16_8 f4; gm_trqfvmjtt4sij f5; } gm_t5rqrtiuf7cri;
_Static_assert(sizeof(gm_t9hl37efuvhj8) == 56 && _Alignof(gm_t9hl37efuvhj8) == 8 && offsetof(gm_t9hl37efuvhj8, f0) == 0 && offsetof(gm_t9hl37efuvhj8, f1) == 8, "layout of `Msg`");
_Static_assert(sizeof(gm_t5rqrtiuf7cri) == 200 && _Alignof(gm_t5rqrtiuf7cri) == 8 && offsetof(gm_t5rqrtiuf7cri, f0) == 0 && offsetof(gm_t5rqrtiuf7cri, f1) == 8 && offsetof(gm_t5rqrtiuf7cri, f2) == 48 && offsetof(gm_t5rqrtiuf7cri, f3) == 88 && offsetof(gm_t5rqrtiuf7cri, f4) == 144 && offsetof(gm_t5rqrtiuf7cri, f5) == 160, "layout of `Fibers`");
typedef struct { void * f0; void * f1; gm_t4grvdhp6dkf1 f2; gm_t9qkh0rsooc8d f3; uint8_t f4; uint8_t f5; uint8_t p6[5]; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; uint64_t f8; gm_t4m55nu62fs91 f9; gm_t16gj5so0pj6j f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_trqfvmjtt4sij f13; void * f14; uint32_t f15; uint8_t p16[4]; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_trqfvmjtt4sij f20; uint64_t f21; uint32_t f22; uint8_t p23[4]; uint64_t f23; gm_t4c7ipjj9s3fp f24; gm_t4c7ipjj9s3fp f25; gm_t4c7ipjj9s3fp f26; gm_trqfvmjtt4sij f27; gm_t4c7ipjj9s3fp f28; gm_b16_8 f29; gm_th2lqbo77pvsc f30; gm_t5rqrtiuf7cri f31; uint64_t f32; gm_t4c7ipjj9s3fp f33; gm_ttse10gjbhca2 f34; gm_tobnqifq2n0g5 f35; void * f36; } gm_tjna7lt0f2p8r;
_Static_assert(sizeof(gm_tjna7lt0f2p8r) == 1728 && _Alignof(gm_tjna7lt0f2p8r) == 8 && offsetof(gm_tjna7lt0f2p8r, f0) == 0 && offsetof(gm_tjna7lt0f2p8r, f1) == 8 && offsetof(gm_tjna7lt0f2p8r, f2) == 16 && offsetof(gm_tjna7lt0f2p8r, f3) == 88 && offsetof(gm_tjna7lt0f2p8r, f4) == 89 && offsetof(gm_tjna7lt0f2p8r, f5) == 90 && offsetof(gm_tjna7lt0f2p8r, f6) == 96 && offsetof(gm_tjna7lt0f2p8r, f7) == 136 && offsetof(gm_tjna7lt0f2p8r, f8) == 176 && offsetof(gm_tjna7lt0f2p8r, f9) == 184 && offsetof(gm_tjna7lt0f2p8r, f10) == 360 && offsetof(gm_tjna7lt0f2p8r, f11) == 416 && offsetof(gm_tjna7lt0f2p8r, f12) == 456 && offsetof(gm_tjna7lt0f2p8r, f13) == 496 && offsetof(gm_tjna7lt0f2p8r, f14) == 536 && offsetof(gm_tjna7lt0f2p8r, f15) == 544 && offsetof(gm_tjna7lt0f2p8r, f16) == 552 && offsetof(gm_tjna7lt0f2p8r, f17) == 592 && offsetof(gm_tjna7lt0f2p8r, f18) == 632 && offsetof(gm_tjna7lt0f2p8r, f19) == 672 && offsetof(gm_tjna7lt0f2p8r, f20) == 712 && offsetof(gm_tjna7lt0f2p8r, f21) == 752 && offsetof(gm_tjna7lt0f2p8r, f22) == 760 && offsetof(gm_tjna7lt0f2p8r, f23) == 768 && offsetof(gm_tjna7lt0f2p8r, f24) == 776 && offsetof(gm_tjna7lt0f2p8r, f25) == 832 && offsetof(gm_tjna7lt0f2p8r, f26) == 888 && offsetof(gm_tjna7lt0f2p8r, f27) == 944 && offsetof(gm_tjna7lt0f2p8r, f28) == 984 && offsetof(gm_tjna7lt0f2p8r, f29) == 1040 && offsetof(gm_tjna7lt0f2p8r, f30) == 1056 && offsetof(gm_tjna7lt0f2p8r, f31) == 1080 && offsetof(gm_tjna7lt0f2p8r, f32) == 1280 && offsetof(gm_tjna7lt0f2p8r, f33) == 1288 && offsetof(gm_tjna7lt0f2p8r, f34) == 1344 && offsetof(gm_tjna7lt0f2p8r, f35) == 1648 && offsetof(gm_tjna7lt0f2p8r, f36) == 1720, "layout of `Machine`");
struct gm_fr_deinit_550ph9b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_exit_status_ckbk1e1 {
    gm_frame h;
    void * l0; /* m */
    gm_trqfvmjtt4sij l7; /* out */
    uint8_t ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_filter_6t4fm65 {
    gm_frame h;
    gm_te2j9rtup22nm ret;
};
struct gm_fr_host_little_m7un29s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_host_write_r26no87 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_int_status_h8a0nf0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_location_dpep111 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_machine_84ep32o {
    gm_frame h;
    gm_t4grvdhp6dkf1 l8; /* <tmp> */
    gm_trqfvmjtt4sij l12; /* <tmp> */
    gm_trqfvmjtt4sij l13; /* <tmp> */
    gm_t4m55nu62fs91 l15; /* <tmp> */
    gm_t16gj5so0pj6j l16; /* <tmp> */
    gm_trqfvmjtt4sij l17; /* <tmp> */
    gm_trqfvmjtt4sij l18; /* <tmp> */
    gm_trqfvmjtt4sij l20; /* <tmp> */
    gm_trqfvmjtt4sij l21; /* <tmp> */
    gm_trqfvmjtt4sij l22; /* <tmp> */
    gm_trqfvmjtt4sij l23; /* <tmp> */
    gm_trqfvmjtt4sij l24; /* <tmp> */
    gm_trqfvmjtt4sij l25; /* <tmp> */
    gm_t4c7ipjj9s3fp l29; /* <tmp> */
    gm_t4c7ipjj9s3fp l30; /* <tmp> */
    gm_t4c7ipjj9s3fp l31; /* <tmp> */
    gm_trqfvmjtt4sij l32; /* <tmp> */
    gm_t4c7ipjj9s3fp l33; /* <tmp> */
    gm_t5rqrtiuf7cri l35; /* <tmp> */
    gm_t4c7ipjj9s3fp l37; /* <tmp> */
    gm_ttse10gjbhca2 l38; /* <tmp> */
    gm_tobnqifq2n0g5 l39; /* <tmp> */
    gm_tjna7lt0f2p8r l40; /* m */
    gm_tjna7lt0f2p8r ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_name_oqt6ams {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_run_entry_jfn1dvn {
    gm_frame h;
    void * l0; /* p */
    uint32_t l1; /* entry */
    gm_tjna7lt0f2p8r l5; /* m */
    void * l6; /* mp */
    gm_tljvbvj59adij ret;
    gm_tljvbvj59adij w0;
};
struct gm_fr_run_pgu7dng {
    gm_frame h;
    gm_tljvbvj59adij ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tmp_next_dalgo1f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trap_qh84l9d {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_trap_report_2rjo4a6 {
    gm_frame h;
    void * l0; /* p */
    void * l1; /* t */
    gm_trqfvmjtt4sij l2; /* out */
    gm_trqfvmjtt4sij l6; /* callers */
    gm_b16_8 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_backtrace_name_mars429(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_deinit_1b3bclj(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_deinit_550ph9b(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_deinit_5mkc6p0(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_6u0ld99(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_9j6adnf(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_b4nirs4(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_d21bit7(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_lb56u7s(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_lkujvd3(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_nfb16jk(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_rjjscji(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_t376cfp(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_v2ik7pl(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_destroy_ca2nrm8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_exit_status_ckbk1e1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fibers_i2ha6hs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_filter_6t4fm65(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bt4apif(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_l0kjo6g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_map_4fb6o1f(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_a2hplai(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_gd2q5f3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_map_l2188m6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
__attribute__((aligned(4))) uint8_t gm_f_host_little_m7un29s(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_int_status_h8a0nf0(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_list_0grrn1c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_jofrtqr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_rif31rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_location_dpep111(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_machine_84ep32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_mem_deinit_0088958(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_memory_dqom95e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_name_oqt6ams(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jd11mvt(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7p5mg9v(void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_push_ctx_1rv0jgf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, const void * a3, uint64_t a4, uint32_t a5, void * a6);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tljvbvj59adij gm_f_run_coroutines_0a7hn79(void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_tljvbvj59adij gm_f_run_entry_jfn1dvn(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_tljvbvj59adij gm_f_run_pgu7dng(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_threads_kh5dldj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trap_report_2rjo4a6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_truncate_ov4pgoq(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_73bbpvi(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lgac4p5(void * a0, gm_tnpifv20irnph a1);
GM_NOINLINE void gm_pf0nmuupvugn(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE void gm_pfmaojsuds6a(gm_node *cx, gm_b16_8 a, const char *file, uint32_t line, uint32_t col);
extern void gm_x_free_eeuh60c(void *) __asm__(GM_SYM("free"));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cga7n0g3amu[8] __attribute__((aligned(8)));
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern uint8_t gm_k26u51dr2uj[40] __attribute__((aligned(8)));
extern uint8_t gm_k2bf31gf1v3[40] __attribute__((aligned(8)));
extern uint8_t gm_k2sohj2k4ib[40] __attribute__((aligned(8)));
extern uint8_t gm_k3q4at3rjhc[40] __attribute__((aligned(8)));
extern uint8_t gm_k6od4kv6k1l[40] __attribute__((aligned(8)));
extern uint8_t gm_k7dtk1q8l8e[40] __attribute__((aligned(8)));
extern uint8_t gm_kc6vr8pmeqd[40] __attribute__((aligned(8)));
extern uint8_t gm_kg5vb7jesk1[40] __attribute__((aligned(8)));
extern uint8_t gm_kgoi1um11jn[40] __attribute__((aligned(8)));
extern uint8_t gm_kp74jj8lk9n[40] __attribute__((aligned(8)));
extern const uint8_t gm_sfd2hpva2vj33[20];
extern const uint8_t gm_sfss9c30cdmun[8];
extern const uint8_t gm_sps94d2dd3g7k[44];
extern const uint8_t gm_sqh82hv8jvv9p[18];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcpit8mdn0781q[];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = gm_l3;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l5 = t15;
                    {
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l2;
                        uint8_t t20 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t19 + t18 * 1), t20);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t22 = gm_l4;
                    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                    uint64_t t25 = r23;
                    gm_l4 = t25;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 381, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 385, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 385, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 385, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 387, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 388, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 388, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 389, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_deinit_550ph9b(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_deinit_550ph9b F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_b16_8 gm_l3; /* c */
    void * gm_l4; /* fr */
    gm_b16_8 gm_l5; /* t */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* s */
    GM_STACK_CHECK(&F, 923u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 184);
        gm_b0_1 t4 = gm_f_mem_deinit_0088958((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((((uint8_t *)t6) + 360) + 0) + 0));
            gm_l1 = t7;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l2;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l2;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t15 * 16));
                    gm_l3 = t19;
                    {
                        gm_b16_8 t21 = gm_l3;
                        gm_b16_8 t22 = t21;
                        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
                        gm_b0_1 t24 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t23, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kc6vr8pmeqd)));
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 480, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t26 = gm_l2;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l2 = t29;
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
    {
        void * t30 = gm_l0;
        void * t31 = (void *)((((uint8_t *)t30) + 360) + 0);
        gm_f_deinit_tq06jtm(t31);
    }
    {
        void * t32 = gm_l0;
        void * t33 = (void *)(((uint8_t *)t32) + 712);
        gm_f_deinit_rjjscji(t33);
    }
    {
        void * t34 = gm_l0;
        void * t35 = (void *)(((uint8_t *)t34) + 416);
        gm_f_deinit_v2ik7pl(t35);
    }
    {
        for (;;) {
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, (((uint8_t *)t37) + 536));
            void * t39 = t38;
            if (!(GM_LD(void *, ((uint8_t *)&t39)) != 0)) goto lb36;
            void * t40 = GM_LD(void *, ((uint8_t *)&t39));
            gm_l4 = t40;
            {
                void * t43 = gm_l0;
                void * t44 = gm_l4;
                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 248));
                GM_ST(void *, (((uint8_t *)t43) + 536), t45);
            }
            {
                void * t46 = gm_l4;
                void * t47 = (void *)(((uint8_t *)t46) + 96);
                gm_f_deinit_v2ik7pl(t47);
            }
            {
                void * t48 = gm_l4;
                void * t49 = (void *)(((uint8_t *)t48) + 136);
                gm_f_deinit_lb56u7s(t49);
            }
            {
                void * t50 = gm_l4;
                void * t51 = (void *)(((uint8_t *)t50) + 176);
                gm_f_deinit_6u0ld99(t51);
            }
            {
                void * t53 = gm_l4;
                uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)t53) + 80) + 8);
                uint8_t t55 = (t54 > ((uint64_t)0ULL));
                if (!t55) goto ie52;
                {
                    void * t57 = gm_l4;
                    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t57) + 80));
                    gm_b16_8 t59 = t58;
                    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                    gm_b0_1 t61 = gm_f_free_l0kjo6g((void *)NULL, cx, NULL, t60, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k26u51dr2uj)));
                    if (gm_unw.kind) {
                        goto gm_ll41;
                    }
                }
                goto ix52;
ie52:;
ix52:;
            }
            {
                void * t62 = gm_l4;
                gm_b0_1 t63 = gm_f_destroy_ca2nrm8((void *)NULL, cx, NULL, t62, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2sohj2k4ib)));
                if (gm_unw.kind) {
                    goto gm_ll41;
                }
            }
lc36:;
        }
gm_ll41:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
        }
        goto gm_lad;
lb36:;
    }
    {
        void * t64 = gm_l0;
        void * t65 = (void *)(((uint8_t *)t64) + 552);
        gm_f_deinit_tq06jtm(t65);
    }
    {
        void * t67 = gm_l0;
        if (!(((((uint8_t *)t67) + 1056))[16] != 0)) goto ie66;
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)t67) + 1056));
        gm_l5 = t68;
        {
            gm_b16_8 t70 = gm_l5;
            gm_b16_8 t71 = t70;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_b0_1 t73 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t72, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kgoi1um11jn)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        void * t74 = gm_l0;
        void * t75 = (void *)(((uint8_t *)t74) + 496);
        gm_f_deinit_v2ik7pl(t75);
    }
    {
        void * t76 = gm_l0;
        void * t77 = (void *)(((uint8_t *)t76) + 456);
        gm_f_deinit_1b3bclj(t77);
    }
    {
        void * t78 = gm_l0;
        void * t79 = (void *)(((uint8_t *)t78) + 592);
        gm_f_deinit_padgogk(t79);
    }
    {
        void * t80 = gm_l0;
        void * t81 = (void *)(((uint8_t *)t80) + 632);
        gm_f_deinit_5mkc6p0(t81);
    }
    {
        void * t82 = gm_l0;
        void * t83 = (void *)(((uint8_t *)t82) + 672);
        gm_f_deinit_padgogk(t83);
    }
    {
        void * t84 = gm_l0;
        void * t85 = (void *)(((uint8_t *)t84) + 944);
        gm_f_deinit_v2ik7pl(t85);
    }
    {
        void * t86 = gm_l0;
        void * t87 = (void *)(((uint8_t *)t86) + 984);
        gm_f_deinit_d21bit7(t87);
    }
    {
        void * t88 = gm_l0;
        void * t89 = (void *)(((uint8_t *)t88) + 776);
        gm_f_deinit_b4nirs4(t89);
    }
    {
        void * t90 = gm_l0;
        void * t91 = (void *)(((uint8_t *)t90) + 832);
        gm_f_deinit_b4nirs4(t91);
    }
    {
        void * t92 = gm_l0;
        void * t93 = (void *)(((uint8_t *)t92) + 888);
        gm_f_deinit_9j6adnf(t93);
    }
    {
        void * t94 = gm_l0;
        void * t95 = (void *)(((uint8_t *)t94) + 1288);
        gm_f_deinit_lkujvd3(t95);
    }
    {
        {
            void * t97 = gm_l0;
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((((uint8_t *)t97) + 1080) + 160) + 0));
            gm_l6 = t98;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t100 = gm_l7;
                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t102 = (t100 < t101);
                if (!t102) goto lb99;
                {
                    uint64_t t105 = gm_l7;
                    uint64_t t106 = (uintptr_t)t105;
                    uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t106 * 16));
                    gm_l8 = t110;
                    {
                        void * t112 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        gm_x_free_eeuh60c(t112);
                    }
                    goto me108;
mn109:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 505, 5);
me108:;
                }
lc99:;
                {
                    uint64_t t114 = gm_l7;
                    uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                    uint64_t t117 = r115;
                    gm_l7 = t117;
                }
            }
lb99:;
        }
    }
    {
        void * t118 = gm_l0;
        void * t119 = (void *)((((uint8_t *)t118) + 1080) + 160);
        gm_f_deinit_tq06jtm(t119);
    }
    {
        void * t120 = gm_l0;
        void * t121 = (void *)((((uint8_t *)t120) + 1080) + 8);
        gm_f_deinit_nfb16jk(t121);
    }
    {
        void * t122 = gm_l0;
        void * t123 = (void *)((((uint8_t *)t122) + 1080) + 48);
        gm_f_deinit_t376cfp(t123);
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
__attribute__((aligned(4))) uint8_t gm_f_exit_status_ckbk1e1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_exit_status_ckbk1e1 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    gm_closure gm_l3; /* int_status */
    gm_t88rcucg1b7uk gm_l4; /* err */
    gm_t88rcucg1b7uk gm_l5; /* ok */
    gm_b16_8 gm_l6; /* payload */
    GM_STACK_CHECK(&F, 887u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_closure t2 = ((gm_closure){(void *)gm_f_int_status_h8a0nf0, (void *)&F.h, cx});
        gm_l3 = t2;
    }
    {
        void * t4 = F.l0;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_htv6093((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)14ULL)) goto ie3;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 0));
        gm_l4 = t9;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l5 = t10;
        {
            void * t12 = F.l0;
            gm_t88rcucg1b7uk t13 = gm_l1;
            gm_b16_8 t14 = gm_l2;
            gm_b16_8 t15 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t15;
        }
        {
            void * t17 = F.l0;
            gm_t88rcucg1b7uk t18 = gm_l1;
            gm_b16_8 t19 = gm_l2;
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            uint8_t t22 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t17, t18, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t23 = (uint8_t)!t22;
            if (!t23) goto ie16;
            {
                gm_closure t25 = gm_l3;
                gm_t88rcucg1b7uk t26 = gm_l5;
                gm_b16_8 t27 = gm_l6;
                gm_b16_8 t28 = t27;
                gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                gm_closure t30 = t25;
                gm_node *ccx31 = t30.cx;
                void * t32 = t30.code;
                uint8_t t33;
                if (GM_PTR_TAGGED(t32)) {
                    t33 = ((uint8_t (*)(gm_csguankko4lmhd, gm_b16_8))GM_PTR_UNTAG(t32))(GM_CONV(gm_csguankko4lmhd, t26), t29);
                } else {
                    t33 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_b16_8))t32)(t30.env, ccx31, NULL, t26, t29);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        {
            gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t35 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sfss9c30cdmun));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(7));
            gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t35, t36);
            gm_tftb1odc739t5 t38 = t37;
            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcpit8mdn0781q, 521, 5);
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
        }
        {
            void * t41 = F.l0;
            gm_t88rcucg1b7uk t42 = gm_l4;
            gm_t57euh612ligr t43;
            gm_f_union_def_h8mvf4t((void *)&t43, t41, GM_CONV(gm_csguankko4lmhd, t42));
            gm_t57euh612ligr t44 = t43;
            uint8_t t45;
            if (((((uint8_t *)&t44))[24] != 0)) goto mn47;
            t45 = ((uint8_t)0);
            goto me46;
mn47:;
            t45 = ((uint8_t)1);
            goto me46;
mn48:;
            gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 523, 8);
me46:;
            if (!t45) goto ie40;
            {
                void * t50 = (void *)((uint8_t *)&F.l7);
                void * t51 = F.l0;
                void * t52 = GM_LD(void *, (((uint8_t *)t51) + 8));
                void * t53 = (void *)(((uint8_t *)t52) + 8);
                gm_t88rcucg1b7uk t54 = gm_l4;
                gm_b16_8 t55 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t50, t55);
                gm_tftb1odc739t5 t57 = t56;
                if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcpit8mdn0781q, 523, 39);
                gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
            }
            goto ix40;
ie40:;
ix40:;
        }
        {
            void * t59 = F.l0;
            void * t60 = (void *)((uint8_t *)&F.l7);
            gm_t88rcucg1b7uk t61 = gm_l4;
            gm_b16_8 t62 = gm_l6;
            gm_tl572d7579hns t63;
            gm_f_format_value_vmle7qr((void *)&t63, (void *)NULL, cx, NULL, t59, t60, t61, t62, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl572d7579hns t64 = t63;
            gm_b0_1 t65;
            if (!((((uint8_t *)&t64))[0] != 0)) {
                gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 8));
                t65 = t66;
            } else {
                void * t68 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t69;
                GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sfd2hpva2vj33));
                GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(19));
                gm_tftb1odc739t5 t70 = gm_f_extend_0cs85gs(t68, t69);
                gm_tftb1odc739t5 t71 = t70;
                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcpit8mdn0781q, 524, 53);
                gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                gm_b0_1 t73 = t72;
                t65 = t73;
            }
        }
        {
            void * t74 = (void *)((uint8_t *)&F.l7);
            gm_tftb1odc739t5 t75 = gm_f_push_t83rg2m(t74, ((uint8_t)10ULL));
            gm_tftb1odc739t5 t76 = t75;
            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcpit8mdn0781q, 525, 5);
            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
        }
        {
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t79 = t78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            gm_pf0nmuupvugn(cx, t80, gm_srcpit8mdn0781q, 526, 5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t81 = 0;
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
        goto ix3;
ie3:;
        {
            gm_closure t83 = gm_l3;
            gm_t88rcucg1b7uk t84 = gm_l1;
            gm_b16_8 t85 = gm_l2;
            gm_b16_8 t86 = t85;
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
            gm_closure t88 = t83;
            gm_node *ccx89 = t88.cx;
            void * t90 = t88.code;
            uint8_t t91;
            if (GM_PTR_TAGGED(t90)) {
                t91 = ((uint8_t (*)(gm_csguankko4lmhd, gm_b16_8))GM_PTR_UNTAG(t90))(GM_CONV(gm_csguankko4lmhd, t84), t87);
            } else {
                t91 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk, gm_b16_8))t90)(t88.env, ccx89, NULL, t84, t87);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
ix3:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)t1) + 88) + 0) == ((uint8_t)3ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_filter_6t4fm65(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1) {
    struct gm_fr_filter_6t4fm65 F;
    gm_b16_8 gm_l0; /* marker */
    uint8_t gm_l1; /* hide */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memset((uint8_t *)&(*(gm_te2j9rtup22nm *)gm_out) + 18, 0, 6);
    gm_b16_8 t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint8_t t3 = gm_l1;
    gm_t5liq177ejcou t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 1), t3);
    gm_tpr5fhtm8rpui t5;
    gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t5 + 0)), (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t5 + 40)), (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_te2j9rtup22nm *)gm_out) + 0, t1);
    GM_ST(gm_t5liq177ejcou, (uint8_t *)&(*(gm_te2j9rtup22nm *)gm_out) + 16, t4);
    GM_ST(gm_tpr5fhtm8rpui, (uint8_t *)&(*(gm_te2j9rtup22nm *)gm_out) + 24, t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_host_little_m7un29s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_host_little_m7un29s F;
    uint64_t gm_l0; /* x */
    GM_STACK_CHECK(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_l0 = ((uint64_t)1ULL);
    }
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = (void *)((uint8_t *)&gm_l0);
        void * t4 = t3;
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)t4 + t2 * 1));
        uint8_t t6 = ((t5 == ((uint8_t)1ULL)));
        uint8_t t7 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_host_write_r26no87 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* fd */
    gm_b16_8 gm_l2; /* data */
    gm_b16_8 gm_l3; /* shown */
    void * gm_l4; /* f */
    uint64_t gm_l5; /* i */
    void * gm_l6; /* p */
    uint64_t gm_l7; /* keep */
    gm_b16_8 gm_l8; /* tail */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* k */
    uint64_t gm_l12; /* at */
    GM_STACK_CHECK(&F, 921u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l2;
        gm_l3 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 1720));
        void * t6 = t5;
        if (!(GM_LD(void *, ((uint8_t *)&t6)) != 0)) goto ie3;
        void * t7 = GM_LD(void *, ((uint8_t *)&t6));
        gm_l4 = t7;
        {
            uint64_t t9;
            uint32_t t11 = gm_l1;
            uint8_t t12 = ((t11 == ((uint32_t)2ULL)));
            if (!t12) goto ie10;
            uint64_t t14 = ((uint64_t)1ULL);
            t9 = t14;
            goto ix10;
ie10:;
            uint64_t t16 = ((uint64_t)0ULL);
            t9 = t16;
ix10:;
            gm_l5 = t9;
        }
        {
            uint64_t t18 = gm_l5;
            uint64_t t19 = (uintptr_t)t18;
            void * t20 = gm_l4;
            if (t19 >= (uintptr_t)(2)) gm_trap_index(t19, 2, gm_srcpit8mdn0781q, 278, 12);
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t20) + 16) + t19 * 1));
            if (!t21) goto ie17;
            {
                uint64_t t23 = gm_l5;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l4;
                if (t24 >= (uintptr_t)(2)) gm_trap_index(t24, 2, gm_srcpit8mdn0781q, 279, 19);
                void * t26 = (void *)((((uint8_t *)t25) + 24) + t24 * 40);
                gm_l6 = t26;
            }
            {
                void * t27 = gm_l6;
                gm_b16_8 t28 = gm_l2;
                gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
                gm_tftb1odc739t5 t30 = t29;
                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcpit8mdn0781q, 280, 13);
                gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
            }
            {
                void * t32 = gm_l6;
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 0));
                gm_b16_8 t34 = t33;
                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                void * t36 = gm_l4;
                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t36) + 0));
                gm_tf5srh4mp4b3i t38 = gm_f_find_nab29co(t35, t37);
                gm_tf5srh4mp4b3i t39 = t38;
                uint64_t t40;
                if (((((uint8_t *)&t39))[8] != 0)) {
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t39) + 0));
                    t40 = t41;
                } else {
                    {
                        void * t44 = gm_l6;
                        uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 0) + 8);
                        void * t46 = gm_l4;
                        uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)t46) + 0) + 8);
                        uint8_t t48 = (t45 > t47);
                        if (!t48) goto ie43;
                        {
                            void * t50 = gm_l6;
                            uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)t50) + 0) + 8);
                            void * t52 = gm_l4;
                            uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)t52) + 0) + 8);
                            uint64_t r54; bool o55 = __builtin_sub_overflow(t51, t53, &r54);
                            if (o55) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 284, 29);
                            uint64_t t56 = r54;
                            gm_l7 = t56;
                        }
                        {
                            void * t57 = gm_l6;
                            uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 0) + 8);
                            void * t59 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 0)));
                            uint64_t t60 = gm_l7;
                            uint64_t t61 = (uintptr_t)t60;
                            void * t62 = gm_l6;
                            uint64_t t63 = GM_LD(uintptr_t, (((uint8_t *)t62) + 0) + 8);
                            uint64_t t64 = (uintptr_t)t63;
                            if (t61 > t64 || t64 > (uintptr_t)t58) gm_trap_slice(t61, t64, t58, gm_srcpit8mdn0781q, 285, 29);
                            gm_b16_8 t65;
                            GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t59 + t61 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - t61));
                            gm_l8 = t65;
                        }
                        {
                            {
                                gm_l10 = ((uint64_t)0ULL);
                            }
                            {
                                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                gm_l9 = t67;
                            }
                            {
                                for (;;) {
                                    uint64_t t69 = gm_l10;
                                    uint64_t t70 = gm_l9;
                                    uint8_t t71 = (t69 < t70);
                                    if (!t71) goto lb68;
                                    {
                                        uint64_t t74 = gm_l10;
                                        uint64_t t75 = t74;
                                        uint64_t t78 = GM_LD(uint64_t, ((uint8_t *)&t75));
                                        gm_l11 = t78;
                                        {
                                            uint64_t t80 = gm_l11;
                                            uint64_t t81 = (uintptr_t)t80;
                                            void * t82 = gm_l6;
                                            uint64_t t83 = GM_LD(uintptr_t, (((uint8_t *)t82) + 0) + 8);
                                            if (t81 >= (uintptr_t)(t83)) gm_trap_index(t81, t83, gm_srcpit8mdn0781q, 286, 44);
                                            uint64_t t84 = gm_l11;
                                            uint64_t t85 = (uintptr_t)t84;
                                            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                            if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_srcpit8mdn0781q, 286, 57);
                                            uint8_t t87 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t85 * 1));
                                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t82) + 0)) + t81 * 1), t87);
                                        }
                                        goto me76;
mn77:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 286, 21);
me76:;
                                    }
lc68:;
                                    {
                                        uint64_t t89 = gm_l10;
                                        uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                                        uint64_t t92 = r90;
                                        gm_l10 = t92;
                                    }
                                }
lb68:;
                            }
                        }
                        {
                            void * t93 = gm_l6;
                            void * t94 = gm_l4;
                            uint64_t t95 = GM_LD(uintptr_t, (((uint8_t *)t94) + 0) + 8);
                            gm_f_truncate_ov4pgoq(t93, t95);
                        }
                        goto ix43;
ie43:;
ix43:;
                    }
                    gm_b0_1 t96;
                    uint8_t t97 = 0;
                    F.ret = t96;
                    goto gm_fret;
                }
                gm_l12 = t40;
            }
            {
                uint64_t t98 = gm_l5;
                uint64_t t99 = (uintptr_t)t98;
                void * t100 = gm_l4;
                if (t99 >= (uintptr_t)(2)) gm_trap_index(t99, 2, gm_srcpit8mdn0781q, 291, 13);
                GM_ST(uint8_t, ((((uint8_t *)t100) + 16) + t99 * 1), ((uint8_t)0));
            }
            {
                void * t101 = gm_l6;
                uint64_t t102 = GM_LD(uintptr_t, (((uint8_t *)t101) + 0) + 8);
                void * t103 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t101) + 0)));
                uint64_t t104 = gm_l12;
                void * t105 = gm_l4;
                uint64_t t106 = GM_LD(uintptr_t, (((uint8_t *)t105) + 0) + 8);
                uint64_t r107; bool o108 = __builtin_add_overflow(t104, t106, &r107);
                if (o108) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 292, 29);
                uint64_t t109 = r107;
                uint64_t t110 = (uintptr_t)t109;
                void * t111 = gm_l6;
                uint64_t t112 = GM_LD(uintptr_t, (((uint8_t *)t111) + 0) + 8);
                uint64_t t113 = (uintptr_t)t112;
                if (t110 > t113 || t113 > (uintptr_t)t102) gm_trap_slice(t110, t113, t102, gm_srcpit8mdn0781q, 292, 21);
                gm_b16_8 t114;
                GM_ST(void *, (uint8_t *)&t114, (void *)(((uint8_t *)t103 + t110 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(t113 - t110));
                gm_b16_8 t115 = t114;
                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                gm_l3 = t116;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t119 = ((t118 == ((uint64_t)0ULL)));
        if (!t119) goto ie117;
        {
            gm_b0_1 t121;
            uint8_t t122 = 0;
            F.ret = t121;
            goto gm_fret;
        }
        goto ix117;
ie117:;
ix117:;
    }
    {
        uint32_t t124 = gm_l1;
        uint8_t t125 = ((t124 == ((uint32_t)2ULL)));
        if (!t125) goto ie123;
        {
            gm_b16_8 t127 = gm_l3;
            gm_pf0nmuupvugn(cx, t127, gm_srcpit8mdn0781q, 296, 18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix123;
ie123:;
        {
            gm_b16_8 t129 = gm_l3;
            gm_pfmaojsuds6a(cx, t129, gm_srcpit8mdn0781q, 296, 44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix123:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_int_status_h8a0nf0(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1) {
    struct gm_fr_int_status_h8a0nf0 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_exit_status_ckbk1e1 *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t4 = gm_l0;
        gm_tg5lql6itufk7 t5 = gm_f_int_info_ulcevdv(t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tg5lql6itufk7 t6 = t5;
        uint8_t t7;
        if (((((uint8_t *)&t6))[8] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 514, 12);
me8:;
        if (!t7) goto ie2;
        {
            uint8_t t12 = 0;
            F.ret = ((uint8_t)0ULL);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_exit_status_ckbk1e1 *)((&F.h)->up))->l0));
        gm_b16_8 t14 = gm_l1;
        unsigned __int128 t15 = gm_f_get_uint_bq8c4b7(t13, t14);
        uint8_t t16 = (uint8_t)t15;
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_location_dpep111(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_location_dpep111 F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* f */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tl874t2atnlh3 t5 = t3;
        gm_tl874t2atnlh3 t6 = t4;
        uint8_t t7 = (gm_eqff85ac10o3(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            gm_th2lqbo77pvsc t9;
            memset(&t9, 0, sizeof t9);
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t16 = gm_f_file_o15fqku(t14, t15);
        gm_l2 = t16;
    }
    {
        void * t17 = gm_l2;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_tcc1rq4n1avb2 t19 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t17, t18));
        gm_l3 = t19;
    }
    {
        gm_t5hdnnftkup6m t20;
        void * t21 = gm_l2;
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t22);
        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
        GM_ST(uint32_t, (uint8_t *)&t20 + 16, t23);
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        GM_ST(uint32_t, (uint8_t *)&t20 + 20, t24);
        gm_tabb710d9mq2e t25;
        gm_f_format_45lt1qt((void *)&t25, (void *)NULL, cx, NULL, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcpit8mdn0781q, 570, 12);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_th2lqbo77pvsc t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
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
__attribute__((aligned(4))) void gm_f_machine_84ep32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2) {
    struct gm_fr_machine_84ep32o F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    gm_b16_8 gm_l2; /* args */
    void * gm_l3; /* s */
    uint64_t gm_l4; /* code_end */
    uint64_t gm_l5; /* data_base */
    void * gm_l6; /* <tmp> */
    void * gm_l7; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l9; /* <tmp> */
    uint8_t gm_l10; /* <tmp> */
    uint8_t gm_l11; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint32_t gm_l19; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint32_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    GM_STACK_CHECK(&F, 866u);
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
        uint64_t t4 = GM_LD(uint64_t, gm_cmiriali67r);
        void * t5 = gm_l3;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        uint64_t t7 = (uint64_t)t6;
        uint64_t r8; bool o9 = __builtin_add_overflow(t7, ((uint64_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 307, 35);
        uint64_t t10 = r8;
        uint64_t r11; bool o12 = __builtin_mul_overflow(((uint64_t)16ULL), t10, &r11);
        if (o12) gm_trap("integer overflow in `*`", gm_srcpit8mdn0781q, 307, 29);
        uint64_t t13 = r11;
        uint64_t r14; bool o15 = __builtin_add_overflow(t4, t13, &r14);
        if (o15) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 307, 17);
        uint64_t t16 = r14;
        gm_l4 = t16;
    }
    {
        uint64_t t17 = gm_l4;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)65536ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 308, 19);
        uint64_t t20 = r18;
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)65535ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 308, 19);
        uint64_t t23 = r21;
        if (((uint64_t)65536ULL) == 0) gm_trap("division by zero", gm_srcpit8mdn0781q, 308, 18);
        uint64_t t24 = (uint64_t)(t23 / ((uint64_t)65536ULL));
        uint64_t r25; bool o26 = __builtin_mul_overflow(t24, ((uint64_t)65536ULL), &r25);
        if (o26) gm_trap("integer overflow in `*`", gm_srcpit8mdn0781q, 308, 18);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        {
            void * t29 = gm_l0;
            gm_l6 = t29;
        }
        {
            void * t30 = gm_l3;
            gm_l7 = t30;
        }
        {
            void * t31 = gm_l3;
            void * t32 = gm_l0;
            gm_f_layouts_au8nfbt((void *)&F.l8, t31, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t32) + 80)));
        }
        {
            gm_t9qkh0rsooc8d t33 = gm_l1;
            gm_l9 = t33;
        }
        {
            void * t34 = gm_l0;
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t34) + 80) + 8));
            gm_l10 = t35;
        }
        {
            uint8_t t36 = 0;
            void * t37 = gm_l0;
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t37) + 80) + 8));
            if (t38) {
                uint8_t t39 = gm_f_host_little_m7un29s();
                t36 = t39;
            } else t36 = 0;
            gm_l11 = t36;
        }
        {
            gm_f_list_go2qg0b((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_go2qg0b((void *)&F.l13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t40 = gm_l0;
            uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t40) + 80) + 0));
            gm_l14 = t41;
        }
        {
            uint64_t t42 = gm_l5;
            void * t43 = gm_l0;
            uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)t43) + 80) + 0));
            gm_f_memory_dqom95e((void *)&F.l15, (void *)NULL, cx, NULL, t42, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_fufbgf7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l16 + 0)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&F.l16 + 40, ((uint64_t)0ULL));
            GM_ST(uint64_t, (uint8_t *)&F.l16 + 48, ((uint64_t)0ULL));
        }
        {
            gm_f_list_kl0t9m0((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_jofrtqr((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l19 = ((uint32_t)0ULL);
        }
        {
            gm_f_list_kl0t9m0((void *)&F.l20, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_fufbgf7((void *)&F.l21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_go2qg0b((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_rif31rv((void *)&F.l23, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_go2qg0b((void *)&F.l24, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_0grrn1c((void *)&F.l25, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l26 = ((uint64_t)1ULL);
        }
        {
            gm_l27 = ((uint32_t)0ULL);
        }
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            gm_f_hash_map_4fb6o1f((void *)&F.l29, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_4fb6o1f((void *)&F.l30, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_l2188m6((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_kl0t9m0((void *)&F.l32, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_hash_map_gd2q5f3((void *)&F.l33, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t45 = gm_l2;
            gm_l34 = t45;
        }
        {
            gm_f_fibers_i2ha6hs((void *)&F.l35, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t46 = (uint64_t)~((uint64_t)0ULL);
            gm_l36 = t46;
        }
        {
            gm_f_hash_map_a2hplai((void *)&F.l37, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_sys_state_cvl1lvl((void *)&F.l38, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_threads_kh5dldj((void *)&F.l39, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        memset((uint8_t *)&F.l40 + 91, 0, 5);
        memset((uint8_t *)&F.l40 + 548, 0, 4);
        memset((uint8_t *)&F.l40 + 764, 0, 4);
        void * t47 = gm_l6;
        GM_ST(void *, (uint8_t *)&F.l40 + 0, t47);
        void * t48 = gm_l7;
        GM_ST(void *, (uint8_t *)&F.l40 + 8, t48);
        memcpy((uint8_t *)&F.l40 + 16, ((uint8_t *)&F.l8), 72);
        gm_t9qkh0rsooc8d t49 = gm_l9;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l40 + 88, t49);
        uint8_t t50 = gm_l10;
        GM_ST(uint8_t, (uint8_t *)&F.l40 + 89, t50);
        uint8_t t51 = gm_l11;
        GM_ST(uint8_t, (uint8_t *)&F.l40 + 90, t51);
        memcpy((uint8_t *)&F.l40 + 96, ((uint8_t *)&F.l12), 40);
        memcpy((uint8_t *)&F.l40 + 136, ((uint8_t *)&F.l13), 40);
        uint64_t t52 = gm_l14;
        GM_ST(uint64_t, (uint8_t *)&F.l40 + 176, t52);
        memcpy((uint8_t *)&F.l40 + 184, ((uint8_t *)&F.l15), 176);
        memcpy((uint8_t *)&F.l40 + 360, ((uint8_t *)&F.l16), 56);
        memcpy((uint8_t *)&F.l40 + 416, ((uint8_t *)&F.l17), 40);
        memcpy((uint8_t *)&F.l40 + 456, ((uint8_t *)&F.l18), 40);
        memcpy((uint8_t *)&F.l40 + 496, ((uint8_t *)&F.l20), 40);
        GM_ST(void *, (uint8_t *)&F.l40 + 536, ((void *)0));
        uint32_t t53 = gm_l19;
        GM_ST(uint32_t, (uint8_t *)&F.l40 + 544, t53);
        memcpy((uint8_t *)&F.l40 + 552, ((uint8_t *)&F.l21), 40);
        memcpy((uint8_t *)&F.l40 + 592, ((uint8_t *)&F.l22), 40);
        memcpy((uint8_t *)&F.l40 + 632, ((uint8_t *)&F.l23), 40);
        memcpy((uint8_t *)&F.l40 + 672, ((uint8_t *)&F.l24), 40);
        memcpy((uint8_t *)&F.l40 + 712, ((uint8_t *)&F.l25), 40);
        uint64_t t54 = gm_l26;
        GM_ST(uint64_t, (uint8_t *)&F.l40 + 752, t54);
        uint32_t t55 = gm_l27;
        GM_ST(uint32_t, (uint8_t *)&F.l40 + 760, t55);
        uint64_t t56 = gm_l28;
        GM_ST(uint64_t, (uint8_t *)&F.l40 + 768, t56);
        memcpy((uint8_t *)&F.l40 + 776, ((uint8_t *)&F.l29), 56);
        memcpy((uint8_t *)&F.l40 + 832, ((uint8_t *)&F.l30), 56);
        memcpy((uint8_t *)&F.l40 + 888, ((uint8_t *)&F.l31), 56);
        memcpy((uint8_t *)&F.l40 + 944, ((uint8_t *)&F.l32), 40);
        memcpy((uint8_t *)&F.l40 + 984, ((uint8_t *)&F.l33), 56);
        gm_b16_8 t57 = gm_l34;
        GM_ST(gm_b16_8, (uint8_t *)&F.l40 + 1040, t57);
        gm_th2lqbo77pvsc t58;
        memset(&t58, 0, sizeof t58);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l40 + 1056, t58);
        memcpy((uint8_t *)&F.l40 + 1080, ((uint8_t *)&F.l35), 200);
        uint64_t t59 = gm_l36;
        GM_ST(uint64_t, (uint8_t *)&F.l40 + 1280, t59);
        memcpy((uint8_t *)&F.l40 + 1288, ((uint8_t *)&F.l37), 56);
        memcpy((uint8_t *)&F.l40 + 1344, ((uint8_t *)&F.l38), 304);
        memcpy((uint8_t *)&F.l40 + 1648, ((uint8_t *)&F.l39), 72);
        GM_ST(void *, (uint8_t *)&F.l40 + 1720, ((void *)0));
    }
    {
        void * t60 = (void *)((((uint8_t *)&F.l40) + 360) + 0);
        uint64_t t61 = GM_LD(uint64_t, gm_cga7n0g3amu);
        gm_tabb710d9mq2e t62;
        gm_f_make_fill_jpvm0kc((void *)&t62, (void *)NULL, cx, NULL, t61, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kp74jj8lk9n)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcpit8mdn0781q, 348, 27);
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
        gm_tftb1odc739t5 t65 = gm_f_push_rluch9r(t60, t64);
        gm_tftb1odc739t5 t66 = t65;
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcpit8mdn0781q, 348, 5);
        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
    }
    {
        void * t68 = (void *)(((uint8_t *)&F.l40) + 632);
        gm_tv2ad0oo3igpi t69;
        memset((uint8_t *)&t69 + 4, 0, 4);
        memset((uint8_t *)&t69 + 92, 0, 4);
        GM_ST(uint32_t, (uint8_t *)&t69 + 0, ((uint32_t)0ULL));
        memset((uint8_t *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t69 + 8)) + 4, 0, 4);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t69 + 8)) + 0, ((uint32_t)0ULL));
        gm_b16_8 t70 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t71 = t70;
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t69 + 8)) + 8, t72);
        gm_t9qkh0rsooc8d t73;
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)0ULL));
        gm_tte9vgf6noic9 t74;
        memset(&t74, 0, sizeof t74);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t74 + 8), t73);
        GM_ST(gm_tte9vgf6noic9, (uint8_t *)&t69 + 32, t74);
        GM_ST(uint64_t, (uint8_t *)&t69 + 80, ((uint64_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t69 + 88, ((uint32_t)0ULL));
        GM_ST(void *, (uint8_t *)&t69 + 96, ((void *)0));
        gm_tftb1odc739t5 t75 = gm_f_push_7p5mg9v(t68, &t69);
        gm_tftb1odc739t5 t76 = t75;
        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcpit8mdn0781q, 350, 5);
        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
    }
    {
        {
            void * t79 = gm_l3;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((((uint8_t *)t79) + 1448) + 0));
            gm_l41 = t80;
        }
        {
            gm_l42 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t82 = gm_l42;
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l42;
                    uint64_t t88 = (uintptr_t)t87;
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                    {
                        void * t93 = (void *)(((uint8_t *)&F.l40) + 944);
                        gm_tftb1odc739t5 t94 = gm_f_push_7o6oubd(t93, ((uint64_t)0ULL));
                        gm_tftb1odc739t5 t95 = t94;
                        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srcpit8mdn0781q, 351, 32);
                        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                    }
                    goto me90;
mn91:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 351, 5);
me90:;
                }
lc81:;
                {
                    uint64_t t98 = gm_l42;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l42 = t101;
                }
            }
lb81:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l40), 1728);
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
extern __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_name_oqt6ams(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_name_oqt6ams F;
    uint64_t gm_l0; /* i */
    gm_t9d4sf0vqn7dt gm_l1; /* c */
    GM_STACK_CHECK(&F, 261u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_trap_report_2rjo4a6 *)((&F.h)->up))->l1));
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 32) + 0));
        gm_b16_8 t4 = t3;
        void * t5 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t4)));
        uint64_t t6 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t t7 = gm_l0;
        uint64_t t8 = (uintptr_t)t7;
        gm_tok0o3qpepqqf t9;
        if (t8 < t6) {
        gm_t9d4sf0vqn7dt t10 = GM_LD(gm_t9d4sf0vqn7dt, ((uint8_t *)t5 + t8 * 16));
        gm_tok0o3qpepqqf t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
        GM_ST(gm_t9d4sf0vqn7dt, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[16] = 1;
            t9 = t11;
        } else {
        gm_tok0o3qpepqqf t12;
        memset(&t12, 0, sizeof t12);
            t9 = t12;
        }
        gm_tok0o3qpepqqf t13 = t9;
        gm_t9d4sf0vqn7dt t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_t9d4sf0vqn7dt t15 = GM_LD(gm_t9d4sf0vqn7dt, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_th2lqbo77pvsc t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l1 = t14;
    }
    {
        void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_trap_report_2rjo4a6 *)((&F.h)->up))->l0));
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_f_backtrace_name_mars429((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t18, t19);
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
extern __attribute__((aligned(4))) void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) gm_tljvbvj59adij gm_f_run_entry_jfn1dvn(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_run_entry_jfn1dvn F;
    gm_t9qkh0rsooc8d gm_l2; /* mode */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* filter */
    gm_closure gm_l7; /* run */
    gm_tljvbvj59adij gm_l8; /* outcome */
    GM_STACK_CHECK(&F, 299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_t3mf58gv9kf3n t2 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_heap_allocator_gihki37());
        gm_t3mf58gv9kf3n t3 = t2;
        gm_node wn4 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t3, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        {
            void * t8 = F.l0;
            gm_t9qkh0rsooc8d t9 = gm_l2;
            gm_b16_8 t10 = gm_l3;
            gm_f_machine_84ep32o((void *)&F.l5, (void *)NULL, (&wn4), NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_ll6;
            }
        }
        {
            void * t11 = gm_l4;
            GM_ST(void *, (((uint8_t *)&F.l5) + 1720), t11);
        }
        {
            void * t12 = (void *)((uint8_t *)&F.l5);
            F.l6 = t12;
        }
        {
            gm_closure t13 = ((gm_closure){(void *)gm_f_run_pgu7dng, (void *)&F.h, (&wn4)});
            gm_l7 = t13;
        }
        {
            void * t14 = F.l6;
            gm_closure t15 = gm_l7;
            gm_tljvbvj59adij t16 = gm_f_run_coroutines_0a7hn79((void *)NULL, (&wn4), NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_ll6;
            }
            gm_l8 = t16;
        }
        {
            void * t17 = F.l6;
            gm_b0_1 t18 = gm_f_deinit_550ph9b((void *)NULL, (&wn4), NULL, t17);
            if (gm_unw.kind) {
                goto gm_ll6;
            }
        }
        gm_tljvbvj59adij t19 = gm_l8;
        gm_tljvbvj59adij t20 = t19;
        F.w0 = t20;
        goto gm_ls21;
gm_ll6:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we5; }
        goto gm_lad;
gm_ls21:;
we5:;
        gm_tljvbvj59adij t22 = F.w0;
        uint8_t t23 = 0;
        F.ret = t22;
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
__attribute__((aligned(4))) gm_tljvbvj59adij gm_f_run_pgu7dng(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_run_pgu7dng F;
    uint32_t gm_l0; /* cx */
    gm_tk8ebsa8coi4p gm_l1; /* a */
    uint32_t gm_l2; /* e */
    uint32_t gm_l3; /* e */
    uint32_t gm_l4; /* e */
    gm_t88rcucg1b7uk gm_l5; /* ret_ty */
    gm_b16_8 gm_l6; /* ret */
    uint8_t gm_l7; /* code */
    void * gm_l8; /* t */
    GM_STACK_CHECK(&F, 1102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_l0 = ((uint32_t)0ULL);
    }
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l0));
        gm_tk8ebsa8coi4p t3 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t2) + 48));
        gm_l1 = t3;
    }
    {
        if (!(((((uint8_t *)&gm_l1) + 0))[4] != 0)) goto ie4;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_l2 = t5;
        {
            void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
            uint32_t t8 = gm_l0;
            gm_tiilmfd16v3fc t9;
            memset((uint8_t *)&t9 + 4, 0, 4);
            uint32_t t10 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t9 + 0, t10);
            gm_b16_8 t11 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 8, t13);
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tte9vgf6noic9 t15;
            memset(&t15, 0, sizeof t15);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 8), t14);
            uint32_t t16 = gm_f_push_ctx_1rv0jgf((void *)NULL, cx, NULL, t7, t8, &t9, &t15, ((uint64_t)0ULL), ((uint32_t)0ULL), ((void *)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l0 = t16;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        if (!(((((uint8_t *)&gm_l1) + 8))[4] != 0)) goto ie17;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
        gm_l3 = t18;
        {
            void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
            uint32_t t21 = gm_l0;
            gm_tiilmfd16v3fc t22;
            memset((uint8_t *)&t22 + 4, 0, 4);
            uint32_t t23 = gm_l3;
            GM_ST(uint32_t, (uint8_t *)&t22 + 0, t23);
            gm_b16_8 t24 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            GM_ST(gm_b16_8, (uint8_t *)&t22 + 8, t26);
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)1ULL));
            gm_tte9vgf6noic9 t28;
            memset(&t28, 0, sizeof t28);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 8), t27);
            uint32_t t29 = gm_f_push_ctx_1rv0jgf((void *)NULL, cx, NULL, t20, t21, &t22, &t28, ((uint64_t)0ULL), ((uint32_t)0ULL), ((void *)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l0 = t29;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        if (!(((((uint8_t *)&gm_l1) + 16))[4] != 0)) goto ie30;
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 16));
        gm_l4 = t31;
        {
            void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
            uint32_t t34 = gm_l0;
            gm_tiilmfd16v3fc t35;
            memset((uint8_t *)&t35 + 4, 0, 4);
            uint32_t t36 = gm_l4;
            GM_ST(uint32_t, (uint8_t *)&t35 + 0, t36);
            gm_b16_8 t37 = gm_f_empty_slice_fh7svmj();
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            GM_ST(gm_b16_8, (uint8_t *)&t35 + 8, t39);
            gm_t9qkh0rsooc8d t40;
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)2ULL));
            gm_tte9vgf6noic9 t41;
            memset(&t41, 0, sizeof t41);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t41 + 8), t40);
            uint32_t t42 = gm_f_push_ctx_1rv0jgf((void *)NULL, cx, NULL, t33, t34, &t35, &t41, ((uint64_t)0ULL), ((uint32_t)0ULL), ((void *)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l0 = t42;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t43 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
        uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l1));
        void * t45 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 64));
        gm_l5 = t46;
    }
    {
        void * t47 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
        void * t48 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
        gm_t88rcucg1b7uk t49 = gm_l5;
        uint64_t t50 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t51 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t47, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t51;
    }
    {
        void * t52 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        uint32_t t54 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l1));
        gm_b16_8 t55 = gm_f_empty_slice_2p7d2kv();
        gm_b16_8 t56 = t55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        uint32_t t58 = gm_l0;
        gm_t5lf9i3n1r3co t59;
        memset(&t59, 0, sizeof t59);
        gm_b16_8 t60 = gm_l6;
        gm_tcfa6l7reoosn t61;
        gm_f_call_function_u7ovth6((void *)&t61, (void *)NULL, cx, NULL, t52, t53, t54, t57, ((void *)0), t58, &t59, ((uint8_t)0), t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t62 = t61;
        gm_tljvbvj59adij t63;
        if (!((((uint8_t *)&t62))[0] != 0)) goto mn65;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)&t62) + 8) + 0));
        if (t66 != ((uint8_t)3ULL)) goto mn65;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&t62) + 8) + 8));
        gm_l7 = t67;
        uint8_t t68 = gm_l7;
        gm_tljvbvj59adij t69;
        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t69 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 12, 0);
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t69 + 8), t68);
        t63 = t69;
        goto me64;
mn65:;
        if (!((((uint8_t *)&t62))[0] != 0)) goto mn70;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&t62) + 8) + 0));
        if (t71 != ((uint8_t)4ULL)) goto mn70;
        void * t72 = GM_LD(void *, ((((uint8_t *)&t62) + 8) + 8));
        gm_l8 = t72;
        void * t73 = gm_l8;
        gm_tljvbvj59adij t74;
        GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)1ULL));
        GM_ST(void *, ((uint8_t *)&t74 + 8), t73);
        t63 = t74;
        goto me64;
mn70:;
        if (!((((uint8_t *)&t62))[0] != 0)) goto mn75;
        gm_tolm6p7jp1hb3 t76;
        memset((uint8_t *)&t76 + 28, 0, 4);
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sps94d2dd3g7k));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(43));
        GM_ST(gm_b16_8, (uint8_t *)&t76 + 0, t77);
        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t76 + 16, t78);
        gm_f_list_j8jr9cr_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t76 + 32)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t79 = gm_f_new_jd11mvt((void *)NULL, cx, NULL, &t76, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2bf31gf1v3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t80 = t79;
        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_srcpit8mdn0781q, 467, 29);
        void * t81 = GM_LD(void *, (((uint8_t *)&t80) + 8));
        gm_tljvbvj59adij t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)1ULL));
        GM_ST(void *, ((uint8_t *)&t82 + 8), t81);
        t63 = t82;
        goto me64;
mn75:;
        void * t84 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_run_entry_jfn1dvn *)((&F.h)->up))->l6));
        gm_t88rcucg1b7uk t85 = gm_l5;
        gm_b16_8 t86 = gm_l6;
        uint8_t t87 = gm_f_exit_status_ckbk1e1((void *)NULL, cx, NULL, t84, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tljvbvj59adij t88;
        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t88 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 12, 0);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t88 + 8), t87);
        t63 = t88;
        goto me64;
mn83:;
        gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 464, 20);
me64:;
        uint8_t t89 = 0;
        F.ret = t63;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_next_dalgo1f F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* top */
    gm_b16_8 gm_l5; /* chunk */
    uint64_t gm_l6; /* base */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 978u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_f_empty_slice_ijtp9lo();
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
        void * t9 = (void *)(((uint8_t *)t8) + 360);
        gm_l2 = t9;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_f_us_cuv01fp(t10);
        gm_l3 = t11;
    }
    {
        void * t12 = gm_l2;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 48));
        gm_l4 = t13;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb14;
            {
                void * t17 = gm_l2;
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 40));
                uint64_t t19 = (uintptr_t)t18;
                void * t20 = gm_l2;
                uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 0) + 0) + 8);
                if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcpit8mdn0781q, 402, 18);
                gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 0) + 0)) + t19 * 16));
                gm_l5 = t22;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t24 = (uintptr_t)t23;
                gm_l6 = t24;
            }
            {
                uint64_t t25 = gm_l6;
                uint64_t t26 = gm_l4;
                uint64_t r27; bool o28 = __builtin_add_overflow(t25, t26, &r27);
                if (o28) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 406, 17);
                uint64_t t29 = r27;
                uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)7ULL), &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 406, 17);
                uint64_t t32 = r30;
                uint64_t t33 = gm_f_us_cuv01fp(((uint64_t)7ULL));
                uint64_t t34 = (uint64_t)~t33;
                uint64_t t35 = (uint64_t)(t32 & t34);
                uint64_t t36 = gm_l6;
                uint64_t r37; bool o38 = __builtin_sub_overflow(t35, t36, &r37);
                if (o38) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 406, 15);
                uint64_t t39 = r37;
                gm_l4 = t39;
            }
            {
                uint64_t t41 = gm_l4;
                uint64_t t42 = gm_l3;
                uint64_t r43; bool o44 = __builtin_add_overflow(t41, t42, &r43);
                if (o44) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 407, 12);
                uint64_t t45 = r43;
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t47 = (t45 <= t46);
                if (!t47) goto ie40;
                {
                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    void * t50 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
                    uint64_t t51 = gm_l4;
                    uint64_t t52 = (uintptr_t)t51;
                    uint64_t t53 = gm_l4;
                    uint64_t t54 = gm_l3;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t53, t54, &r55);
                    if (o56) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 408, 29);
                    uint64_t t57 = r55;
                    uint64_t t58 = (uintptr_t)t57;
                    if (t52 > t58 || t58 > (uintptr_t)t49) gm_trap_slice(t52, t58, t49, gm_srcpit8mdn0781q, 408, 18);
                    gm_b16_8 t59;
                    GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t50 + t52 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58 - t52));
                    gm_l7 = t59;
                }
                {
                    void * t60 = gm_l2;
                    uint64_t t61 = gm_l4;
                    uint64_t t62 = gm_l3;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t61, t62, &r63);
                    if (o64) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 409, 22);
                    uint64_t t65 = r63;
                    GM_ST(uint64_t, (((uint8_t *)t60) + 48), t65);
                }
                {
                    gm_b16_8 t66 = gm_l7;
                    gm_f_fill_abmf5g5(t66, ((uint8_t)0ULL));
                }
                {
                    gm_b16_8 t67 = gm_l7;
                    uint8_t t68 = 0;
                    F.ret = t67;
                    goto gm_fret;
                }
                goto ix40;
ie40:;
ix40:;
            }
            {
                void * t69 = gm_l2;
                void * t70 = gm_l2;
                uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t70) + 40));
                uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                if (o73) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 413, 9);
                uint64_t t74 = r72;
                GM_ST(uint64_t, (((uint8_t *)t69) + 40), t74);
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                void * t76 = gm_l2;
                uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)t76) + 40));
                void * t78 = gm_l2;
                uint64_t t79 = GM_LD(uintptr_t, ((((uint8_t *)t78) + 0) + 0) + 8);
                uint8_t t80 = ((t77 == t79));
                if (!t80) goto ie75;
                {
                    void * t82 = gm_l2;
                    void * t83 = (void *)(((uint8_t *)t82) + 0);
                    uint64_t t84 = gm_l3;
                    uint64_t t85 = GM_LD(uint64_t, gm_cga7n0g3amu);
                    uint64_t t86 = (t84 > t85 ? t84 : t85);
                    uint64_t r87; bool o88 = __builtin_add_overflow(t86, ((uint64_t)8ULL), &r87);
                    if (o88) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 416, 38);
                    uint64_t t89 = r87;
                    gm_tabb710d9mq2e t90;
                    gm_f_make_fill_jpvm0kc((void *)&t90, (void *)NULL, cx, NULL, t89, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3q4at3rjhc)));
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                    gm_tabb710d9mq2e t91 = t90;
                    if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_srcpit8mdn0781q, 416, 28);
                    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
                    gm_tftb1odc739t5 t93 = gm_f_push_rluch9r(t83, t92);
                    gm_tftb1odc739t5 t94 = t93;
                    if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcpit8mdn0781q, 416, 13);
                    gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                }
                goto ix75;
ie75:;
                void * t97 = gm_l2;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)t97) + 40));
                uint64_t t99 = (uintptr_t)t98;
                void * t100 = gm_l2;
                uint64_t t101 = GM_LD(uintptr_t, ((((uint8_t *)t100) + 0) + 0) + 8);
                if (t99 >= (uintptr_t)(t101)) gm_trap_index(t99, t101, gm_srcpit8mdn0781q, 417, 19);
                uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t100) + 0) + 0)) + t99 * 16) + 8);
                uint64_t t103 = gm_l3;
                uint8_t t104 = (t102 < t103);
                if (!t104) goto ie96;
                {
                    void * t106 = gm_l2;
                    uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)t106) + 40));
                    uint64_t t108 = (uintptr_t)t107;
                    void * t109 = gm_l2;
                    uint64_t t110 = GM_LD(uintptr_t, ((((uint8_t *)t109) + 0) + 0) + 8);
                    if (t108 >= (uintptr_t)(t110)) gm_trap_index(t108, t110, gm_srcpit8mdn0781q, 419, 18);
                    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t109) + 0) + 0)) + t108 * 16));
                    gm_b16_8 t112 = t111;
                    gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                    gm_b0_1 t114 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t113, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kg5vb7jesk1)));
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                }
                {
                    void * t115 = gm_l2;
                    uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)t115) + 40));
                    uint64_t t117 = (uintptr_t)t116;
                    void * t118 = gm_l2;
                    uint64_t t119 = GM_LD(uintptr_t, ((((uint8_t *)t118) + 0) + 0) + 8);
                    if (t117 >= (uintptr_t)(t119)) gm_trap_index(t117, t119, gm_srcpit8mdn0781q, 420, 13);
                    uint8_t *t120 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t118) + 0) + 0)) + t117 * 16);
                    uint64_t t121 = gm_l3;
                    uint64_t t122 = GM_LD(uint64_t, gm_cga7n0g3amu);
                    uint64_t t123 = (t121 > t122 ? t121 : t122);
                    uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)8ULL), &r124);
                    if (o125) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 420, 49);
                    uint64_t t126 = r124;
                    gm_tabb710d9mq2e t127;
                    gm_f_make_fill_jpvm0kc((void *)&t127, (void *)NULL, cx, NULL, t126, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7dtk1q8l8e)));
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                    gm_tabb710d9mq2e t128 = t127;
                    if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_srcpit8mdn0781q, 420, 39);
                    gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
                    GM_ST(gm_b16_8, t120, t129);
                }
                goto ix96;
ie96:;
ix96:;
ix75:;
            }
lc14:;
        }
gm_ll15:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
        }
        goto gm_lad;
lb14:;
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
inline __attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_trap_qh84l9d F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tolm6p7jp1hb3 t1;
    memset((uint8_t *)&t1 + 28, 0, 4);
    gm_b16_8 t2 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    gm_tl874t2atnlh3 t3 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 16, t3);
    gm_f_list_j8jr9cr_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t1 + 32)), (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t4 = gm_f_new_jd11mvt((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6od4kv6k1l)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcpit8mdn0781q, 429, 96);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
    gm_t76123gfet73g t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)4ULL));
    GM_ST(void *, ((uint8_t *)&t7 + 8), t6);
    F.ret = t7;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_trap_report_2rjo4a6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_trap_report_2rjo4a6 F;
    gm_closure gm_l3; /* name */
    gm_b16_8 gm_l4; /* at */
    gm_b16_8 gm_l5; /* n */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* at */
    gm_b16_8 gm_l11; /* n */
    uint64_t gm_l12; /* max */
    uint64_t gm_l13; /* n */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    gm_b16_8 gm_l17; /* line */
    GM_STACK_CHECK(&F, 1549u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        void * t3 = F.l1;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tftb1odc739t5 t6 = gm_f_write_cdouvhd(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcpit8mdn0781q, 534, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        gm_closure t9 = ((gm_closure){(void *)gm_f_name_oqt6ams, (void *)&F.h, cx});
        gm_l3 = t9;
    }
    {
        void * t11 = F.l0;
        void * t12 = F.l1;
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 16));
        gm_th2lqbo77pvsc t14;
        gm_f_location_dpep111((void *)&t14, (void *)NULL, cx, NULL, t11, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t15 = t14;
        if (!((((uint8_t *)&t15))[16] != 0)) goto ie10;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_l4 = t16;
        {
            void * t18 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t19 = gm_l4;
            gm_tnpifv20irnph t20;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
            gm_tftb1odc739t5 t21 = gm_f_write_lgac4p5(t18, t20);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcpit8mdn0781q, 540, 9);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        {
            gm_closure t25 = gm_l3;
            gm_closure t26 = t25;
            gm_node *ccx27 = t26.cx;
            void * t28 = t26.code;
            gm_th2lqbo77pvsc t29;
            if (GM_PTR_TAGGED(t28)) {
                t29 = ((gm_th2lqbo77pvsc (*)(uint64_t))GM_PTR_UNTAG(t28))(((uint64_t)0ULL));
            } else {
                ((void (*)(void *, void *, gm_node *, gm_node *, uint64_t))t28)((void *)&t29, t26.env, ccx27, NULL, ((uint64_t)0ULL));
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t30 = t29;
            if (!((((uint8_t *)&t30))[16] != 0)) goto ie24;
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            gm_l5 = t31;
            {
                void * t33 = (void *)((uint8_t *)&F.l2);
                gm_b16_8 t34 = gm_l5;
                gm_tnpifv20irnph t35;
                GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
                gm_tftb1odc739t5 t36 = gm_f_write_73bbpvi(t33, t35);
                gm_tftb1odc739t5 t37 = t36;
                if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcpit8mdn0781q, 541, 27);
                gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
            }
            goto ix24;
ie24:;
ix24:;
        }
        {
            void * t39 = (void *)((uint8_t *)&F.l2);
            gm_tftb1odc739t5 t40 = gm_f_push_t83rg2m(t39, ((uint8_t)10ULL));
            gm_tftb1odc739t5 t41 = t40;
            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcpit8mdn0781q, 542, 9);
            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t44 = F.l1;
            uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 32) + 0) + 8);
            gm_l7 = t45;
        }
        {
            for (;;) {
                uint64_t t47 = gm_l8;
                uint64_t t48 = gm_l7;
                uint8_t t49 = (t47 < t48);
                if (!t49) goto lb46;
                {
                    uint64_t t52 = gm_l8;
                    uint64_t t53 = t52;
                    uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&t53));
                    gm_l9 = t56;
                    {
                        void * t58 = F.l0;
                        uint64_t t59 = gm_l9;
                        uint64_t t60 = (uintptr_t)t59;
                        void * t61 = F.l1;
                        uint64_t t62 = GM_LD(uintptr_t, ((((uint8_t *)t61) + 32) + 0) + 8);
                        if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_srcpit8mdn0781q, 547, 27);
                        gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t61) + 32) + 0)) + t60 * 16) + 4));
                        gm_th2lqbo77pvsc t64;
                        gm_f_location_dpep111((void *)&t64, (void *)NULL, cx, NULL, t58, t63);
                        if (gm_unw.kind) {
                            goto gm_ll50;
                        }
                        gm_th2lqbo77pvsc t65 = t64;
                        gm_b16_8 t66;
                        if (((((uint8_t *)&t65))[16] != 0)) {
                            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 0));
                            t66 = t67;
                        } else {
                            goto lc46;
                        }
                        gm_l10 = t66;
                    }
                    {
                        gm_closure t68 = gm_l3;
                        uint64_t t69 = gm_l9;
                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                        if (o71) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 548, 19);
                        uint64_t t72 = r70;
                        gm_closure t73 = t68;
                        gm_node *ccx74 = t73.cx;
                        void * t75 = t73.code;
                        gm_th2lqbo77pvsc t76;
                        if (GM_PTR_TAGGED(t75)) {
                            t76 = ((gm_th2lqbo77pvsc (*)(uint64_t))GM_PTR_UNTAG(t75))(t72);
                        } else {
                            ((void (*)(void *, void *, gm_node *, gm_node *, uint64_t))t75)((void *)&t76, t73.env, ccx74, NULL, t72);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll50;
                        }
                        gm_th2lqbo77pvsc t77 = t76;
                        gm_b16_8 t78;
                        if (((((uint8_t *)&t77))[16] != 0)) {
                            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 0));
                            t78 = t79;
                        } else {
                            goto lc46;
                        }
                        gm_l11 = t78;
                    }
                    {
                        void * t80 = (void *)((uint8_t *)&F.l6);
                        gm_t6j6smj7qoctu t81;
                        gm_b16_8 t82 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t81 + 0, t82);
                        gm_b16_8 t83 = gm_l11;
                        GM_ST(gm_b16_8, (uint8_t *)&t81 + 16, t83);
                        gm_tabb710d9mq2e t84;
                        gm_f_format_bt4apif((void *)&t84, (void *)NULL, cx, NULL, &t81);
                        if (gm_unw.kind) {
                            goto gm_ll50;
                        }
                        gm_tabb710d9mq2e t85 = t84;
                        if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srcpit8mdn0781q, 549, 22);
                        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
                        gm_b16_8 t87 = t86;
                        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                        gm_tftb1odc739t5 t89 = gm_f_push_3ef746b(t80, t88);
                        gm_tftb1odc739t5 t90 = t89;
                        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srcpit8mdn0781q, 549, 9);
                        gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 546, 5);
me54:;
                }
lc46:;
                {
                    uint64_t t93 = gm_l8;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l8 = t96;
                }
            }
gm_ll50:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
            }
            goto gm_lad;
lb46:;
        }
    }
    {
        gm_l12 = ((uint64_t)20ULL);
    }
    {
        uint64_t t97 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        gm_l13 = t97;
    }
    {
        {
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t100 = t99;
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            gm_l14 = t101;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t103 = gm_l15;
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t105 = (t103 < t104);
                if (!t105) goto lb102;
                {
                    gm_tf1bjg81u0glv t108;
                    uint64_t t109 = gm_l15;
                    GM_ST(uint64_t, (uint8_t *)&t108 + 0, t109);
                    uint64_t t110 = gm_l15;
                    uint64_t t111 = (uintptr_t)t110;
                    uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t111 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t108 + 8, t113);
                    gm_tf1bjg81u0glv t114 = t108;
                    uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&t114) + 0));
                    gm_l16 = t117;
                    gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                    gm_l17 = t118;
                    {
                        uint8_t t121 = 0;
                        uint8_t t122 = 0;
                        uint64_t t123 = gm_l13;
                        uint64_t t124 = gm_l12;
                        uint8_t t125 = (t123 <= t124);
                        if (!t125) {
                            uint64_t t126 = gm_l16;
                            uint64_t t127 = gm_l12;
                            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcpit8mdn0781q, 556, 28);
                            uint64_t t128 = (uint64_t)(t127 / ((uint64_t)2ULL));
                            uint8_t t129 = (t126 < t128);
                            t122 = t129;
                        } else t122 = 1;
                        if (!t122) {
                            uint64_t t130 = gm_l16;
                            uint64_t t131 = gm_l13;
                            uint64_t t132 = gm_l12;
                            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcpit8mdn0781q, 556, 48);
                            uint64_t t133 = (uint64_t)(t132 / ((uint64_t)2ULL));
                            uint64_t r134; bool o135 = __builtin_sub_overflow(t131, t133, &r134);
                            if (o135) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 556, 44);
                            uint64_t t136 = r134;
                            uint8_t t137 = (t130 >= t136);
                            t121 = t137;
                        } else t121 = 1;
                        if (!t121) goto ie120;
                        {
                            void * t139 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t140 = gm_l17;
                            gm_tftb1odc739t5 t141 = gm_f_extend_0cs85gs(t139, t140);
                            gm_tftb1odc739t5 t142 = t141;
                            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srcpit8mdn0781q, 557, 13);
                            gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
                        }
                        goto ix120;
ie120:;
                        uint64_t t145 = gm_l16;
                        uint64_t t146 = gm_l12;
                        if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcpit8mdn0781q, 558, 24);
                        uint64_t t147 = (uint64_t)(t146 / ((uint64_t)2ULL));
                        uint8_t t148 = ((t145 == t147));
                        if (!t148) goto ie144;
                        {
                            void * t150 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t151;
                            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sqh82hv8jvv9p));
                            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(17));
                            gm_tftb1odc739t5 t152 = gm_f_extend_0cs85gs(t150, t151);
                            gm_tftb1odc739t5 t153 = t152;
                            if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 1, gm_srcpit8mdn0781q, 559, 13);
                            gm_b0_1 t154 = GM_LD(gm_b0_1, (((uint8_t *)&t153) + 1));
                        }
                        goto ix144;
ie144:;
ix144:;
ix120:;
                    }
                    goto me115;
mn116:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpit8mdn0781q, 555, 5);
me115:;
                }
lc102:;
                {
                    uint64_t t156 = gm_l15;
                    uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                    uint64_t t159 = r157;
                    gm_l15 = t159;
                }
            }
lb102:;
        }
    }
    {
        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t161 = t160;
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
        uint8_t t163 = 0;
        F.ret = t162;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_46el73v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcpit8mdn0781q, 432, 107);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t9);
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
extern __attribute__((aligned(4))) uint64_t gm_f_us_cuv01fp(uint64_t a0);

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
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
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
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
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
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
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
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
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
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
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
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_7od16il {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hii1vte {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s6l2ctu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tslvjbf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_module_scope_011vo5a {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_vi643oe {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
struct gm_fr_is_effects_kind_uutbf9u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_leave_module_scope_4hp5mc4 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_4kh12ib {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
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
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_may_unify_0noeh56 {
    gm_frame h;
    gm_t53at80pfdus3 l3; /* ka */
    gm_t53at80pfdus3 l4; /* kb */
    gm_toa2vhe3l4hfr l54; /* p */
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9gv7k4m {
    gm_frame h;
    gm_t9qmk75lrp9p4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_b9c0fdf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_mi6l2kj {
    gm_frame h;
    gm_tvc83pqnepc2e l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_qkkb0em {
    gm_frame h;
    gm_tbeui4bbnm52b l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5mao3rk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8d45hjk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_a304ri6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ub69oip {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unscope_type_params_bofhhep {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void);
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vi643oe(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void gm_f_format_74vh99o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8h6eueo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_klj2nqm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lveqqdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mgpfvh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tv3i6qj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_handler_template_args_dlm07kb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_may_unify_0noeh56(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gv7k4m(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_b9c0fdf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mi6l2kj(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qkkb0em(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5mao3rk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8d45hjk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a304ri6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remove_76216q8(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_scope_type_params_2di4dm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_eref_kf9o1q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_template_instance_is_ovv1pcj(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_template_map_lfm02i0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_template_probe_is_d1ep4c6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgjkgsqdq34nu[37];
extern const uint8_t gm_sho5nuaja1vvf[5];
extern const uint8_t gm_shqja2jbdo2o0[58];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcgr2mvgis129a[];
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&t1));
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7od16il F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 96)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 224)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hii1vte F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s6l2ctu F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tslvjbf F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) void gm_f_enter_module_scope_011vo5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_module_scope_011vo5a F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 0, (((uint8_t *)t2) + 1568), 40);
        void * t3 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 40, (((uint8_t *)t3) + 1608), 40);
    }
    {
        void * t4 = gm_l0;
        uint8_t *t5 = (((uint8_t *)t4) + 1568);
        gm_f_list_1jabh22((void *)&(*(gm_trqfvmjtt4sij *)t5), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        uint8_t *t7 = (((uint8_t *)t6) + 1608);
        gm_f_list_0bj26n7((void *)&(*(gm_trqfvmjtt4sij *)t7), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 80);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vi643oe(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vi643oe F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_7sj41hq(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 56)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 56);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_51177of F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK(&F, 103u);
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
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)19ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l1 = t8;
        {
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_declare_handler_gd3gpcg *)((&F.h)->up))->l26);
            uint32_t t11 = gm_l1;
            gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t10, t11);
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcgr2mvgis129a, 57, 45);
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
inline __attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_is_uo88ron F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* name */
    gm_t08olq97rscn3 gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
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
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t12 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_effects_kind_uutbf9u F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            uint8_t t7 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l1) + 0) + 8) == 7) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l1) + 0)), gm_sgbumttiou4de, 7) == 0));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t10 = 0;
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
inline __attribute__((aligned(4))) void gm_f_leave_module_scope_4hp5mc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_module_scope_4hp5mc4 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 0));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 1568), t3);
    }
    {
        void * t4 = gm_l0;
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 40));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t4) + 1608), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_4kh12ib F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s6l2ctu();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8fnsr5h();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_r81t436();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_v0p8ic9(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tl874t2atnlh3 t21 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)t20 + t18 * 12), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 12)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9gv7k4m(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9gv7k4m F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_7od16il();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_a304ri6(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t9qmk75lrp9p4 t21 = GM_LD(gm_t9qmk75lrp9p4, ((uint8_t *)a1));
        GM_ST(gm_t9qmk75lrp9p4, ((uint8_t *)t20 + t18 * 96), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 96)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_b9c0fdf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_b9c0fdf F;
    void * gm_l0; /* l */
    gm_tm9oc6v1he136 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_tslvjbf();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ub69oip(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tm9oc6v1he136 t21 = GM_LD(gm_tm9oc6v1he136, ((uint8_t *)a1));
        GM_ST(gm_tm9oc6v1he136, ((uint8_t *)t20 + t18 * 32), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h69n5fi F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_8fnsr5h();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_1sqb3ir(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tplpbqf3duvn3 t21 = gm_l1;
        GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)t20 + t18 * 8), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_d914i5u();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_2onqfca(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint32_t t21 = gm_l1;
        GM_ST(uint32_t, ((uint8_t *)t20 + t18 * 4), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4iu2lk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t88rcucg1b7uk t21 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t20 + t18 * 4), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_jbu9fo3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8skjmj4(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mi6l2kj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_mi6l2kj F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_hii1vte();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8d45hjk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tvc83pqnepc2e t21 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)a1));
        GM_ST(gm_tvc83pqnepc2e, ((uint8_t *)t20 + t18 * 64), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 64)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_he774nl();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_7sj41hq(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t5v0c8ia6a5mo t21 = F.l1;
        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)t20 + t18 * 56), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qkkb0em(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_qkkb0em F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_s6l2ctu();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_5mao3rk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tbeui4bbnm52b t21 = GM_LD(gm_tbeui4bbnm52b, ((uint8_t *)a1));
        GM_ST(gm_tbeui4bbnm52b, ((uint8_t *)t20 + t18 * 48), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1sqb3ir F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5mao3rk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5mao3rk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8d45hjk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8d45hjk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a304ri6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_a304ri6 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)96ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ub69oip(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ub69oip F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)6ULL));
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
inline __attribute__((aligned(4))) void gm_f_unscope_type_params_bofhhep(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unscope_type_params_bofhhep F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* before */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_restore_scope_edqq1v8(t2, &(*(gm_t66j1im9ip128 *)((uint8_t *)a1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_arg_may_unify_qmi580g(void * a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arg_may_unify_qmi580g F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* y */
    gm_t428ue2u3hf5g gm_l5; /* x */
    gm_t428ue2u3hf5g gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 366u);
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
        uint8_t t21;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
        if (t24 != ((uint8_t)19ULL)) goto mn23;
        t21 = ((uint8_t)1);
        goto me22;
mn23:;
        t21 = ((uint8_t)0);
        goto me22;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 227, 33);
me22:;
        uint8_t t26 = 0;
        F.ret = t21;
        goto gm_fret;
        goto me6;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 225, 21);
me6:;
        goto me2;
mn3:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t28 != ((uint8_t)1ULL)) goto mn27;
        gm_t428ue2u3hf5g t29 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)a1) + 8));
        gm_l5 = t29;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t32 != ((uint8_t)1ULL)) goto mn31;
        gm_t428ue2u3hf5g t33 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&F.l2) + 8));
        gm_l6 = t33;
        void * t34 = (void *)((uint8_t *)&gm_l5);
        void * t35 = (void *)((uint8_t *)&gm_l6);
        int32_t t36 = gm_f_cmp_key_c6mirrv(t34, t35);
        uint8_t t37 = ((t36 == ((int32_t)0LL)));
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
        goto me30;
mn31:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t40 != ((uint8_t)0ULL)) goto mn39;
        gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 8));
        gm_l7 = t41;
        void * t42 = gm_l0;
        void * t43 = (void *)(((uint8_t *)t42) + 8);
        gm_t88rcucg1b7uk t44 = gm_l7;
        gm_t53at80pfdus3 t45;
        gm_f_kind_jcnt4hm((void *)&t45, t43, GM_CONV(gm_csguankko4lmhd, t44));
        uint8_t t46;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t49 != ((uint8_t)19ULL)) goto mn48;
        t46 = ((uint8_t)1);
        goto me47;
mn48:;
        t46 = ((uint8_t)0);
        goto me47;
mn50:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 231, 32);
me47:;
        uint8_t t51 = 0;
        F.ret = t46;
        goto gm_fret;
        goto me30;
mn39:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 229, 22);
me30:;
        goto me2;
mn27:;
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
    GM_STACK_CHECK_SHALLOW(&F, 56u);
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
    uint8_t t22 = (gm_eq9m6mvtmgde(((uint8_t *)&t20), ((uint8_t *)&t21)));
    uint8_t t23 = t22;
    t15 = t23;
    goto ix16;
ie16:;
    uint8_t t25 = ((uint8_t)0);
    t15 = t25;
ix16:;
    t1 = t15;
    goto me2;
mn12:;
ma5:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t27 != ((uint8_t)2ULL)) goto mn26;
    uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)2ULL)));
    t1 = t28;
    goto me2;
mn26:;
ma6:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)3ULL)) goto mn29;
    uint8_t t31 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)3ULL)));
    t1 = t31;
    goto me2;
mn29:;
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
    GM_STACK_CHECK(&F, 1377u);
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
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_f_scope_type_params_2di4dm5((void *)&gm_l5, (void *)NULL, cx, NULL, t10, t12);
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
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 40));
            gm_l7 = t14;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l8;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l8;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t5v0c8ia6a5mo t26 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t22 * 56));
                    F.l9 = t26;
                    {
                        uint8_t t29;
                        if ((((((uint8_t *)&F.l9) + 40))[4] != 0)) goto mn31;
                        t29 = ((uint8_t)1);
                        goto me30;
mn31:;
                        t29 = ((uint8_t)0);
                        goto me30;
mn32:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 278, 12);
me30:;
                        if (!t29) goto ie28;
                        {
                            void * t34 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t35 = gm_f_push_o8u01sh(t34, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l9)));
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srcgr2mvgis129a, 278, 32);
                            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 277, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t38 = gm_l8;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l8 = t41;
                }
            }
lb15:;
        }
    }
    {
        void * t42 = gm_l0;
        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
        gm_tnpifv20irnph t45 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l2) + 64));
        gm_f_fn_cx_hgvq15e((void *)&F.l10, (void *)NULL, cx, NULL, t42, t43, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        gm_t9805qtonbsag t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t47), t46);
        ((uint8_t *)&t47)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)&F.l10) + 400), t47);
    }
    {
        gm_tnqmlaad6uchl t48;
        memset((uint8_t *)&t48 + 1, 0, 3);
        memset((uint8_t *)&t48 + 28, 0, 4);
        gm_t9qkh0rsooc8d t49 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a1) + 20));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t48 + 0, t49);
        gm_t9805qtonbsag t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t48 + 4, t50);
        uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        gm_t9805qtonbsag t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t52), t51);
        ((uint8_t *)&t52)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t48 + 12, t52);
        gm_tkn42u7na3t81 t53;
        memset(&t53, 0, sizeof t53);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t48 + 20, t53);
        gm_tjvtgq0p0sehv t54;
        memset(&t54, 0, sizeof t54);
        GM_ST(gm_tjvtgq0p0sehv, (uint8_t *)&t48 + 32, t54);
        gm_tkn42u7na3t81 t55;
        memset(&t55, 0, sizeof t55);
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t48 + 72, t55);
        gm_tiilmfd16v3fc t56 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)a1) + 24));
        gm_tqu3m2l3n8mnc t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t57), t56);
        ((uint8_t *)&t57)[24] = 1;
        GM_ST(gm_tqu3m2l3n8mnc, (uint8_t *)&t48 + 80, t57);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)&F.l10) + 800)) + 113, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)&F.l10) + 800)) + 114, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)&F.l10) + 800)) + 116, 0);
        ((uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)&F.l10) + 800)))[112] = 1;
        GM_ST(gm_tnqmlaad6uchl, (uint8_t *)&(*(gm_t403eokj7ifg9 *)(((uint8_t *)&F.l10) + 800)) + 0, t48);
    }
    {
        void * t59 = gm_l3;
        uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)t59) + 32) + 8);
        uint64_t t61 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t62 = (!(t60 == t61));
        if (!t62) goto ie58;
        {
            void * t64 = (void *)((uint8_t *)&F.l10);
            void * t65 = gm_l3;
            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 56));
            gm_t3877pk7lv748 t67;
            void * t68 = gm_l3;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((((uint8_t *)t68) + 0) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t67 + 0, t69);
            uint64_t t70 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            GM_ST(uint64_t, (uint8_t *)&t67 + 16, t70);
            gm_tabb710d9mq2e t71;
            gm_f_format_lveqqdp((void *)&t71, (void *)NULL, cx, NULL, &t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srcgr2mvgis129a, 284, 26);
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_b0_1 t74 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t66, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        void * t75 = (void *)((uint8_t *)&F.l10);
        void * t76 = gm_l3;
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b16_8 t79 = gm_f_declare_clause_params_7bqbscg((void *)NULL, cx, NULL, t75, t76, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t79;
    }
    {
        void * t80 = (void *)((uint8_t *)&F.l10);
        void * t81 = gm_l3;
        gm_t9qkh0rsooc8d t82 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a1) + 20));
        gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
        void * t84 = gm_f_clause_value_tpeb80m((void *)NULL, cx, NULL, t80, t81, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t84;
    }
    {
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            void * t86 = gm_l0;
            uint64_t t87 = GM_LD(uintptr_t, ((((uint8_t *)t86) + 720) + 0) + 8);
            gm_l13 = t87;
        }
        {
            for (;;) {
                uint64_t t89 = gm_l14;
                uint64_t t90 = gm_l13;
                uint8_t t91 = (t89 < t90);
                if (!t91) goto lb88;
                {
                    uint64_t t94 = gm_l14;
                    uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&t94));
                    gm_l15 = t97;
                    {
                        uint64_t t99 = gm_l15;
                        uint64_t t100 = (uintptr_t)t99;
                        void * t101 = gm_l0;
                        uint64_t t102 = GM_LD(uintptr_t, ((((uint8_t *)t101) + 720) + 0) + 8);
                        if (t100 >= (uintptr_t)(t102)) gm_trap_index(t100, t102, gm_srcgr2mvgis129a, 290, 15);
                        void * t103 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t101) + 720) + 0)) + t100 * 20);
                        gm_l16 = t103;
                    }
                    {
                        void * t105 = gm_l16;
                        uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)t105) + 0));
                        uint32_t t107 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
                        uint8_t t108 = ((t106 == t107));
                        if (!t108) goto ie104;
                        {
                            void * t110 = gm_l16;
                            uint8_t *t111 = (((uint8_t *)t110) + 4);
                            void * t112 = (void *)((uint8_t *)&F.l10);
                            void * t113 = gm_l16;
                            gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t113) + 4));
                            void * t115 = gm_l16;
                            gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t115) + 8));
                            gm_t88rcucg1b7uk t117 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t112, t114, t116);
                            if (gm_unw.kind) {
                                goto gm_ll92;
                            }
                            GM_ST(gm_t88rcucg1b7uk, t111, t117);
                        }
                        goto ix104;
ie104:;
ix104:;
                    }
                    goto me95;
mn96:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 289, 5);
me95:;
                }
lc88:;
                {
                    uint64_t t119 = gm_l14;
                    uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                    uint64_t t122 = r120;
                    gm_l14 = t122;
                }
            }
gm_ll92:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb88; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc88; }
            }
            goto gm_lad;
lb88:;
        }
    }
    {
        void * t123 = (void *)((uint8_t *)&F.l10);
        void * t124 = gm_l12;
        gm_b0_1 t125 = gm_f_finish_m8t0599((void *)NULL, cx, NULL, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t126 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        uint64_t t127 = (uint64_t)t126;
        uint64_t t128 = (uintptr_t)t127;
        void * t129 = gm_l0;
        uint64_t t130 = GM_LD(uintptr_t, ((((uint8_t *)t129) + 384) + 0) + 8);
        if (t128 >= (uintptr_t)(t130)) gm_trap_index(t128, t130, gm_srcgr2mvgis129a, 294, 13);
        void * t131 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t129) + 384) + 0)) + t128 * 64);
        gm_l17 = t131;
    }
    {
        void * t132 = gm_l17;
        gm_b16_8 t133 = gm_l11;
        GM_ST(gm_b16_8, (((uint8_t *)t132) + 8), t133);
    }
    {
        void * t134 = gm_l17;
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l10) + 16) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)t134) + 24), t135);
    }
    {
        void * t136 = gm_l17;
        void * t137 = gm_l12;
        GM_ST(void *, (((uint8_t *)t136) + 40), t137);
    }
    {
        void * t138 = gm_l0;
        gm_f_unscope_type_params_bofhhep(t138, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l5)));
    }
    {
        void * t139 = gm_l0;
        gm_f_leave_module_scope_4hp5mc4(t139, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l4)));
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
    GM_STACK_CHECK(&F, 947u);
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
                                    uint32_t t32 = GM_LD(uint32_t, ((uint8_t *)&t29));
                                    gm_l7 = t32;
                                    {
                                        {
                                            uint32_t t35 = gm_l7;
                                            uint64_t t36 = (uint64_t)t35;
                                            uint64_t t37 = (uintptr_t)t36;
                                            void * t38 = gm_l0;
                                            uint64_t t39 = GM_LD(uintptr_t, ((((uint8_t *)t38) + 544) + 0) + 8);
                                            if (t37 >= (uintptr_t)(t39)) gm_trap_index(t37, t39, gm_srcgr2mvgis129a, 308, 22);
                                            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t38) + 544) + 0)) + t37 * 64) + 48));
                                            gm_l8 = t40;
                                        }
                                        {
                                            gm_l9 = ((uint64_t)0ULL);
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t42 = gm_l9;
                                                uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                                uint8_t t44 = (t42 < t43);
                                                if (!t44) goto lb41;
                                                {
                                                    uint64_t t47 = gm_l9;
                                                    uint64_t t48 = (uintptr_t)t47;
                                                    uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                                    uint32_t t50 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t48 * 4));
                                                    uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)&t50));
                                                    gm_l10 = t53;
                                                    {
                                                        {
                                                            void * t56 = gm_l0;
                                                            gm_b16_8 t57 = GM_LD(gm_b16_8, ((((uint8_t *)t56) + 720) + 0));
                                                            gm_l11 = t57;
                                                        }
                                                        {
                                                            gm_l12 = ((uint64_t)0ULL);
                                                        }
                                                        {
                                                            for (;;) {
                                                                uint64_t t59 = gm_l12;
                                                                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                                uint8_t t61 = (t59 < t60);
                                                                if (!t61) goto lb58;
                                                                {
                                                                    uint64_t t64 = gm_l12;
                                                                    uint64_t t65 = (uintptr_t)t64;
                                                                    uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                                                    gm_taqu3436kubri t69 = GM_LD(gm_taqu3436kubri, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t65 * 20));
                                                                    gm_l13 = t69;
                                                                    {
                                                                        uint8_t t72 = 0;
                                                                        uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 0));
                                                                        uint32_t t74 = gm_l10;
                                                                        uint8_t t75 = (!(t73 == t74));
                                                                        if (!t75) {
                                                                            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                                                                            uint8_t t78 = (gm_eq9m6mvtmgde(((uint8_t *)&t76), ((uint8_t *)&t77)));
                                                                            t72 = t78;
                                                                        } else t72 = 1;
                                                                        if (!t72) goto ie71;
                                                                        {
                                                                            goto lc58;
                                                                        }
                                                                        goto ix71;
ie71:;
ix71:;
                                                                    }
                                                                    {
                                                                        uint8_t t81 = 0;
                                                                        uint8_t t82 = 0;
                                                                        void * t83 = gm_l0;
                                                                        void * t84 = (void *)(((uint8_t *)t83) + 8);
                                                                        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                        gm_t53at80pfdus3 t86;
                                                                        gm_f_kind_jcnt4hm((void *)&t86, t84, GM_CONV(gm_csguankko4lmhd, t85));
                                                                        uint8_t t87 = ((GM_LD(uint8_t, ((uint8_t *)&t86) + 0) == ((uint8_t)5ULL)));
                                                                        if (!t87) {
                                                                            void * t88 = gm_l0;
                                                                            void * t89 = (void *)(((uint8_t *)t88) + 8);
                                                                            gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                            uint8_t t91 = gm_f_is_error_8k27gd1(t89, GM_CONV(gm_csguankko4lmhd, t90));
                                                                            t82 = t91;
                                                                        } else t82 = 1;
                                                                        if (!t82) {
                                                                            gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                                                                            uint8_t t94 = 0;
                                                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t93) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t93))) + i * 12, ((uint8_t *)&t92))) { t94 = 1; break; }
                                                                            uint8_t t95 = (uint8_t)(t94);
                                                                            t81 = t95;
                                                                        } else t81 = 1;
                                                                        if (!t81) goto ie80;
                                                                        {
                                                                            goto lc58;
                                                                        }
                                                                        goto ix80;
ie80:;
ix80:;
                                                                    }
                                                                    {
                                                                        void * t97 = (void *)((uint8_t *)&F.l1);
                                                                        gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                        gm_tftb1odc739t5 t99 = gm_f_push_0mk7udh(t97, GM_CONV(gm_cseahvg8v7p0vs, t98));
                                                                        if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 1, gm_srcgr2mvgis129a, 312, 21);
                                                                        gm_b0_1 t100 = GM_LD(gm_b0_1, (((uint8_t *)&t99) + 1));
                                                                    }
                                                                    {
                                                                        gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 8));
                                                                        gm_t6j6smj7qoctu t102;
                                                                        void * t103 = gm_l0;
                                                                        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 4));
                                                                        gm_b16_8 t105 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t103, t104);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll62;
                                                                        }
                                                                        GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t105);
                                                                        void * t106 = gm_l0;
                                                                        gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 16));
                                                                        gm_b16_8 t108 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t106, t107);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll62;
                                                                        }
                                                                        GM_ST(gm_b16_8, (uint8_t *)&t102 + 16, t108);
                                                                        gm_tabb710d9mq2e t109;
                                                                        gm_f_format_klj2nqm((void *)&t109, (void *)NULL, cx, NULL, &t102);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll62;
                                                                        }
                                                                        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srcgr2mvgis129a, 313, 45);
                                                                        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                                                                        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                                                                        gm_f_error_s6hn871((void *)&F.l14, (void *)NULL, cx, NULL, t101, t111);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll62;
                                                                        }
                                                                    }
                                                                    {
                                                                        void * t112 = (void *)((uint8_t *)&F.l14);
                                                                        gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 20));
                                                                        gm_b16_8 t114;
                                                                        GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sgjkgsqdq34nu));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(36));
                                                                        gm_f_add_label_50fj7vv(t112, GM_CONV(gm_cseahvg8v7p0vs, t113), t114);
                                                                    }
                                                                    {
                                                                        void * t115 = gm_l0;
                                                                        gm_b0_1 t116 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t115, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l14)));
                                                                        if (gm_unw.kind) {
                                                                            goto gm_ll62;
                                                                        }
                                                                    }
                                                                    goto me67;
mn68:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 309, 17);
me67:;
                                                                }
lc58:;
                                                                {
                                                                    uint64_t t118 = gm_l12;
                                                                    uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                                                    uint64_t t121 = r119;
                                                                    gm_l12 = t121;
                                                                }
                                                            }
gm_ll62:;
                                                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb58; }
                                                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc58; }
                                                            }
                                                            goto gm_ll45;
lb58:;
                                                        }
                                                    }
                                                    goto me51;
mn52:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 308, 13);
me51:;
                                                }
lc41:;
                                                {
                                                    uint64_t t123 = gm_l9;
                                                    uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                                                    uint64_t t126 = r124;
                                                    gm_l9 = t126;
                                                }
                                            }
gm_ll45:;
                                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb41; }
                                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc41; }
                                            }
                                            goto gm_ll24;
lb41:;
                                        }
                                    }
                                    goto me30;
mn31:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 307, 9);
me30:;
                                }
lc20:;
                                {
                                    uint64_t t128 = gm_l6;
                                    uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                                    uint64_t t131 = r129;
                                    gm_l6 = t131;
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
                    uint64_t t133 = gm_l3;
                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                    uint64_t t136 = r134;
                    gm_l3 = t136;
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
    GM_STACK_CHECK(&F, 276u);
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
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcgr2mvgis129a, 251, 13);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        goto ix21;
ie21:;
                        {
                            void * t34 = gm_l0;
                            gm_b0_1 t35 = gm_f_check_clause_1b0dqf0((void *)NULL, cx, NULL, t34, &(*(gm_tbeui4bbnm52b *)((uint8_t *)&F.l4)));
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
                    uint64_t t37 = gm_l3;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l3 = t40;
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
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 680) + 0));
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
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
                    gm_tt01bthgiqsjo t12;
                    uint64_t t13 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
                    uint64_t t14 = gm_l3;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t12 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 48), 48);
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t12) + 0));
                    gm_l4 = t19;
                    F.l5 = (*(gm_tbeui4bbnm52b *)(((uint8_t *)&t12) + 8));
                    {
                        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 16));
                        uint32_t t23 = gm_l1;
                        uint8_t t24 = (!(t22 == t23));
                        if (!t24) goto ie21;
                        {
                            goto lc6;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    {
                        void * t26 = gm_l0;
                        void * t27 = gm_l0;
                        void * t28 = (void *)(((uint8_t *)t27) + 680);
                        uint64_t t29 = gm_l4;
                        gm_tbeui4bbnm52b t30;
                        gm_f_remove_76216q8((void *)&t30, t28, t29);
                        gm_b0_1 t31 = gm_f_check_clause_1b0dqf0((void *)NULL, cx, NULL, t26, &t30);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    {
                        uint8_t t32 = 0;
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 261, 5);
me17:;
                }
lc6:;
                {
                    uint64_t t34 = gm_l3;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l3 = t37;
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
        uint8_t t38 = 0;
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
    GM_STACK_CHECK_SHALLOW(&F, 252u);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l4 = t14;
                    {
                        uint64_t t17 = gm_l4;
                        uint64_t t18 = (uintptr_t)t17;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 64) + 8);
                        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcgr2mvgis129a, 238, 12);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 64)) + t18 * 72) + 0) + 0));
                        gm_b16_8 t22 = gm_l1;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie16;
                        {
                            uint64_t t25 = gm_l4;
                            gm_tf5srh4mp4b3i t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[8] = 1;
                            uint8_t t27 = 0;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix16;
ie16:;
ix16:;
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 237, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l3;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l3 = t32;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t33;
        memset(&t33, 0, sizeof t33);
        uint8_t t34 = 0;
        F.ret = t33;
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
    GM_STACK_CHECK(&F, 4675u);
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
                            if (!(GM_LD(void *, ((uint8_t *)&t26)) != 0)) goto ie25;
                            void * t27 = GM_LD(void *, ((uint8_t *)&t26));
                            gm_l10 = t27;
                            {
                                void * t30 = gm_l10;
                                uint8_t t31 = gm_f_is_effects_kind_uutbf9u(t30);
                                if (!t31) goto ie29;
                                {
                                    gm_tnnjeerolos0a t33;
                                    GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
                                    gm_l8 = t33;
                                }
                                goto ix29;
ie29:;
                                uint8_t t35 = 0;
                                void * t36 = gm_l10;
                                uint8_t t37;
                                uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                                if (t40 != ((uint8_t)6ULL)) goto mn39;
                                t37 = ((uint8_t)1);
                                goto me38;
mn39:;
                                t37 = ((uint8_t)0);
                                goto me38;
mn41:;
                                gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 28, 32);
me38:;
                                if (t37) {
                                    void * t42 = gm_l10;
                                    gm_b16_8 t43;
                                    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s2737i1iq57nf));
                                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(4));
                                    uint8_t t44 = gm_f_ident_is_uo88ron(t42, t43);
                                    t35 = t44;
                                } else t35 = 0;
                                uint8_t t45 = (uint8_t)!t35;
                                if (!t45) goto ie34;
                                {
                                    void * t47 = F.l0;
                                    uint64_t t48 = gm_l1;
                                    void * t49 = gm_l10;
                                    gm_t88rcucg1b7uk t50 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t47, t48, t49);
                                    if (gm_unw.kind) {
                                        goto gm_ll12;
                                    }
                                    gm_l9 = t50;
                                }
                                {
                                    gm_t88rcucg1b7uk t51 = gm_l9;
                                    gm_tnnjeerolos0a t52;
                                    GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)2ULL));
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t52 + 4), t51);
                                    gm_l8 = t52;
                                }
                                goto ix34;
ie34:;
ix34:;
ix29:;
                            }
                            goto ix25;
ie25:;
                            {
                                void * t54 = F.l0;
                                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 40));
                                gm_b16_8 t56;
                                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_shqja2jbdo2o0));
                                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(57));
                                gm_b0_1 t57 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t54, t55, t56);
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
                                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
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
                                        gm_t08olq97rscn3 t71 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t67 * 32));
                                        gm_l13 = t71;
                                        {
                                            void * t73 = F.l0;
                                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                            gm_tnnjeerolos0a t75 = gm_l8;
                                            uint32_t t76 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t73, t74, t75);
                                            if (gm_unw.kind) {
                                                goto gm_ll64;
                                            }
                                            gm_l14 = t76;
                                        }
                                        {
                                            void * t77 = (void *)((uint8_t *)&F.l3);
                                            gm_t5v0c8ia6a5mo t78;
                                            memset((uint8_t *)&t78 + 49, 0, 7);
                                            gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t79);
                                            gm_t88rcucg1b7uk t80 = gm_l9;
                                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t78 + 16, t80);
                                            void * t81 = gm_l2;
                                            gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t81) + 80));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t78 + 20, t82);
                                            GM_ST(void *, (uint8_t *)&t78 + 32, ((void *)0));
                                            uint32_t t83 = gm_l14;
                                            gm_t9805qtonbsag t84;
                                            GM_ST(uint8_t, (uint8_t *)&t84 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t84 + 6, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t84), t83);
                                            ((uint8_t *)&t84)[4] = 1;
                                            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t78 + 40, t84);
                                            GM_ST(uint8_t, (uint8_t *)&t78 + 48, ((uint8_t)0));
                                            gm_tftb1odc739t5 t85 = gm_f_push_o8u01sh(t77, &t78);
                                            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcgr2mvgis129a, 38, 17);
                                            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
                                        }
                                        goto me69;
mn70:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 36, 13);
me69:;
                                    }
lc60:;
                                    {
                                        uint64_t t88 = gm_l12;
                                        uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                                        uint64_t t91 = r89;
                                        gm_l12 = t91;
                                    }
                                }
gm_ll64:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
                                }
                                goto gm_ll12;
lb60:;
                            }
                        }
                        goto me17;
mn18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 22, 9);
me17:;
                    }
lc8:;
                    {
                        uint64_t t93 = gm_l6;
                        uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                        uint64_t t96 = r94;
                        gm_l6 = t96;
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
        void * t97 = F.l0;
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
        gm_f_scope_type_params_2di4dm5((void *)&gm_l15, (void *)NULL, cx, NULL, t97, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t100 = F.l0;
        uint64_t t101 = gm_l1;
        void * t102 = gm_l2;
        void * t103 = GM_LD(void *, (((uint8_t *)t102) + 24));
        gm_f_resolve_effect_ref_bggirs2((void *)&gm_l16, (void *)NULL, cx, NULL, t100, t101, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t104 = F.l0;
        uint64_t t105 = gm_l1;
        void * t106 = gm_l2;
        void * t107 = GM_LD(void *, (((uint8_t *)t106) + 32));
        gm_t88rcucg1b7uk t108 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t104, t105, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t108;
    }
    {
        gm_tnpifv20irnph t109 = gm_f_empty_effects_r0iv3pv();
        gm_l18 = t109;
    }
    {
        void * t111 = gm_l2;
        if (!(((((uint8_t *)t111) + 40))[16] != 0)) goto ie110;
        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)t111) + 40));
        gm_l19 = t112;
        {
            void * t114 = F.l0;
            uint64_t t115 = gm_l1;
            gm_b16_8 t116 = gm_l19;
            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
            gm_tnpifv20irnph t118 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t114, t115, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t118;
        }
        goto ix110;
ie110:;
ix110:;
    }
    {
        void * t119 = F.l0;
        gm_f_unscope_type_params_bofhhep(t119, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l15)));
    }
    {
        gm_tqu3m2l3n8mnc t120 = gm_l16;
        gm_tiilmfd16v3fc t121;
        if (((((uint8_t *)&t120))[24] != 0)) {
            t121 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t120) + 0));
        } else {
            gm_b0_1 t122;
            uint8_t t123 = 0;
            F.ret = t122;
            goto gm_fret;
        }
        gm_l20 = t121;
    }
    {
        void * t125 = F.l0;
        void * t126 = (void *)(((uint8_t *)t125) + 8);
        gm_t88rcucg1b7uk t127 = gm_l17;
        uint8_t t128 = gm_f_is_error_8k27gd1(t126, GM_CONV(gm_csguankko4lmhd, t127));
        if (!t128) goto ie124;
        {
            gm_b0_1 t130;
            uint8_t t131 = 0;
            F.ret = t130;
            goto gm_fret;
        }
        goto ix124;
ie124:;
ix124:;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l22 = t133;
        }
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t135 = gm_l23;
                uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                uint8_t t137 = (t135 < t136);
                if (!t137) goto lb134;
                {
                    uint64_t t140 = gm_l23;
                    uint64_t t141 = (uintptr_t)t140;
                    uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    gm_t5v0c8ia6a5mo t145 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t141 * 56));
                    F.l24 = t145;
                    {
                        if (!(((((uint8_t *)&F.l24) + 40))[4] != 0)) goto ie147;
                        uint32_t t148 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + 40));
                        gm_l25 = t148;
                        {
                            void * t150 = (void *)((uint8_t *)&F.l21);
                            uint32_t t151 = gm_l25;
                            gm_tftb1odc739t5 t152 = gm_f_push_im9d4ro(t150, t151);
                            if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 1, gm_srcgr2mvgis129a, 52, 29);
                            gm_b0_1 t153 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 1));
                        }
                        goto ix147;
ie147:;
ix147:;
                    }
                    goto me143;
mn144:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 51, 5);
me143:;
                }
lc134:;
                {
                    uint64_t t155 = gm_l23;
                    uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                    uint64_t t158 = r156;
                    gm_l23 = t158;
                }
            }
lb134:;
        }
    }
    {
        uint64_t t160 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        uint8_t t161 = (t160 > ((uint64_t)0ULL));
        if (!t161) goto ie159;
        {
            gm_f_list_go2qg0b((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t163 = F.l0;
            gm_t88rcucg1b7uk t164 = gm_l17;
            gm_closure t165 = ((gm_closure){(void *)gm_f_fn_51177of, (void *)&F.h, cx});
            gm_f_walk_v858co7(t163, GM_CONV(gm_csguankko4lmhd, t164), t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                gm_l27 = t167;
            }
            {
                gm_l28 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t169 = gm_l28;
                    uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t171 = (t169 < t170);
                    if (!t171) goto lb168;
                    {
                        uint64_t t174 = gm_l28;
                        uint64_t t175 = (uintptr_t)t174;
                        uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        gm_t5v0c8ia6a5mo t179 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t175 * 56));
                        F.l29 = t179;
                        {
                            gm_t9805qtonbsag t181 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l29) + 40));
                            uint32_t t182;
                            if (((((uint8_t *)&t181))[4] != 0)) {
                                uint32_t t183 = GM_LD(uint32_t, (((uint8_t *)&t181) + 0));
                                t182 = t183;
                            } else {
                                goto lc168;
                            }
                            gm_l30 = t182;
                        }
                        {
                            uint32_t t185 = gm_l30;
                            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                            uint8_t t187 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t186) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t186))) + i * 4, ((uint8_t *)&t185))) { t187 = 1; break; }
                            uint8_t t188 = (uint8_t)(!t187);
                            if (!t188) goto ie184;
                            {
                                void * t190 = F.l0;
                                void * t191 = gm_l2;
                                gm_tl874t2atnlh3 t192 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t191) + 80));
                                gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                                gm_tnpifv20irnph t194;
                                GM_ST(gm_b16_8, ((uint8_t *)&t194 + 0), t193);
                                gm_tabb710d9mq2e t195;
                                gm_f_format_mgpfvh0((void *)&t195, (void *)NULL, cx, NULL, t194);
                                if (gm_unw.kind) {
                                    goto gm_ll172;
                                }
                                if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 8, gm_srcgr2mvgis129a, 62, 33);
                                gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&t195) + 8));
                                gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
                                gm_b0_1 t198 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t190, t192, t197);
                                if (gm_unw.kind) {
                                    goto gm_ll172;
                                }
                            }
                            {
                                gm_b0_1 t199;
                                uint8_t t200 = 0;
                                F.ret = t199;
                                goto gm_fret;
                            }
                            goto ix184;
ie184:;
ix184:;
                        }
                        goto me177;
mn178:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 59, 9);
me177:;
                    }
lc168:;
                    {
                        uint64_t t202 = gm_l28;
                        uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                        uint64_t t205 = r203;
                        gm_l28 = t205;
                    }
                }
gm_ll172:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb168; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc168; }
                }
                goto gm_lad;
lb168:;
            }
        }
        goto ix159;
ie159:;
ix159:;
    }
    {
        uint32_t t206 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 0));
        uint64_t t207 = (uint64_t)t206;
        uint64_t t208 = (uintptr_t)t207;
        void * t209 = F.l0;
        uint64_t t210 = GM_LD(uintptr_t, ((((uint8_t *)t209) + 1048) + 0) + 8);
        if (t208 >= (uintptr_t)(t210)) gm_trap_index(t208, t210, gm_srcgr2mvgis129a, 67, 13);
        gm_tammvh1iespi3 t211 = GM_LD(gm_tammvh1iespi3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t209) + 1048) + 0)) + t208 * 56));
        F.l31 = t211;
    }
    {
        gm_l32 = ((uint8_t)0);
    }
    {
        uint64_t t213 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        uint8_t t214 = ((t213 == ((uint64_t)0ULL)));
        if (!t214) goto ie212;
        {
            {
                void * t217 = F.l0;
                gm_b16_8 t218 = GM_LD(gm_b16_8, ((((uint8_t *)t217) + 544) + 0));
                gm_l33 = t218;
            }
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t220 = gm_l34;
                    uint64_t t221 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint8_t t222 = (t220 < t221);
                    if (!t222) goto lb219;
                    {
                        uint64_t t225 = gm_l34;
                        uint64_t t226 = (uintptr_t)t225;
                        uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        gm_tvc83pqnepc2e t230 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t226 * 64));
                        F.l35 = t230;
                        {
                            void * t233 = (void *)(((uint8_t *)&F.l35) + 0);
                            void * t234 = (void *)((uint8_t *)&gm_l20);
                            int32_t t235 = gm_f_cmp_ref_8ntrqll(t233, t234);
                            uint8_t t236 = ((t235 == ((int32_t)0LL)));
                            if (!t236) goto ie232;
                            gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l35) + 24));
                            gm_t88rcucg1b7uk t238 = gm_l17;
                            uint8_t t239 = (gm_eq9m6mvtmgde(((uint8_t *)&t237), ((uint8_t *)&t238)));
                            if (!t239) goto ie232;
                            {
                                gm_l32 = ((uint8_t)1);
                            }
                            goto ix232;
ie232:;
ix232:;
                        }
                        goto me228;
mn229:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 72, 9);
me228:;
                    }
lc219:;
                    {
                        uint64_t t242 = gm_l34;
                        uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                        uint64_t t245 = r243;
                        gm_l34 = t245;
                    }
                }
lb219:;
            }
        }
        {
            uint8_t t247 = gm_l32;
            uint8_t t248 = (uint8_t)!t247;
            if (!t248) goto ie246;
            {
                {
                    gm_l37 = ((uint64_t)0ULL);
                }
                {
                    void * t251 = F.l0;
                    uint64_t t252 = GM_LD(uintptr_t, ((((uint8_t *)t251) + 592) + 0) + 8);
                    gm_l36 = t252;
                }
                {
                    for (;;) {
                        uint64_t t254 = gm_l37;
                        uint64_t t255 = gm_l36;
                        uint8_t t256 = (t254 < t255);
                        if (!t256) goto lb253;
                        {
                            uint64_t t259 = gm_l37;
                            uint64_t t262 = GM_LD(uint64_t, ((uint8_t *)&t259));
                            gm_l38 = t262;
                            {
                                void * t265 = F.l0;
                                uint64_t t266 = gm_l38;
                                void * t267 = (void *)((uint8_t *)&gm_l20);
                                gm_t88rcucg1b7uk t268 = gm_l17;
                                uint8_t t269 = gm_f_template_instance_is_ovv1pcj((void *)NULL, cx, NULL, t265, t266, t267, t268);
                                if (gm_unw.kind) {
                                    goto gm_ll257;
                                }
                                if (!t269) goto ie264;
                                {
                                    gm_l32 = ((uint8_t)1);
                                }
                                goto ix264;
ie264:;
ix264:;
                            }
                            goto me260;
mn261:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 76, 13);
me260:;
                        }
lc253:;
                        {
                            uint64_t t272 = gm_l37;
                            uint64_t r273; bool o274 = __builtin_add_overflow(t272, ((uint64_t)1ULL), &r273);
                            uint64_t t275 = r273;
                            gm_l37 = t275;
                        }
                    }
gm_ll257:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb253; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc253; }
                    }
                    goto gm_lad;
lb253:;
                }
            }
            goto ix246;
ie246:;
ix246:;
        }
        goto ix212;
ie212:;
        {
            memset((uint8_t *)&F.l39 + 44, 0, 4);
            memset((uint8_t *)&F.l39 + 92, 0, 4);
            gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&F.l39 + 0, t277);
            memcpy((uint8_t *)&F.l39 + 16, ((uint8_t *)&gm_l20), 24);
            gm_t88rcucg1b7uk t278 = gm_l17;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l39 + 40, t278);
            gm_tnpifv20irnph t279 = gm_l18;
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&F.l39 + 48, t279);
            gm_b16_8 t280 = gm_f_empty_slice_d914i5u();
            GM_ST(gm_b16_8, (uint8_t *)&F.l39 + 64, t280);
            void * t281 = gm_l2;
            gm_tl874t2atnlh3 t282 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t281) + 80));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l39 + 80, t282);
        }
        {
            {
                void * t284 = F.l0;
                gm_b16_8 t285 = GM_LD(gm_b16_8, ((((uint8_t *)t284) + 544) + 0));
                gm_l40 = t285;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t287 = gm_l41;
                    uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    uint8_t t289 = (t287 < t288);
                    if (!t289) goto lb286;
                    {
                        uint64_t t292 = gm_l41;
                        uint64_t t293 = (uintptr_t)t292;
                        uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        gm_tvc83pqnepc2e t297 = GM_LD(gm_tvc83pqnepc2e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t293 * 64));
                        F.l42 = t297;
                        {
                            void * t300 = F.l0;
                            void * t301 = (void *)((uint8_t *)&F.l39);
                            void * t302 = (void *)(((uint8_t *)&F.l42) + 0);
                            gm_t88rcucg1b7uk t303 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l42) + 24));
                            uint8_t t304 = gm_f_template_probe_is_d1ep4c6((void *)NULL, cx, NULL, t300, t301, t302, t303);
                            if (gm_unw.kind) {
                                goto gm_ll290;
                            }
                            if (!t304) goto ie299;
                            {
                                gm_l32 = ((uint8_t)1);
                            }
                            goto ix299;
ie299:;
ix299:;
                        }
                        goto me295;
mn296:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 82, 9);
me295:;
                    }
lc286:;
                    {
                        uint64_t t307 = gm_l41;
                        uint64_t r308; bool o309 = __builtin_add_overflow(t307, ((uint64_t)1ULL), &r308);
                        uint64_t t310 = r308;
                        gm_l41 = t310;
                    }
                }
gm_ll290:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb286; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc286; }
                }
                goto gm_lad;
lb286:;
            }
        }
        {
            {
                void * t312 = F.l0;
                gm_b16_8 t313 = GM_LD(gm_b16_8, ((((uint8_t *)t312) + 592) + 0));
                gm_l43 = t313;
            }
            {
                gm_l44 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t315 = gm_l44;
                    uint64_t t316 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                    uint8_t t317 = (t315 < t316);
                    if (!t317) goto lb314;
                    {
                        uint64_t t320 = gm_l44;
                        uint64_t t321 = (uintptr_t)t320;
                        uint64_t t322 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                        gm_t9qmk75lrp9p4 t325 = GM_LD(gm_t9qmk75lrp9p4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t321 * 96));
                        F.l45 = t325;
                        {
                            uint32_t t328 = GM_LD(uint32_t, ((((uint8_t *)&F.l45) + 16) + 0));
                            uint32_t t329 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 0));
                            uint8_t t330 = ((t328 == t329));
                            if (!t330) goto ie327;
                            void * t331 = F.l0;
                            gm_t88rcucg1b7uk t332 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l45) + 40));
                            gm_t88rcucg1b7uk t333 = gm_l17;
                            uint8_t t334 = gm_f_may_unify_0noeh56(t331, GM_CONV(gm_csguankko4lmhd, t332), GM_CONV(gm_csguankko4lmhd, t333));
                            if (!t334) goto ie327;
                            uint64_t t335 = GM_LD(uintptr_t, ((((uint8_t *)&F.l45) + 16) + 8) + 8);
                            uint64_t t336 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                            uint8_t t337 = ((t335 == t336));
                            if (!t337) goto ie327;
                            {
                                gm_l46 = ((uint8_t)1);
                            }
                            {
                                {
                                    gm_b16_8 t340 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l45) + 16) + 8));
                                    gm_l47 = t340;
                                }
                                {
                                    gm_l48 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t342 = gm_l48;
                                        uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                        uint8_t t344 = (t342 < t343);
                                        if (!t344) goto lb341;
                                        {
                                            gm_tdr8bh2bm3fc8 t347;
                                            uint64_t t348 = gm_l48;
                                            GM_ST(uint64_t, (uint8_t *)&t347 + 0, t348);
                                            uint64_t t349 = gm_l48;
                                            uint64_t t350 = (uintptr_t)t349;
                                            uint64_t t351 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                            memcpy((uint8_t *)&t347 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t350 * 40), 40);
                                            uint64_t t354 = GM_LD(uint64_t, (((uint8_t *)&t347) + 0));
                                            gm_l49 = t354;
                                            F.l50 = (*(gm_toa2vhe3l4hfr *)(((uint8_t *)&t347) + 8));
                                            {
                                                void * t357 = F.l0;
                                                uint64_t t358 = gm_l49;
                                                uint64_t t359 = (uintptr_t)t358;
                                                uint64_t t360 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                                                if (t359 >= (uintptr_t)(t360)) gm_trap_index(t359, t360, gm_srcgr2mvgis129a, 89, 48);
                                                uint8_t t361 = gm_f_arg_may_unify_qmi580g(t357, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&F.l50)), &(*(gm_toa2vhe3l4hfr *)((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l20) + 8)) + t359 * 40)));
                                                uint8_t t362 = (uint8_t)!t361;
                                                if (!t362) goto ie356;
                                                {
                                                    gm_l46 = ((uint8_t)0);
                                                }
                                                goto ix356;
ie356:;
ix356:;
                                            }
                                            goto me352;
mn353:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 88, 17);
me352:;
                                        }
lc341:;
                                        {
                                            uint64_t t365 = gm_l48;
                                            uint64_t r366; bool o367 = __builtin_add_overflow(t365, ((uint64_t)1ULL), &r366);
                                            uint64_t t368 = r366;
                                            gm_l48 = t368;
                                        }
                                    }
lb341:;
                                }
                            }
                            {
                                uint8_t t370 = gm_l46;
                                if (!t370) goto ie369;
                                {
                                    gm_l32 = ((uint8_t)1);
                                }
                                goto ix369;
ie369:;
ix369:;
                            }
                            goto ix327;
ie327:;
ix327:;
                        }
                        goto me323;
mn324:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 85, 9);
me323:;
                    }
lc314:;
                    {
                        uint64_t t373 = gm_l44;
                        uint64_t r374; bool o375 = __builtin_add_overflow(t373, ((uint64_t)1ULL), &r374);
                        uint64_t t376 = r374;
                        gm_l44 = t376;
                    }
                }
lb314:;
            }
        }
ix212:;
    }
    {
        uint8_t t378 = gm_l32;
        if (!t378) goto ie377;
        {
            void * t380 = F.l0;
            void * t381 = gm_l2;
            gm_tl874t2atnlh3 t382 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t381) + 80));
            gm_t6j6smj7qoctu t383;
            gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t383 + 0, t384);
            void * t385 = F.l0;
            gm_t88rcucg1b7uk t386 = gm_l17;
            gm_b16_8 t387 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t385, t386);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t383 + 16, t387);
            gm_tabb710d9mq2e t388;
            gm_f_format_8h6eueo((void *)&t388, (void *)NULL, cx, NULL, &t383);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t388))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t388) + 8, gm_srcgr2mvgis129a, 96, 25);
            gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)&t388) + 8));
            gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&t389));
            gm_b0_1 t391 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t380, t382, t390);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t392;
            uint8_t t393 = 0;
            F.ret = t392;
            goto gm_fret;
        }
        goto ix377;
ie377:;
ix377:;
    }
    {
        {
            void * t395 = gm_l2;
            gm_b16_8 t396 = GM_LD(gm_b16_8, (((uint8_t *)t395) + 64));
            gm_l51 = t396;
        }
        {
            gm_l52 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t398 = gm_l52;
                uint64_t t399 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                uint8_t t400 = (t398 < t399);
                if (!t400) goto lb397;
                {
                    uint64_t t403 = gm_l52;
                    uint64_t t404 = (uintptr_t)t403;
                    uint64_t t405 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                    gm_t99vmhfllumbh t408 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t404 * 72));
                    F.l53 = t408;
                    {
                        gm_l54 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 40));
                            gm_l55 = t411;
                        }
                        {
                            gm_l56 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t413 = gm_l56;
                                uint64_t t414 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                uint8_t t415 = (t413 < t414);
                                if (!t415) goto lb412;
                                {
                                    uint64_t t418 = gm_l56;
                                    uint64_t t419 = (uintptr_t)t418;
                                    uint64_t t420 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                    gm_tv0k2vg3054a6 t423 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t419 * 48));
                                    F.l57 = t423;
                                    {
                                        gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&F.l57) + 0));
                                        gm_b16_8 t427 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l53) + 0) + 0));
                                        uint8_t t428 = (gm_eq0iqiubfm7e(((uint8_t *)&t426), ((uint8_t *)&t427)));
                                        if (!t428) goto ie425;
                                        {
                                            gm_l54 = ((uint8_t)1);
                                        }
                                        goto ix425;
ie425:;
ix425:;
                                    }
                                    goto me421;
mn422:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 101, 9);
me421:;
                                }
lc412:;
                                {
                                    uint64_t t431 = gm_l56;
                                    uint64_t r432; bool o433 = __builtin_add_overflow(t431, ((uint64_t)1ULL), &r432);
                                    uint64_t t434 = r432;
                                    gm_l56 = t434;
                                }
                            }
lb412:;
                        }
                    }
                    {
                        uint8_t t436 = gm_l54;
                        uint8_t t437 = (uint8_t)!t436;
                        if (!t437) goto ie435;
                        {
                            void * t439 = F.l0;
                            gm_tl874t2atnlh3 t440 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l53) + 0) + 16));
                            gm_t6j6smj7qoctu t441;
                            gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t441 + 0, t442);
                            gm_b16_8 t443 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l53) + 0) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t441 + 16, t443);
                            gm_tabb710d9mq2e t444;
                            gm_f_format_74vh99o((void *)&t444, (void *)NULL, cx, NULL, &t441);
                            if (gm_unw.kind) {
                                goto gm_ll401;
                            }
                            if (((((uint8_t *)&t444))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t444) + 8, gm_srcgr2mvgis129a, 104, 45);
                            gm_b16_8 t445 = GM_LD(gm_b16_8, (((uint8_t *)&t444) + 8));
                            gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&t445));
                            gm_b0_1 t447 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t439, t440, t446);
                            if (gm_unw.kind) {
                                goto gm_ll401;
                            }
                        }
                        goto ix435;
ie435:;
ix435:;
                    }
                    goto me406;
mn407:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 99, 5);
me406:;
                }
lc397:;
                {
                    uint64_t t449 = gm_l52;
                    uint64_t r450; bool o451 = __builtin_add_overflow(t449, ((uint64_t)1ULL), &r450);
                    uint64_t t452 = r450;
                    gm_l52 = t452;
                }
            }
gm_ll401:;
            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb397; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc397; }
            }
            goto gm_lad;
lb397:;
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
            gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 40));
            gm_l59 = t454;
        }
        {
            gm_l60 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t456 = gm_l60;
                uint64_t t457 = GM_LD(uintptr_t, ((uint8_t *)&gm_l59) + 8);
                uint8_t t458 = (t456 < t457);
                if (!t458) goto lb455;
                {
                    uint64_t t461 = gm_l60;
                    uint64_t t462 = (uintptr_t)t461;
                    uint64_t t463 = GM_LD(uintptr_t, ((uint8_t *)&gm_l59) + 8);
                    gm_tv0k2vg3054a6 t466 = GM_LD(gm_tv0k2vg3054a6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l59)) + t462 * 48));
                    F.l61 = t466;
                    {
                        void * t468 = gm_l2;
                        gm_b16_8 t469 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                        gm_tf5srh4mp4b3i t470 = gm_f_decl_clause_0i5kmeb(t468, t469);
                        uint64_t t471;
                        if (((((uint8_t *)&t470))[8] != 0)) {
                            uint64_t t472 = GM_LD(uint64_t, (((uint8_t *)&t470) + 0));
                            t471 = t472;
                        } else {
                            {
                                void * t474 = F.l0;
                                void * t475 = gm_l2;
                                gm_tl874t2atnlh3 t476 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t475) + 80));
                                gm_b16_8 t477 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                                gm_tnpifv20irnph t478;
                                GM_ST(gm_b16_8, ((uint8_t *)&t478 + 0), t477);
                                gm_tabb710d9mq2e t479;
                                gm_f_format_tv3i6qj((void *)&t479, (void *)NULL, cx, NULL, t478);
                                if (gm_unw.kind) {
                                    goto gm_ll459;
                                }
                                if (((((uint8_t *)&t479))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t479) + 8, gm_srcgr2mvgis129a, 109, 29);
                                gm_b16_8 t480 = GM_LD(gm_b16_8, (((uint8_t *)&t479) + 8));
                                gm_b16_8 t481 = GM_LD(gm_b16_8, ((uint8_t *)&t480));
                                gm_b0_1 t482 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t474, t476, t481);
                                if (gm_unw.kind) {
                                    goto gm_ll459;
                                }
                            }
                            goto lc455;
                        }
                        gm_l62 = t471;
                    }
                    {
                        void * t483 = F.l0;
                        void * t484 = (void *)((uint8_t *)&gm_l20);
                        void * t485 = (void *)((uint8_t *)&F.l61);
                        gm_f_clause_signature_9v4rkv8((void *)&gm_l63, (void *)NULL, cx, NULL, t483, t484, t485);
                        if (gm_unw.kind) {
                            goto gm_ll459;
                        }
                    }
                    {
                        gm_f_list_af74s0q((void *)&F.l64, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll459;
                        }
                    }
                    {
                        void * t486 = (void *)((uint8_t *)&F.l64);
                        gm_b16_8 t487 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t488 = GM_LD(gm_b16_8, ((uint8_t *)&t487));
                        gm_tftb1odc739t5 t489 = gm_f_extend_vi643oe(t486, t488);
                        if (((((uint8_t *)&t489))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t489) + 1, gm_srcgr2mvgis129a, 115, 9);
                        gm_b0_1 t490 = GM_LD(gm_b0_1, (((uint8_t *)&t489) + 1));
                    }
                    {
                        void * t491 = (void *)((uint8_t *)&F.l64);
                        gm_t5v0c8ia6a5mo t492;
                        memset((uint8_t *)&t492 + 49, 0, 7);
                        gm_b16_8 t493;
                        GM_ST(void *, (uint8_t *)&t493, (void *)(gm_sho5nuaja1vvf));
                        GM_ST(uintptr_t, (uint8_t *)&t493 + 8, (uintptr_t)(4));
                        GM_ST(gm_b16_8, (uint8_t *)&t492 + 0, t493);
                        gm_t88rcucg1b7uk t494 = gm_l17;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t492 + 16, t494);
                        void * t495 = gm_l2;
                        void * t496 = GM_LD(void *, (((uint8_t *)t495) + 32));
                        gm_tl874t2atnlh3 t497 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t496) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t492 + 20, t497);
                        GM_ST(void *, (uint8_t *)&t492 + 32, ((void *)0));
                        gm_t9805qtonbsag t498;
                        memset(&t498, 0, sizeof t498);
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t492 + 40, t498);
                        GM_ST(uint8_t, (uint8_t *)&t492 + 48, ((uint8_t)0));
                        gm_tftb1odc739t5 t499 = gm_f_push_o8u01sh(t491, &t492);
                        if (((((uint8_t *)&t499))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t499) + 1, gm_srcgr2mvgis129a, 116, 9);
                        gm_b0_1 t500 = GM_LD(gm_b0_1, (((uint8_t *)&t499) + 1));
                    }
                    {
                        void * t501 = (void *)((uint8_t *)&F.l64);
                        gm_b16_8 t502 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 0));
                        gm_b16_8 t503 = GM_LD(gm_b16_8, ((uint8_t *)&t502));
                        gm_tftb1odc739t5 t504 = gm_f_extend_vi643oe(t501, t503);
                        if (((((uint8_t *)&t504))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t504) + 1, gm_srcgr2mvgis129a, 117, 9);
                        gm_b0_1 t505 = GM_LD(gm_b0_1, (((uint8_t *)&t504) + 1));
                    }
                    {
                        void * t506 = F.l0;
                        gm_t9qkh0rsooc8d t507;
                        GM_ST(uint8_t, ((uint8_t *)&t507 + 0), ((uint8_t)2ULL));
                        gm_b16_8 t508 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                        uint64_t t509 = gm_l62;
                        uint64_t t510 = (uintptr_t)t509;
                        void * t511 = gm_l2;
                        uint64_t t512 = GM_LD(uintptr_t, (((uint8_t *)t511) + 64) + 8);
                        if (t510 >= (uintptr_t)(t512)) gm_trap_index(t510, t512, gm_srcgr2mvgis129a, 118, 51);
                        gm_tl874t2atnlh3 t513 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t511) + 64)) + t510 * 72) + 56));
                        uint64_t t514 = gm_l1;
                        gm_b16_8 t515 = GM_LD(gm_b16_8, (((uint8_t *)&F.l64) + 0));
                        gm_t88rcucg1b7uk t516 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l63) + 16));
                        gm_tnpifv20irnph t517 = gm_l18;
                        uint32_t t518 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t506, t507, t508, t513, t514, t515, t516, t517);
                        if (gm_unw.kind) {
                            goto gm_ll459;
                        }
                        gm_l65 = t518;
                    }
                    {
                        uint32_t t519 = gm_l65;
                        uint64_t t520 = (uint64_t)t519;
                        uint64_t t521 = (uintptr_t)t520;
                        void * t522 = F.l0;
                        uint64_t t523 = GM_LD(uintptr_t, ((((uint8_t *)t522) + 344) + 0) + 8);
                        if (t521 >= (uintptr_t)(t523)) gm_trap_index(t521, t523, gm_srcgr2mvgis129a, 120, 9);
                        gm_b16_8 t524 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t522) + 344) + 0)) + t521 * 136) + 80), t524);
                    }
                    {
                        void * t525 = (void *)((uint8_t *)&F.l58);
                        uint32_t t526 = gm_l65;
                        gm_tftb1odc739t5 t527 = gm_f_push_im9d4ro(t525, t526);
                        if (((((uint8_t *)&t527))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t527) + 1, gm_srcgr2mvgis129a, 121, 9);
                        gm_b0_1 t528 = GM_LD(gm_b0_1, (((uint8_t *)&t527) + 1));
                    }
                    {
                        void * t529 = F.l0;
                        void * t530 = (void *)(((uint8_t *)t529) + 680);
                        gm_tbeui4bbnm52b t531;
                        memset((uint8_t *)&t531 + 21, 0, 3);
                        uint64_t t532 = gm_l1;
                        GM_ST(uint64_t, (uint8_t *)&t531 + 0, t532);
                        uint64_t t533 = gm_l62;
                        uint64_t t534 = (uintptr_t)t533;
                        void * t535 = gm_l2;
                        uint64_t t536 = GM_LD(uintptr_t, (((uint8_t *)t535) + 64) + 8);
                        if (t534 >= (uintptr_t)(t536)) gm_trap_index(t534, t536, gm_srcgr2mvgis129a, 122, 76);
                        void * t537 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t535) + 64)) + t534 * 72);
                        GM_ST(void *, (uint8_t *)&t531 + 8, t537);
                        uint32_t t538 = gm_l65;
                        GM_ST(uint32_t, (uint8_t *)&t531 + 16, t538);
                        gm_t9qkh0rsooc8d t539 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l61) + 16));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t531 + 20, t539);
                        memcpy((uint8_t *)&t531 + 24, ((uint8_t *)&gm_l20), 24);
                        gm_tftb1odc739t5 t540 = gm_f_push_qkkb0em(t530, &t531);
                        if (((((uint8_t *)&t540))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t540) + 1, gm_srcgr2mvgis129a, 122, 9);
                        gm_b0_1 t541 = GM_LD(gm_b0_1, (((uint8_t *)&t540) + 1));
                    }
                    goto me464;
mn465:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 107, 5);
me464:;
                }
lc455:;
                {
                    uint64_t t543 = gm_l60;
                    uint64_t r544; bool o545 = __builtin_add_overflow(t543, ((uint64_t)1ULL), &r544);
                    uint64_t t546 = r544;
                    gm_l60 = t546;
                }
            }
gm_ll459:;
            if (gm_unw.frame == &F.h && gm_unw.label == 11) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb455; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc455; }
            }
            goto gm_lad;
lb455:;
        }
    }
    {
        uint64_t t548 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 0) + 8);
        uint8_t t549 = ((t548 == ((uint64_t)0ULL)));
        if (!t549) goto ie547;
        {
            void * t551 = F.l0;
            void * t552 = (void *)(((uint8_t *)t551) + 544);
            gm_tvc83pqnepc2e t553;
            memset((uint8_t *)&t553 + 28, 0, 4);
            memcpy((uint8_t *)&t553 + 0, ((uint8_t *)&gm_l20), 24);
            gm_t88rcucg1b7uk t554 = gm_l17;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t553 + 24, t554);
            gm_tnpifv20irnph t555 = gm_l18;
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&t553 + 32, t555);
            gm_b16_8 t556 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t553 + 48, t556);
            gm_tftb1odc739t5 t557 = gm_f_push_mi6l2kj(t552, &t553);
            if (((((uint8_t *)&t557))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t557) + 1, gm_srcgr2mvgis129a, 125, 9);
            gm_b0_1 t558 = GM_LD(gm_b0_1, (((uint8_t *)&t557) + 1));
        }
        goto ix547;
ie547:;
        {
            void * t560 = F.l0;
            void * t561 = (void *)(((uint8_t *)t560) + 592);
            gm_t9qmk75lrp9p4 t562;
            memset((uint8_t *)&t562 + 44, 0, 4);
            memset((uint8_t *)&t562 + 92, 0, 4);
            gm_b16_8 t563 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t562 + 0, t563);
            memcpy((uint8_t *)&t562 + 16, ((uint8_t *)&gm_l20), 24);
            gm_t88rcucg1b7uk t564 = gm_l17;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t562 + 40, t564);
            gm_tnpifv20irnph t565 = gm_l18;
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&t562 + 48, t565);
            gm_b16_8 t566 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t562 + 64, t566);
            void * t567 = gm_l2;
            gm_tl874t2atnlh3 t568 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t567) + 80));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t562 + 80, t568);
            gm_tftb1odc739t5 t569 = gm_f_push_9gv7k4m(t561, &t562);
            if (((((uint8_t *)&t569))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t569) + 1, gm_srcgr2mvgis129a, 127, 9);
            gm_b0_1 t570 = GM_LD(gm_b0_1, (((uint8_t *)&t569) + 1));
        }
ix547:;
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
    GM_STACK_CHECK(&F, 372u);
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
                    uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&t19));
                    gm_l3 = t22;
                    {
                        uint64_t t24 = gm_l3;
                        uint64_t t25 = (uintptr_t)t24;
                        void * t26 = gm_l0;
                        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
                        uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 280) + 0) + 8);
                        if (t25 >= (uintptr_t)(t28)) gm_trap_index(t25, t28, gm_srcgr2mvgis129a, 12, 14);
                        gm_tp8h583g7s5hd t29 = GM_LD(gm_tp8h583g7s5hd, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 280) + 0)) + t25 * 168));
                        F.l4 = t29;
                    }
                    {
                        uint8_t t31 = GM_LD(uint8_t, (((((uint8_t *)&F.l4) + 8) + 64) + 0));
                        if (t31 != ((uint8_t)2ULL)) goto ie30;
                        void * t32 = GM_LD(void *, (((((uint8_t *)&F.l4) + 8) + 64) + 8));
                        gm_l5 = t32;
                        {
                            void * t34 = gm_l0;
                            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 0));
                            void * t36 = gm_l5;
                            gm_b0_1 t37 = gm_f_declare_handler_gd3gpcg((void *)NULL, cx, NULL, t34, t35, t36);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                        }
                        goto ix30;
ie30:;
                        {
                            goto lc13;
                        }
ix30:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 11, 5);
me20:;
                }
lc13:;
                {
                    uint64_t t40 = gm_l2;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l2 = t43;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51177of(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_handler_template_args_dlm07kb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_handler_template_args_dlm07kb F;
    void * gm_l0; /* s */
    void * gm_l1; /* tpl */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 272u);
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
                    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&t20));
                    gm_l7 = t23;
                    {
                        void * t25 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                        uint32_t t28 = gm_l7;
                        gm_tkn42u7na3t81 t29 = gm_f_bound_to_5c11t0h(t27, t28);
                        gm_t88rcucg1b7uk t30;
                        if (((((uint8_t *)&t29))[4] != 0)) {
                            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t29) + 0));
                            t30 = t31;
                        } else {
                            gm_th2lqbo77pvsc t32;
                            memset(&t32, 0, sizeof t32);
                            uint8_t t33 = 0;
                            F.ret = t32;
                            goto gm_fret;
                        }
                        gm_tftb1odc739t5 t34 = gm_f_push_k3l9nic(t25, GM_CONV(gm_csguankko4lmhd, t30));
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcgr2mvgis129a, 142, 29);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 142, 5);
me21:;
                }
lc11:;
                {
                    uint64_t t37 = gm_l6;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l6 = t40;
                }
            }
lb11:;
        }
    }
    {
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t41);
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
    GM_STACK_CHECK(&F, 1885u);
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
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
            gm_l3 = t7;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l4;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    gm_tcth15utccicj t14;
                    uint64_t t15 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t14 + 0, t15);
                    uint64_t t16 = gm_l4;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    memcpy((uint8_t *)&t14 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t17 * 64), 64);
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t14) + 0));
                    gm_l5 = t21;
                    F.l6 = (*(gm_tvc83pqnepc2e *)(((uint8_t *)&t14) + 8));
                    {
                        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 24));
                        gm_t88rcucg1b7uk t25 = gm_l1;
                        uint8_t t26 = (gm_eq9m6mvtmgde(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie23;
                        {
                            void * t28 = (void *)((uint8_t *)&F.l2);
                            uint64_t t29 = gm_l5;
                            uint32_t t30 = (uint32_t)t29;
                            gm_tftb1odc739t5 t31 = gm_f_push_im9d4ro(t28, t30);
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcgr2mvgis129a, 328, 28);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 327, 5);
me19:;
                }
lc8:;
                {
                    uint64_t t34 = gm_l4;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l4 = t37;
                }
            }
lb8:;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            void * t39 = gm_l0;
            uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)t39) + 592) + 0) + 8);
            gm_l7 = t40;
        }
        {
            for (;;) {
                uint64_t t42 = gm_l8;
                uint64_t t43 = gm_l7;
                uint8_t t44 = (t42 < t43);
                if (!t44) goto lb41;
                {
                    uint64_t t47 = gm_l8;
                    uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)&t47));
                    gm_l9 = t50;
                    {
                        uint64_t t52 = gm_l9;
                        uint64_t t53 = (uintptr_t)t52;
                        void * t54 = gm_l0;
                        uint64_t t55 = GM_LD(uintptr_t, ((((uint8_t *)t54) + 592) + 0) + 8);
                        if (t53 >= (uintptr_t)(t55)) gm_trap_index(t53, t55, gm_srcgr2mvgis129a, 331, 16);
                        gm_t9qmk75lrp9p4 t56 = GM_LD(gm_t9qmk75lrp9p4, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t54) + 592) + 0)) + t53 * 96));
                        F.l10 = t56;
                    }
                    {
                        void * t57 = gm_l0;
                        void * t58 = (void *)((uint8_t *)&F.l10);
                        gm_t88rcucg1b7uk t59 = gm_l1;
                        gm_th2lqbo77pvsc t60;
                        gm_f_handler_template_args_dlm07kb((void *)&t60, (void *)NULL, cx, NULL, t57, t58, t59);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_b16_8 t61;
                        if (((((uint8_t *)&t60))[16] != 0)) {
                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 0));
                            t61 = t62;
                        } else {
                            goto lc41;
                        }
                        gm_l11 = t61;
                    }
                    {
                        void * t63 = (void *)((uint8_t *)&F.l10);
                        gm_b16_8 t64 = gm_l11;
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                        gm_b16_8 t66 = gm_f_template_map_lfm02i0((void *)NULL, cx, NULL, t63, t65);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_l12 = t66;
                    }
                    {
                        void * t68 = gm_l0;
                        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 40));
                        gm_b16_8 t70 = gm_l12;
                        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                        gm_t88rcucg1b7uk t72 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t68, t69, t71);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_t88rcucg1b7uk t73 = gm_l1;
                        uint8_t t74 = (!gm_eq9m6mvtmgde(((uint8_t *)&t72), ((uint8_t *)&t73)));
                        if (!t74) goto ie67;
                        {
                            goto lc41;
                        }
                        goto ix67;
ie67:;
ix67:;
                    }
                    {
                        gm_l13 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                            gm_l14 = t77;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t79 = gm_l15;
                                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t81 = (t79 < t80);
                                if (!t81) goto lb78;
                                {
                                    uint64_t t84 = gm_l15;
                                    uint64_t t85 = (uintptr_t)t84;
                                    uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    uint32_t t87 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t85 * 4));
                                    uint32_t t90 = GM_LD(uint32_t, ((uint8_t *)&t87));
                                    gm_l16 = t90;
                                    {
                                        uint32_t t93 = gm_l16;
                                        uint64_t t94 = (uint64_t)t93;
                                        uint64_t t95 = (uintptr_t)t94;
                                        void * t96 = gm_l0;
                                        uint64_t t97 = GM_LD(uintptr_t, ((((uint8_t *)t96) + 544) + 0) + 8);
                                        if (t95 >= (uintptr_t)(t97)) gm_trap_index(t95, t97, gm_srcgr2mvgis129a, 338, 16);
                                        uint32_t t98 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t96) + 544) + 0)) + t95 * 64) + 0) + 0));
                                        uint32_t t99 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 16) + 0));
                                        uint8_t t100 = ((t98 == t99));
                                        if (!t100) goto ie92;
                                        {
                                            gm_l13 = ((uint8_t)1);
                                        }
                                        goto ix92;
ie92:;
ix92:;
                                    }
                                    goto me88;
mn89:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 337, 9);
me88:;
                                }
lc78:;
                                {
                                    uint64_t t103 = gm_l15;
                                    uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                                    uint64_t t106 = r104;
                                    gm_l15 = t106;
                                }
                            }
lb78:;
                        }
                    }
                    {
                        uint8_t t108 = gm_l13;
                        if (!t108) goto ie107;
                        {
                            goto lc41;
                        }
                        goto ix107;
ie107:;
ix107:;
                    }
                    {
                        gm_t9805qtonbsag t110;
                        memset(&t110, 0, sizeof t110);
                        gm_l17 = t110;
                    }
                    {
                        {
                            void * t112 = gm_l0;
                            gm_b16_8 t113 = GM_LD(gm_b16_8, ((((uint8_t *)t112) + 632) + 0));
                            gm_l18 = t113;
                        }
                        {
                            gm_l19 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t115 = gm_l19;
                                uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                uint8_t t117 = (t115 < t116);
                                if (!t117) goto lb114;
                                {
                                    uint64_t t120 = gm_l19;
                                    uint64_t t121 = (uintptr_t)t120;
                                    uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    gm_tm9oc6v1he136 t125 = GM_LD(gm_tm9oc6v1he136, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t121 * 32));
                                    gm_l20 = t125;
                                    {
                                        uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 0));
                                        uint64_t t129 = gm_l9;
                                        uint8_t t130 = ((t128 == t129));
                                        if (!t130) goto ie127;
                                        uint64_t t131 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                                        uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                        uint8_t t133 = ((t131 == t132));
                                        if (!t133) goto ie127;
                                        {
                                            gm_l21 = ((uint8_t)1);
                                        }
                                        {
                                            {
                                                gm_b16_8 t136 = gm_l11;
                                                gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                                                gm_l22 = t137;
                                            }
                                            {
                                                gm_l23 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t139 = gm_l23;
                                                    uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                                    uint8_t t141 = (t139 < t140);
                                                    if (!t141) goto lb138;
                                                    {
                                                        uint64_t t144 = gm_l23;
                                                        uint64_t t145 = gm_l23;
                                                        uint64_t t146 = (uintptr_t)t145;
                                                        uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                                        gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t146 * 4));
                                                        gm_tlcm4lvu3g1a8 t149;
                                                        GM_ST(uint32_t, (uint8_t *)&t149 + 12, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&t149 + 0), t144);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t149 + 8), t148);
                                                        uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&t149) + 0));
                                                        gm_l24 = t152;
                                                        gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t149) + 8));
                                                        gm_l25 = t153;
                                                        {
                                                            uint64_t t156 = gm_l24;
                                                            uint64_t t157 = (uintptr_t)t156;
                                                            uint64_t t158 = GM_LD(uintptr_t, (((uint8_t *)&gm_l20) + 8) + 8);
                                                            if (t157 >= (uintptr_t)(t158)) gm_trap_index(t157, t158, gm_srcgr2mvgis129a, 346, 24);
                                                            gm_t88rcucg1b7uk t159 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l20) + 8)) + t157 * 4));
                                                            gm_t88rcucg1b7uk t160 = gm_l25;
                                                            uint8_t t161 = (!gm_eq9m6mvtmgde(((uint8_t *)&t159), ((uint8_t *)&t160)));
                                                            if (!t161) goto ie155;
                                                            {
                                                                gm_l21 = ((uint8_t)0);
                                                            }
                                                            goto ix155;
ie155:;
ix155:;
                                                        }
                                                        goto me150;
mn151:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 345, 17);
me150:;
                                                    }
lc138:;
                                                    {
                                                        uint64_t t164 = gm_l23;
                                                        uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                                                        uint64_t t167 = r165;
                                                        gm_l23 = t167;
                                                    }
                                                }
lb138:;
                                            }
                                        }
                                        {
                                            uint8_t t169 = gm_l21;
                                            if (!t169) goto ie168;
                                            {
                                                uint32_t t171 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 24));
                                                gm_t9805qtonbsag t172;
                                                GM_ST(uint8_t, (uint8_t *)&t172 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t172 + 6, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t172), t171);
                                                ((uint8_t *)&t172)[4] = 1;
                                                gm_l17 = t172;
                                            }
                                            goto ix168;
ie168:;
ix168:;
                                        }
                                        goto ix127;
ie127:;
ix127:;
                                    }
                                    goto me123;
mn124:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 342, 9);
me123:;
                                }
lc114:;
                                {
                                    uint64_t t174 = gm_l19;
                                    uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
                                    uint64_t t177 = r175;
                                    gm_l19 = t177;
                                }
                            }
lb114:;
                        }
                    }
                    {
                        if (!((((uint8_t *)&gm_l17))[4] != 0)) goto ie178;
                        uint32_t t179 = GM_LD(uint32_t, ((uint8_t *)&gm_l17));
                        gm_l26 = t179;
                        {
                            void * t181 = (void *)((uint8_t *)&F.l2);
                            uint32_t t182 = gm_l26;
                            gm_tftb1odc739t5 t183 = gm_f_push_im9d4ro(t181, t182);
                            if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 1, gm_srcgr2mvgis129a, 352, 13);
                            gm_b0_1 t184 = GM_LD(gm_b0_1, (((uint8_t *)&t183) + 1));
                        }
                        {
                            goto lc41;
                        }
                        goto ix178;
ie178:;
ix178:;
                    }
                    {
                        gm_f_list_go2qg0b((void *)&F.l27, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                    }
                    {
                        {
                            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 64));
                            gm_l28 = t186;
                        }
                        {
                            gm_l29 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t188 = gm_l29;
                                uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                uint8_t t190 = (t188 < t189);
                                if (!t190) goto lb187;
                                {
                                    uint64_t t193 = gm_l29;
                                    uint64_t t194 = (uintptr_t)t193;
                                    uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                    uint32_t t196 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t194 * 4));
                                    uint32_t t199 = GM_LD(uint32_t, ((uint8_t *)&t196));
                                    gm_l30 = t199;
                                    {
                                        void * t201 = (void *)((uint8_t *)&F.l27);
                                        void * t202 = gm_l0;
                                        uint32_t t203 = gm_l30;
                                        gm_b16_8 t204 = gm_l11;
                                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&t204));
                                        gm_tl874t2atnlh3 t206 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 80));
                                        uint32_t t207 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t202, t203, t205, t206);
                                        if (gm_unw.kind) {
                                            goto gm_ll191;
                                        }
                                        gm_tftb1odc739t5 t208 = gm_f_push_im9d4ro(t201, t207);
                                        if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 1, gm_srcgr2mvgis129a, 356, 32);
                                        gm_b0_1 t209 = GM_LD(gm_b0_1, (((uint8_t *)&t208) + 1));
                                    }
                                    goto me197;
mn198:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 356, 9);
me197:;
                                }
lc187:;
                                {
                                    uint64_t t211 = gm_l29;
                                    uint64_t r212; bool o213 = __builtin_add_overflow(t211, ((uint64_t)1ULL), &r212);
                                    uint64_t t214 = r212;
                                    gm_l29 = t214;
                                }
                            }
gm_ll191:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb187; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc187; }
                            }
                            goto gm_ll45;
lb187:;
                        }
                    }
                    {
                        void * t215 = gm_l0;
                        uint64_t t216 = GM_LD(uintptr_t, ((((uint8_t *)t215) + 544) + 0) + 8);
                        uint32_t t217 = (uint32_t)t216;
                        gm_l31 = t217;
                    }
                    {
                        void * t218 = gm_l0;
                        void * t219 = (void *)(((uint8_t *)t218) + 544);
                        gm_tvc83pqnepc2e t220;
                        memset((uint8_t *)&t220 + 28, 0, 4);
                        void * t221 = gm_l0;
                        void * t222 = (void *)(((uint8_t *)&F.l10) + 16);
                        gm_b16_8 t223 = gm_l12;
                        gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&t223));
                        gm_f_subst_eref_kf9o1q5((void *)&(*(gm_tiilmfd16v3fc *)((uint8_t *)&t220 + 0)), (void *)NULL, cx, NULL, t221, t222, t224);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        gm_t88rcucg1b7uk t225 = gm_l1;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t220 + 24, t225);
                        void * t226 = gm_l0;
                        void * t227 = (void *)(((uint8_t *)&F.l10) + 48);
                        gm_b16_8 t228 = gm_l12;
                        gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
                        gm_tnpifv20irnph t230 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t226, t227, t229);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        GM_ST(gm_tnpifv20irnph, (uint8_t *)&t220 + 32, t230);
                        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t220 + 48, t231);
                        gm_tftb1odc739t5 t232 = gm_f_push_mi6l2kj(t219, &t220);
                        if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 1, gm_srcgr2mvgis129a, 358, 9);
                        gm_b0_1 t233 = GM_LD(gm_b0_1, (((uint8_t *)&t232) + 1));
                    }
                    {
                        void * t234 = gm_l0;
                        void * t235 = (void *)(((uint8_t *)t234) + 632);
                        gm_tm9oc6v1he136 t236;
                        memset((uint8_t *)&t236 + 28, 0, 4);
                        uint64_t t237 = gm_l9;
                        GM_ST(uint64_t, (uint8_t *)&t236 + 0, t237);
                        gm_b16_8 t238 = gm_l11;
                        gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
                        GM_ST(gm_b16_8, (uint8_t *)&t236 + 8, t239);
                        uint32_t t240 = gm_l31;
                        GM_ST(uint32_t, (uint8_t *)&t236 + 24, t240);
                        gm_tftb1odc739t5 t241 = gm_f_push_b9c0fdf(t235, &t236);
                        if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_srcgr2mvgis129a, 359, 9);
                        gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
                    }
                    {
                        void * t243 = (void *)((uint8_t *)&F.l2);
                        uint32_t t244 = gm_l31;
                        gm_tftb1odc739t5 t245 = gm_f_push_im9d4ro(t243, t244);
                        if (((((uint8_t *)&t245))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t245) + 1, gm_srcgr2mvgis129a, 360, 9);
                        gm_b0_1 t246 = GM_LD(gm_b0_1, (((uint8_t *)&t245) + 1));
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 330, 5);
me48:;
                }
lc41:;
                {
                    uint64_t t248 = gm_l8;
                    uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                    uint64_t t251 = r249;
                    gm_l8 = t251;
                }
            }
gm_ll45:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb41; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc41; }
            }
            goto gm_lad;
lb41:;
        }
    }
    {
        gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t253 = 0;
        F.ret = t252;
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
extern __attribute__((aligned(4))) uint8_t gm_f_ident_is_uo88ron(void * a0, gm_b16_8 a1);
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
    GM_STACK_CHECK(&F, 1534u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        uint8_t t5 = (gm_eq9m6mvtmgde(((uint8_t *)&t3), ((uint8_t *)&t4)));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t9, GM_CONV(gm_csguankko4lmhd, t10));
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_t88rcucg1b7uk t13 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l4, t12, GM_CONV(gm_csguankko4lmhd, t13));
    }
    {
        uint8_t t15 = 0;
        uint8_t t16;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t19 != ((uint8_t)19ULL)) goto mn18;
        t16 = ((uint8_t)1);
        goto me17;
mn18:;
        t16 = ((uint8_t)0);
        goto me17;
mn20:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 174, 8);
me17:;
        if (!t16) {
            uint8_t t21;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t24 != ((uint8_t)19ULL)) goto mn23;
            t21 = ((uint8_t)1);
            goto me22;
mn23:;
            t21 = ((uint8_t)0);
            goto me22;
mn25:;
            gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 174, 27);
me22:;
            t15 = t21;
        } else t15 = 1;
        if (!t15) goto ie14;
        {
            uint8_t t27 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        switch (t43) { case ((uint8_t)0ULL): goto ma41; case ((uint8_t)1ULL): goto ma41; case ((uint8_t)2ULL): goto ma41; case ((uint8_t)3ULL): goto ma41; case ((uint8_t)4ULL): goto ma41; case ((uint8_t)5ULL): goto ma41; case ((uint8_t)6ULL): goto ma41; case ((uint8_t)7ULL): goto ma41; case ((uint8_t)8ULL): goto ma31; case ((uint8_t)9ULL): goto ma33; case ((uint8_t)10ULL): goto ma32; case ((uint8_t)11ULL): goto ma30; case ((uint8_t)12ULL): goto ma37; case ((uint8_t)13ULL): goto ma34; case ((uint8_t)14ULL): goto ma36; case ((uint8_t)15ULL): goto ma39; case ((uint8_t)16ULL): goto ma40; case ((uint8_t)17ULL): goto ma38; case ((uint8_t)18ULL): goto ma35; case ((uint8_t)19ULL): goto ma41; case ((uint8_t)20ULL): goto ma29; case ((uint8_t)21ULL): goto ma41; case ((uint8_t)22ULL): goto ma41; case ((uint8_t)23ULL): goto ma41; case ((uint8_t)24ULL): goto ma41; case ((uint8_t)25ULL): goto ma41; case ((uint8_t)26ULL): goto ma41; default: goto ma41; }
ma29:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t45 != ((uint8_t)20ULL)) goto mn44;
        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l5 = t46;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t51 != ((uint8_t)20ULL)) goto pa50;
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l6 = t52;
        goto po49;
pa50:;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t54 != ((uint8_t)11ULL)) goto pa53;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l6 = t55;
        goto po49;
pa53:;
        goto mn48;
po49:;
        void * t56 = gm_l0;
        gm_t88rcucg1b7uk t57 = gm_l5;
        gm_t88rcucg1b7uk t58 = gm_l6;
        uint8_t t59 = gm_f_may_unify_0noeh56(t56, GM_CONV(gm_csguankko4lmhd, t57), GM_CONV(gm_csguankko4lmhd, t58));
        uint8_t t60 = 0;
        F.ret = t59;
        goto gm_fret;
        goto me47;
mn48:;
        uint8_t t62 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me47;
mn61:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 176, 30);
me47:;
        goto me28;
mn44:;
ma30:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t64 != ((uint8_t)11ULL)) goto mn63;
        uint64_t t65 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l8 = t65;
        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 8));
        gm_l9 = t66;
        gm_tnnjeerolos0a t67 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l3) + 8) + 12));
        gm_l10 = t67;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        switch (t73) { case ((uint8_t)0ULL): goto ma71; case ((uint8_t)1ULL): goto ma71; case ((uint8_t)2ULL): goto ma71; case ((uint8_t)3ULL): goto ma71; case ((uint8_t)4ULL): goto ma71; case ((uint8_t)5ULL): goto ma71; case ((uint8_t)6ULL): goto ma71; case ((uint8_t)7ULL): goto ma71; case ((uint8_t)8ULL): goto ma71; case ((uint8_t)9ULL): goto ma71; case ((uint8_t)10ULL): goto ma71; case ((uint8_t)11ULL): goto ma70; case ((uint8_t)12ULL): goto ma71; case ((uint8_t)13ULL): goto ma71; case ((uint8_t)14ULL): goto ma71; case ((uint8_t)15ULL): goto ma71; case ((uint8_t)16ULL): goto ma71; case ((uint8_t)17ULL): goto ma71; case ((uint8_t)18ULL): goto ma71; case ((uint8_t)19ULL): goto ma71; case ((uint8_t)20ULL): goto ma69; case ((uint8_t)21ULL): goto ma71; case ((uint8_t)22ULL): goto ma71; case ((uint8_t)23ULL): goto ma71; case ((uint8_t)24ULL): goto ma71; case ((uint8_t)25ULL): goto ma71; case ((uint8_t)26ULL): goto ma71; default: goto ma71; }
ma69:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t75 != ((uint8_t)20ULL)) goto mn74;
        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l11 = t76;
        void * t77 = gm_l0;
        gm_t88rcucg1b7uk t78 = gm_l9;
        gm_t88rcucg1b7uk t79 = gm_l11;
        uint8_t t80 = gm_f_may_unify_0noeh56(t77, GM_CONV(gm_csguankko4lmhd, t78), GM_CONV(gm_csguankko4lmhd, t79));
        uint8_t t81 = 0;
        F.ret = t80;
        goto gm_fret;
        goto me68;
mn74:;
ma70:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t83 != ((uint8_t)11ULL)) goto mn82;
        uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l12 = t84;
        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l13 = t85;
        gm_tnnjeerolos0a t86 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l4) + 8) + 12));
        gm_l14 = t86;
        uint8_t t87 = 0;
        uint8_t t88 = 0;
        uint64_t t89 = gm_l8;
        uint64_t t90 = gm_l12;
        uint8_t t91 = ((t89 == t90));
        if (t91) {
            gm_tnnjeerolos0a t92 = gm_l10;
            gm_tnnjeerolos0a t93 = gm_l14;
            uint8_t t94 = gm_f_array_kind_same_rl3epej(t92, t93);
            t88 = t94;
        } else t88 = 0;
        if (t88) {
            void * t95 = gm_l0;
            gm_t88rcucg1b7uk t96 = gm_l9;
            gm_t88rcucg1b7uk t97 = gm_l13;
            uint8_t t98 = gm_f_may_unify_0noeh56(t95, GM_CONV(gm_csguankko4lmhd, t96), GM_CONV(gm_csguankko4lmhd, t97));
            t87 = t98;
        } else t87 = 0;
        uint8_t t99 = 0;
        F.ret = t87;
        goto gm_fret;
        goto me68;
mn82:;
ma71:;
        uint8_t t101 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me68;
mn100:;
ma72:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 180, 28);
me68:;
        goto me28;
mn63:;
ma31:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t103 != ((uint8_t)8ULL)) goto mn102;
        uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l15 = t104;
        gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l16 = t105;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t107 != ((uint8_t)8ULL)) goto ie106;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l17 = t108;
        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l18 = t109;
        {
            uint8_t t111 = 0;
            uint8_t t112 = gm_l15;
            uint8_t t113 = gm_l17;
            uint8_t t114 = ((t112 == t113));
            if (t114) {
                void * t115 = gm_l0;
                gm_t88rcucg1b7uk t116 = gm_l16;
                gm_t88rcucg1b7uk t117 = gm_l18;
                uint8_t t118 = gm_f_may_unify_0noeh56(t115, GM_CONV(gm_csguankko4lmhd, t116), GM_CONV(gm_csguankko4lmhd, t117));
                t111 = t118;
            } else t111 = 0;
            uint8_t t119 = 0;
            F.ret = t111;
            goto gm_fret;
        }
        goto ix106;
ie106:;
ix106:;
        goto me28;
mn102:;
ma32:;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t121 != ((uint8_t)10ULL)) goto mn120;
        uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l19 = t122;
        gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l20 = t123;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t125 != ((uint8_t)10ULL)) goto ie124;
        uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l21 = t126;
        gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l22 = t127;
        {
            uint8_t t129 = 0;
            uint8_t t130 = gm_l19;
            uint8_t t131 = gm_l21;
            uint8_t t132 = ((t130 == t131));
            if (t132) {
                void * t133 = gm_l0;
                gm_t88rcucg1b7uk t134 = gm_l20;
                gm_t88rcucg1b7uk t135 = gm_l22;
                uint8_t t136 = gm_f_may_unify_0noeh56(t133, GM_CONV(gm_csguankko4lmhd, t134), GM_CONV(gm_csguankko4lmhd, t135));
                t129 = t136;
            } else t129 = 0;
            uint8_t t137 = 0;
            F.ret = t129;
            goto gm_fret;
        }
        goto ix124;
ie124:;
ix124:;
        goto me28;
mn120:;
ma33:;
        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t139 != ((uint8_t)9ULL)) goto mn138;
        uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l23 = t140;
        gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l24 = t141;
        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t143 != ((uint8_t)9ULL)) goto ie142;
        uint8_t t144 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l25 = t144;
        gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l26 = t145;
        {
            uint8_t t147 = 0;
            uint8_t t148 = gm_l23;
            uint8_t t149 = gm_l25;
            uint8_t t150 = ((t148 == t149));
            if (t150) {
                void * t151 = gm_l0;
                gm_t88rcucg1b7uk t152 = gm_l24;
                gm_t88rcucg1b7uk t153 = gm_l26;
                uint8_t t154 = gm_f_may_unify_0noeh56(t151, GM_CONV(gm_csguankko4lmhd, t152), GM_CONV(gm_csguankko4lmhd, t153));
                t147 = t154;
            } else t147 = 0;
            uint8_t t155 = 0;
            F.ret = t147;
            goto gm_fret;
        }
        goto ix142;
ie142:;
ix142:;
        goto me28;
mn138:;
ma34:;
        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t157 != ((uint8_t)13ULL)) goto mn156;
        gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
        gm_l27 = t158;
        uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t160 != ((uint8_t)13ULL)) goto ie159;
        gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l28 = t161;
        {
            void * t163 = gm_l0;
            gm_t88rcucg1b7uk t164 = gm_l27;
            gm_t88rcucg1b7uk t165 = gm_l28;
            uint8_t t166 = gm_f_may_unify_0noeh56(t163, GM_CONV(gm_csguankko4lmhd, t164), GM_CONV(gm_csguankko4lmhd, t165));
            uint8_t t167 = 0;
            F.ret = t166;
            goto gm_fret;
        }
        goto ix159;
ie159:;
ix159:;
        goto me28;
mn156:;
ma35:;
        uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t169 != ((uint8_t)18ULL)) goto mn168;
        gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
        gm_l29 = t170;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t172 != ((uint8_t)18ULL)) goto ie171;
        gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l30 = t173;
        {
            void * t175 = gm_l0;
            gm_t88rcucg1b7uk t176 = gm_l29;
            gm_t88rcucg1b7uk t177 = gm_l30;
            uint8_t t178 = gm_f_may_unify_0noeh56(t175, GM_CONV(gm_csguankko4lmhd, t176), GM_CONV(gm_csguankko4lmhd, t177));
            uint8_t t179 = 0;
            F.ret = t178;
            goto gm_fret;
        }
        goto ix171;
ie171:;
ix171:;
        goto me28;
mn168:;
ma36:;
        uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t181 != ((uint8_t)14ULL)) goto mn180;
        gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l31 = t182;
        gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
        gm_l32 = t183;
        uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t185 != ((uint8_t)14ULL)) goto ie184;
        gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l33 = t186;
        gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l34 = t187;
        {
            uint8_t t189 = 0;
            void * t190 = gm_l0;
            gm_t88rcucg1b7uk t191 = gm_l31;
            gm_t88rcucg1b7uk t192 = gm_l33;
            uint8_t t193 = gm_f_may_unify_0noeh56(t190, GM_CONV(gm_csguankko4lmhd, t191), GM_CONV(gm_csguankko4lmhd, t192));
            if (t193) {
                void * t194 = gm_l0;
                gm_t88rcucg1b7uk t195 = gm_l32;
                gm_t88rcucg1b7uk t196 = gm_l34;
                uint8_t t197 = gm_f_may_unify_0noeh56(t194, GM_CONV(gm_csguankko4lmhd, t195), GM_CONV(gm_csguankko4lmhd, t196));
                t189 = t197;
            } else t189 = 0;
            uint8_t t198 = 0;
            F.ret = t189;
            goto gm_fret;
        }
        goto ix184;
ie184:;
ix184:;
        goto me28;
mn180:;
ma37:;
        uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t200 != ((uint8_t)12ULL)) goto mn199;
        gm_b16_8 t201 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l35 = t201;
        gm_b16_8 t202 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 16));
        gm_l36 = t202;
        uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t204 != ((uint8_t)12ULL)) goto ie203;
        gm_b16_8 t205 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l37 = t205;
        gm_b16_8 t206 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 16));
        gm_l38 = t206;
        {
            uint8_t t209 = 0;
            uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
            uint64_t t211 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
            uint8_t t212 = (!(t210 == t211));
            if (!t212) {
                uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                uint8_t t215 = (!(t213 == t214));
                t209 = t215;
            } else t209 = 1;
            if (!t209) goto ie208;
            {
                uint8_t t217 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix208;
ie208:;
ix208:;
        }
        {
            {
                gm_b16_8 t219 = gm_l36;
                gm_l39 = t219;
            }
            {
                gm_l40 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t221 = gm_l40;
                    uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                    uint8_t t223 = (t221 < t222);
                    if (!t223) goto lb220;
                    {
                        gm_tf1bjg81u0glv t226;
                        uint64_t t227 = gm_l40;
                        GM_ST(uint64_t, (uint8_t *)&t226 + 0, t227);
                        uint64_t t228 = gm_l40;
                        uint64_t t229 = (uintptr_t)t228;
                        uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                        gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t229 * 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t226 + 8, t231);
                        uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&t226) + 0));
                        gm_l41 = t234;
                        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t226) + 8));
                        gm_l42 = t235;
                        {
                            gm_b16_8 t238 = gm_l42;
                            uint64_t t239 = gm_l41;
                            uint64_t t240 = (uintptr_t)t239;
                            uint64_t t241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                            if (t240 >= (uintptr_t)(t241)) gm_trap_index(t240, t241, gm_srcgr2mvgis129a, 194, 30);
                            gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t240 * 16));
                            uint8_t t243 = (!gm_eq0iqiubfm7e(((uint8_t *)&t238), ((uint8_t *)&t242)));
                            if (!t243) goto ie237;
                            {
                                uint8_t t245 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix237;
ie237:;
ix237:;
                        }
                        goto me232;
mn233:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 193, 13);
me232:;
                    }
lc220:;
                    {
                        uint64_t t247 = gm_l40;
                        uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                        uint64_t t250 = r248;
                        gm_l40 = t250;
                    }
                }
lb220:;
            }
        }
        {
            {
                gm_b16_8 t252 = gm_l35;
                gm_l43 = t252;
            }
            {
                gm_l44 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t254 = gm_l44;
                    uint64_t t255 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                    uint8_t t256 = (t254 < t255);
                    if (!t256) goto lb253;
                    {
                        uint64_t t259 = gm_l44;
                        uint64_t t260 = gm_l44;
                        uint64_t t261 = (uintptr_t)t260;
                        uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                        gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t261 * 4));
                        gm_tlcm4lvu3g1a8 t264;
                        GM_ST(uint32_t, (uint8_t *)&t264 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t264 + 0), t259);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t264 + 8), t263);
                        uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&t264) + 0));
                        gm_l45 = t267;
                        gm_t88rcucg1b7uk t268 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t264) + 8));
                        gm_l46 = t268;
                        {
                            void * t271 = gm_l0;
                            gm_t88rcucg1b7uk t272 = gm_l46;
                            uint64_t t273 = gm_l45;
                            uint64_t t274 = (uintptr_t)t273;
                            uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                            if (t274 >= (uintptr_t)(t275)) gm_trap_index(t274, t275, gm_srcgr2mvgis129a, 197, 40);
                            gm_t88rcucg1b7uk t276 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t274 * 4));
                            uint8_t t277 = gm_f_may_unify_0noeh56(t271, GM_CONV(gm_csguankko4lmhd, t272), GM_CONV(gm_csguankko4lmhd, t276));
                            uint8_t t278 = (uint8_t)!t277;
                            if (!t278) goto ie270;
                            {
                                uint8_t t280 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix270;
ie270:;
ix270:;
                        }
                        goto me265;
mn266:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 196, 13);
me265:;
                    }
lc253:;
                    {
                        uint64_t t282 = gm_l44;
                        uint64_t r283; bool o284 = __builtin_add_overflow(t282, ((uint64_t)1ULL), &r283);
                        uint64_t t285 = r283;
                        gm_l44 = t285;
                    }
                }
lb253:;
            }
        }
        {
            uint8_t t286 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix203;
ie203:;
ix203:;
        goto me28;
mn199:;
ma38:;
        uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t288 != ((uint8_t)17ULL)) goto mn287;
        uint32_t t289 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 8) + 0));
        gm_l47 = t289;
        gm_b16_8 t290 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 8));
        gm_l48 = t290;
        uint8_t t292 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t292 != ((uint8_t)17ULL)) goto ie291;
        uint32_t t293 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l49 = t293;
        gm_b16_8 t294 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l50 = t294;
        {
            uint8_t t297 = 0;
            void * t298 = gm_l0;
            void * t299 = (void *)(((uint8_t *)t298) + 8);
            uint32_t t300 = gm_l47;
            uint32_t t301 = gm_f_template_9o92cvv(t299, t300);
            void * t302 = gm_l0;
            void * t303 = (void *)(((uint8_t *)t302) + 8);
            uint32_t t304 = gm_l49;
            uint32_t t305 = gm_f_template_9o92cvv(t303, t304);
            uint8_t t306 = (!(t301 == t305));
            if (!t306) {
                uint64_t t307 = GM_LD(uintptr_t, ((uint8_t *)&gm_l48) + 8);
                uint64_t t308 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                uint8_t t309 = (!(t307 == t308));
                t297 = t309;
            } else t297 = 1;
            if (!t297) goto ie296;
            {
                uint8_t t311 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix296;
ie296:;
ix296:;
        }
        {
            {
                gm_b16_8 t313 = gm_l48;
                gm_l51 = t313;
            }
            {
                gm_l52 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t315 = gm_l52;
                    uint64_t t316 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                    uint8_t t317 = (t315 < t316);
                    if (!t317) goto lb314;
                    {
                        gm_tdr8bh2bm3fc8 t320;
                        uint64_t t321 = gm_l52;
                        GM_ST(uint64_t, (uint8_t *)&t320 + 0, t321);
                        uint64_t t322 = gm_l52;
                        uint64_t t323 = (uintptr_t)t322;
                        uint64_t t324 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                        memcpy((uint8_t *)&t320 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t323 * 40), 40);
                        uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)&t320) + 0));
                        gm_l53 = t327;
                        F.l54 = (*(gm_toa2vhe3l4hfr *)(((uint8_t *)&t320) + 8));
                        {
                            void * t330 = gm_l0;
                            uint64_t t331 = gm_l53;
                            uint64_t t332 = (uintptr_t)t331;
                            uint64_t t333 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                            if (t332 >= (uintptr_t)(t333)) gm_trap_index(t332, t333, gm_srcgr2mvgis129a, 204, 44);
                            uint8_t t334 = gm_f_arg_may_unify_qmi580g(t330, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&F.l54)), &(*(gm_toa2vhe3l4hfr *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t332 * 40)));
                            uint8_t t335 = (uint8_t)!t334;
                            if (!t335) goto ie329;
                            {
                                uint8_t t337 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix329;
ie329:;
ix329:;
                        }
                        goto me325;
mn326:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 203, 13);
me325:;
                    }
lc314:;
                    {
                        uint64_t t339 = gm_l52;
                        uint64_t r340; bool o341 = __builtin_add_overflow(t339, ((uint64_t)1ULL), &r340);
                        uint64_t t342 = r340;
                        gm_l52 = t342;
                    }
                }
lb314:;
            }
        }
        {
            uint8_t t343 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix291;
ie291:;
ix291:;
        goto me28;
mn287:;
ma39:;
        uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t345 != ((uint8_t)15ULL)) goto mn344;
        uint8_t t346;
        uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t349 != ((uint8_t)15ULL)) goto mn348;
        t346 = ((uint8_t)1);
        goto me347;
mn348:;
        t346 = ((uint8_t)0);
        goto me347;
mn350:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 209, 26);
me347:;
        uint8_t t351 = 0;
        F.ret = t346;
        goto gm_fret;
        goto me28;
mn344:;
ma40:;
        uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t353 != ((uint8_t)16ULL)) goto mn352;
        uint8_t t354;
        uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t357 != ((uint8_t)16ULL)) goto mn356;
        t354 = ((uint8_t)1);
        goto me355;
mn356:;
        t354 = ((uint8_t)0);
        goto me355;
mn358:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 210, 29);
me355:;
        uint8_t t359 = 0;
        F.ret = t354;
        goto gm_fret;
        goto me28;
mn352:;
ma41:;
        goto me28;
mn360:;
ma42:;
        gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 175, 5);
me28:;
    }
    {
        uint8_t t362 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_template_instance_is_ovv1pcj(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_template_map_lfm02i0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_template_map_lfm02i0 F;
    void * gm_l0; /* tpl */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* p */
    GM_STACK_CHECK(&F, 296u);
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
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
            gm_l3 = t5;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = gm_l4;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t14 * 4));
                    gm_tn68el8d14vku t17;
                    GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                    GM_ST(uint32_t, ((uint8_t *)&t17 + 8), t16);
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l5 = t20;
                    uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&t17) + 8));
                    gm_l6 = t21;
                    {
                        void * t23 = (void *)((uint8_t *)&F.l2);
                        uint32_t t24 = gm_l6;
                        uint64_t t25 = gm_l5;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcgr2mvgis129a, 148, 77);
                        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 4));
                        gm_tplpbqf3duvn3 t29;
                        GM_ST(uint32_t, ((uint8_t *)&t29 + 0), t24);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t29 + 4), t28);
                        gm_tftb1odc739t5 t30 = gm_f_push_h69n5fi(t23, GM_CONV(gm_cs7e9ppsd384bj, t29));
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcgr2mvgis129a, 148, 43);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgr2mvgis129a, 148, 5);
me18:;
                }
lc6:;
                {
                    uint64_t t33 = gm_l4;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l4 = t36;
                }
            }
lb6:;
        }
    }
    {
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
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
inline __attribute__((aligned(4))) uint8_t gm_f_template_probe_is_d1ep4c6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_template_probe_is_d1ep4c6 F;
    void * gm_l0; /* s */
    void * gm_l1; /* tpl */
    void * gm_l2; /* eref */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* map */
    gm_tiilmfd16v3fc gm_l6; /* inst */
    GM_STACK_CHECK(&F, 385u);
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
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t14;
    }
    {
        void * t17 = gm_l1;
        gm_b16_8 t18 = gm_l4;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_b16_8 t20 = gm_f_template_map_lfm02i0((void *)NULL, cx, NULL, t17, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t20;
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 40));
        gm_b16_8 t25 = gm_l5;
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
        gm_t88rcucg1b7uk t27 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t22, t24, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t28 = gm_l3;
        uint8_t t29 = (!gm_eq9m6mvtmgde(((uint8_t *)&t27), ((uint8_t *)&t28)));
        if (!t29) goto ie21;
        {
            uint8_t t31 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t32 = gm_l0;
        void * t33 = gm_l1;
        void * t34 = (void *)(((uint8_t *)t33) + 16);
        gm_b16_8 t35 = gm_l5;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_f_subst_eref_kf9o1q5((void *)&gm_l6, (void *)NULL, cx, NULL, t32, t34, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t37 = (void *)((uint8_t *)&gm_l6);
        void * t38 = gm_l2;
        int32_t t39 = gm_f_cmp_ref_8ntrqll(t37, t38);
        uint8_t t40 = ((t39 == ((int32_t)0LL)));
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

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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { int64_t a[4]; } gm_t767tucha9lsh;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tm98ap4112hee f0; gm_b16_8 f1; } gm_t3olsle24rveu;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; gm_t9qkh0rsooc8d f10; gm_t9qkh0rsooc8d f11; gm_t9qkh0rsooc8d f12; gm_t9qkh0rsooc8d f13; } gm_t4qfn59m757do;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_t4ce0jd3tdro4 f0; uint8_t f1; uint8_t pe[7]; } gm_t6ie8iajlnpt1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t767tucha9lsh) == 32 && _Alignof(gm_t767tucha9lsh) == 8 && offsetof(gm_t767tucha9lsh, a) == 0, "layout of `[4]i64`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; } gm_t9rbkbpjtolar;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; uint16_t v1; } f1; } gm_tvcqs9u9iqpct;
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_t617vr72bd7b3 f0; uint8_t f1; uint8_t pe[7]; } gm_t11t9d8s5diq9;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3olsle24rveu) == 24 && _Alignof(gm_t3olsle24rveu) == 8 && offsetof(gm_t3olsle24rveu, f0) == 0 && offsetof(gm_t3olsle24rveu, f1) == 8, "layout of `(file: File, buf: []u8)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t4qfn59m757do) == 14 && _Alignof(gm_t4qfn59m757do) == 1 && offsetof(gm_t4qfn59m757do, f0) == 0 && offsetof(gm_t4qfn59m757do, f1) == 1 && offsetof(gm_t4qfn59m757do, f2) == 2 && offsetof(gm_t4qfn59m757do, f3) == 3 && offsetof(gm_t4qfn59m757do, f4) == 4 && offsetof(gm_t4qfn59m757do, f5) == 5 && offsetof(gm_t4qfn59m757do, f6) == 6 && offsetof(gm_t4qfn59m757do, f7) == 7 && offsetof(gm_t4qfn59m757do, f8) == 8 && offsetof(gm_t4qfn59m757do, f9) == 9 && offsetof(gm_t4qfn59m757do, f10) == 10 && offsetof(gm_t4qfn59m757do, f11) == 11 && offsetof(gm_t4qfn59m757do, f12) == 12 && offsetof(gm_t4qfn59m757do, f13) == 13, "layout of `(SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp, SysOp)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6ie8iajlnpt1) == 16 && _Alignof(gm_t6ie8iajlnpt1) == 8 && offsetof(gm_t6ie8iajlnpt1, f0) == 0 && offsetof(gm_t6ie8iajlnpt1, f1) == 8, "layout of `?Duration`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_trqfvmjtt4sij v1; } f1; } gm_t9922qjstblh1;
_Static_assert(sizeof(gm_t9rbkbpjtolar) == 6 && _Alignof(gm_t9rbkbpjtolar) == 1 && offsetof(gm_t9rbkbpjtolar, f0) == 0 && offsetof(gm_t9rbkbpjtolar, f1) == 1 && offsetof(gm_t9rbkbpjtolar, f2) == 2 && offsetof(gm_t9rbkbpjtolar, f3) == 3 && offsetof(gm_t9rbkbpjtolar, f4) == 4 && offsetof(gm_t9rbkbpjtolar, f5) == 5, "layout of `(Constraint, Constraint, Constraint, Constraint, Constraint, Constraint)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; int64_t v1; } f1; } gm_tarq9lp8tdk6f;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_thf5kr0is9u9i f0; gm_tppo2hmut88dq f1; } gm_tfvg1o4f8mmhi;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3olsle24rveu v1; gm_tvaas22dl64e0 v2; gm_tm98ap4112hee v3; gm_t3olsle24rveu v4; gm_t3olsle24rveu v5; } f1; } gm_tmvdbo28o1i6h;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t62ebbi2i5rkd v1; } f1; } gm_toqti1ubjnfd1;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_t4c7ipjj9s3fp f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; int64_t f5; gm_tf5srh4mp4b3i f6; } gm_ttse10gjbhca2;
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_tvcqs9u9iqpct) == 12 && _Alignof(gm_tvcqs9u9iqpct) == 4 && offsetof(gm_tvcqs9u9iqpct, f0) == 0 && offsetof(gm_tvcqs9u9iqpct, f1) == 4, "layout of `IoError!u16`");
_Static_assert(sizeof(gm_t11t9d8s5diq9) == 24 && _Alignof(gm_t11t9d8s5diq9) == 8 && offsetof(gm_t11t9d8s5diq9, f0) == 0 && offsetof(gm_t11t9d8s5diq9, f1) == 16, "layout of `?DirEntry`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tfvg1o4f8mmhi v1; } f1; } gm_t6j8vdmi81h7c;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_t9922qjstblh1) == 48 && _Alignof(gm_t9922qjstblh1) == 8 && offsetof(gm_t9922qjstblh1, f0) == 0 && offsetof(gm_t9922qjstblh1, f1) == 8, "layout of `Flow!List(string)`");
_Static_assert(sizeof(gm_tarq9lp8tdk6f) == 32 && _Alignof(gm_tarq9lp8tdk6f) == 8 && offsetof(gm_tarq9lp8tdk6f, f0) == 0 && offsetof(gm_tarq9lp8tdk6f, f1) == 8, "layout of `Flow!i64`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tfvg1o4f8mmhi) == 32 && _Alignof(gm_tfvg1o4f8mmhi) == 8 && offsetof(gm_tfvg1o4f8mmhi, f0) == 0 && offsetof(gm_tfvg1o4f8mmhi, f1) == 16, "layout of `HostResult`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t11t9d8s5diq9 v1; } f1; } gm_tiuq7k5hm0kak;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
_Static_assert(sizeof(gm_tmvdbo28o1i6h) == 32 && _Alignof(gm_tmvdbo28o1i6h) == 8 && offsetof(gm_tmvdbo28o1i6h, f0) == 0 && offsetof(gm_tmvdbo28o1i6h, f1) == 8, "layout of `HostOp`");
_Static_assert(sizeof(gm_toqti1ubjnfd1) == 32 && _Alignof(gm_toqti1ubjnfd1) == 8 && offsetof(gm_toqti1ubjnfd1, f0) == 0 && offsetof(gm_toqti1ubjnfd1, f1) == 8, "layout of `Flow!IoError!Socket`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
_Static_assert(sizeof(gm_ttse10gjbhca2) == 304 && _Alignof(gm_ttse10gjbhca2) == 8 && offsetof(gm_ttse10gjbhca2, f0) == 0 && offsetof(gm_ttse10gjbhca2, f1) == 56 && offsetof(gm_ttse10gjbhca2, f2) == 112 && offsetof(gm_ttse10gjbhca2, f3) == 168 && offsetof(gm_ttse10gjbhca2, f4) == 224 && offsetof(gm_ttse10gjbhca2, f5) == 280 && offsetof(gm_ttse10gjbhca2, f6) == 288, "layout of `SysState`");
_Static_assert(sizeof(gm_t6j8vdmi81h7c) == 40 && _Alignof(gm_t6j8vdmi81h7c) == 8 && offsetof(gm_t6j8vdmi81h7c, f0) == 0 && offsetof(gm_t6j8vdmi81h7c, f1) == 8, "layout of `Flow!HostResult`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tiuq7k5hm0kak) == 32 && _Alignof(gm_tiuq7k5hm0kak) == 8 && offsetof(gm_tiuq7k5hm0kak, f0) == 0 && offsetof(gm_tiuq7k5hm0kak, f1) == 8, "layout of `IoError!?DirEntry`");
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cpu_count_0o5882g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_empty_slice_16ab7e4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_57qa4h8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6l50clp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hgrt2o4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_khi2i98 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_1lff729 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2gkkusq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aqcn8lk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cijad0r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ducmvdu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j9i1sr8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mslt7ot {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r8m0qjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rg62rr4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tlii61s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_trt04qi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vhnrvmh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_map_3d5nsdr {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_3sndrtb {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_abi2bn7 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_irh77t1 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ql49boo {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
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
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_socket_on_host_thread_u8c2vg5 {
    gm_frame h;
    gm_toqti1ubjnfd1 ret;
};
struct gm_fr_sys_call_6jqv32o {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* vals */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_sys_code_cjtlfo1 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_count_50c5lcp {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_dest_rtrb14h {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_fs_u0grkf6 {
    gm_frame h;
    gm_tpekhfs32op0f l28; /* err */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_has_0sego5l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sys_int_32e79ed {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_kind_sjfdqbt {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_aff7h5l {
    gm_frame h;
    gm_b16_8 l6; /* host */
    uint16_t l7; /* port */
    gm_t62ebbi2i5rkd l8; /* r */
    gm_b16_8 l12; /* host */
    uint16_t l13; /* port */
    gm_t62ebbi2i5rkd l14; /* r */
    gm_tm98ap4112hee l18; /* l */
    gm_tvcqs9u9iqpct l19; /* r */
    gm_tm98ap4112hee l22; /* l */
    gm_t62ebbi2i5rkd l23; /* r */
    gm_b16_8 l27; /* data */
    gm_tm98ap4112hee l28; /* s */
    gm_thf5kr0is9u9i l29; /* r */
    gm_b16_8 l30; /* buf */
    gm_tm98ap4112hee l31; /* s */
    gm_thf5kr0is9u9i l32; /* r */
    gm_tm98ap4112hee l33; /* s */
    gm_tm98ap4112hee l34; /* l */
    gm_t9qkh0rsooc8d l35; /* how */
    gm_tm98ap4112hee l36; /* s */
    gm_ttln0d0hd70en l37; /* r */
    gm_tm98ap4112hee l40; /* s */
    gm_t6ie8iajlnpt1 l41; /* recv */
    gm_t6ie8iajlnpt1 l42; /* send */
    gm_ttln0d0hd70en l43; /* r */
    gm_tm98ap4112hee l44; /* s */
    gm_b16_8 l45; /* buf */
    gm_thf5kr0is9u9i l46; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_net_code_ihgq920 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_net_count_46l7ueq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_next_handle_r35ek81 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_sys_proc_jhdp277 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* argv */
    gm_trqfvmjtt4sij l6; /* env */
    gm_t32dckm4jfarl l10; /* opts */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_read_b0gj9fg {
    gm_frame h;
    gm_b16_8 l3; /* buf */
    gm_thf5kr0is9u9i l4; /* r */
    gm_tarq9lp8tdk6f ret;
};
struct gm_fr_sys_ret_vpa2k8i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sys_run_2vqjo3u {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_sys_src_5svsr6i {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_sys_state_cvl1lvl {
    gm_frame h;
    gm_ttse10gjbhca2 ret;
};
struct gm_fr_sys_stdio_nsl9o9u {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_sys_strings_r85ragc {
    gm_frame h;
    gm_tpekhfs32op0f l5; /* e */
    gm_trqfvmjtt4sij l7; /* out */
    gm_t9922qjstblh1 ret;
};
struct gm_fr_sys_unit_nrsqciq {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void);
__attribute__((aligned(4))) void gm_f_defaults_p077jgf(gm_closure a0);
__attribute__((aligned(4))) void gm_f_deinit_gvn47sj(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void);
__attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_ev_args_j1qj6gl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_8o0rtd2(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_f2mbmm8(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_fqooi5f(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_gej4j8d(void * a0, int64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_v07up09(void * a0, int64_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0c6jnkb(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_0tusk8g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_endcgjj(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ha6815g(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qkbvor7(void * a0, int64_t a1, gm_csfq64m1m1rt6m a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_20jm5n8(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_3j2d0bb(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_7bvuc21(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_82qm8hc(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_remove_jvpr521(void * a0, int64_t a1);
__attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_sys_call_6jqv32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
__attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
__attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cbcngu2pqs7[8] __attribute__((aligned(8)));
extern uint8_t gm_cndfnv7mbeh[8] __attribute__((aligned(8)));
extern const uint8_t gm_s27n8348bef64[63];
extern const uint8_t gm_sd8o4m7695hpq[38];
extern const uint8_t gm_sqn7kgtu67hrg[42];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcn3jjc71a2bv3[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stfd80s46313k[38];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cpu_count_0o5882g F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t t1 = gm_cpu_count();
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_16ab7e4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_57qa4h8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_57qa4h8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6l50clp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6l50clp F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hgrt2o4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_khi2i98(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_khi2i98 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1lff729 F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l34));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 418, 50);
        gm_b0_1 t4;
        t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[7])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_2gkkusq F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l44));
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l45));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 448, 50);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[10])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l46), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_aqcn8lk F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l6));
        uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l7));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 353, 54);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l8), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_cijad0r F;
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l12));
        uint16_t t3 = GM_LD(uint16_t, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l13));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 364, 50);
        gm_t62ebbi2i5rkd t5;
        t5 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn4->clauses[1])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l14), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_ducmvdu F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l31));
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l30));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 408, 54);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[5])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l32), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_j9i1sr8 F;
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l40));
        gm_t6ie8iajlnpt1 t3 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l41));
        gm_t6ie8iajlnpt1 t4 = GM_LD(gm_t6ie8iajlnpt1, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l42));
        gm_node *pn5 = gm_find(cx, 163658498);
        if (!pn5) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 441, 50);
        gm_ttln0d0hd70en t6;
        t6 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t6ie8iajlnpt1, gm_t6ie8iajlnpt1))pn5->clauses[9])(pn5->env, pn5->hctx, pn5, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l43), t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_mslt7ot F;
    GM_STACK_CHECK(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l18));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 373, 50);
        gm_tvcqs9u9iqpct t4;
        t4 = ((gm_tvcqs9u9iqpct (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[2])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_tvcqs9u9iqpct, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l19), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_r8m0qjr F;
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l33));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 414, 50);
        gm_b0_1 t4;
        t4 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[6])(pn3->env, pn3->hctx, pn3, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_rg62rr4 F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l22));
        gm_node *pn3 = gm_find(cx, 163658498);
        if (!pn3) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 383, 54);
        gm_t62ebbi2i5rkd t4;
        t4 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn3->clauses[3])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t62ebbi2i5rkd, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l23), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_tlii61s F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l28));
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l27));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 397, 54);
        gm_thf5kr0is9u9i t5;
        t5 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn4->clauses[4])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l29), t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_trt04qi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l3));
        gm_node *pn3 = gm_find(cx, 1018230276);
        if (!pn3) gm_trap("no handler installed for effect `In`", gm_srcn3jjc71a2bv3, 207, 41);
        gm_thf5kr0is9u9i t4;
        t4 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[0])(pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, ((uint8_t *)&((struct gm_fr_sys_read_b0gj9fg *)((&F.h)->up))->l4), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_vhnrvmh F;
    GM_STACK_CHECK(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tm98ap4112hee t2 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l36));
        gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l35));
        gm_node *pn4 = gm_find(cx, 163658498);
        if (!pn4) gm_trap("no handler installed for effect `Net`", gm_srcn3jjc71a2bv3, 431, 50);
        gm_ttln0d0hd70en t5;
        t5 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_t9qkh0rsooc8d))pn4->clauses[8])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_ttln0d0hd70en, ((uint8_t *)&((struct gm_fr_sys_net_aff7h5l *)((&F.h)->up))->l37), t5);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_3d5nsdr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3d5nsdr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_hgrt2o4();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_3sndrtb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_3sndrtb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_57qa4h8();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_abi2bn7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_abi2bn7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_6l50clp();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_irh77t1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_irh77t1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_16ab7e4();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ql49boo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ql49boo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_khi2i98();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2p7d2kv();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
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
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l3;
        uint8_t t12 = gm_f_aligned_q6sor4h(t10, t11);
        if (!t12) goto ie9;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            uint64_t t17 = gm_l2;
            void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
            if (!(GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto ie14;
            void * t19 = GM_LD(void *, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l4;
                void * t22 = ((uint8_t *)t21);
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_f_us_cuv01fp(t24);
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t27);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t t30 = gm_l2;
        uint64_t t31 = gm_l3;
        gm_tpekhfs32op0f t32;
        gm_f_fault_u31b92l((void *)&t32, t28, t29, t30, t31, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_count_50c5lcp F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    if (!((((uint8_t *)&gm_l0))[0] != 0)) goto mn3;
    gm_toe5fmr3vvf4d t4 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
    gm_l1 = t4;
    gm_toe5fmr3vvf4d t5 = gm_l1;
    int64_t t6 = gm_f_sys_code_cjtlfo1(t5);
    t1 = t6;
    goto me2;
mn3:;
    if (((((uint8_t *)&gm_l0))[0] != 0)) goto mn7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t8;
    uint64_t t9 = gm_l2;
    int64_t t10 = (int64_t)t9;
    t1 = t10;
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 78, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_has_0sego5l F;
    uint32_t gm_l0; /* flags */
    uint32_t gm_l1; /* bit */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l0;
    uint32_t t2 = gm_l1;
    uint32_t t3 = (uint32_t)(t1 & t2);
    uint8_t t4 = (!(t3 == ((uint32_t)0ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_kind_sjfdqbt F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 30u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    t1 = ((int64_t)0LL);
    goto me2;
mn9:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    t1 = ((int64_t)1LL);
    goto me2;
mn11:;
ma5:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn13;
    t1 = ((int64_t)2LL);
    goto me2;
mn13:;
ma6:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)3ULL)) goto mn15;
    t1 = ((int64_t)3LL);
    goto me2;
mn15:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 96, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_code_ihgq920 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)3ULL)));
    if (!t3) goto ie2;
    int64_t t5 = GM_LD(int64_t, gm_cbcngu2pqs7);
    int64_t t6 = t5;
    t1 = t6;
    goto ix2;
ie2:;
    gm_toe5fmr3vvf4d t8 = gm_l0;
    int64_t t9 = gm_f_sys_code_cjtlfo1(t8);
    int64_t t10 = t9;
    t1 = t10;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_net_count_46l7ueq F;
    gm_thf5kr0is9u9i gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    if (!((((uint8_t *)&gm_l0))[0] != 0)) goto mn3;
    gm_toe5fmr3vvf4d t4 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 8));
    gm_l1 = t4;
    gm_toe5fmr3vvf4d t5 = gm_l1;
    int64_t t6 = gm_f_sys_net_code_ihgq920(t5);
    t1 = t6;
    goto me2;
mn3:;
    if (((((uint8_t *)&gm_l0))[0] != 0)) goto mn7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t8;
    uint64_t t9 = gm_l2;
    int64_t t10 = (int64_t)t9;
    t1 = t10;
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 84, 49);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_next_handle_r35ek81 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        int64_t t4 = GM_LD(int64_t, (((uint8_t *)t3) + 280));
        int64_t r5; bool o6 = __builtin_add_overflow(t4, ((int64_t)1LL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 50, 5);
        int64_t t7 = r5;
        GM_ST(int64_t, (((uint8_t *)t2) + 280), t7);
    }
    {
        void * t8 = gm_l0;
        int64_t t9 = GM_LD(int64_t, (((uint8_t *)t8) + 280));
        int64_t r10; bool o11 = __builtin_add_overflow(t9, ((int64_t)2LL), &r10);
        if (o11) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 51, 12);
        int64_t t12 = r10;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_ret_vpa2k8i F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* dst */
    int64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        int64_t t4 = gm_l2;
        uint64_t t5;
        memset(&t5, 0, sizeof t5); memcpy(&t5, ((uint8_t *)&t4), sizeof t5 < 8 ? sizeof t5 : 8);
        unsigned __int128 t6 = (unsigned __int128)t5;
        gm_f_put_uint_cddin99(t2, t3, t6);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_stdio_nsl9o9u F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    gm_t9qkh0rsooc8d t2;
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t5 == ((uint32_t)1ULL))) goto mn4;
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)1ULL));
    t2 = t6;
    goto me3;
mn4:;
    uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t8 == ((uint32_t)2ULL))) goto mn7;
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
    t2 = t9;
    goto me3;
mn7:;
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
    t2 = t11;
    goto me3;
mn10:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 106, 48);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_unit_nrsqciq F;
    gm_ttln0d0hd70en gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    if (!((((uint8_t *)&gm_l0))[0] != 0)) goto mn3;
    gm_toe5fmr3vvf4d t4 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t4;
    gm_toe5fmr3vvf4d t5 = gm_l1;
    int64_t t6 = gm_f_sys_code_cjtlfo1(t5);
    t1 = t6;
    goto me2;
mn3:;
    t1 = ((int64_t)0LL);
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 90, 41);
me2:;
    F.ret = t1;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l4 = t10;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        uint64_t t15 = (uint64_t)t14;
        uint8_t t16 = (t12 >= t15);
        if (!t16) goto ie11;
        {
            uint8_t t18 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((((uint8_t *)t19) + 0) + 0));
        void * t21 = t20;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        gm_l5 = t25;
    }
    {
        void * t26 = gm_l5;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28;
        if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
            void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            uint8_t t30 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t28;
    }
    {
        uint64_t t31 = gm_l1;
        void * t32 = gm_l5;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
        uint64_t r34; bool o35 = __builtin_sub_overflow(t31, t33, &r34);
        if (o35) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t36 = r34;
        gm_l7 = t36;
    }
    {
        uint64_t t37 = gm_l7;
        void * t38 = gm_l5;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 16));
        unsigned __int128 n40 = (unsigned __int128)t39;
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t41 = (uint64_t)(t37 >> (int)n40);
        gm_l8 = t41;
    }
    {
        void * t42 = gm_l6;
        void * t43 = t42;
        uint64_t t44 = gm_l8;
        uint64_t t45 = gm_f_us_cuv01fp(t44);
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = (void *)((uint8_t *)t43 + t46 * 24);
        gm_l9 = t47;
    }
    {
        uint64_t t48 = gm_l7;
        uint64_t t49 = gm_l8;
        void * t50 = gm_l5;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
        unsigned __int128 n52 = (unsigned __int128)t51;
        if (n52 >= 64) gm_trap_shift((uint64_t)n52, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r53 = (uint64_t)((uint64_t)t49 << (int)n52);
        bool o54 = (uint64_t)(r53 >> (int)n52) != t49;
        if (o54) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t55 = r53;
        uint64_t r56; bool o57 = __builtin_sub_overflow(t48, t55, &r56);
        if (o57) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t58 = r56;
        gm_l10 = t58;
    }
    {
        uint8_t t60 = 0;
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        void * t63 = gm_l9;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 17));
        uint8_t t65 = (uint8_t)!t64;
        if (!t65) {
            uint64_t t66 = gm_l2;
            void * t67 = gm_l9;
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
            uint8_t t69 = (t66 > t68);
            t62 = t69;
        } else t62 = 1;
        if (!t62) {
            uint64_t t70 = gm_l10;
            void * t71 = gm_l9;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
            uint64_t t73 = gm_l2;
            uint64_t r74; bool o75 = __builtin_sub_overflow(t72, t73, &r74);
            if (o75) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t76 = r74;
            uint8_t t77 = (t70 > t76);
            t61 = t77;
        } else t61 = 1;
        if (!t61) {
            uint8_t t78 = 0;
            uint8_t t79 = gm_l3;
            if (t79) {
                void * t80 = gm_l9;
                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 18));
                uint8_t t82 = (uint8_t)!t81;
                t78 = t82;
            } else t78 = 0;
            t60 = t78;
        } else t60 = 1;
        if (!t60) goto ie59;
        {
            uint8_t t84 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t85 = gm_l9;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        void * t87 = t86;
        uint64_t t88 = gm_l10;
        uint64_t t89 = gm_f_us_cuv01fp(t88);
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = (void *)((uint8_t *)t87 + t90 * 1);
        void * t92 = t91;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l3;
        uint8_t t12 = gm_f_aligned_q6sor4h(t10, t11);
        if (!t12) goto ie9;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            uint64_t t17 = gm_l2;
            void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)1));
            if (!(GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto ie14;
            void * t19 = GM_LD(void *, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l4;
                void * t22 = ((uint8_t *)t21);
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_f_us_cuv01fp(t24);
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t27);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t t30 = gm_l2;
        uint64_t t31 = gm_l3;
        gm_tpekhfs32op0f t32;
        gm_f_fault_u31b92l((void *)&t32, t28, t29, t30, t31, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1lff729(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2gkkusq(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aqcn8lk(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cijad0r(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ducmvdu(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9i1sr8(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mslt7ot(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r8m0qjr(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rg62rr4(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tlii61s(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_trt04qi(void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vhnrvmh(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_socket_on_host_thread_u8c2vg5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_socket_on_host_thread_u8c2vg5 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmvdbo28o1i6h gm_l2; /* op */
    gm_tfvg1o4f8mmhi gm_l3; /* r */
    gm_t62ebbi2i5rkd gm_l4; /* result */
    gm_tm98ap4112hee gm_l5; /* s */
    gm_toe5fmr3vvf4d gm_l6; /* e */
    GM_STACK_CHECK(&F, 360u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_t6j8vdmi81h7c t4;
        gm_f_on_host_thread_kmegc07((void *)&t4, (void *)NULL, cx, NULL, t2, t3, &(*(gm_tmvdbo28o1i6h *)((uint8_t *)&gm_l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_toqti1ubjnfd1 t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t5 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t4) + 8)));
            F.ret = t5;
            goto gm_fret;
        }
        gm_l3 = (*(gm_tfvg1o4f8mmhi *)(((uint8_t *)&t4) + 8));
    }
    {
        gm_toe5fmr3vvf4d t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)12ULL));
        gm_t62ebbi2i5rkd t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        ((uint8_t *)&t7)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 8), t6);
        gm_l4 = t7;
    }
    {
        if (!(((((uint8_t *)&gm_l3) + 16))[8] != 0)) goto ie8;
        gm_tm98ap4112hee t9 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l3) + 16));
        gm_l5 = t9;
        {
            gm_tm98ap4112hee t11 = gm_l5;
            gm_t62ebbi2i5rkd t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 0;
            GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t12 + 8), t11);
            gm_l4 = t12;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        {
            gm_thf5kr0is9u9i t14 = GM_LD(gm_thf5kr0is9u9i, (((uint8_t *)&gm_l3) + 0));
            uint64_t t15;
            if (!((((uint8_t *)&t14))[0] != 0)) {
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&t14) + 8));
                t15 = t16;
            } else {
                gm_toe5fmr3vvf4d t17 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t14) + 8));
                gm_l6 = t17;
                {
                    gm_toe5fmr3vvf4d t19 = gm_l6;
                    gm_t62ebbi2i5rkd t20;
                    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                    ((uint8_t *)&t20)[0] = 1;
                    GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t20 + 8), t19);
                    gm_l4 = t20;
                }
                gm_t62ebbi2i5rkd t21 = gm_l4;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out))[0] = 0;
                GM_ST(gm_t62ebbi2i5rkd, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 8, t21);
                goto gm_fdone;
            }
        }
    }
    {
        gm_t62ebbi2i5rkd t22 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out))[0] = 0;
        GM_ST(gm_t62ebbi2i5rkd, (uint8_t *)&(*(gm_toqti1ubjnfd1 *)gm_out) + 8, t22);
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
inline __attribute__((aligned(4))) void gm_f_sys_call_6jqv32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_sys_call_6jqv32o F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* e */
    gm_b16_8 gm_l5; /* dst */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_tcfa6l7reoosn gm_l8; /* r */
    GM_STACK_CHECK(&F, 610u);
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
        gm_t2lqu8jsj511d t3 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t2));
        gm_l6 = t3;
    }
    {
        gm_f_list_fufbgf7((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        gm_tbl8il92l97ut t5 = gm_l1;
        gm_b16_8 t6 = gm_l3;
        void * t7 = (void *)((uint8_t *)&F.l7);
        gm_tcfa6l7reoosn t8;
        gm_f_ev_args_j1qj6gl((void *)&t8, (void *)NULL, cx, NULL, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_tcfa6l7reoosn t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t9 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t8) + 8)));
            F.ret = t9;
            goto gm_fret;
        }
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 8));
    }
    {
        gm_tcfa6l7reoosn t11;
        gm_t9qkh0rsooc8d t13 = gm_l2;
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)47ULL));
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)48ULL));
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)49ULL));
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)50ULL));
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)51ULL));
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)52ULL));
        gm_t9rbkbpjtolar t20;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 0), t14);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t15);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 2), t16);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 3), t17);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 4), t18);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 5), t19);
        uint8_t t21 = 0;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 0, ((uint8_t *)&t13))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 1, ((uint8_t *)&t13))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 2, ((uint8_t *)&t13))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 3, ((uint8_t *)&t13))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 4, ((uint8_t *)&t13))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 5, ((uint8_t *)&t13))) t21 = 1;
        uint8_t t22 = (uint8_t)(t21);
        if (!t22) goto ie12;
        void * t24 = gm_l0;
        void * t25 = gm_l4;
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 112));
        gm_t9qkh0rsooc8d t27 = gm_l2;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t29 = gm_l5;
        gm_tcfa6l7reoosn t30;
        gm_f_thread_op_pfli7b1((void *)&t30, (void *)NULL, cx, NULL, t24, t26, t27, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t31 = t30;
        t11 = t31;
        goto ix12;
ie12:;
        void * t33 = gm_l0;
        void * t34 = gm_l4;
        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 112));
        gm_t9qkh0rsooc8d t36 = gm_l2;
        gm_b16_8 t37 = gm_l3;
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_b16_8 t40 = gm_l5;
        gm_tcfa6l7reoosn t41;
        gm_f_sys_run_2vqjo3u((void *)&t41, (void *)NULL, cx, NULL, t33, t35, t36, t37, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t42 = t41;
        t11 = t42;
ix12:;
        gm_l8 = t11;
    }
    {
        void * t43 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_tq06jtm(t43);
    }
    {
        void * t44 = gm_l0;
        gm_t2lqu8jsj511d t45 = gm_l6;
        gm_f_release_jn5kqdi(t44, GM_CONV(gm_csc7lc6jlkqkuq, t45));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l8), 32);
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
inline __attribute__((aligned(4))) int64_t gm_f_sys_code_cjtlfo1(gm_toe5fmr3vvf4d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_code_cjtlfo1 F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    int32_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    t1 = ((int64_t)-1LL);
    goto me2;
mn19:;
ma4:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)1ULL)) goto mn21;
    t1 = ((int64_t)-2LL);
    goto me2;
mn21:;
ma5:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t24 != ((uint8_t)2ULL)) goto mn23;
    t1 = ((int64_t)-3LL);
    goto me2;
mn23:;
ma6:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)3ULL)) goto mn25;
    t1 = ((int64_t)-4LL);
    goto me2;
mn25:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)4ULL)) goto mn27;
    t1 = ((int64_t)-5LL);
    goto me2;
mn27:;
ma8:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)5ULL)) goto mn29;
    t1 = ((int64_t)-6LL);
    goto me2;
mn29:;
ma9:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)6ULL)) goto mn31;
    t1 = ((int64_t)-7LL);
    goto me2;
mn31:;
ma10:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)7ULL)) goto mn33;
    t1 = ((int64_t)-8LL);
    goto me2;
mn33:;
ma11:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t36 != ((uint8_t)8ULL)) goto mn35;
    t1 = ((int64_t)-9LL);
    goto me2;
mn35:;
ma12:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)9ULL)) goto mn37;
    t1 = ((int64_t)-10LL);
    goto me2;
mn37:;
ma13:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)10ULL)) goto mn39;
    t1 = ((int64_t)-11LL);
    goto me2;
mn39:;
ma14:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t42 != ((uint8_t)11ULL)) goto mn41;
    t1 = ((int64_t)-12LL);
    goto me2;
mn41:;
ma15:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)12ULL)) goto mn43;
    t1 = ((int64_t)-13LL);
    goto me2;
mn43:;
ma16:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t46 != ((uint8_t)13ULL)) goto mn45;
    int32_t t47 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t47;
    int32_t t48 = gm_l1;
    int64_t t49 = (int64_t)t48;
    int64_t r50; bool o51 = __builtin_sub_overflow(((int64_t)-1000LL), t49, &r50);
    if (o51) gm_trap("integer overflow in `-`", gm_srcn3jjc71a2bv3, 70, 18);
    int64_t t52 = r50;
    t1 = t52;
    goto me2;
mn45:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 56, 38);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_sys_count_50c5lcp(gm_thf5kr0is9u9i a0);
inline __attribute__((aligned(4))) void gm_f_sys_dest_rtrb14h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_dest_rtrb14h F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_t934k1j7u70ct t9;
        gm_f_write_view_pebit6h((void *)&t9, t6, t7, t8, ((uint64_t)1ULL));
        gm_b16_8 t10;
        if (!((((uint8_t *)&t9))[0] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            t10 = t11;
        } else {
            F.l4 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t9) + 8));
            void * t12 = gm_l0;
            gm_tl874t2atnlh3 t13 = gm_l1;
            gm_t76123gfet73g t14;
            gm_f_mem_trap_ecogko0((void *)&t14, (void *)NULL, cx, NULL, t12, t13, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8, t14);
            goto gm_fdone;
        }
        gm_l5 = t10;
    }
    {
        gm_b16_8 t15 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8, t15);
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
__attribute__((aligned(4))) void gm_f_sys_fs_u0grkf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_fs_u0grkf6 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_b16_8 gm_l6; /* path */
    uint32_t gm_l7; /* flags */
    gm_t4q2b5ibsrdik gm_l8; /* opts */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* f */
    int64_t gm_l11; /* handle */
    gm_tm98ap4112hee gm_l12; /* f */
    gm_b16_8 gm_l13; /* buf */
    gm_tm98ap4112hee gm_l14; /* f */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* f */
    int64_t gm_l17; /* off */
    int64_t gm_l18; /* whence */
    gm_tm98ap4112hee gm_l19; /* f */
    gm_t9qkh0rsooc8d gm_l20; /* from */
    gm_toe5fmr3vvf4d gm_l21; /* err */
    uint64_t gm_l22; /* pos */
    gm_b16_8 gm_l23; /* path */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_t5j14m8ru132p gm_l25; /* info */
    gm_t2lqu8jsj511d gm_l26; /* sp */
    uint64_t gm_l27; /* count */
    gm_b16_8 gm_l29; /* view */
    gm_t767tucha9lsh gm_l30; /* fields */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* i */
    gm_b16_8 gm_l34; /* from */
    gm_b16_8 gm_l35; /* to */
    gm_b16_8 gm_l36; /* path */
    gm_toe5fmr3vvf4d gm_l37; /* err */
    gm_tm98ap4112hee gm_l38; /* d */
    int64_t gm_l39; /* handle */
    gm_tm98ap4112hee gm_l40; /* d */
    gm_b16_8 gm_l41; /* buf */
    gm_toe5fmr3vvf4d gm_l42; /* err */
    gm_t11t9d8s5diq9 gm_l43; /* next */
    gm_t617vr72bd7b3 gm_l44; /* entry */
    gm_tm98ap4112hee gm_l45; /* d */
    uint64_t gm_l46; /* size */
    gm_tm98ap4112hee gm_l47; /* f */
    gm_tm98ap4112hee gm_l48; /* f */
    gm_b16_8 gm_l49; /* buf */
    gm_b16_8 gm_l50; /* path */
    gm_b16_8 gm_l51; /* buf */
    gm_b16_8 gm_l52; /* path */
    gm_b16_8 gm_l53; /* buf */
    gm_b16_8 gm_l54; /* target */
    gm_b16_8 gm_l55; /* path */
    gm_b16_8 gm_l56; /* path */
    gm_b16_8 gm_l57; /* path */
    GM_STACK_CHECK(&F, 7125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((int64_t)-1LL);
    }
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t4 = (t3 > ((uint64_t)0ULL));
        if (!t4) goto ie2;
        gm_t9qkh0rsooc8d t5 = gm_l2;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)5ULL));
        gm_t9qkh0rsooc8d t7;
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)10ULL));
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)18ULL));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)11ULL));
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)12ULL));
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)13ULL));
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)14ULL));
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)21ULL));
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)22ULL));
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)23ULL));
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)24ULL));
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)25ULL));
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)26ULL));
        gm_t4qfn59m757do t20;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 0), t6);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 1), t7);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 2), t8);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 3), t9);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 4), t10);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 5), t11);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 6), t12);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 7), t13);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 8), t14);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 9), t15);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 10), t16);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 11), t17);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 12), t18);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 13), t19);
        uint8_t t21 = 0;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 0, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 1, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 2, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 3, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 4, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 5, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 6, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 7, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 8, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 9, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 10, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 11, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 12, ((uint8_t *)&t5))) t21 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t20) + 13, ((uint8_t *)&t5))) t21 = 1;
        uint8_t t22 = (uint8_t)(!t21);
        if (!t22) goto ie2;
        {
            void * t24 = gm_l0;
            uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcn3jjc71a2bv3, 215, 24);
            void * t27 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t25 * 8));
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 96));
            uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_srcn3jjc71a2bv3, 215, 36);
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t29 * 16));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            int64_t t33 = gm_f_sys_int_32e79ed(t24, GM_CONV(gm_csguankko4lmhd, t28), t32);
            gm_l5 = t33;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t58) { case ((uint8_t)0ULL): goto ma56; case ((uint8_t)1ULL): goto ma56; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma56; case ((uint8_t)4ULL): goto ma56; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma36; case ((uint8_t)7ULL): goto ma37; case ((uint8_t)8ULL): goto ma38; case ((uint8_t)9ULL): goto ma39; case ((uint8_t)10ULL): goto ma40; case ((uint8_t)11ULL): goto ma41; case ((uint8_t)12ULL): goto ma42; case ((uint8_t)13ULL): goto ma43; case ((uint8_t)14ULL): goto ma44; case ((uint8_t)15ULL): goto ma45; case ((uint8_t)16ULL): goto ma46; case ((uint8_t)17ULL): goto ma47; case ((uint8_t)18ULL): goto ma40; case ((uint8_t)19ULL): goto ma48; case ((uint8_t)20ULL): goto ma49; case ((uint8_t)21ULL): goto ma50; case ((uint8_t)22ULL): goto ma51; case ((uint8_t)23ULL): goto ma52; case ((uint8_t)24ULL): goto ma53; case ((uint8_t)25ULL): goto ma54; case ((uint8_t)26ULL): goto ma55; case ((uint8_t)27ULL): goto ma56; case ((uint8_t)28ULL): goto ma56; case ((uint8_t)29ULL): goto ma56; case ((uint8_t)30ULL): goto ma56; case ((uint8_t)31ULL): goto ma56; case ((uint8_t)32ULL): goto ma56; case ((uint8_t)33ULL): goto ma56; case ((uint8_t)34ULL): goto ma56; case ((uint8_t)35ULL): goto ma56; case ((uint8_t)36ULL): goto ma56; case ((uint8_t)37ULL): goto ma56; case ((uint8_t)38ULL): goto ma56; case ((uint8_t)39ULL): goto ma56; case ((uint8_t)40ULL): goto ma56; case ((uint8_t)41ULL): goto ma56; case ((uint8_t)42ULL): goto ma56; case ((uint8_t)43ULL): goto ma56; case ((uint8_t)44ULL): goto ma56; case ((uint8_t)45ULL): goto ma56; case ((uint8_t)46ULL): goto ma56; case ((uint8_t)47ULL): goto ma56; case ((uint8_t)48ULL): goto ma56; case ((uint8_t)49ULL): goto ma56; case ((uint8_t)50ULL): goto ma56; case ((uint8_t)51ULL): goto ma56; case ((uint8_t)52ULL): goto ma56; case ((uint8_t)53ULL): goto ma56; case ((uint8_t)54ULL): goto ma56; default: goto ma56; }
ma35:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t60 != ((uint8_t)5ULL)) goto mn59;
        {
            void * t62 = gm_l0;
            gm_tl874t2atnlh3 t63 = gm_l1;
            uint64_t t64 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcn3jjc71a2bv3, 219, 41);
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t64 * 16));
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
            gm_td0ept12e7q8b t68;
            gm_f_sys_src_5svsr6i((void *)&t68, (void *)NULL, cx, NULL, t62, t63, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t68))[0] != 0)) {
                gm_tarq9lp8tdk6f t69;
                GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                ((uint8_t *)&t69)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t69 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t68) + 8)));
                F.ret = t69;
                goto gm_fret;
            }
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
            gm_l6 = t70;
        }
        {
            void * t71 = gm_l0;
            uint64_t t72 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcn3jjc71a2bv3, 220, 33);
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t72 * 16));
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
            unsigned __int128 t76 = gm_f_get_uint_bq8c4b7(t71, t75);
            uint32_t t77 = (uint32_t)t76;
            gm_l7 = t77;
        }
        {
            uint32_t t78 = gm_l7;
            uint8_t t79 = gm_f_sys_has_0sego5l(t78, ((uint32_t)1ULL));
            uint32_t t80 = gm_l7;
            uint8_t t81 = gm_f_sys_has_0sego5l(t80, ((uint32_t)2ULL));
            uint32_t t82 = gm_l7;
            uint8_t t83 = gm_f_sys_has_0sego5l(t82, ((uint32_t)4ULL));
            uint32_t t84 = gm_l7;
            uint8_t t85 = gm_f_sys_has_0sego5l(t84, ((uint32_t)8ULL));
            uint32_t t86 = gm_l7;
            uint8_t t87 = gm_f_sys_has_0sego5l(t86, ((uint32_t)16ULL));
            uint32_t t88 = gm_l7;
            uint8_t t89 = gm_f_sys_has_0sego5l(t88, ((uint32_t)32ULL));
            gm_t4q2b5ibsrdik t90;
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), t79);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 1), t81);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 2), t83);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 3), t85);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 4), t87);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 5), t89);
            gm_l8 = t90;
        }
        {
            gm_b16_8 t91 = gm_l6;
            gm_t4q2b5ibsrdik t92 = gm_l8;
            gm_node *pn93 = gm_find(cx, 2126215853);
            if (!pn93) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 229, 18);
            gm_t62ebbi2i5rkd t94;
            t94 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn93->clauses[0])(pn93->env, pn93->hctx, pn93, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tm98ap4112hee t95;
            if (!((((uint8_t *)&t94))[0] != 0)) {
                gm_tm98ap4112hee t96 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t94) + 8));
                t95 = t96;
            } else {
                gm_toe5fmr3vvf4d t97 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t94) + 8));
                gm_l9 = t97;
                gm_toe5fmr3vvf4d t98 = gm_l9;
                int64_t t99 = gm_f_sys_code_cjtlfo1(t98);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t99);
                goto gm_fdone;
            }
            gm_l10 = t95;
        }
        {
            void * t100 = gm_l0;
            void * t101 = (void *)(((uint8_t *)t100) + 1352);
            int64_t t102 = gm_f_sys_next_handle_r35ek81(t101);
            gm_l11 = t102;
        }
        {
            void * t103 = gm_l0;
            void * t104 = (void *)((((uint8_t *)t103) + 1352) + 0);
            int64_t t105 = gm_l11;
            gm_tm98ap4112hee t106 = gm_l10;
            gm_tftb1odc739t5 t107 = gm_f_put_endcgjj(t104, t105, GM_CONV(gm_csfq64m1m1rt6m, t106));
            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcn3jjc71a2bv3, 231, 13);
            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
        }
        {
            int64_t t109 = gm_l11;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t109);
            goto gm_fdone;
        }
        goto me34;
mn59:;
ma36:;
        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t111 != ((uint8_t)6ULL)) goto mn110;
        {
            void * t113 = gm_l0;
            void * t114 = (void *)((((uint8_t *)t113) + 1352) + 0);
            int64_t t115 = gm_l5;
            gm_tppo2hmut88dq t116 = gm_f_remove_82qm8hc(t114, t115);
            gm_tm98ap4112hee t117;
            if (((((uint8_t *)&t116))[8] != 0)) {
                gm_tm98ap4112hee t118 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t116) + 0));
                t117 = t118;
            } else {
                int64_t t119 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t119);
                goto gm_fdone;
            }
            gm_l12 = t117;
        }
        {
            gm_tm98ap4112hee t120 = gm_l12;
            gm_node *pn121 = gm_find(cx, 2126215853);
            if (!pn121) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 236, 13);
            gm_b0_1 t122;
            t122 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn121->clauses[1])(pn121->env, pn121->hctx, pn121, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
            goto gm_fdone;
        }
        goto me34;
mn110:;
ma37:;
        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t124 != ((uint8_t)7ULL)) goto mn123;
        {
            void * t126 = gm_l0;
            gm_tl874t2atnlh3 t127 = gm_l1;
            uint64_t t128 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_srcn3jjc71a2bv3, 240, 41);
            gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t128 * 16));
            gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
            gm_td0ept12e7q8b t132;
            gm_f_sys_dest_rtrb14h((void *)&t132, (void *)NULL, cx, NULL, t126, t127, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t132))[0] != 0)) {
                gm_tarq9lp8tdk6f t133;
                GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                ((uint8_t *)&t133)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t133 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t132) + 8)));
                F.ret = t133;
                goto gm_fret;
            }
            gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
            gm_l13 = t134;
        }
        {
            void * t135 = gm_l0;
            void * t136 = (void *)((((uint8_t *)t135) + 1352) + 0);
            int64_t t137 = gm_l5;
            gm_tppo2hmut88dq t138 = gm_f_get_f2mbmm8(t136, t137);
            gm_tm98ap4112hee t139;
            if (((((uint8_t *)&t138))[8] != 0)) {
                gm_tm98ap4112hee t140 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t138) + 0));
                t139 = t140;
            } else {
                int64_t t141 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t141);
                goto gm_fdone;
            }
            gm_l14 = t139;
        }
        {
            void * t143 = gm_l0;
            uint64_t t144 = GM_LD(uintptr_t, (((((uint8_t *)t143) + 1656) + 0) + 0) + 8);
            uint8_t t145 = (t144 > ((uint64_t)1ULL));
            if (!t145) goto ie142;
            {
                void * t147 = gm_l0;
                gm_tl874t2atnlh3 t148 = gm_l1;
                gm_t3olsle24rveu t149;
                gm_tm98ap4112hee t150 = gm_l14;
                GM_ST(gm_tm98ap4112hee, (uint8_t *)&t149 + 0, t150);
                gm_b16_8 t151 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t149 + 8, t151);
                gm_tmvdbo28o1i6h t152;
                GM_ST(uint8_t, (uint8_t *)&t152 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t152 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t152 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t152 + 8), t149);
                gm_t6j8vdmi81h7c t153;
                gm_f_on_host_thread_kmegc07((void *)&t153, (void *)NULL, cx, NULL, t147, t148, &t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t153))[0] != 0)) {
                    gm_tarq9lp8tdk6f t154;
                    GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                    ((uint8_t *)&t154)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t154 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t153) + 8)));
                    F.ret = t154;
                    goto gm_fret;
                }
                gm_thf5kr0is9u9i t155 = GM_LD(gm_thf5kr0is9u9i, ((((uint8_t *)&t153) + 8) + 0));
                int64_t t156 = gm_f_sys_count_50c5lcp(t155);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t156);
                goto gm_fdone;
            }
            goto ix142;
ie142:;
ix142:;
        }
        {
            gm_tm98ap4112hee t157 = gm_l14;
            gm_b16_8 t158 = gm_l13;
            gm_node *pn159 = gm_find(cx, 2126215853);
            if (!pn159) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 243, 30);
            gm_thf5kr0is9u9i t160;
            t160 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn159->clauses[2])(pn159->env, pn159->hctx, pn159, t157, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t161 = gm_f_sys_count_50c5lcp(t160);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t161);
            goto gm_fdone;
        }
        goto me34;
mn123:;
ma38:;
        uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t163 != ((uint8_t)8ULL)) goto mn162;
        {
            void * t165 = gm_l0;
            gm_tl874t2atnlh3 t166 = gm_l1;
            uint64_t t167 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t168 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t167 >= (uintptr_t)(t168)) gm_trap_index(t167, t168, gm_srcn3jjc71a2bv3, 246, 41);
            gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t167 * 16));
            gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
            gm_td0ept12e7q8b t171;
            gm_f_sys_src_5svsr6i((void *)&t171, (void *)NULL, cx, NULL, t165, t166, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t171))[0] != 0)) {
                gm_tarq9lp8tdk6f t172;
                GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
                ((uint8_t *)&t172)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t172 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t171) + 8)));
                F.ret = t172;
                goto gm_fret;
            }
            gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&t171) + 8));
            gm_l15 = t173;
        }
        {
            void * t174 = gm_l0;
            void * t175 = (void *)((((uint8_t *)t174) + 1352) + 0);
            int64_t t176 = gm_l5;
            gm_tppo2hmut88dq t177 = gm_f_get_f2mbmm8(t175, t176);
            gm_tm98ap4112hee t178;
            if (((((uint8_t *)&t177))[8] != 0)) {
                gm_tm98ap4112hee t179 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t177) + 0));
                t178 = t179;
            } else {
                int64_t t180 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t180);
                goto gm_fdone;
            }
            gm_l16 = t178;
        }
        {
            gm_tm98ap4112hee t181 = gm_l16;
            gm_b16_8 t182 = gm_l15;
            gm_node *pn183 = gm_find(cx, 2126215853);
            if (!pn183) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 248, 30);
            gm_thf5kr0is9u9i t184;
            t184 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn183->clauses[3])(pn183->env, pn183->hctx, pn183, t181, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t185 = gm_f_sys_count_50c5lcp(t184);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t185);
            goto gm_fdone;
        }
        goto me34;
mn162:;
ma39:;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t187 != ((uint8_t)9ULL)) goto mn186;
        {
            void * t189 = gm_l0;
            uint64_t t190 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t190 >= (uintptr_t)(t191)) gm_trap_index(t190, t191, gm_srcn3jjc71a2bv3, 251, 31);
            void * t192 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t190 * 8));
            gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t192) + 96));
            uint64_t t194 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srcn3jjc71a2bv3, 251, 43);
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t194 * 16));
            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
            int64_t t198 = gm_f_sys_int_32e79ed(t189, GM_CONV(gm_csguankko4lmhd, t193), t197);
            gm_l17 = t198;
        }
        {
            void * t199 = gm_l0;
            uint64_t t200 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t200 >= (uintptr_t)(t201)) gm_trap_index(t200, t201, gm_srcn3jjc71a2bv3, 252, 34);
            void * t202 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t200 * 8));
            gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t202) + 96));
            uint64_t t204 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t204 >= (uintptr_t)(t205)) gm_trap_index(t204, t205, gm_srcn3jjc71a2bv3, 252, 46);
            gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t204 * 16));
            gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
            int64_t t208 = gm_f_sys_int_32e79ed(t199, GM_CONV(gm_csguankko4lmhd, t203), t207);
            gm_l18 = t208;
        }
        {
            void * t209 = gm_l0;
            void * t210 = (void *)((((uint8_t *)t209) + 1352) + 0);
            int64_t t211 = gm_l5;
            gm_tppo2hmut88dq t212 = gm_f_get_f2mbmm8(t210, t211);
            gm_tm98ap4112hee t213;
            if (((((uint8_t *)&t212))[8] != 0)) {
                gm_tm98ap4112hee t214 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t212) + 0));
                t213 = t214;
            } else {
                int64_t t215 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t215);
                goto gm_fdone;
            }
            gm_l19 = t213;
        }
        {
            int64_t t217 = gm_l18;
            uint8_t t218 = ((t217 == ((int64_t)0LL)));
            if (!t218) goto ie216;
            int64_t t219 = gm_l17;
            uint8_t t220 = (t219 < ((int64_t)0LL));
            if (!t220) goto ie216;
            {
                int64_t t222 = GM_LD(int64_t, gm_cndfnv7mbeh);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t222);
                goto gm_fdone;
            }
            goto ix216;
ie216:;
ix216:;
        }
        {
            int64_t t223 = gm_l18;
            gm_t9qkh0rsooc8d t224;
            int64_t t227 = GM_LD(int64_t, ((uint8_t *)&t223));
            if (!(t227 == ((int64_t)0LL))) goto mn226;
            gm_t9qkh0rsooc8d t228;
            GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)0ULL));
            t224 = t228;
            goto me225;
mn226:;
            int64_t t230 = GM_LD(int64_t, ((uint8_t *)&t223));
            if (!(t230 == ((int64_t)1LL))) goto mn229;
            gm_t9qkh0rsooc8d t231;
            GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)1ULL));
            t224 = t231;
            goto me225;
mn229:;
            int64_t t233 = GM_LD(int64_t, ((uint8_t *)&t223));
            if (!(t233 == ((int64_t)2LL))) goto mn232;
            gm_t9qkh0rsooc8d t234;
            GM_ST(uint8_t, ((uint8_t *)&t234 + 0), ((uint8_t)2ULL));
            t224 = t234;
            goto me225;
mn232:;
            int64_t t236 = GM_LD(int64_t, gm_cndfnv7mbeh);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t236);
            goto gm_fdone;
            goto me225;
mn235:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 255, 33);
me225:;
            gm_l20 = t224;
        }
        {
            gm_tm98ap4112hee t237 = gm_l19;
            int64_t t238 = gm_l17;
            gm_t9qkh0rsooc8d t239 = gm_l20;
            gm_node *pn240 = gm_find(cx, 2126215853);
            if (!pn240) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 261, 20);
            gm_thf5kr0is9u9i t241;
            t241 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, int64_t, gm_t9qkh0rsooc8d))pn240->clauses[4])(pn240->env, pn240->hctx, pn240, t237, t238, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t242;
            if (!((((uint8_t *)&t241))[0] != 0)) {
                uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&t241) + 8));
                t242 = t243;
            } else {
                gm_toe5fmr3vvf4d t244 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t241) + 8));
                gm_l21 = t244;
                gm_toe5fmr3vvf4d t245 = gm_l21;
                int64_t t246 = gm_f_sys_code_cjtlfo1(t245);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t246);
                goto gm_fdone;
            }
            gm_l22 = t242;
        }
        {
            uint64_t t247 = gm_l22;
            int64_t t248;
            memset(&t248, 0, sizeof t248); memcpy(&t248, ((uint8_t *)&t247), sizeof t248 < 8 ? sizeof t248 : 8);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t248);
            goto gm_fdone;
        }
        goto me34;
mn186:;
ma40:;
        uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t252 != ((uint8_t)10ULL)) goto pa251;
        goto po250;
pa251:;
        uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t254 != ((uint8_t)18ULL)) goto pa253;
        goto po250;
pa253:;
        goto mn249;
po250:;
        {
            void * t256 = gm_l0;
            gm_tl874t2atnlh3 t257 = gm_l1;
            uint64_t t258 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t259 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t258 >= (uintptr_t)(t259)) gm_trap_index(t258, t259, gm_srcn3jjc71a2bv3, 265, 41);
            gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t258 * 16));
            gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&t260));
            gm_td0ept12e7q8b t262;
            gm_f_sys_src_5svsr6i((void *)&t262, (void *)NULL, cx, NULL, t256, t257, t261);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t262))[0] != 0)) {
                gm_tarq9lp8tdk6f t263;
                GM_ST(uint8_t, (uint8_t *)&t263 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t263 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t263 + 4, 0);
                ((uint8_t *)&t263)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t263 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t262) + 8)));
                F.ret = t263;
                goto gm_fret;
            }
            gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&t262) + 8));
            gm_l23 = t264;
        }
        {
            gm_tih6h4avv5nrb t265;
            uint8_t t267 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)10ULL)));
            if (!t267) goto ie266;
            gm_b16_8 t269 = gm_l23;
            gm_node *pn270 = gm_find(cx, 2126215853);
            if (!pn270) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 266, 41);
            gm_tih6h4avv5nrb t271;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn270->clauses[5])((void *)&t271, pn270->env, pn270->hctx, pn270, t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tih6h4avv5nrb t272 = t271;
            t265 = t272;
            goto ix266;
ie266:;
            gm_b16_8 t274 = gm_l23;
            gm_node *pn275 = gm_find(cx, 2126215853);
            if (!pn275) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 266, 64);
            gm_tih6h4avv5nrb t276;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn275->clauses[13])((void *)&t276, pn275->env, pn275->hctx, pn275, t274);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tih6h4avv5nrb t277 = t276;
            t265 = t277;
ix266:;
            gm_t5j14m8ru132p t278;
            if (!((((uint8_t *)&t265))[0] != 0)) {
                t278 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t265) + 8));
            } else {
                gm_toe5fmr3vvf4d t279 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t265) + 8));
                gm_l24 = t279;
                gm_toe5fmr3vvf4d t280 = gm_l24;
                int64_t t281 = gm_f_sys_code_cjtlfo1(t280);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t281);
                goto gm_fdone;
            }
            gm_l25 = t278;
        }
        {
            void * t282 = gm_l0;
            uint64_t t283 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t283 >= (uintptr_t)(t284)) gm_trap_index(t283, t284, gm_srcn3jjc71a2bv3, 267, 33);
            gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t283 * 16));
            gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
            gm_t2lqu8jsj511d t287 = gm_f_slice_parts_1se5hu4(t282, t286);
            gm_l26 = t287;
        }
        {
            uint64_t t288 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
            uint64_t t289 = (t288 < ((uint64_t)4ULL) ? t288 : ((uint64_t)4ULL));
            gm_l27 = t289;
        }
        {
            void * t290 = gm_l0;
            void * t291 = (void *)(((uint8_t *)t290) + 184);
            uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 0));
            uint64_t t293 = gm_l27;
            uint64_t r294; bool o295 = __builtin_mul_overflow(t293, ((uint64_t)8ULL), &r294);
            if (o295) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 269, 46);
            uint64_t t296 = r294;
            gm_t934k1j7u70ct t297;
            gm_f_write_view_pebit6h((void *)&t297, t291, t292, t296, ((uint64_t)8ULL));
            gm_b16_8 t298;
            if (!((((uint8_t *)&t297))[0] != 0)) {
                gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&t297) + 8));
                t298 = t299;
            } else {
                F.l28 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t297) + 8));
                void * t300 = gm_l0;
                gm_tl874t2atnlh3 t301 = gm_l1;
                gm_t76123gfet73g t302;
                gm_f_mem_trap_ecogko0((void *)&t302, (void *)NULL, cx, NULL, t300, t301, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l28)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t302);
                goto gm_fdone;
            }
            gm_l29 = t298;
        }
        {
            uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 0));
            int64_t t304;
            memset(&t304, 0, sizeof t304); memcpy(&t304, ((uint8_t *)&t303), sizeof t304 < 8 ? sizeof t304 : 8);
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 0, t304);
            gm_t9qkh0rsooc8d t305 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l25) + 8));
            int64_t t306 = gm_f_sys_kind_sjfdqbt(t305);
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 8, t306);
            int64_t t307 = GM_LD(int64_t, (((uint8_t *)&gm_l25) + 16));
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 16, t307);
            uint32_t t308 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 24));
            int64_t t309 = (int64_t)t308;
            GM_ST(int64_t, (uint8_t *)&gm_l30 + 24, t309);
        }
        {
            {
                gm_l32 = ((uint64_t)0ULL);
            }
            {
                uint64_t t311 = gm_l27;
                uint64_t t312 = gm_f_us_cuv01fp(t311);
                gm_l31 = t312;
            }
            {
                for (;;) {
                    uint64_t t314 = gm_l32;
                    uint64_t t315 = gm_l31;
                    uint8_t t316 = (t314 < t315);
                    if (!t316) goto lb313;
                    {
                        uint64_t t319 = gm_l32;
                        uint64_t t322 = GM_LD(uint64_t, ((uint8_t *)&t319));
                        gm_l33 = t322;
                        {
                            void * t324 = gm_l0;
                            uint64_t t325 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            void * t326 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)));
                            uint64_t t327 = gm_l33;
                            uint64_t r328; bool o329 = __builtin_mul_overflow(t327, ((uint64_t)8ULL), &r328);
                            if (o329) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 271, 53);
                            uint64_t t330 = r328;
                            uint64_t t331 = (uintptr_t)t330;
                            uint64_t t332 = gm_l33;
                            uint64_t r333; bool o334 = __builtin_add_overflow(t332, ((uint64_t)1ULL), &r333);
                            if (o334) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 271, 61);
                            uint64_t t335 = r333;
                            uint64_t r336; bool o337 = __builtin_mul_overflow(t335, ((uint64_t)8ULL), &r336);
                            if (o337) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 271, 60);
                            uint64_t t338 = r336;
                            uint64_t t339 = (uintptr_t)t338;
                            if (t331 > t339 || t339 > (uintptr_t)t325) gm_trap_slice(t331, t339, t325, gm_srcn3jjc71a2bv3, 271, 48);
                            gm_b16_8 t340;
                            GM_ST(void *, (uint8_t *)&t340, (void *)(((uint8_t *)t326 + t331 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(t339 - t331));
                            uint64_t t341 = gm_l33;
                            uint64_t t342 = (uintptr_t)t341;
                            if (t342 >= (uintptr_t)(4)) gm_trap_index(t342, 4, gm_srcn3jjc71a2bv3, 271, 79);
                            int64_t t343 = GM_LD(int64_t, (((uint8_t *)&gm_l30) + t342 * 8));
                            uint64_t t344;
                            memset(&t344, 0, sizeof t344); memcpy(&t344, ((uint8_t *)&t343), sizeof t344 < 8 ? sizeof t344 : 8);
                            unsigned __int128 t345 = (unsigned __int128)t344;
                            gm_f_put_uint_cddin99(t324, t340, t345);
                        }
                        goto me320;
mn321:;
                        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 271, 13);
me320:;
                    }
lc313:;
                    {
                        uint64_t t347 = gm_l32;
                        uint64_t r348; bool o349 = __builtin_add_overflow(t347, ((uint64_t)1ULL), &r348);
                        uint64_t t350 = r348;
                        gm_l32 = t350;
                    }
                }
lb313:;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
            goto gm_fdone;
        }
        goto me34;
mn249:;
ma41:;
        uint8_t t352 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t352 != ((uint8_t)11ULL)) goto mn351;
        void * t353 = gm_l0;
        gm_tl874t2atnlh3 t354 = gm_l1;
        uint64_t t355 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t356 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t355 >= (uintptr_t)(t356)) gm_trap_index(t355, t356, gm_srcn3jjc71a2bv3, 274, 68);
        gm_b16_8 t357 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t355 * 16));
        gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&t357));
        gm_td0ept12e7q8b t359;
        gm_f_sys_src_5svsr6i((void *)&t359, (void *)NULL, cx, NULL, t353, t354, t358);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t359))[0] != 0)) {
            gm_tarq9lp8tdk6f t360;
            GM_ST(uint8_t, (uint8_t *)&t360 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t360 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t360 + 4, 0);
            ((uint8_t *)&t360)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t360 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t359) + 8)));
            F.ret = t360;
            goto gm_fret;
        }
        gm_b16_8 t361 = GM_LD(gm_b16_8, (((uint8_t *)&t359) + 8));
        gm_node *pn362 = gm_find(cx, 2126215853);
        if (!pn362) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 274, 38);
        gm_ttln0d0hd70en t363;
        t363 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn362->clauses[6])(pn362->env, pn362->hctx, pn362, t361);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t364 = gm_f_sys_unit_nrsqciq(t363);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
        GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t364);
        goto gm_fdone;
        goto me34;
mn351:;
ma42:;
        uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t366 != ((uint8_t)12ULL)) goto mn365;
        {
            void * t368 = gm_l0;
            gm_tl874t2atnlh3 t369 = gm_l1;
            uint64_t t370 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t370 >= (uintptr_t)(t371)) gm_trap_index(t370, t371, gm_srcn3jjc71a2bv3, 276, 41);
            gm_b16_8 t372 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t370 * 16));
            gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
            gm_td0ept12e7q8b t374;
            gm_f_sys_src_5svsr6i((void *)&t374, (void *)NULL, cx, NULL, t368, t369, t373);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t374))[0] != 0)) {
                gm_tarq9lp8tdk6f t375;
                GM_ST(uint8_t, (uint8_t *)&t375 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t375 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t375 + 4, 0);
                ((uint8_t *)&t375)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t375 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t374) + 8)));
                F.ret = t375;
                goto gm_fret;
            }
            gm_b16_8 t376 = GM_LD(gm_b16_8, (((uint8_t *)&t374) + 8));
            gm_l34 = t376;
        }
        {
            void * t377 = gm_l0;
            gm_tl874t2atnlh3 t378 = gm_l1;
            uint64_t t379 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t380 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t379 >= (uintptr_t)(t380)) gm_trap_index(t379, t380, gm_srcn3jjc71a2bv3, 277, 39);
            gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t379 * 16));
            gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&t381));
            gm_td0ept12e7q8b t383;
            gm_f_sys_src_5svsr6i((void *)&t383, (void *)NULL, cx, NULL, t377, t378, t382);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t383))[0] != 0)) {
                gm_tarq9lp8tdk6f t384;
                GM_ST(uint8_t, (uint8_t *)&t384 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t384 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t384 + 4, 0);
                ((uint8_t *)&t384)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t384 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t383) + 8)));
                F.ret = t384;
                goto gm_fret;
            }
            gm_b16_8 t385 = GM_LD(gm_b16_8, (((uint8_t *)&t383) + 8));
            gm_l35 = t385;
        }
        {
            gm_b16_8 t386 = gm_l34;
            gm_b16_8 t387 = gm_l35;
            gm_node *pn388 = gm_find(cx, 2126215853);
            if (!pn388) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 278, 29);
            gm_ttln0d0hd70en t389;
            t389 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn388->clauses[7])(pn388->env, pn388->hctx, pn388, t386, t387);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t390 = gm_f_sys_unit_nrsqciq(t389);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t390);
            goto gm_fdone;
        }
        goto me34;
mn365:;
ma43:;
        uint8_t t392 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t392 != ((uint8_t)13ULL)) goto mn391;
        void * t393 = gm_l0;
        gm_tl874t2atnlh3 t394 = gm_l1;
        uint64_t t395 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t396 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t395 >= (uintptr_t)(t396)) gm_trap_index(t395, t396, gm_srcn3jjc71a2bv3, 280, 71);
        gm_b16_8 t397 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t395 * 16));
        gm_b16_8 t398 = GM_LD(gm_b16_8, ((uint8_t *)&t397));
        gm_td0ept12e7q8b t399;
        gm_f_sys_src_5svsr6i((void *)&t399, (void *)NULL, cx, NULL, t393, t394, t398);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t399))[0] != 0)) {
            gm_tarq9lp8tdk6f t400;
            GM_ST(uint8_t, (uint8_t *)&t400 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t400 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t400 + 4, 0);
            ((uint8_t *)&t400)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t400 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t399) + 8)));
            F.ret = t400;
            goto gm_fret;
        }
        gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&t399) + 8));
        gm_node *pn402 = gm_find(cx, 2126215853);
        if (!pn402) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 280, 39);
        gm_ttln0d0hd70en t403;
        t403 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn402->clauses[8])(pn402->env, pn402->hctx, pn402, t401);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t404 = gm_f_sys_unit_nrsqciq(t403);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
        GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t404);
        goto gm_fdone;
        goto me34;
mn391:;
ma44:;
        uint8_t t406 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t406 != ((uint8_t)14ULL)) goto mn405;
        void * t407 = gm_l0;
        gm_tl874t2atnlh3 t408 = gm_l1;
        uint64_t t409 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t410 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t409 >= (uintptr_t)(t410)) gm_trap_index(t409, t410, gm_srcn3jjc71a2bv3, 281, 75);
        gm_b16_8 t411 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t409 * 16));
        gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&t411));
        gm_td0ept12e7q8b t413;
        gm_f_sys_src_5svsr6i((void *)&t413, (void *)NULL, cx, NULL, t407, t408, t412);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t413))[0] != 0)) {
            gm_tarq9lp8tdk6f t414;
            GM_ST(uint8_t, (uint8_t *)&t414 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t414 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t414 + 4, 0);
            ((uint8_t *)&t414)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t414 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t413) + 8)));
            F.ret = t414;
            goto gm_fret;
        }
        gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)&t413) + 8));
        gm_node *pn416 = gm_find(cx, 2126215853);
        if (!pn416) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 281, 41);
        gm_ttln0d0hd70en t417;
        t417 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn416->clauses[9])(pn416->env, pn416->hctx, pn416, t415);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t418 = gm_f_sys_unit_nrsqciq(t417);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
        GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t418);
        goto gm_fdone;
        goto me34;
mn405:;
ma45:;
        uint8_t t420 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t420 != ((uint8_t)15ULL)) goto mn419;
        {
            void * t422 = gm_l0;
            gm_tl874t2atnlh3 t423 = gm_l1;
            uint64_t t424 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t425 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t424 >= (uintptr_t)(t425)) gm_trap_index(t424, t425, gm_srcn3jjc71a2bv3, 283, 41);
            gm_b16_8 t426 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t424 * 16));
            gm_b16_8 t427 = GM_LD(gm_b16_8, ((uint8_t *)&t426));
            gm_td0ept12e7q8b t428;
            gm_f_sys_src_5svsr6i((void *)&t428, (void *)NULL, cx, NULL, t422, t423, t427);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t428))[0] != 0)) {
                gm_tarq9lp8tdk6f t429;
                GM_ST(uint8_t, (uint8_t *)&t429 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t429 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t429 + 4, 0);
                ((uint8_t *)&t429)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t429 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t428) + 8)));
                F.ret = t429;
                goto gm_fret;
            }
            gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)&t428) + 8));
            gm_l36 = t430;
        }
        {
            gm_b16_8 t431 = gm_l36;
            gm_node *pn432 = gm_find(cx, 2126215853);
            if (!pn432) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 284, 18);
            gm_t62ebbi2i5rkd t433;
            t433 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8))pn432->clauses[10])(pn432->env, pn432->hctx, pn432, t431);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tm98ap4112hee t434;
            if (!((((uint8_t *)&t433))[0] != 0)) {
                gm_tm98ap4112hee t435 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t433) + 8));
                t434 = t435;
            } else {
                gm_toe5fmr3vvf4d t436 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t433) + 8));
                gm_l37 = t436;
                gm_toe5fmr3vvf4d t437 = gm_l37;
                int64_t t438 = gm_f_sys_code_cjtlfo1(t437);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t438);
                goto gm_fdone;
            }
            gm_l38 = t434;
        }
        {
            void * t439 = gm_l0;
            void * t440 = (void *)(((uint8_t *)t439) + 1352);
            int64_t t441 = gm_f_sys_next_handle_r35ek81(t440);
            gm_l39 = t441;
        }
        {
            void * t442 = gm_l0;
            void * t443 = (void *)((((uint8_t *)t442) + 1352) + 56);
            int64_t t444 = gm_l39;
            gm_tm98ap4112hee t445 = gm_l38;
            gm_tftb1odc739t5 t446 = gm_f_put_0c6jnkb(t443, t444, GM_CONV(gm_csfq64m1m1rt6m, t445));
            if (((((uint8_t *)&t446))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t446) + 1, gm_srcn3jjc71a2bv3, 286, 13);
            gm_b0_1 t447 = GM_LD(gm_b0_1, (((uint8_t *)&t446) + 1));
        }
        {
            int64_t t448 = gm_l39;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t448);
            goto gm_fdone;
        }
        goto me34;
mn419:;
ma46:;
        uint8_t t450 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t450 != ((uint8_t)16ULL)) goto mn449;
        {
            void * t452 = gm_l0;
            void * t453 = (void *)((((uint8_t *)t452) + 1352) + 56);
            int64_t t454 = gm_l5;
            gm_tppo2hmut88dq t455 = gm_f_get_v07up09(t453, t454);
            gm_tm98ap4112hee t456;
            if (((((uint8_t *)&t455))[8] != 0)) {
                gm_tm98ap4112hee t457 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t455) + 0));
                t456 = t457;
            } else {
                int64_t t458 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t458);
                goto gm_fdone;
            }
            gm_l40 = t456;
        }
        {
            void * t459 = gm_l0;
            gm_tl874t2atnlh3 t460 = gm_l1;
            uint64_t t461 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t462 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t461 >= (uintptr_t)(t462)) gm_trap_index(t461, t462, gm_srcn3jjc71a2bv3, 291, 41);
            gm_b16_8 t463 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t461 * 16));
            gm_b16_8 t464 = GM_LD(gm_b16_8, ((uint8_t *)&t463));
            gm_td0ept12e7q8b t465;
            gm_f_sys_dest_rtrb14h((void *)&t465, (void *)NULL, cx, NULL, t459, t460, t464);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t465))[0] != 0)) {
                gm_tarq9lp8tdk6f t466;
                GM_ST(uint8_t, (uint8_t *)&t466 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t466 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t466 + 4, 0);
                ((uint8_t *)&t466)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t466 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t465) + 8)));
                F.ret = t466;
                goto gm_fret;
            }
            gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&t465) + 8));
            gm_l41 = t467;
        }
        {
            gm_tm98ap4112hee t468 = gm_l40;
            gm_b16_8 t469 = gm_l41;
            gm_node *pn470 = gm_find(cx, 2126215853);
            if (!pn470) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 292, 21);
            gm_tiuq7k5hm0kak t471;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn470->clauses[11])((void *)&t471, pn470->env, pn470->hctx, pn470, t468, t469);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t11t9d8s5diq9 t472;
            if (!((((uint8_t *)&t471))[0] != 0)) {
                t472 = (*(gm_t11t9d8s5diq9 *)(((uint8_t *)&t471) + 8));
            } else {
                gm_toe5fmr3vvf4d t473 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t471) + 8));
                gm_l42 = t473;
                gm_toe5fmr3vvf4d t474 = gm_l42;
                int64_t t475 = gm_f_sys_code_cjtlfo1(t474);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t475);
                goto gm_fdone;
            }
            gm_l43 = t472;
        }
        {
            gm_t11t9d8s5diq9 t476 = gm_l43;
            gm_t617vr72bd7b3 t477;
            if (((((uint8_t *)&t476))[16] != 0)) {
                gm_t617vr72bd7b3 t478 = GM_LD(gm_t617vr72bd7b3, (((uint8_t *)&t476) + 0));
                t477 = t478;
            } else {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
                goto gm_fdone;
            }
            gm_l44 = t477;
        }
        {
            uint64_t t479 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 0));
            int64_t t480 = (int64_t)t479;
            gm_t9qkh0rsooc8d t481 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l44) + 8));
            int64_t t482 = gm_f_sys_kind_sjfdqbt(t481);
            unsigned __int128 n483 = (unsigned __int128)((uint64_t)32ULL);
            if (n483 >= 64) gm_trap_shift((uint64_t)n483, "i64", gm_srcn3jjc71a2bv3, 294, 44);
            int64_t r484 = (int64_t)((uint64_t)t482 << (int)n483);
            bool o485 = (int64_t)(r484 >> (int)n483) != t482;
            if (o485) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 294, 44);
            int64_t t486 = r484;
            int64_t r487; bool o488 = __builtin_add_overflow(t480, t486, &r487);
            if (o488) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 294, 20);
            int64_t t489 = r487;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t489);
            goto gm_fdone;
        }
        goto me34;
mn449:;
ma47:;
        uint8_t t491 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t491 != ((uint8_t)17ULL)) goto mn490;
        {
            void * t493 = gm_l0;
            void * t494 = (void *)((((uint8_t *)t493) + 1352) + 56);
            int64_t t495 = gm_l5;
            gm_tppo2hmut88dq t496 = gm_f_remove_20jm5n8(t494, t495);
            gm_tm98ap4112hee t497;
            if (((((uint8_t *)&t496))[8] != 0)) {
                gm_tm98ap4112hee t498 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t496) + 0));
                t497 = t498;
            } else {
                int64_t t499 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t499);
                goto gm_fdone;
            }
            gm_l45 = t497;
        }
        {
            gm_tm98ap4112hee t500 = gm_l45;
            gm_node *pn501 = gm_find(cx, 2126215853);
            if (!pn501) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 298, 13);
            gm_b0_1 t502;
            t502 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn501->clauses[12])(pn501->env, pn501->hctx, pn501, t500);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
            goto gm_fdone;
        }
        goto me34;
mn490:;
ma48:;
        uint8_t t504 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t504 != ((uint8_t)19ULL)) goto mn503;
        {
            void * t506 = gm_l0;
            uint64_t t507 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t508 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t507 >= (uintptr_t)(t508)) gm_trap_index(t507, t508, gm_srcn3jjc71a2bv3, 302, 32);
            gm_b16_8 t509 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t507 * 16));
            gm_b16_8 t510 = GM_LD(gm_b16_8, ((uint8_t *)&t509));
            unsigned __int128 t511 = gm_f_get_uint_bq8c4b7(t506, t510);
            uint64_t t512 = (uint64_t)t511;
            gm_l46 = t512;
        }
        {
            void * t513 = gm_l0;
            void * t514 = (void *)((((uint8_t *)t513) + 1352) + 0);
            int64_t t515 = gm_l5;
            gm_tppo2hmut88dq t516 = gm_f_get_f2mbmm8(t514, t515);
            gm_tm98ap4112hee t517;
            if (((((uint8_t *)&t516))[8] != 0)) {
                gm_tm98ap4112hee t518 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t516) + 0));
                t517 = t518;
            } else {
                int64_t t519 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t519);
                goto gm_fdone;
            }
            gm_l47 = t517;
        }
        {
            gm_tm98ap4112hee t520 = gm_l47;
            uint64_t t521 = gm_l46;
            gm_node *pn522 = gm_find(cx, 2126215853);
            if (!pn522) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 304, 29);
            gm_ttln0d0hd70en t523;
            t523 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, uint64_t))pn522->clauses[14])(pn522->env, pn522->hctx, pn522, t520, t521);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t524 = gm_f_sys_unit_nrsqciq(t523);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t524);
            goto gm_fdone;
        }
        goto me34;
mn503:;
ma49:;
        uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t526 != ((uint8_t)20ULL)) goto mn525;
        {
            void * t528 = gm_l0;
            void * t529 = (void *)((((uint8_t *)t528) + 1352) + 0);
            int64_t t530 = gm_l5;
            gm_tppo2hmut88dq t531 = gm_f_get_f2mbmm8(t529, t530);
            gm_tm98ap4112hee t532;
            if (((((uint8_t *)&t531))[8] != 0)) {
                gm_tm98ap4112hee t533 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t531) + 0));
                t532 = t533;
            } else {
                int64_t t534 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t534);
                goto gm_fdone;
            }
            gm_l48 = t532;
        }
        {
            gm_tm98ap4112hee t535 = gm_l48;
            gm_node *pn536 = gm_find(cx, 2126215853);
            if (!pn536) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 308, 29);
            gm_ttln0d0hd70en t537;
            t537 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn536->clauses[15])(pn536->env, pn536->hctx, pn536, t535);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t538 = gm_f_sys_unit_nrsqciq(t537);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t538);
            goto gm_fdone;
        }
        goto me34;
mn525:;
ma50:;
        uint8_t t540 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t540 != ((uint8_t)21ULL)) goto mn539;
        {
            void * t542 = gm_l0;
            gm_tl874t2atnlh3 t543 = gm_l1;
            uint64_t t544 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t545 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t544 >= (uintptr_t)(t545)) gm_trap_index(t544, t545, gm_srcn3jjc71a2bv3, 311, 41);
            gm_b16_8 t546 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t544 * 16));
            gm_b16_8 t547 = GM_LD(gm_b16_8, ((uint8_t *)&t546));
            gm_td0ept12e7q8b t548;
            gm_f_sys_dest_rtrb14h((void *)&t548, (void *)NULL, cx, NULL, t542, t543, t547);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t548))[0] != 0)) {
                gm_tarq9lp8tdk6f t549;
                GM_ST(uint8_t, (uint8_t *)&t549 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t549 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t549 + 4, 0);
                ((uint8_t *)&t549)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t549 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t548) + 8)));
                F.ret = t549;
                goto gm_fret;
            }
            gm_b16_8 t550 = GM_LD(gm_b16_8, (((uint8_t *)&t548) + 8));
            gm_l49 = t550;
        }
        {
            gm_b16_8 t551 = gm_l49;
            gm_node *pn552 = gm_find(cx, 2126215853);
            if (!pn552) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 312, 30);
            gm_thf5kr0is9u9i t553;
            t553 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn552->clauses[16])(pn552->env, pn552->hctx, pn552, t551);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t554 = gm_f_sys_count_50c5lcp(t553);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t554);
            goto gm_fdone;
        }
        goto me34;
mn539:;
ma51:;
        uint8_t t556 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t556 != ((uint8_t)22ULL)) goto mn555;
        {
            void * t558 = gm_l0;
            gm_tl874t2atnlh3 t559 = gm_l1;
            uint64_t t560 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t561 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t560 >= (uintptr_t)(t561)) gm_trap_index(t560, t561, gm_srcn3jjc71a2bv3, 315, 41);
            gm_b16_8 t562 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t560 * 16));
            gm_b16_8 t563 = GM_LD(gm_b16_8, ((uint8_t *)&t562));
            gm_td0ept12e7q8b t564;
            gm_f_sys_src_5svsr6i((void *)&t564, (void *)NULL, cx, NULL, t558, t559, t563);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t564))[0] != 0)) {
                gm_tarq9lp8tdk6f t565;
                GM_ST(uint8_t, (uint8_t *)&t565 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t565 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t565 + 4, 0);
                ((uint8_t *)&t565)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t565 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t564) + 8)));
                F.ret = t565;
                goto gm_fret;
            }
            gm_b16_8 t566 = GM_LD(gm_b16_8, (((uint8_t *)&t564) + 8));
            gm_l50 = t566;
        }
        {
            void * t567 = gm_l0;
            gm_tl874t2atnlh3 t568 = gm_l1;
            uint64_t t569 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t570 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t569 >= (uintptr_t)(t570)) gm_trap_index(t569, t570, gm_srcn3jjc71a2bv3, 316, 41);
            gm_b16_8 t571 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t569 * 16));
            gm_b16_8 t572 = GM_LD(gm_b16_8, ((uint8_t *)&t571));
            gm_td0ept12e7q8b t573;
            gm_f_sys_dest_rtrb14h((void *)&t573, (void *)NULL, cx, NULL, t567, t568, t572);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t573))[0] != 0)) {
                gm_tarq9lp8tdk6f t574;
                GM_ST(uint8_t, (uint8_t *)&t574 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t574 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t574 + 4, 0);
                ((uint8_t *)&t574)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t574 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t573) + 8)));
                F.ret = t574;
                goto gm_fret;
            }
            gm_b16_8 t575 = GM_LD(gm_b16_8, (((uint8_t *)&t573) + 8));
            gm_l51 = t575;
        }
        {
            gm_b16_8 t576 = gm_l50;
            gm_b16_8 t577 = gm_l51;
            gm_node *pn578 = gm_find(cx, 2126215853);
            if (!pn578) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 317, 30);
            gm_thf5kr0is9u9i t579;
            t579 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn578->clauses[17])(pn578->env, pn578->hctx, pn578, t576, t577);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t580 = gm_f_sys_count_50c5lcp(t579);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t580);
            goto gm_fdone;
        }
        goto me34;
mn555:;
ma52:;
        uint8_t t582 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t582 != ((uint8_t)23ULL)) goto mn581;
        {
            void * t584 = gm_l0;
            gm_tl874t2atnlh3 t585 = gm_l1;
            uint64_t t586 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t587 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t586 >= (uintptr_t)(t587)) gm_trap_index(t586, t587, gm_srcn3jjc71a2bv3, 320, 41);
            gm_b16_8 t588 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t586 * 16));
            gm_b16_8 t589 = GM_LD(gm_b16_8, ((uint8_t *)&t588));
            gm_td0ept12e7q8b t590;
            gm_f_sys_src_5svsr6i((void *)&t590, (void *)NULL, cx, NULL, t584, t585, t589);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t590))[0] != 0)) {
                gm_tarq9lp8tdk6f t591;
                GM_ST(uint8_t, (uint8_t *)&t591 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t591 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t591 + 4, 0);
                ((uint8_t *)&t591)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t591 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t590) + 8)));
                F.ret = t591;
                goto gm_fret;
            }
            gm_b16_8 t592 = GM_LD(gm_b16_8, (((uint8_t *)&t590) + 8));
            gm_l52 = t592;
        }
        {
            void * t593 = gm_l0;
            gm_tl874t2atnlh3 t594 = gm_l1;
            uint64_t t595 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t596 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t595 >= (uintptr_t)(t596)) gm_trap_index(t595, t596, gm_srcn3jjc71a2bv3, 321, 41);
            gm_b16_8 t597 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t595 * 16));
            gm_b16_8 t598 = GM_LD(gm_b16_8, ((uint8_t *)&t597));
            gm_td0ept12e7q8b t599;
            gm_f_sys_dest_rtrb14h((void *)&t599, (void *)NULL, cx, NULL, t593, t594, t598);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t599))[0] != 0)) {
                gm_tarq9lp8tdk6f t600;
                GM_ST(uint8_t, (uint8_t *)&t600 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t600 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t600 + 4, 0);
                ((uint8_t *)&t600)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t600 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t599) + 8)));
                F.ret = t600;
                goto gm_fret;
            }
            gm_b16_8 t601 = GM_LD(gm_b16_8, (((uint8_t *)&t599) + 8));
            gm_l53 = t601;
        }
        {
            gm_b16_8 t602 = gm_l52;
            gm_b16_8 t603 = gm_l53;
            gm_node *pn604 = gm_find(cx, 2126215853);
            if (!pn604) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 322, 30);
            gm_thf5kr0is9u9i t605;
            t605 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn604->clauses[18])(pn604->env, pn604->hctx, pn604, t602, t603);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t606 = gm_f_sys_count_50c5lcp(t605);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t606);
            goto gm_fdone;
        }
        goto me34;
mn581:;
ma53:;
        uint8_t t608 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t608 != ((uint8_t)24ULL)) goto mn607;
        {
            void * t610 = gm_l0;
            gm_tl874t2atnlh3 t611 = gm_l1;
            uint64_t t612 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t613 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t612 >= (uintptr_t)(t613)) gm_trap_index(t612, t613, gm_srcn3jjc71a2bv3, 325, 43);
            gm_b16_8 t614 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t612 * 16));
            gm_b16_8 t615 = GM_LD(gm_b16_8, ((uint8_t *)&t614));
            gm_td0ept12e7q8b t616;
            gm_f_sys_src_5svsr6i((void *)&t616, (void *)NULL, cx, NULL, t610, t611, t615);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t616))[0] != 0)) {
                gm_tarq9lp8tdk6f t617;
                GM_ST(uint8_t, (uint8_t *)&t617 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t617 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t617 + 4, 0);
                ((uint8_t *)&t617)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t617 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t616) + 8)));
                F.ret = t617;
                goto gm_fret;
            }
            gm_b16_8 t618 = GM_LD(gm_b16_8, (((uint8_t *)&t616) + 8));
            gm_l54 = t618;
        }
        {
            void * t619 = gm_l0;
            gm_tl874t2atnlh3 t620 = gm_l1;
            uint64_t t621 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t622 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t621 >= (uintptr_t)(t622)) gm_trap_index(t621, t622, gm_srcn3jjc71a2bv3, 326, 41);
            gm_b16_8 t623 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t621 * 16));
            gm_b16_8 t624 = GM_LD(gm_b16_8, ((uint8_t *)&t623));
            gm_td0ept12e7q8b t625;
            gm_f_sys_src_5svsr6i((void *)&t625, (void *)NULL, cx, NULL, t619, t620, t624);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t625))[0] != 0)) {
                gm_tarq9lp8tdk6f t626;
                GM_ST(uint8_t, (uint8_t *)&t626 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t626 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t626 + 4, 0);
                ((uint8_t *)&t626)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t626 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t625) + 8)));
                F.ret = t626;
                goto gm_fret;
            }
            gm_b16_8 t627 = GM_LD(gm_b16_8, (((uint8_t *)&t625) + 8));
            gm_l55 = t627;
        }
        {
            gm_b16_8 t628 = gm_l54;
            gm_b16_8 t629 = gm_l55;
            gm_node *pn630 = gm_find(cx, 2126215853);
            if (!pn630) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 327, 29);
            gm_ttln0d0hd70en t631;
            t631 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn630->clauses[19])(pn630->env, pn630->hctx, pn630, t628, t629);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t632 = gm_f_sys_unit_nrsqciq(t631);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t632);
            goto gm_fdone;
        }
        goto me34;
mn607:;
ma54:;
        uint8_t t634 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t634 != ((uint8_t)25ULL)) goto mn633;
        {
            void * t636 = gm_l0;
            gm_tl874t2atnlh3 t637 = gm_l1;
            uint64_t t638 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t639 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t638 >= (uintptr_t)(t639)) gm_trap_index(t638, t639, gm_srcn3jjc71a2bv3, 330, 41);
            gm_b16_8 t640 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t638 * 16));
            gm_b16_8 t641 = GM_LD(gm_b16_8, ((uint8_t *)&t640));
            gm_td0ept12e7q8b t642;
            gm_f_sys_src_5svsr6i((void *)&t642, (void *)NULL, cx, NULL, t636, t637, t641);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t642))[0] != 0)) {
                gm_tarq9lp8tdk6f t643;
                GM_ST(uint8_t, (uint8_t *)&t643 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t643 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t643 + 4, 0);
                ((uint8_t *)&t643)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t643 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t642) + 8)));
                F.ret = t643;
                goto gm_fret;
            }
            gm_b16_8 t644 = GM_LD(gm_b16_8, (((uint8_t *)&t642) + 8));
            gm_l56 = t644;
        }
        {
            gm_b16_8 t645 = gm_l56;
            void * t646 = gm_l0;
            uint64_t t647 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t648 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t647 >= (uintptr_t)(t648)) gm_trap_index(t647, t648, gm_srcn3jjc71a2bv3, 331, 58);
            gm_b16_8 t649 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t647 * 16));
            gm_b16_8 t650 = GM_LD(gm_b16_8, ((uint8_t *)&t649));
            unsigned __int128 t651 = gm_f_get_uint_bq8c4b7(t646, t650);
            uint32_t t652 = (uint32_t)t651;
            gm_node *pn653 = gm_find(cx, 2126215853);
            if (!pn653) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 331, 29);
            gm_ttln0d0hd70en t654;
            t654 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, uint32_t))pn653->clauses[20])(pn653->env, pn653->hctx, pn653, t645, t652);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t655 = gm_f_sys_unit_nrsqciq(t654);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t655);
            goto gm_fdone;
        }
        goto me34;
mn633:;
ma55:;
        uint8_t t657 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t657 != ((uint8_t)26ULL)) goto mn656;
        {
            void * t659 = gm_l0;
            gm_tl874t2atnlh3 t660 = gm_l1;
            uint64_t t661 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t662 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t661 >= (uintptr_t)(t662)) gm_trap_index(t661, t662, gm_srcn3jjc71a2bv3, 334, 41);
            gm_b16_8 t663 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t661 * 16));
            gm_b16_8 t664 = GM_LD(gm_b16_8, ((uint8_t *)&t663));
            gm_td0ept12e7q8b t665;
            gm_f_sys_src_5svsr6i((void *)&t665, (void *)NULL, cx, NULL, t659, t660, t664);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t665))[0] != 0)) {
                gm_tarq9lp8tdk6f t666;
                GM_ST(uint8_t, (uint8_t *)&t666 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t666 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t666 + 4, 0);
                ((uint8_t *)&t666)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t666 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t665) + 8)));
                F.ret = t666;
                goto gm_fret;
            }
            gm_b16_8 t667 = GM_LD(gm_b16_8, (((uint8_t *)&t665) + 8));
            gm_l57 = t667;
        }
        {
            gm_b16_8 t668 = gm_l57;
            void * t669 = gm_l0;
            uint64_t t670 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t671 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t670 >= (uintptr_t)(t671)) gm_trap_index(t670, t671, gm_srcn3jjc71a2bv3, 335, 62);
            void * t672 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t670 * 8));
            gm_t88rcucg1b7uk t673 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t672) + 96));
            uint64_t t674 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t675 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t674 >= (uintptr_t)(t675)) gm_trap_index(t674, t675, gm_srcn3jjc71a2bv3, 335, 74);
            gm_b16_8 t676 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t674 * 16));
            gm_b16_8 t677 = GM_LD(gm_b16_8, ((uint8_t *)&t676));
            int64_t t678 = gm_f_sys_int_32e79ed(t669, GM_CONV(gm_csguankko4lmhd, t673), t677);
            gm_node *pn679 = gm_find(cx, 2126215853);
            if (!pn679) gm_trap("no handler installed for effect `Fs`", gm_srcn3jjc71a2bv3, 335, 29);
            gm_ttln0d0hd70en t680;
            t680 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, int64_t))pn679->clauses[21])(pn679->env, pn679->hctx, pn679, t668, t678);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t681 = gm_f_sys_unit_nrsqciq(t680);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t681);
            goto gm_fdone;
        }
        goto me34;
mn656:;
ma56:;
        void * t683 = gm_l0;
        gm_tl874t2atnlh3 t684 = gm_l1;
        gm_b16_8 t685;
        GM_ST(void *, (uint8_t *)&t685, (void *)(gm_sqn7kgtu67hrg));
        GM_ST(uintptr_t, (uint8_t *)&t685 + 8, (uintptr_t)(41));
        gm_t76123gfet73g t686;
        gm_f_trap_qh84l9d((void *)&t686, (void *)NULL, cx, NULL, t683, t684, t685);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t686);
        goto gm_fdone;
        goto me34;
mn682:;
ma57:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 217, 5);
me34:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_sys_has_0sego5l(uint32_t a0, uint32_t a1);
inline __attribute__((aligned(4))) int64_t gm_f_sys_int_32e79ed(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_int_32e79ed F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* b */
    __int128 gm_l3; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
        __int128 t6;
        memset(&t6, 0, sizeof t6); memcpy(&t6, ((uint8_t *)&t5), sizeof t6 < 16 ? sizeof t6 : 16);
        gm_l3 = t6;
    }
    {
        uint8_t t8 = 0;
        __int128 t9 = gm_l3;
        uint8_t t10 = (t9 < ((__int128)(((unsigned __int128)18446744073709551615u << 64) | 9223372036854775808u)));
        if (!t10) {
            __int128 t11 = gm_l3;
            uint8_t t12 = (t11 > ((__int128)(((unsigned __int128)0u << 64) | 9223372036854775807u)));
            t8 = t12;
        } else t8 = 1;
        if (!t8) goto ie7;
        {
            uint8_t t14 = 0;
            F.ret = ((int64_t)-1LL);
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        __int128 t15 = gm_l3;
        int64_t t16 = (int64_t)t15;
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_sys_kind_sjfdqbt(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sys_net_aff7h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_net_aff7h5l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    int64_t gm_l5; /* h */
    gm_toe5fmr3vvf4d gm_l9; /* err */
    gm_tm98ap4112hee gm_l10; /* s */
    int64_t gm_l11; /* handle */
    gm_toe5fmr3vvf4d gm_l15; /* err */
    gm_tm98ap4112hee gm_l16; /* l */
    int64_t gm_l17; /* handle */
    gm_toe5fmr3vvf4d gm_l20; /* err */
    uint16_t gm_l21; /* port */
    gm_toe5fmr3vvf4d gm_l24; /* err */
    gm_tm98ap4112hee gm_l25; /* s */
    int64_t gm_l26; /* handle */
    uint64_t gm_l38; /* recv_ns */
    uint64_t gm_l39; /* send_ns */
    GM_STACK_CHECK(&F, 4005u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((int64_t)-1LL);
    }
    {
        gm_t9qkh0rsooc8d t3 = gm_l2;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)27ULL));
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)28ULL));
        gm_t0am46iohlpol t6;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 0), t4);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 1), t5);
        uint8_t t7 = 0;
        if (gm_eqtje7cegoml(((uint8_t *)&t6) + 0, ((uint8_t *)&t3))) t7 = 1;
        if (gm_eqtje7cegoml(((uint8_t *)&t6) + 1, ((uint8_t *)&t3))) t7 = 1;
        uint8_t t8 = (uint8_t)(!t7);
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srcn3jjc71a2bv3, 344, 70);
            void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 8));
            gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 96));
            uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcn3jjc71a2bv3, 344, 82);
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t15 * 16));
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            int64_t t19 = gm_f_sys_int_32e79ed(t10, GM_CONV(gm_csguankko4lmhd, t14), t18);
            gm_l5 = t19;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t33) { case ((uint8_t)0ULL): goto ma31; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma31; case ((uint8_t)3ULL): goto ma31; case ((uint8_t)4ULL): goto ma31; case ((uint8_t)5ULL): goto ma31; case ((uint8_t)6ULL): goto ma31; case ((uint8_t)7ULL): goto ma31; case ((uint8_t)8ULL): goto ma31; case ((uint8_t)9ULL): goto ma31; case ((uint8_t)10ULL): goto ma31; case ((uint8_t)11ULL): goto ma31; case ((uint8_t)12ULL): goto ma31; case ((uint8_t)13ULL): goto ma31; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma31; case ((uint8_t)16ULL): goto ma31; case ((uint8_t)17ULL): goto ma31; case ((uint8_t)18ULL): goto ma31; case ((uint8_t)19ULL): goto ma31; case ((uint8_t)20ULL): goto ma31; case ((uint8_t)21ULL): goto ma31; case ((uint8_t)22ULL): goto ma31; case ((uint8_t)23ULL): goto ma31; case ((uint8_t)24ULL): goto ma31; case ((uint8_t)25ULL): goto ma31; case ((uint8_t)26ULL): goto ma31; case ((uint8_t)27ULL): goto ma21; case ((uint8_t)28ULL): goto ma22; case ((uint8_t)29ULL): goto ma23; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma25; case ((uint8_t)32ULL): goto ma26; case ((uint8_t)33ULL): goto ma27; case ((uint8_t)34ULL): goto ma28; case ((uint8_t)35ULL): goto ma29; case ((uint8_t)36ULL): goto ma30; case ((uint8_t)37ULL): goto ma31; case ((uint8_t)38ULL): goto ma31; case ((uint8_t)39ULL): goto ma31; case ((uint8_t)40ULL): goto ma31; case ((uint8_t)41ULL): goto ma31; case ((uint8_t)42ULL): goto ma31; case ((uint8_t)43ULL): goto ma31; case ((uint8_t)44ULL): goto ma31; case ((uint8_t)45ULL): goto ma31; case ((uint8_t)46ULL): goto ma31; case ((uint8_t)47ULL): goto ma31; case ((uint8_t)48ULL): goto ma31; case ((uint8_t)49ULL): goto ma31; case ((uint8_t)50ULL): goto ma31; case ((uint8_t)51ULL): goto ma31; case ((uint8_t)52ULL): goto ma31; case ((uint8_t)53ULL): goto ma31; case ((uint8_t)54ULL): goto ma31; default: goto ma31; }
ma21:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t35 != ((uint8_t)27ULL)) goto mn34;
        {
            void * t37 = gm_l0;
            gm_tl874t2atnlh3 t38 = gm_l1;
            uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_srcn3jjc71a2bv3, 347, 41);
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t39 * 16));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_td0ept12e7q8b t43;
            gm_f_sys_src_5svsr6i((void *)&t43, (void *)NULL, cx, NULL, t37, t38, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t43))[0] != 0)) {
                gm_tarq9lp8tdk6f t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                ((uint8_t *)&t44)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t44 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t43) + 8)));
                F.ret = t44;
                goto gm_fret;
            }
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
            F.l6 = t45;
        }
        {
            void * t46 = gm_l0;
            uint64_t t47 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t47 >= (uintptr_t)(t48)) gm_trap_index(t47, t48, gm_srcn3jjc71a2bv3, 348, 32);
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t47 * 16));
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            unsigned __int128 t51 = gm_f_get_uint_bq8c4b7(t46, t50);
            uint16_t t52 = (uint16_t)t51;
            F.l7 = t52;
        }
        {
            gm_toe5fmr3vvf4d t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)12ULL));
            gm_t62ebbi2i5rkd t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            ((uint8_t *)&t54)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t54 + 8), t53);
            F.l8 = t54;
        }
        {
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uintptr_t, (((((uint8_t *)t56) + 1656) + 0) + 0) + 8);
            uint8_t t58 = (t57 > ((uint64_t)1ULL));
            if (!t58) goto ie55;
            {
                void * t60 = gm_l0;
                gm_tl874t2atnlh3 t61 = gm_l1;
                gm_tvaas22dl64e0 t62;
                memset((uint8_t *)&t62 + 18, 0, 6);
                gm_b16_8 t63 = F.l6;
                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t64);
                uint16_t t65 = F.l7;
                GM_ST(uint16_t, (uint8_t *)&t62 + 16, t65);
                gm_tmvdbo28o1i6h t66;
                GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)2ULL));
                GM_ST(gm_tvaas22dl64e0, ((uint8_t *)&t66 + 8), t62);
                gm_toqti1ubjnfd1 t67;
                gm_f_socket_on_host_thread_u8c2vg5((void *)&t67, (void *)NULL, cx, NULL, t60, t61, &t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t67))[0] != 0)) {
                    gm_tarq9lp8tdk6f t68;
                    GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                    ((uint8_t *)&t68)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t68 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t67) + 8)));
                    F.ret = t68;
                    goto gm_fret;
                }
                gm_t62ebbi2i5rkd t69 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&t67) + 8));
                F.l8 = t69;
            }
            goto ix55;
ie55:;
            {
                gm_closure t71 = ((gm_closure){(void *)gm_f_fn_aqcn8lk, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix55:;
        }
        {
            gm_t62ebbi2i5rkd t72 = F.l8;
            gm_tm98ap4112hee t73;
            if (!((((uint8_t *)&t72))[0] != 0)) {
                gm_tm98ap4112hee t74 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t72) + 8));
                t73 = t74;
            } else {
                gm_toe5fmr3vvf4d t75 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t72) + 8));
                gm_l9 = t75;
                gm_toe5fmr3vvf4d t76 = gm_l9;
                int64_t t77 = gm_f_sys_code_cjtlfo1(t76);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t77);
                goto gm_fdone;
            }
            gm_l10 = t73;
        }
        {
            void * t78 = gm_l0;
            void * t79 = (void *)(((uint8_t *)t78) + 1352);
            int64_t t80 = gm_f_sys_next_handle_r35ek81(t79);
            gm_l11 = t80;
        }
        {
            void * t81 = gm_l0;
            void * t82 = (void *)((((uint8_t *)t81) + 1352) + 112);
            int64_t t83 = gm_l11;
            gm_tm98ap4112hee t84 = gm_l10;
            gm_tftb1odc739t5 t85 = gm_f_put_qkbvor7(t82, t83, GM_CONV(gm_csfq64m1m1rt6m, t84));
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcn3jjc71a2bv3, 357, 13);
            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
        }
        {
            int64_t t87 = gm_l11;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t87);
            goto gm_fdone;
        }
        goto me20;
mn34:;
ma22:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t89 != ((uint8_t)28ULL)) goto mn88;
        {
            void * t91 = gm_l0;
            gm_tl874t2atnlh3 t92 = gm_l1;
            uint64_t t93 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcn3jjc71a2bv3, 361, 41);
            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t93 * 16));
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            gm_td0ept12e7q8b t97;
            gm_f_sys_src_5svsr6i((void *)&t97, (void *)NULL, cx, NULL, t91, t92, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t97))[0] != 0)) {
                gm_tarq9lp8tdk6f t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                ((uint8_t *)&t98)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t98 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t97) + 8)));
                F.ret = t98;
                goto gm_fret;
            }
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
            F.l12 = t99;
        }
        {
            void * t100 = gm_l0;
            uint64_t t101 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srcn3jjc71a2bv3, 362, 32);
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t101 * 16));
            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
            unsigned __int128 t105 = gm_f_get_uint_bq8c4b7(t100, t104);
            uint16_t t106 = (uint16_t)t105;
            F.l13 = t106;
        }
        {
            gm_toe5fmr3vvf4d t107;
            GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)12ULL));
            gm_t62ebbi2i5rkd t108;
            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
            ((uint8_t *)&t108)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t108 + 8), t107);
            F.l14 = t108;
        }
        {
            gm_closure t109 = ((gm_closure){(void *)gm_f_fn_cijad0r, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t62ebbi2i5rkd t110 = F.l14;
            gm_tm98ap4112hee t111;
            if (!((((uint8_t *)&t110))[0] != 0)) {
                gm_tm98ap4112hee t112 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t110) + 8));
                t111 = t112;
            } else {
                gm_toe5fmr3vvf4d t113 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t110) + 8));
                gm_l15 = t113;
                gm_toe5fmr3vvf4d t114 = gm_l15;
                int64_t t115 = gm_f_sys_code_cjtlfo1(t114);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t115);
                goto gm_fdone;
            }
            gm_l16 = t111;
        }
        {
            void * t116 = gm_l0;
            void * t117 = (void *)(((uint8_t *)t116) + 1352);
            int64_t t118 = gm_f_sys_next_handle_r35ek81(t117);
            gm_l17 = t118;
        }
        {
            void * t119 = gm_l0;
            void * t120 = (void *)((((uint8_t *)t119) + 1352) + 168);
            int64_t t121 = gm_l17;
            gm_tm98ap4112hee t122 = gm_l16;
            gm_tftb1odc739t5 t123 = gm_f_put_ha6815g(t120, t121, GM_CONV(gm_csfq64m1m1rt6m, t122));
            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srcn3jjc71a2bv3, 367, 13);
            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
        }
        {
            int64_t t125 = gm_l17;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t125);
            goto gm_fdone;
        }
        goto me20;
mn88:;
ma23:;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t127 != ((uint8_t)29ULL)) goto mn126;
        {
            void * t129 = gm_l0;
            void * t130 = (void *)((((uint8_t *)t129) + 1352) + 168);
            int64_t t131 = gm_l5;
            gm_tppo2hmut88dq t132 = gm_f_get_8o0rtd2(t130, t131);
            gm_tm98ap4112hee t133;
            if (((((uint8_t *)&t132))[8] != 0)) {
                gm_tm98ap4112hee t134 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t132) + 0));
                t133 = t134;
            } else {
                int64_t t135 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t135);
                goto gm_fdone;
            }
            F.l18 = t133;
        }
        {
            gm_toe5fmr3vvf4d t136;
            GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)12ULL));
            gm_tvcqs9u9iqpct t137;
            GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
            ((uint8_t *)&t137)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t137 + 4), t136);
            F.l19 = t137;
        }
        {
            gm_closure t138 = ((gm_closure){(void *)gm_f_fn_mslt7ot, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tvcqs9u9iqpct t139 = F.l19;
            uint16_t t140;
            if (!((((uint8_t *)&t139))[0] != 0)) {
                uint16_t t141 = GM_LD(uint16_t, (((uint8_t *)&t139) + 4));
                t140 = t141;
            } else {
                gm_toe5fmr3vvf4d t142 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t139) + 4));
                gm_l20 = t142;
                gm_toe5fmr3vvf4d t143 = gm_l20;
                int64_t t144 = gm_f_sys_code_cjtlfo1(t143);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t144);
                goto gm_fdone;
            }
            gm_l21 = t140;
        }
        {
            uint16_t t145 = gm_l21;
            int64_t t146 = (int64_t)t145;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t146);
            goto gm_fdone;
        }
        goto me20;
mn126:;
ma24:;
        uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t148 != ((uint8_t)30ULL)) goto mn147;
        {
            void * t150 = gm_l0;
            void * t151 = (void *)((((uint8_t *)t150) + 1352) + 168);
            int64_t t152 = gm_l5;
            gm_tppo2hmut88dq t153 = gm_f_get_8o0rtd2(t151, t152);
            gm_tm98ap4112hee t154;
            if (((((uint8_t *)&t153))[8] != 0)) {
                gm_tm98ap4112hee t155 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t153) + 0));
                t154 = t155;
            } else {
                int64_t t156 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t156);
                goto gm_fdone;
            }
            F.l22 = t154;
        }
        {
            gm_toe5fmr3vvf4d t157;
            GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)12ULL));
            gm_t62ebbi2i5rkd t158;
            GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
            ((uint8_t *)&t158)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t158 + 8), t157);
            F.l23 = t158;
        }
        {
            void * t160 = gm_l0;
            uint64_t t161 = GM_LD(uintptr_t, (((((uint8_t *)t160) + 1656) + 0) + 0) + 8);
            uint8_t t162 = (t161 > ((uint64_t)1ULL));
            if (!t162) goto ie159;
            {
                void * t164 = gm_l0;
                gm_tl874t2atnlh3 t165 = gm_l1;
                gm_tm98ap4112hee t166 = F.l22;
                gm_tmvdbo28o1i6h t167;
                GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t167 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t167 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)3ULL));
                GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t167 + 8), t166);
                gm_toqti1ubjnfd1 t168;
                gm_f_socket_on_host_thread_u8c2vg5((void *)&t168, (void *)NULL, cx, NULL, t164, t165, &t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t168))[0] != 0)) {
                    gm_tarq9lp8tdk6f t169;
                    GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
                    ((uint8_t *)&t169)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t169 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t168) + 8)));
                    F.ret = t169;
                    goto gm_fret;
                }
                gm_t62ebbi2i5rkd t170 = GM_LD(gm_t62ebbi2i5rkd, (((uint8_t *)&t168) + 8));
                F.l23 = t170;
            }
            goto ix159;
ie159:;
            {
                gm_closure t172 = ((gm_closure){(void *)gm_f_fn_rg62rr4, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix159:;
        }
        {
            gm_t62ebbi2i5rkd t173 = F.l23;
            gm_tm98ap4112hee t174;
            if (!((((uint8_t *)&t173))[0] != 0)) {
                gm_tm98ap4112hee t175 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t173) + 8));
                t174 = t175;
            } else {
                gm_toe5fmr3vvf4d t176 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t173) + 8));
                gm_l24 = t176;
                gm_toe5fmr3vvf4d t177 = gm_l24;
                int64_t t178 = gm_f_sys_code_cjtlfo1(t177);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t178);
                goto gm_fdone;
            }
            gm_l25 = t174;
        }
        {
            void * t179 = gm_l0;
            void * t180 = (void *)(((uint8_t *)t179) + 1352);
            int64_t t181 = gm_f_sys_next_handle_r35ek81(t180);
            gm_l26 = t181;
        }
        {
            void * t182 = gm_l0;
            void * t183 = (void *)((((uint8_t *)t182) + 1352) + 112);
            int64_t t184 = gm_l26;
            gm_tm98ap4112hee t185 = gm_l25;
            gm_tftb1odc739t5 t186 = gm_f_put_qkbvor7(t183, t184, GM_CONV(gm_csfq64m1m1rt6m, t185));
            if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 1, gm_srcn3jjc71a2bv3, 387, 13);
            gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t186) + 1));
        }
        {
            int64_t t188 = gm_l26;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t188);
            goto gm_fdone;
        }
        goto me20;
mn147:;
ma25:;
        uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t190 != ((uint8_t)31ULL)) goto mn189;
        {
            void * t192 = gm_l0;
            gm_tl874t2atnlh3 t193 = gm_l1;
            uint64_t t194 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srcn3jjc71a2bv3, 391, 41);
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t194 * 16));
            gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
            gm_td0ept12e7q8b t198;
            gm_f_sys_src_5svsr6i((void *)&t198, (void *)NULL, cx, NULL, t192, t193, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t198))[0] != 0)) {
                gm_tarq9lp8tdk6f t199;
                GM_ST(uint8_t, (uint8_t *)&t199 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t199 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
                ((uint8_t *)&t199)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t199 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t198) + 8)));
                F.ret = t199;
                goto gm_fret;
            }
            gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
            F.l27 = t200;
        }
        {
            void * t201 = gm_l0;
            void * t202 = (void *)((((uint8_t *)t201) + 1352) + 112);
            int64_t t203 = gm_l5;
            gm_tppo2hmut88dq t204 = gm_f_get_gej4j8d(t202, t203);
            gm_tm98ap4112hee t205;
            if (((((uint8_t *)&t204))[8] != 0)) {
                gm_tm98ap4112hee t206 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t204) + 0));
                t205 = t206;
            } else {
                int64_t t207 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t207);
                goto gm_fdone;
            }
            F.l28 = t205;
        }
        {
            gm_toe5fmr3vvf4d t208;
            GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)12ULL));
            gm_thf5kr0is9u9i t209;
            GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
            ((uint8_t *)&t209)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t209 + 8), t208);
            F.l29 = t209;
        }
        {
            void * t211 = gm_l0;
            uint64_t t212 = GM_LD(uintptr_t, (((((uint8_t *)t211) + 1656) + 0) + 0) + 8);
            uint8_t t213 = (t212 > ((uint64_t)1ULL));
            if (!t213) goto ie210;
            {
                void * t215 = gm_l0;
                gm_tl874t2atnlh3 t216 = gm_l1;
                gm_t3olsle24rveu t217;
                gm_tm98ap4112hee t218 = F.l28;
                GM_ST(gm_tm98ap4112hee, (uint8_t *)&t217 + 0, t218);
                gm_b16_8 t219 = F.l27;
                GM_ST(gm_b16_8, (uint8_t *)&t217 + 8, t219);
                gm_tmvdbo28o1i6h t220;
                GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)4ULL));
                GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t220 + 8), t217);
                gm_t6j8vdmi81h7c t221;
                gm_f_on_host_thread_kmegc07((void *)&t221, (void *)NULL, cx, NULL, t215, t216, &t220);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t221))[0] != 0)) {
                    gm_tarq9lp8tdk6f t222;
                    GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                    ((uint8_t *)&t222)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t222 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t221) + 8)));
                    F.ret = t222;
                    goto gm_fret;
                }
                gm_thf5kr0is9u9i t223 = GM_LD(gm_thf5kr0is9u9i, ((((uint8_t *)&t221) + 8) + 0));
                F.l29 = t223;
            }
            goto ix210;
ie210:;
            {
                gm_closure t225 = ((gm_closure){(void *)gm_f_fn_tlii61s, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t225);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix210:;
        }
        {
            gm_thf5kr0is9u9i t226 = F.l29;
            int64_t t227 = gm_f_sys_net_count_46l7ueq(t226);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t227);
            goto gm_fdone;
        }
        goto me20;
mn189:;
ma26:;
        uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t229 != ((uint8_t)32ULL)) goto mn228;
        {
            void * t231 = gm_l0;
            gm_tl874t2atnlh3 t232 = gm_l1;
            uint64_t t233 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t233 >= (uintptr_t)(t234)) gm_trap_index(t233, t234, gm_srcn3jjc71a2bv3, 402, 41);
            gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t233 * 16));
            gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
            gm_td0ept12e7q8b t237;
            gm_f_sys_dest_rtrb14h((void *)&t237, (void *)NULL, cx, NULL, t231, t232, t236);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t237))[0] != 0)) {
                gm_tarq9lp8tdk6f t238;
                GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                ((uint8_t *)&t238)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t238 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t237) + 8)));
                F.ret = t238;
                goto gm_fret;
            }
            gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&t237) + 8));
            F.l30 = t239;
        }
        {
            void * t240 = gm_l0;
            void * t241 = (void *)((((uint8_t *)t240) + 1352) + 112);
            int64_t t242 = gm_l5;
            gm_tppo2hmut88dq t243 = gm_f_get_gej4j8d(t241, t242);
            gm_tm98ap4112hee t244;
            if (((((uint8_t *)&t243))[8] != 0)) {
                gm_tm98ap4112hee t245 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t243) + 0));
                t244 = t245;
            } else {
                int64_t t246 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t246);
                goto gm_fdone;
            }
            F.l31 = t244;
        }
        {
            gm_toe5fmr3vvf4d t247;
            GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)12ULL));
            gm_thf5kr0is9u9i t248;
            GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
            ((uint8_t *)&t248)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t248 + 8), t247);
            F.l32 = t248;
        }
        {
            void * t250 = gm_l0;
            uint64_t t251 = GM_LD(uintptr_t, (((((uint8_t *)t250) + 1656) + 0) + 0) + 8);
            uint8_t t252 = (t251 > ((uint64_t)1ULL));
            if (!t252) goto ie249;
            {
                void * t254 = gm_l0;
                gm_tl874t2atnlh3 t255 = gm_l1;
                gm_t3olsle24rveu t256;
                gm_tm98ap4112hee t257 = F.l31;
                GM_ST(gm_tm98ap4112hee, (uint8_t *)&t256 + 0, t257);
                gm_b16_8 t258 = F.l30;
                GM_ST(gm_b16_8, (uint8_t *)&t256 + 8, t258);
                gm_tmvdbo28o1i6h t259;
                GM_ST(uint8_t, (uint8_t *)&t259 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t259 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t259 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)5ULL));
                GM_ST(gm_t3olsle24rveu, ((uint8_t *)&t259 + 8), t256);
                gm_t6j8vdmi81h7c t260;
                gm_f_on_host_thread_kmegc07((void *)&t260, (void *)NULL, cx, NULL, t254, t255, &t259);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t260))[0] != 0)) {
                    gm_tarq9lp8tdk6f t261;
                    GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                    ((uint8_t *)&t261)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t261 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t260) + 8)));
                    F.ret = t261;
                    goto gm_fret;
                }
                gm_thf5kr0is9u9i t262 = GM_LD(gm_thf5kr0is9u9i, ((((uint8_t *)&t260) + 8) + 0));
                F.l32 = t262;
            }
            goto ix249;
ie249:;
            {
                gm_closure t264 = ((gm_closure){(void *)gm_f_fn_ducmvdu, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t264);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix249:;
        }
        {
            gm_thf5kr0is9u9i t265 = F.l32;
            int64_t t266 = gm_f_sys_net_count_46l7ueq(t265);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t266);
            goto gm_fdone;
        }
        goto me20;
mn228:;
ma27:;
        uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t268 != ((uint8_t)33ULL)) goto mn267;
        {
            void * t271 = gm_l0;
            void * t272 = (void *)((((uint8_t *)t271) + 1352) + 112);
            int64_t t273 = gm_l5;
            gm_tppo2hmut88dq t274 = gm_f_remove_7bvuc21(t272, t273);
            if (!((((uint8_t *)&t274))[8] != 0)) goto ie270;
            gm_tm98ap4112hee t275 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&t274));
            F.l33 = t275;
            {
                gm_closure t277 = ((gm_closure){(void *)gm_f_fn_r8m0qjr, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t277);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
                goto gm_fdone;
            }
            goto ix270;
ie270:;
ix270:;
        }
        {
            void * t279 = gm_l0;
            void * t280 = (void *)((((uint8_t *)t279) + 1352) + 168);
            int64_t t281 = gm_l5;
            gm_tppo2hmut88dq t282 = gm_f_remove_3j2d0bb(t280, t281);
            if (!((((uint8_t *)&t282))[8] != 0)) goto ie278;
            gm_tm98ap4112hee t283 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)&t282));
            F.l34 = t283;
            {
                gm_closure t285 = ((gm_closure){(void *)gm_f_fn_1lff729, (void *)&F.h, cx});
                gm_f_defaults_p077jgf(t285);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
                goto gm_fdone;
            }
            goto ix278;
ie278:;
ix278:;
        }
        {
            int64_t t286 = GM_LD(int64_t, gm_c3qosqjl81k);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t286);
            goto gm_fdone;
        }
        goto me20;
mn267:;
ma28:;
        uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t288 != ((uint8_t)34ULL)) goto mn287;
        {
            void * t290 = gm_l0;
            uint64_t t291 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t292 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t291 >= (uintptr_t)(t292)) gm_trap_index(t291, t292, gm_srcn3jjc71a2bv3, 424, 50);
            void * t293 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t291 * 8));
            gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t293) + 96));
            uint64_t t295 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t296 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t295 >= (uintptr_t)(t296)) gm_trap_index(t295, t296, gm_srcn3jjc71a2bv3, 424, 62);
            gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t295 * 16));
            gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&t297));
            int64_t t299 = gm_f_sys_int_32e79ed(t290, GM_CONV(gm_csguankko4lmhd, t294), t298);
            gm_t9qkh0rsooc8d t300;
            int64_t t303 = GM_LD(int64_t, ((uint8_t *)&t299));
            if (!(t303 == ((int64_t)0LL))) goto mn302;
            gm_t9qkh0rsooc8d t304;
            GM_ST(uint8_t, ((uint8_t *)&t304 + 0), ((uint8_t)0ULL));
            t300 = t304;
            goto me301;
mn302:;
            int64_t t306 = GM_LD(int64_t, ((uint8_t *)&t299));
            if (!(t306 == ((int64_t)1LL))) goto mn305;
            gm_t9qkh0rsooc8d t307;
            GM_ST(uint8_t, ((uint8_t *)&t307 + 0), ((uint8_t)1ULL));
            t300 = t307;
            goto me301;
mn305:;
            gm_t9qkh0rsooc8d t309;
            GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)2ULL));
            t300 = t309;
            goto me301;
mn308:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 424, 33);
me301:;
            F.l35 = t300;
        }
        {
            void * t310 = gm_l0;
            void * t311 = (void *)((((uint8_t *)t310) + 1352) + 112);
            int64_t t312 = gm_l5;
            gm_tppo2hmut88dq t313 = gm_f_get_gej4j8d(t311, t312);
            gm_tm98ap4112hee t314;
            if (((((uint8_t *)&t313))[8] != 0)) {
                gm_tm98ap4112hee t315 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t313) + 0));
                t314 = t315;
            } else {
                int64_t t316 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t316);
                goto gm_fdone;
            }
            F.l36 = t314;
        }
        {
            gm_toe5fmr3vvf4d t317;
            GM_ST(uint8_t, (uint8_t *)&t317 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t317 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t317 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t317 + 0), ((uint8_t)12ULL));
            gm_ttln0d0hd70en t318;
            GM_ST(uint8_t, (uint8_t *)&t318 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t318 + 2, 0);
            ((uint8_t *)&t318)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t318 + 4), t317);
            F.l37 = t318;
        }
        {
            gm_closure t319 = ((gm_closure){(void *)gm_f_fn_vhnrvmh, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t319);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ttln0d0hd70en t320 = F.l37;
            int64_t t321 = gm_f_sys_unit_nrsqciq(t320);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t321);
            goto gm_fdone;
        }
        goto me20;
mn287:;
ma29:;
        uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t323 != ((uint8_t)35ULL)) goto mn322;
        {
            void * t325 = gm_l0;
            uint64_t t326 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t327 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t326 >= (uintptr_t)(t327)) gm_trap_index(t326, t327, gm_srcn3jjc71a2bv3, 435, 35);
            gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t326 * 16));
            gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
            unsigned __int128 t330 = gm_f_get_uint_bq8c4b7(t325, t329);
            uint64_t t331 = (uint64_t)t330;
            gm_l38 = t331;
        }
        {
            void * t332 = gm_l0;
            uint64_t t333 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t334 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t333 >= (uintptr_t)(t334)) gm_trap_index(t333, t334, gm_srcn3jjc71a2bv3, 436, 35);
            gm_b16_8 t335 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t333 * 16));
            gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
            unsigned __int128 t337 = gm_f_get_uint_bq8c4b7(t332, t336);
            uint64_t t338 = (uint64_t)t337;
            gm_l39 = t338;
        }
        {
            void * t339 = gm_l0;
            void * t340 = (void *)((((uint8_t *)t339) + 1352) + 112);
            int64_t t341 = gm_l5;
            gm_tppo2hmut88dq t342 = gm_f_get_gej4j8d(t340, t341);
            gm_tm98ap4112hee t343;
            if (((((uint8_t *)&t342))[8] != 0)) {
                gm_tm98ap4112hee t344 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t342) + 0));
                t343 = t344;
            } else {
                int64_t t345 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t345);
                goto gm_fdone;
            }
            F.l40 = t343;
        }
        {
            gm_t6ie8iajlnpt1 t346;
            uint64_t t348 = gm_l38;
            uint8_t t349 = (t348 > ((uint64_t)0ULL));
            if (!t349) goto ie347;
            uint64_t t351 = gm_l38;
            gm_t4ce0jd3tdro4 t352;
            GM_ST(uint64_t, ((uint8_t *)&t352 + 0), t351);
            gm_t4ce0jd3tdro4 t353 = t352;
            gm_t6ie8iajlnpt1 t354;
            GM_ST(uint8_t, (uint8_t *)&t354 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t354 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t354 + 12, 0);
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t354), t353);
            ((uint8_t *)&t354)[8] = 1;
            t346 = t354;
            goto ix347;
ie347:;
            gm_t6ie8iajlnpt1 t356;
            memset(&t356, 0, sizeof t356);
            gm_t6ie8iajlnpt1 t357 = t356;
            t346 = t357;
ix347:;
            F.l41 = t346;
        }
        {
            gm_t6ie8iajlnpt1 t358;
            uint64_t t360 = gm_l39;
            uint8_t t361 = (t360 > ((uint64_t)0ULL));
            if (!t361) goto ie359;
            uint64_t t363 = gm_l39;
            gm_t4ce0jd3tdro4 t364;
            GM_ST(uint64_t, ((uint8_t *)&t364 + 0), t363);
            gm_t4ce0jd3tdro4 t365 = t364;
            gm_t6ie8iajlnpt1 t366;
            GM_ST(uint8_t, (uint8_t *)&t366 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t366 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t366 + 12, 0);
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t366), t365);
            ((uint8_t *)&t366)[8] = 1;
            t358 = t366;
            goto ix359;
ie359:;
            gm_t6ie8iajlnpt1 t368;
            memset(&t368, 0, sizeof t368);
            gm_t6ie8iajlnpt1 t369 = t368;
            t358 = t369;
ix359:;
            F.l42 = t358;
        }
        {
            gm_toe5fmr3vvf4d t370;
            GM_ST(uint8_t, (uint8_t *)&t370 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t370 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t370 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)12ULL));
            gm_ttln0d0hd70en t371;
            GM_ST(uint8_t, (uint8_t *)&t371 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t371 + 2, 0);
            ((uint8_t *)&t371)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t371 + 4), t370);
            F.l43 = t371;
        }
        {
            gm_closure t372 = ((gm_closure){(void *)gm_f_fn_j9i1sr8, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ttln0d0hd70en t373 = F.l43;
            int64_t t374 = gm_f_sys_unit_nrsqciq(t373);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t374);
            goto gm_fdone;
        }
        goto me20;
mn322:;
ma30:;
        uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t376 != ((uint8_t)36ULL)) goto mn375;
        {
            void * t378 = gm_l0;
            void * t379 = (void *)((((uint8_t *)t378) + 1352) + 112);
            int64_t t380 = gm_l5;
            gm_tppo2hmut88dq t381 = gm_f_get_gej4j8d(t379, t380);
            gm_tm98ap4112hee t382;
            if (((((uint8_t *)&t381))[8] != 0)) {
                gm_tm98ap4112hee t383 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t381) + 0));
                t382 = t383;
            } else {
                int64_t t384 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t384);
                goto gm_fdone;
            }
            F.l44 = t382;
        }
        {
            void * t385 = gm_l0;
            gm_tl874t2atnlh3 t386 = gm_l1;
            uint64_t t387 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t388 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t387 >= (uintptr_t)(t388)) gm_trap_index(t387, t388, gm_srcn3jjc71a2bv3, 446, 41);
            gm_b16_8 t389 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t387 * 16));
            gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&t389));
            gm_td0ept12e7q8b t391;
            gm_f_sys_dest_rtrb14h((void *)&t391, (void *)NULL, cx, NULL, t385, t386, t390);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t391))[0] != 0)) {
                gm_tarq9lp8tdk6f t392;
                GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
                ((uint8_t *)&t392)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t392 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t391) + 8)));
                F.ret = t392;
                goto gm_fret;
            }
            gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&t391) + 8));
            F.l45 = t393;
        }
        {
            gm_toe5fmr3vvf4d t394;
            GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t394 + 0), ((uint8_t)12ULL));
            gm_thf5kr0is9u9i t395;
            GM_ST(uint8_t, (uint8_t *)&t395 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t395 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t395 + 4, 0);
            ((uint8_t *)&t395)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t395 + 8), t394);
            F.l46 = t395;
        }
        {
            gm_closure t396 = ((gm_closure){(void *)gm_f_fn_2gkkusq, (void *)&F.h, cx});
            gm_f_defaults_p077jgf(t396);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_thf5kr0is9u9i t397 = F.l46;
            int64_t t398 = gm_f_sys_count_50c5lcp(t397);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t398);
            goto gm_fdone;
        }
        goto me20;
mn375:;
ma31:;
        void * t400 = gm_l0;
        gm_tl874t2atnlh3 t401 = gm_l1;
        gm_b16_8 t402;
        GM_ST(void *, (uint8_t *)&t402, (void *)(gm_sd8o4m7695hpq));
        GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(37));
        gm_t76123gfet73g t403;
        gm_f_trap_qh84l9d((void *)&t403, (void *)NULL, cx, NULL, t400, t401, t402);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t403);
        goto gm_fdone;
        goto me20;
mn399:;
ma32:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 345, 5);
me20:;
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_code_ihgq920(gm_toe5fmr3vvf4d a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_net_count_46l7ueq(gm_thf5kr0is9u9i a0);
extern __attribute__((aligned(4))) int64_t gm_f_sys_next_handle_r35ek81(void * a0);
__attribute__((aligned(4))) void gm_f_sys_proc_jhdp277(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_proc_jhdp277 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l7; /* dir */
    gm_b16_8 gm_l8; /* program */
    uint32_t gm_l9; /* flags */
    gm_toe5fmr3vvf4d gm_l11; /* err */
    gm_tm98ap4112hee gm_l12; /* c */
    int64_t gm_l13; /* handle */
    int64_t gm_l14; /* h */
    gm_b16_8 gm_l15; /* data */
    gm_tm98ap4112hee gm_l16; /* c */
    gm_tm98ap4112hee gm_l17; /* c */
    gm_b16_8 gm_l18; /* buf */
    gm_tm98ap4112hee gm_l19; /* c */
    gm_toe5fmr3vvf4d gm_l20; /* err */
    gm_thdrvp9q0u7b5 gm_l21; /* got */
    gm_te0mgnjdphs16 gm_l22; /* chunk */
    gm_tm98ap4112hee gm_l23; /* c */
    gm_tlmjd3nndlf3r gm_l24; /* r */
    gm_toe5fmr3vvf4d gm_l25; /* err */
    gm_tsoiri44d0i6q gm_l26; /* status */
    uint32_t gm_l27; /* code */
    int32_t gm_l28; /* sig */
    gm_tm98ap4112hee gm_l29; /* c */
    gm_tr6norheal11n gm_l30; /* r */
    gm_toe5fmr3vvf4d gm_l31; /* err */
    gm_tqnlpelep1582 gm_l32; /* got */
    gm_tsoiri44d0i6q gm_l33; /* status */
    uint32_t gm_l34; /* code */
    int32_t gm_l35; /* sig */
    gm_tm98ap4112hee gm_l36; /* c */
    GM_STACK_CHECK(&F, 3285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
        if (!t3) goto ie2;
        {
            void * t5 = gm_l0;
            gm_tl874t2atnlh3 t6 = gm_l1;
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srcn3jjc71a2bv3, 470, 41);
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t7 * 16));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_t9922qjstblh1 t11;
            gm_f_sys_strings_r85ragc((void *)&t11, (void *)NULL, cx, NULL, t5, t6, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t11))[0] != 0)) {
                gm_tarq9lp8tdk6f t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
                ((uint8_t *)&t12)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t12 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t11) + 8)));
                F.ret = t12;
                goto gm_fret;
            }
            F.l5 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t11) + 8));
        }
        {
            void * t13 = gm_l0;
            gm_tl874t2atnlh3 t14 = gm_l1;
            uint64_t t15 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcn3jjc71a2bv3, 471, 40);
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t15 * 16));
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_t9922qjstblh1 t19;
            gm_f_sys_strings_r85ragc((void *)&t19, (void *)NULL, cx, NULL, t13, t14, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t19))[0] != 0)) {
                gm_tarq9lp8tdk6f t20;
                GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t20 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t19) + 8)));
                F.ret = t20;
                goto gm_fret;
            }
            F.l6 = (*(gm_trqfvmjtt4sij *)(((uint8_t *)&t19) + 8));
        }
        {
            void * t21 = gm_l0;
            gm_tl874t2atnlh3 t22 = gm_l1;
            uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcn3jjc71a2bv3, 472, 36);
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t23 * 16));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_td0ept12e7q8b t27;
            gm_f_sys_src_5svsr6i((void *)&t27, (void *)NULL, cx, NULL, t21, t22, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) {
                gm_tarq9lp8tdk6f t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t28 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t27) + 8)));
                F.ret = t28;
                goto gm_fret;
            }
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_l7 = t29;
        }
        {
            void * t30 = gm_l0;
            gm_tl874t2atnlh3 t31 = gm_l1;
            uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcn3jjc71a2bv3, 473, 40);
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 16));
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            gm_td0ept12e7q8b t36;
            gm_f_sys_src_5svsr6i((void *)&t36, (void *)NULL, cx, NULL, t30, t31, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_tarq9lp8tdk6f t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                ((uint8_t *)&t37)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t37 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t36) + 8)));
                F.ret = t37;
                goto gm_fret;
            }
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_l8 = t38;
        }
        {
            void * t39 = gm_l0;
            uint64_t t40 = (uintptr_t)((uint64_t)4ULL);
            uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_srcn3jjc71a2bv3, 474, 29);
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t40 * 16));
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            unsigned __int128 t44 = gm_f_get_uint_bq8c4b7(t39, t43);
            uint32_t t45 = (uint32_t)t44;
            gm_l9 = t45;
        }
        {
        }
        {
        }
        {
            uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            uint8_t t50 = ((t49 == ((uint64_t)0ULL)));
            if (!t50) goto ie48;
            {
                int64_t t52 = GM_LD(int64_t, gm_cndfnv7mbeh);
                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
                ((uint8_t *)&F.ret)[0] = 0;
                GM_ST(int64_t, (uint8_t *)&F.ret + 8, t52);
                uint8_t t53 = ((((uint8_t *)&F.ret))[0] != 0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t53;
                goto gm_ld4_2;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            memset((uint8_t *)&F.l10 + 3, 0, 5);
            memset((uint8_t *)&F.l10 + 50, 0, 6);
            uint32_t t54 = gm_l9;
            uint32_t t55 = (uint32_t)(t54 & ((uint32_t)3ULL));
            gm_t9qkh0rsooc8d t56 = gm_f_sys_stdio_nsl9o9u(t55);
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l10 + 0, t56);
            uint32_t t57 = gm_l9;
            unsigned __int128 n58 = (unsigned __int128)((uint64_t)2ULL);
            if (n58 >= 32) gm_trap_shift((uint64_t)n58, "u32", gm_srcn3jjc71a2bv3, 480, 32);
            uint32_t t59 = (uint32_t)(t57 >> (int)n58);
            uint32_t t60 = (uint32_t)(t59 & ((uint32_t)3ULL));
            gm_t9qkh0rsooc8d t61 = gm_f_sys_stdio_nsl9o9u(t60);
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l10 + 1, t61);
            uint32_t t62 = gm_l9;
            unsigned __int128 n63 = (unsigned __int128)((uint64_t)4ULL);
            if (n63 >= 32) gm_trap_shift((uint64_t)n63, "u32", gm_srcn3jjc71a2bv3, 481, 32);
            uint32_t t64 = (uint32_t)(t62 >> (int)n63);
            uint32_t t65 = (uint32_t)(t64 & ((uint32_t)3ULL));
            gm_t9qkh0rsooc8d t66 = gm_f_sys_stdio_nsl9o9u(t65);
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&F.l10 + 2, t66);
            gm_th2lqbo77pvsc t67;
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            uint8_t t70 = (t69 > ((uint64_t)0ULL));
            if (!t70) goto ie68;
            gm_b16_8 t72 = gm_l7;
            gm_b16_8 t73 = t72;
            gm_th2lqbo77pvsc t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t74), t73);
            ((uint8_t *)&t74)[16] = 1;
            t67 = t74;
            goto ix68;
ie68:;
            gm_th2lqbo77pvsc t76;
            memset(&t76, 0, sizeof t76);
            gm_th2lqbo77pvsc t77 = t76;
            t67 = t77;
ix68:;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l10 + 8, t67);
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            GM_ST(gm_b16_8, (uint8_t *)&F.l10 + 32, t79);
            uint32_t t80 = gm_l9;
            uint8_t t81 = gm_f_sys_has_0sego5l(t80, ((uint32_t)64ULL));
            GM_ST(uint8_t, (uint8_t *)&F.l10 + 48, t81);
            uint32_t t82 = gm_l9;
            uint8_t t83 = gm_f_sys_has_0sego5l(t82, ((uint32_t)128ULL));
            GM_ST(uint8_t, (uint8_t *)&F.l10 + 49, t83);
            gm_th2lqbo77pvsc t84;
            uint32_t t86 = gm_l9;
            uint8_t t87 = gm_f_sys_has_0sego5l(t86, ((uint32_t)256ULL));
            if (!t87) goto ie85;
            gm_b16_8 t89 = gm_l8;
            gm_b16_8 t90 = t89;
            gm_th2lqbo77pvsc t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t91), t90);
            ((uint8_t *)&t91)[16] = 1;
            t84 = t91;
            goto ix85;
ie85:;
            gm_th2lqbo77pvsc t93;
            memset(&t93, 0, sizeof t93);
            gm_th2lqbo77pvsc t94 = t93;
            t84 = t94;
ix85:;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l10 + 56, t84);
        }
        {
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            gm_node *pn97 = gm_find(cx, 2119230);
            if (!pn97) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 488, 14);
            gm_t62ebbi2i5rkd t98;
            t98 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, const void *))pn97->clauses[0])(pn97->env, pn97->hctx, pn97, t96, &(*(gm_t32dckm4jfarl *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_ld4_2;
            }
            gm_tm98ap4112hee t99;
            if (!((((uint8_t *)&t98))[0] != 0)) {
                gm_tm98ap4112hee t100 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t98) + 8));
                t99 = t100;
            } else {
                gm_toe5fmr3vvf4d t101 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t98) + 8));
                gm_l11 = t101;
                gm_toe5fmr3vvf4d t102 = gm_l11;
                int64_t t103 = gm_f_sys_code_cjtlfo1(t102);
                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
                ((uint8_t *)&F.ret)[0] = 0;
                GM_ST(int64_t, (uint8_t *)&F.ret + 8, t103);
                uint8_t t104 = ((((uint8_t *)&F.ret))[0] != 0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t104;
                goto gm_ld4_2;
            }
            gm_l12 = t99;
        }
        {
            void * t105 = gm_l0;
            void * t106 = (void *)(((uint8_t *)t105) + 1352);
            int64_t t107 = gm_f_sys_next_handle_r35ek81(t106);
            gm_l13 = t107;
        }
        {
            void * t108 = gm_l0;
            void * t109 = (void *)((((uint8_t *)t108) + 1352) + 224);
            int64_t t110 = gm_l13;
            gm_tm98ap4112hee t111 = gm_l12;
            gm_tftb1odc739t5 t112 = gm_f_put_0tusk8g(t109, t110, GM_CONV(gm_csfq64m1m1rt6m, t111));
            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 1, gm_srcn3jjc71a2bv3, 490, 9);
            gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
        }
        {
            int64_t t114 = gm_l13;
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
            ((uint8_t *)&F.ret)[0] = 0;
            GM_ST(int64_t, (uint8_t *)&F.ret + 8, t114);
            uint8_t t115 = ((((uint8_t *)&F.ret))[0] != 0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t115;
            goto gm_ld4_2;
        }
        {
            void * t116 = (void *)((uint8_t *)&F.l6);
            gm_f_deinit_gvn47sj(t116);
        }
        {
            void * t117 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_gvn47sj(t117);
        }
        goto gm_ls118;
gm_ld4_2:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t119 = (void *)((uint8_t *)&F.l6);
            gm_f_deinit_gvn47sj(t119);
            gm_unw = gm_saved;
        }
gm_ld4_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t120 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_gvn47sj(t120);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls118:;
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t121 = gm_l0;
        uint64_t t122 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t122 >= (uintptr_t)(t123)) gm_trap_index(t122, t123, gm_srcn3jjc71a2bv3, 493, 21);
        void * t124 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t122 * 8));
        gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t124) + 96));
        uint64_t t126 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t126 >= (uintptr_t)(t127)) gm_trap_index(t126, t127, gm_srcn3jjc71a2bv3, 493, 33);
        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t126 * 16));
        gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
        int64_t t130 = gm_f_sys_int_32e79ed(t121, GM_CONV(gm_csguankko4lmhd, t125), t129);
        gm_l14 = t130;
    }
    {
        uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t140) { case ((uint8_t)0ULL): goto ma138; case ((uint8_t)1ULL): goto ma138; case ((uint8_t)2ULL): goto ma138; case ((uint8_t)3ULL): goto ma138; case ((uint8_t)4ULL): goto ma138; case ((uint8_t)5ULL): goto ma138; case ((uint8_t)6ULL): goto ma138; case ((uint8_t)7ULL): goto ma138; case ((uint8_t)8ULL): goto ma138; case ((uint8_t)9ULL): goto ma138; case ((uint8_t)10ULL): goto ma138; case ((uint8_t)11ULL): goto ma138; case ((uint8_t)12ULL): goto ma138; case ((uint8_t)13ULL): goto ma138; case ((uint8_t)14ULL): goto ma138; case ((uint8_t)15ULL): goto ma138; case ((uint8_t)16ULL): goto ma138; case ((uint8_t)17ULL): goto ma138; case ((uint8_t)18ULL): goto ma138; case ((uint8_t)19ULL): goto ma138; case ((uint8_t)20ULL): goto ma138; case ((uint8_t)21ULL): goto ma138; case ((uint8_t)22ULL): goto ma138; case ((uint8_t)23ULL): goto ma138; case ((uint8_t)24ULL): goto ma138; case ((uint8_t)25ULL): goto ma138; case ((uint8_t)26ULL): goto ma138; case ((uint8_t)27ULL): goto ma138; case ((uint8_t)28ULL): goto ma138; case ((uint8_t)29ULL): goto ma138; case ((uint8_t)30ULL): goto ma138; case ((uint8_t)31ULL): goto ma138; case ((uint8_t)32ULL): goto ma138; case ((uint8_t)33ULL): goto ma138; case ((uint8_t)34ULL): goto ma138; case ((uint8_t)35ULL): goto ma138; case ((uint8_t)36ULL): goto ma138; case ((uint8_t)37ULL): goto ma138; case ((uint8_t)38ULL): goto ma132; case ((uint8_t)39ULL): goto ma133; case ((uint8_t)40ULL): goto ma134; case ((uint8_t)41ULL): goto ma135; case ((uint8_t)42ULL): goto ma137; case ((uint8_t)43ULL): goto ma136; case ((uint8_t)44ULL): goto ma138; case ((uint8_t)45ULL): goto ma138; case ((uint8_t)46ULL): goto ma138; case ((uint8_t)47ULL): goto ma138; case ((uint8_t)48ULL): goto ma138; case ((uint8_t)49ULL): goto ma138; case ((uint8_t)50ULL): goto ma138; case ((uint8_t)51ULL): goto ma138; case ((uint8_t)52ULL): goto ma138; case ((uint8_t)53ULL): goto ma138; case ((uint8_t)54ULL): goto ma138; default: goto ma138; }
ma132:;
        uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t142 != ((uint8_t)38ULL)) goto mn141;
        {
            void * t144 = gm_l0;
            gm_tl874t2atnlh3 t145 = gm_l1;
            uint64_t t146 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t146 >= (uintptr_t)(t147)) gm_trap_index(t146, t147, gm_srcn3jjc71a2bv3, 496, 41);
            gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t146 * 16));
            gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
            gm_td0ept12e7q8b t150;
            gm_f_sys_src_5svsr6i((void *)&t150, (void *)NULL, cx, NULL, t144, t145, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t150))[0] != 0)) {
                gm_tarq9lp8tdk6f t151;
                GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                ((uint8_t *)&t151)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t151 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t150) + 8)));
                F.ret = t151;
                goto gm_fret;
            }
            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&t150) + 8));
            gm_l15 = t152;
        }
        {
            void * t153 = gm_l0;
            void * t154 = (void *)((((uint8_t *)t153) + 1352) + 224);
            int64_t t155 = gm_l14;
            gm_tppo2hmut88dq t156 = gm_f_get_fqooi5f(t154, t155);
            gm_tm98ap4112hee t157;
            if (((((uint8_t *)&t156))[8] != 0)) {
                gm_tm98ap4112hee t158 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t156) + 0));
                t157 = t158;
            } else {
                int64_t t159 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t159);
                goto gm_fdone;
            }
            gm_l16 = t157;
        }
        {
            gm_tm98ap4112hee t160 = gm_l16;
            gm_b16_8 t161 = gm_l15;
            gm_node *pn162 = gm_find(cx, 2119230);
            if (!pn162) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 498, 30);
            gm_thf5kr0is9u9i t163;
            t163 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn162->clauses[1])(pn162->env, pn162->hctx, pn162, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t164 = gm_f_sys_count_50c5lcp(t163);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t164);
            goto gm_fdone;
        }
        goto me131;
mn141:;
ma133:;
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t166 != ((uint8_t)39ULL)) goto mn165;
        {
            void * t168 = gm_l0;
            void * t169 = (void *)((((uint8_t *)t168) + 1352) + 224);
            int64_t t170 = gm_l14;
            gm_tppo2hmut88dq t171 = gm_f_get_fqooi5f(t169, t170);
            gm_tm98ap4112hee t172;
            if (((((uint8_t *)&t171))[8] != 0)) {
                gm_tm98ap4112hee t173 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t171) + 0));
                t172 = t173;
            } else {
                int64_t t174 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t174);
                goto gm_fdone;
            }
            gm_l17 = t172;
        }
        {
            gm_tm98ap4112hee t175 = gm_l17;
            gm_node *pn176 = gm_find(cx, 2119230);
            if (!pn176) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 502, 13);
            gm_b0_1 t177;
            t177 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn176->clauses[2])(pn176->env, pn176->hctx, pn176, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
            goto gm_fdone;
        }
        goto me131;
mn165:;
ma134:;
        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t179 != ((uint8_t)40ULL)) goto mn178;
        {
            void * t181 = gm_l0;
            gm_tl874t2atnlh3 t182 = gm_l1;
            uint64_t t183 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t183 >= (uintptr_t)(t184)) gm_trap_index(t183, t184, gm_srcn3jjc71a2bv3, 506, 41);
            gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t183 * 16));
            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
            gm_td0ept12e7q8b t187;
            gm_f_sys_dest_rtrb14h((void *)&t187, (void *)NULL, cx, NULL, t181, t182, t186);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t187))[0] != 0)) {
                gm_tarq9lp8tdk6f t188;
                GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                ((uint8_t *)&t188)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t188 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t187) + 8)));
                F.ret = t188;
                goto gm_fret;
            }
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t187) + 8));
            gm_l18 = t189;
        }
        {
            void * t190 = gm_l0;
            void * t191 = (void *)((((uint8_t *)t190) + 1352) + 224);
            int64_t t192 = gm_l14;
            gm_tppo2hmut88dq t193 = gm_f_get_fqooi5f(t191, t192);
            gm_tm98ap4112hee t194;
            if (((((uint8_t *)&t193))[8] != 0)) {
                gm_tm98ap4112hee t195 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t193) + 0));
                t194 = t195;
            } else {
                int64_t t196 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t196);
                goto gm_fdone;
            }
            gm_l19 = t194;
        }
        {
            gm_tm98ap4112hee t197 = gm_l19;
            gm_b16_8 t198 = gm_l18;
            gm_node *pn199 = gm_find(cx, 2119230);
            if (!pn199) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 508, 20);
            gm_tlbmih0s72in3 t200;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn199->clauses[3])((void *)&t200, pn199->env, pn199->hctx, pn199, t197, t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thdrvp9q0u7b5 t201;
            if (!((((uint8_t *)&t200))[0] != 0)) {
                t201 = (*(gm_thdrvp9q0u7b5 *)(((uint8_t *)&t200) + 8));
            } else {
                gm_toe5fmr3vvf4d t202 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t200) + 8));
                gm_l20 = t202;
                gm_toe5fmr3vvf4d t203 = gm_l20;
                int64_t t204 = gm_f_sys_code_cjtlfo1(t203);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t204);
                goto gm_fdone;
            }
            gm_l21 = t201;
        }
        {
            gm_thdrvp9q0u7b5 t205 = gm_l21;
            gm_te0mgnjdphs16 t206;
            if (((((uint8_t *)&t205))[16] != 0)) {
                gm_te0mgnjdphs16 t207 = GM_LD(gm_te0mgnjdphs16, (((uint8_t *)&t205) + 0));
                t206 = t207;
            } else {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, ((int64_t)0LL));
                goto gm_fdone;
            }
            gm_l22 = t206;
        }
        {
            uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
            int64_t t209 = (int64_t)t208;
            int64_t t210;
            uint8_t t212 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0) + 0) == ((uint8_t)1ULL)));
            if (!t212) goto ie211;
            unsigned __int128 n214 = (unsigned __int128)((uint64_t)32ULL);
            if (n214 >= 64) gm_trap_shift((uint64_t)n214, "i64", gm_srcn3jjc71a2bv3, 510, 71);
            int64_t r215 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n214);
            bool o216 = (int64_t)(r215 >> (int)n214) != ((int64_t)1LL);
            if (o216) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 510, 71);
            int64_t t217 = r215;
            int64_t t218 = t217;
            t210 = t218;
            goto ix211;
ie211:;
            int64_t t220 = ((int64_t)0LL);
            t210 = t220;
ix211:;
            int64_t r221; bool o222 = __builtin_add_overflow(t209, t210, &r221);
            if (o222) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 510, 20);
            int64_t t223 = r221;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t223);
            goto gm_fdone;
        }
        goto me131;
mn178:;
ma135:;
        uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t225 != ((uint8_t)41ULL)) goto mn224;
        {
            void * t227 = gm_l0;
            void * t228 = (void *)((((uint8_t *)t227) + 1352) + 224);
            int64_t t229 = gm_l14;
            gm_tppo2hmut88dq t230 = gm_f_get_fqooi5f(t228, t229);
            gm_tm98ap4112hee t231;
            if (((((uint8_t *)&t230))[8] != 0)) {
                gm_tm98ap4112hee t232 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t230) + 0));
                t231 = t232;
            } else {
                int64_t t233 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t233);
                goto gm_fdone;
            }
            gm_l23 = t231;
        }
        {
            gm_tm98ap4112hee t234 = gm_l23;
            gm_node *pn235 = gm_find(cx, 2119230);
            if (!pn235) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 514, 18);
            gm_tlmjd3nndlf3r t236;
            t236 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn235->clauses[4])(pn235->env, pn235->hctx, pn235, t234);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t236;
        }
        {
            {
                void * t238 = gm_l0;
                void * t239 = (void *)((((uint8_t *)t238) + 1352) + 224);
                int64_t t240 = gm_l14;
                gm_tppo2hmut88dq t241 = gm_f_remove_jvpr521(t239, t240);
            }
        }
        {
            gm_tlmjd3nndlf3r t242 = gm_l24;
            gm_tsoiri44d0i6q t243;
            if (!((((uint8_t *)&t242))[0] != 0)) {
                gm_tsoiri44d0i6q t244 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&t242) + 4));
                t243 = t244;
            } else {
                gm_toe5fmr3vvf4d t245 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t242) + 4));
                gm_l25 = t245;
                gm_toe5fmr3vvf4d t246 = gm_l25;
                int64_t t247 = gm_f_sys_code_cjtlfo1(t246);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t247);
                goto gm_fdone;
            }
            gm_l26 = t243;
        }
        {
            gm_tarq9lp8tdk6f t248;
            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            if (t251 != ((uint8_t)0ULL)) goto mn250;
            uint32_t t252 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 4));
            gm_l27 = t252;
            uint32_t t253 = gm_l27;
            int64_t t254 = (int64_t)t253;
            gm_tarq9lp8tdk6f t255;
            GM_ST(uint8_t, (uint8_t *)&t255 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t255 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t255 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t255 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t255 + 24, 0);
            ((uint8_t *)&t255)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t255 + 8), t254);
            t248 = t255;
            goto me249;
mn250:;
            uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            if (t257 != ((uint8_t)1ULL)) goto mn256;
            int32_t t258 = GM_LD(int32_t, (((uint8_t *)&gm_l26) + 4));
            gm_l28 = t258;
            int32_t t259 = gm_l28;
            int64_t t260 = (int64_t)t259;
            unsigned __int128 n261 = (unsigned __int128)((uint64_t)32ULL);
            if (n261 >= 64) gm_trap_shift((uint64_t)n261, "i64", gm_srcn3jjc71a2bv3, 519, 47);
            int64_t r262 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n261);
            bool o263 = (int64_t)(r262 >> (int)n261) != ((int64_t)1LL);
            if (o263) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 519, 47);
            int64_t t264 = r262;
            int64_t r265; bool o266 = __builtin_add_overflow(t260, t264, &r265);
            if (o266) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 519, 35);
            int64_t t267 = r265;
            gm_tarq9lp8tdk6f t268;
            GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t268 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t268 + 24, 0);
            ((uint8_t *)&t268)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t268 + 8), t267);
            t248 = t268;
            goto me249;
mn256:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 517, 20);
me249:;
            uint8_t t269 = ((((uint8_t *)&t248))[0] != 0);
            F.ret = t248;
            goto gm_fret;
        }
        goto me131;
mn224:;
ma136:;
        uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t271 != ((uint8_t)43ULL)) goto mn270;
        {
            void * t273 = gm_l0;
            void * t274 = (void *)((((uint8_t *)t273) + 1352) + 224);
            int64_t t275 = gm_l14;
            gm_tppo2hmut88dq t276 = gm_f_get_fqooi5f(t274, t275);
            gm_tm98ap4112hee t277;
            if (((((uint8_t *)&t276))[8] != 0)) {
                gm_tm98ap4112hee t278 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t276) + 0));
                t277 = t278;
            } else {
                int64_t t279 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t279);
                goto gm_fdone;
            }
            gm_l29 = t277;
        }
        {
            gm_tm98ap4112hee t280 = gm_l29;
            gm_node *pn281 = gm_find(cx, 2119230);
            if (!pn281) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 524, 18);
            gm_tr6norheal11n t282;
            t282 = ((gm_tr6norheal11n (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn281->clauses[5])(pn281->env, pn281->hctx, pn281, t280);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t282;
        }
        {
            gm_tr6norheal11n t283 = gm_l30;
            gm_tqnlpelep1582 t284;
            if (!((((uint8_t *)&t283))[0] != 0)) {
                gm_tqnlpelep1582 t285 = GM_LD(gm_tqnlpelep1582, (((uint8_t *)&t283) + 4));
                t284 = t285;
            } else {
                gm_toe5fmr3vvf4d t286 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t283) + 4));
                gm_l31 = t286;
                gm_toe5fmr3vvf4d t287 = gm_l31;
                int64_t t288 = gm_f_sys_code_cjtlfo1(t287);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t288);
                goto gm_fdone;
            }
            gm_l32 = t284;
        }
        {
            gm_tqnlpelep1582 t289 = gm_l32;
            gm_tsoiri44d0i6q t290;
            if (((((uint8_t *)&t289))[8] != 0)) {
                gm_tsoiri44d0i6q t291 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&t289) + 0));
                t290 = t291;
            } else {
                unsigned __int128 n292 = (unsigned __int128)((uint64_t)33ULL);
                if (n292 >= 64) gm_trap_shift((uint64_t)n292, "i64", gm_srcn3jjc71a2bv3, 526, 39);
                int64_t r293 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n292);
                bool o294 = (int64_t)(r293 >> (int)n292) != ((int64_t)1LL);
                if (o294) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 526, 39);
                int64_t t295 = r293;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t295);
                goto gm_fdone;
            }
            gm_l33 = t290;
        }
        {
            {
                void * t297 = gm_l0;
                void * t298 = (void *)((((uint8_t *)t297) + 1352) + 224);
                int64_t t299 = gm_l14;
                gm_tppo2hmut88dq t300 = gm_f_remove_jvpr521(t298, t299);
            }
        }
        {
            gm_tarq9lp8tdk6f t301;
            uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
            if (t304 != ((uint8_t)0ULL)) goto mn303;
            uint32_t t305 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 4));
            gm_l34 = t305;
            uint32_t t306 = gm_l34;
            int64_t t307 = (int64_t)t306;
            gm_tarq9lp8tdk6f t308;
            GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t308 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t308 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t308 + 24, 0);
            ((uint8_t *)&t308)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t308 + 8), t307);
            t301 = t308;
            goto me302;
mn303:;
            uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
            if (t310 != ((uint8_t)1ULL)) goto mn309;
            int32_t t311 = GM_LD(int32_t, (((uint8_t *)&gm_l33) + 4));
            gm_l35 = t311;
            int32_t t312 = gm_l35;
            int64_t t313 = (int64_t)t312;
            unsigned __int128 n314 = (unsigned __int128)((uint64_t)32ULL);
            if (n314 >= 64) gm_trap_shift((uint64_t)n314, "i64", gm_srcn3jjc71a2bv3, 530, 47);
            int64_t r315 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n314);
            bool o316 = (int64_t)(r315 >> (int)n314) != ((int64_t)1LL);
            if (o316) gm_trap("integer overflow in `<<`", gm_srcn3jjc71a2bv3, 530, 47);
            int64_t t317 = r315;
            int64_t r318; bool o319 = __builtin_add_overflow(t313, t317, &r318);
            if (o319) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 530, 35);
            int64_t t320 = r318;
            gm_tarq9lp8tdk6f t321;
            GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t321 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t321 + 24, 0);
            ((uint8_t *)&t321)[0] = 0;
            GM_ST(int64_t, ((uint8_t *)&t321 + 8), t320);
            t301 = t321;
            goto me302;
mn309:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 528, 20);
me302:;
            uint8_t t322 = ((((uint8_t *)&t301))[0] != 0);
            F.ret = t301;
            goto gm_fret;
        }
        goto me131;
mn270:;
ma137:;
        uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t324 != ((uint8_t)42ULL)) goto mn323;
        {
            void * t326 = gm_l0;
            void * t327 = (void *)((((uint8_t *)t326) + 1352) + 224);
            int64_t t328 = gm_l14;
            gm_tppo2hmut88dq t329 = gm_f_get_fqooi5f(t327, t328);
            gm_tm98ap4112hee t330;
            if (((((uint8_t *)&t329))[8] != 0)) {
                gm_tm98ap4112hee t331 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t329) + 0));
                t330 = t331;
            } else {
                int64_t t332 = GM_LD(int64_t, gm_c3qosqjl81k);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
                GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t332);
                goto gm_fdone;
            }
            gm_l36 = t330;
        }
        {
            gm_tm98ap4112hee t333 = gm_l36;
            gm_node *pn334 = gm_find(cx, 2119230);
            if (!pn334) gm_trap("no handler installed for effect `Proc`", gm_srcn3jjc71a2bv3, 535, 29);
            gm_ttln0d0hd70en t335;
            t335 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn334->clauses[6])(pn334->env, pn334->hctx, pn334, t333);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t336 = gm_f_sys_unit_nrsqciq(t335);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t336);
            goto gm_fdone;
        }
        goto me131;
mn323:;
ma138:;
        void * t338 = gm_l0;
        gm_tl874t2atnlh3 t339 = gm_l1;
        gm_b16_8 t340;
        GM_ST(void *, (uint8_t *)&t340, (void *)(gm_stfd80s46313k));
        GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(37));
        gm_t76123gfet73g t341;
        gm_f_trap_qh84l9d((void *)&t341, (void *)NULL, cx, NULL, t338, t339, t340);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t341);
        goto gm_fdone;
        goto me131;
mn337:;
ma139:;
        gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 494, 5);
me131:;
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
inline __attribute__((aligned(4))) void gm_f_sys_read_b0gj9fg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_read_b0gj9fg F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* vals */
    GM_STACK_CHECK(&F, 437u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_srcn3jjc71a2bv3, 204, 33);
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t4 * 16));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_td0ept12e7q8b t8;
        gm_f_sys_dest_rtrb14h((void *)&t8, (void *)NULL, cx, NULL, t2, t3, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_tarq9lp8tdk6f t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t9 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t8) + 8)));
            F.ret = t9;
            goto gm_fret;
        }
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        F.l3 = t10;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, (((((uint8_t *)t12) + 1656) + 0) + 0) + 8);
        uint8_t t14 = (t13 > ((uint64_t)1ULL));
        if (!t14) goto ie11;
        {
            void * t16 = gm_l0;
            gm_tl874t2atnlh3 t17 = gm_l1;
            gm_b16_8 t18 = F.l3;
            gm_tmvdbo28o1i6h t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
            gm_t6j8vdmi81h7c t20;
            gm_f_on_host_thread_kmegc07((void *)&t20, (void *)NULL, cx, NULL, t16, t17, &t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t20))[0] != 0)) {
                gm_tarq9lp8tdk6f t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                ((uint8_t *)&t21)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t21 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t20) + 8)));
                F.ret = t21;
                goto gm_fret;
            }
            gm_thf5kr0is9u9i t22 = GM_LD(gm_thf5kr0is9u9i, ((((uint8_t *)&t20) + 8) + 0));
            int64_t t23 = gm_f_sys_count_50c5lcp(t22);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_toe5fmr3vvf4d t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)12ULL));
        gm_thf5kr0is9u9i t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        ((uint8_t *)&t25)[0] = 1;
        GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t25 + 8), t24);
        F.l4 = t25;
    }
    {
        gm_closure t26 = ((gm_closure){(void *)gm_f_fn_trt04qi, (void *)&F.h, cx});
        gm_f_defaults_p077jgf(t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_thf5kr0is9u9i t27 = F.l4;
        int64_t t28 = gm_f_sys_count_50c5lcp(t27);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out))[0] = 0;
        GM_ST(int64_t, (uint8_t *)&(*(gm_tarq9lp8tdk6f *)gm_out) + 8, t28);
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
extern __attribute__((aligned(4))) void gm_f_sys_ret_vpa2k8i(void * a0, gm_b16_8 a1, int64_t a2);
__attribute__((aligned(4))) void gm_f_sys_run_2vqjo3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_sys_run_2vqjo3u F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* dst */
    int64_t gm_l6; /* n */
    uint64_t gm_l7; /* now */
    uint64_t gm_l8; /* base */
    gm_b16_8 gm_l9; /* buf */
    gm_b16_8 gm_l10; /* data */
    GM_STACK_CHECK(&F, 1675u);
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
        {
            gm_l6 = ((int64_t)0LL);
        }
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t19) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma14; case ((uint8_t)45ULL): goto ma15; case ((uint8_t)46ULL): goto ma16; case ((uint8_t)47ULL): goto ma17; case ((uint8_t)48ULL): goto ma17; case ((uint8_t)49ULL): goto ma17; case ((uint8_t)50ULL): goto ma17; case ((uint8_t)51ULL): goto ma17; case ((uint8_t)52ULL): goto ma17; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma9; default: goto ma17; }
ma4:;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t21 != ((uint8_t)0ULL)) goto mn20;
            {
                void * t23 = gm_l0;
                gm_tl874t2atnlh3 t24 = gm_l1;
                gm_b16_8 t25 = gm_l4;
                gm_tarq9lp8tdk6f t26;
                gm_f_sys_read_b0gj9fg((void *)&t26, (void *)NULL, cx, NULL, t23, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t26))[0] != 0)) {
                    gm_tcfa6l7reoosn t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                    ((uint8_t *)&t27)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t26) + 8)));
                    F.ret = t27;
                    goto gm_fret;
                }
                int64_t t28 = GM_LD(int64_t, (((uint8_t *)&t26) + 8));
                gm_l6 = t28;
            }
            goto me3;
mn20:;
ma5:;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t30 != ((uint8_t)1ULL)) goto mn29;
            {
                gm_node *pn32 = gm_find(cx, 1579066750);
                if (!pn32) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 170, 20);
                gm_t4ce0jd3tdro4 t33;
                t33 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn32->clauses[0])(pn32->env, pn32->hctx, pn32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t33) + 0));
                gm_l7 = t34;
            }
            {
                void * t36 = gm_l0;
                uint8_t t37;
                if (((((((uint8_t *)t36) + 1352) + 288))[8] != 0)) goto mn39;
                t37 = ((uint8_t)1);
                goto me38;
mn39:;
                t37 = ((uint8_t)0);
                goto me38;
mn40:;
                gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 171, 16);
me38:;
                if (!t37) goto ie35;
                {
                    void * t42 = gm_l0;
                    uint64_t t43 = gm_l7;
                    gm_tf5srh4mp4b3i t44;
                    GM_ST(uint8_t, (uint8_t *)&t44 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t44 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t44), t43);
                    ((uint8_t *)&t44)[8] = 1;
                    GM_ST(gm_tf5srh4mp4b3i, ((((uint8_t *)t42) + 1352) + 288), t44);
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                void * t45 = gm_l0;
                gm_tf5srh4mp4b3i t46 = GM_LD(gm_tf5srh4mp4b3i, ((((uint8_t *)t45) + 1352) + 288));
                uint64_t t47;
                if (((((uint8_t *)&t46))[8] != 0)) {
                    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&t46) + 0));
                    t47 = t48;
                } else {
                    t47 = ((uint64_t)0ULL);
                }
                gm_l8 = t47;
            }
            {
                void * t49 = gm_l0;
                gm_b16_8 t50 = gm_l5;
                uint64_t t51 = gm_l7;
                uint64_t t52 = gm_l8;
                uint64_t r53; bool o54 = __builtin_sub_overflow(t51, t52, &r53);
                uint64_t t55 = r53;
                unsigned __int128 t56 = (unsigned __int128)t55;
                gm_f_put_uint_cddin99(t49, t50, t56);
            }
            {
                gm_b0_1 t57;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t57);
                goto gm_fdone;
            }
            goto me3;
mn29:;
ma6:;
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t59 != ((uint8_t)2ULL)) goto mn58;
            {
                gm_node *pn61 = gm_find(cx, 1579066750);
                if (!pn61) gm_trap("no handler installed for effect `Clock`", gm_srcn3jjc71a2bv3, 176, 29);
                int64_t t62;
                t62 = ((int64_t (*)(void *, gm_node *, gm_node *))pn61->clauses[1])(pn61->env, pn61->hctx, pn61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t62;
            }
            goto me3;
mn58:;
ma7:;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t64 != ((uint8_t)3ULL)) goto mn63;
            {
                void * t66 = gm_l0;
                gm_tl874t2atnlh3 t67 = gm_l1;
                void * t68 = gm_l0;
                uint64_t t69 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_srcn3jjc71a2bv3, 178, 45);
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t69 * 16));
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                unsigned __int128 t73 = gm_f_get_uint_bq8c4b7(t68, t72);
                uint64_t t74 = (uint64_t)t73;
                gm_f_sleep_gqtie06((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t66, t67, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me3;
mn63:;
ma8:;
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t76 != ((uint8_t)4ULL)) goto mn75;
            {
                void * t78 = gm_l0;
                gm_tl874t2atnlh3 t79 = gm_l1;
                uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcn3jjc71a2bv3, 181, 41);
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t80 * 16));
                gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                gm_td0ept12e7q8b t84;
                gm_f_sys_dest_rtrb14h((void *)&t84, (void *)NULL, cx, NULL, t78, t79, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t84))[0] != 0)) {
                    gm_tcfa6l7reoosn t85;
                    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                    ((uint8_t *)&t85)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t85 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t84) + 8)));
                    F.ret = t85;
                    goto gm_fret;
                }
                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t84) + 8));
                gm_l9 = t86;
            }
            {
                gm_b16_8 t87 = gm_l9;
                gm_node *pn88 = gm_find(cx, 1087599154);
                if (!pn88) gm_trap("no handler installed for effect `Rand`", gm_srcn3jjc71a2bv3, 182, 13);
                gm_b0_1 t89;
                t89 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn88->clauses[0])(pn88->env, pn88->hctx, pn88, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t90;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t90);
                goto gm_fdone;
            }
            goto me3;
mn75:;
ma9:;
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t92 != ((uint8_t)54ULL)) goto mn91;
            {
                void * t94 = gm_l0;
                gm_tl874t2atnlh3 t95 = gm_l1;
                uint64_t t96 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_srcn3jjc71a2bv3, 186, 41);
                gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t96 * 16));
                gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                gm_td0ept12e7q8b t100;
                gm_f_sys_src_5svsr6i((void *)&t100, (void *)NULL, cx, NULL, t94, t95, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t100))[0] != 0)) {
                    gm_tcfa6l7reoosn t101;
                    GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                    ((uint8_t *)&t101)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t101 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t100) + 8)));
                    F.ret = t101;
                    goto gm_fret;
                }
                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
                gm_l10 = t102;
            }
            {
                void * t103 = gm_l0;
                uint32_t t104;
                void * t106 = gm_l0;
                uint64_t t107 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_srcn3jjc71a2bv3, 187, 40);
                void * t109 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t107 * 8));
                gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t109) + 96));
                uint64_t t111 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_srcn3jjc71a2bv3, 187, 52);
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t111 * 16));
                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                int64_t t115 = gm_f_sys_int_32e79ed(t106, GM_CONV(gm_csguankko4lmhd, t110), t114);
                uint8_t t116 = ((t115 == ((int64_t)2LL)));
                if (!t116) goto ie105;
                uint32_t t118 = ((uint32_t)2ULL);
                t104 = t118;
                goto ix105;
ie105:;
                uint32_t t120 = ((uint32_t)1ULL);
                t104 = t120;
ix105:;
                gm_b16_8 t121 = gm_l10;
                gm_b0_1 t122 = gm_f_host_write_r26no87((void *)NULL, cx, NULL, t103, t104, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t123;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t123);
                goto gm_fdone;
            }
            goto me3;
mn91:;
ma10:;
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t125 != ((uint8_t)53ULL)) goto mn124;
            {
                uint32_t t127 = gm_f_cpu_count_0o5882g();
                int64_t t128 = (int64_t)t127;
                gm_l6 = t128;
            }
            goto me3;
mn124:;
ma11:;
            uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t132 != ((uint8_t)5ULL)) goto pa131;
            goto po130;
pa131:;
            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t134 != ((uint8_t)6ULL)) goto pa133;
            goto po130;
pa133:;
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t136 != ((uint8_t)7ULL)) goto pa135;
            goto po130;
pa135:;
            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t138 != ((uint8_t)8ULL)) goto pa137;
            goto po130;
pa137:;
            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t140 != ((uint8_t)9ULL)) goto pa139;
            goto po130;
pa139:;
            uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t142 != ((uint8_t)10ULL)) goto pa141;
            goto po130;
pa141:;
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t144 != ((uint8_t)11ULL)) goto pa143;
            goto po130;
pa143:;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t146 != ((uint8_t)12ULL)) goto pa145;
            goto po130;
pa145:;
            uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t148 != ((uint8_t)13ULL)) goto pa147;
            goto po130;
pa147:;
            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t150 != ((uint8_t)14ULL)) goto pa149;
            goto po130;
pa149:;
            uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t152 != ((uint8_t)15ULL)) goto pa151;
            goto po130;
pa151:;
            uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t154 != ((uint8_t)16ULL)) goto pa153;
            goto po130;
pa153:;
            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t156 != ((uint8_t)17ULL)) goto pa155;
            goto po130;
pa155:;
            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t158 != ((uint8_t)18ULL)) goto pa157;
            goto po130;
pa157:;
            uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t160 != ((uint8_t)19ULL)) goto pa159;
            goto po130;
pa159:;
            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t162 != ((uint8_t)20ULL)) goto pa161;
            goto po130;
pa161:;
            uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t164 != ((uint8_t)21ULL)) goto pa163;
            goto po130;
pa163:;
            uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t166 != ((uint8_t)22ULL)) goto pa165;
            goto po130;
pa165:;
            uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t168 != ((uint8_t)23ULL)) goto pa167;
            goto po130;
pa167:;
            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t170 != ((uint8_t)24ULL)) goto pa169;
            goto po130;
pa169:;
            uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t172 != ((uint8_t)25ULL)) goto pa171;
            goto po130;
pa171:;
            uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t174 != ((uint8_t)26ULL)) goto pa173;
            goto po130;
pa173:;
            goto mn129;
po130:;
            {
                void * t176 = gm_l0;
                gm_tl874t2atnlh3 t177 = gm_l1;
                gm_t9qkh0rsooc8d t178 = gm_l2;
                gm_b16_8 t179 = gm_l3;
                gm_b16_8 t180 = gm_l4;
                gm_tarq9lp8tdk6f t181;
                gm_f_sys_fs_u0grkf6((void *)&t181, (void *)NULL, cx, NULL, t176, t177, t178, t179, t180);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t181))[0] != 0)) {
                    gm_tcfa6l7reoosn t182;
                    GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
                    ((uint8_t *)&t182)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t182 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t181) + 8)));
                    F.ret = t182;
                    goto gm_fret;
                }
                int64_t t183 = GM_LD(int64_t, (((uint8_t *)&t181) + 8));
                gm_l6 = t183;
            }
            goto me3;
mn129:;
ma12:;
            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t187 != ((uint8_t)27ULL)) goto pa186;
            goto po185;
pa186:;
            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t189 != ((uint8_t)28ULL)) goto pa188;
            goto po185;
pa188:;
            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t191 != ((uint8_t)29ULL)) goto pa190;
            goto po185;
pa190:;
            uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t193 != ((uint8_t)30ULL)) goto pa192;
            goto po185;
pa192:;
            uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t195 != ((uint8_t)31ULL)) goto pa194;
            goto po185;
pa194:;
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t197 != ((uint8_t)32ULL)) goto pa196;
            goto po185;
pa196:;
            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t199 != ((uint8_t)33ULL)) goto pa198;
            goto po185;
pa198:;
            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t201 != ((uint8_t)34ULL)) goto pa200;
            goto po185;
pa200:;
            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t203 != ((uint8_t)35ULL)) goto pa202;
            goto po185;
pa202:;
            uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t205 != ((uint8_t)36ULL)) goto pa204;
            goto po185;
pa204:;
            goto mn184;
po185:;
            {
                void * t207 = gm_l0;
                gm_tl874t2atnlh3 t208 = gm_l1;
                gm_t9qkh0rsooc8d t209 = gm_l2;
                gm_b16_8 t210 = gm_l3;
                gm_b16_8 t211 = gm_l4;
                gm_tarq9lp8tdk6f t212;
                gm_f_sys_net_aff7h5l((void *)&t212, (void *)NULL, cx, NULL, t207, t208, t209, t210, t211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t212))[0] != 0)) {
                    gm_tcfa6l7reoosn t213;
                    GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t213 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t213 + 4, 0);
                    ((uint8_t *)&t213)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t213 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t212) + 8)));
                    F.ret = t213;
                    goto gm_fret;
                }
                int64_t t214 = GM_LD(int64_t, (((uint8_t *)&t212) + 8));
                gm_l6 = t214;
            }
            goto me3;
mn184:;
ma13:;
            uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t218 != ((uint8_t)37ULL)) goto pa217;
            goto po216;
pa217:;
            uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t220 != ((uint8_t)38ULL)) goto pa219;
            goto po216;
pa219:;
            uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t222 != ((uint8_t)39ULL)) goto pa221;
            goto po216;
pa221:;
            uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t224 != ((uint8_t)40ULL)) goto pa223;
            goto po216;
pa223:;
            uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t226 != ((uint8_t)41ULL)) goto pa225;
            goto po216;
pa225:;
            uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t228 != ((uint8_t)43ULL)) goto pa227;
            goto po216;
pa227:;
            uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t230 != ((uint8_t)42ULL)) goto pa229;
            goto po216;
pa229:;
            goto mn215;
po216:;
            {
                void * t232 = gm_l0;
                gm_tl874t2atnlh3 t233 = gm_l1;
                gm_t9qkh0rsooc8d t234 = gm_l2;
                gm_b16_8 t235 = gm_l3;
                gm_b16_8 t236 = gm_l4;
                gm_tarq9lp8tdk6f t237;
                gm_f_sys_proc_jhdp277((void *)&t237, (void *)NULL, cx, NULL, t232, t233, t234, t235, t236);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t237))[0] != 0)) {
                    gm_tcfa6l7reoosn t238;
                    GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                    ((uint8_t *)&t238)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t238 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t237) + 8)));
                    F.ret = t238;
                    goto gm_fret;
                }
                int64_t t239 = GM_LD(int64_t, (((uint8_t *)&t237) + 8));
                gm_l6 = t239;
            }
            goto me3;
mn215:;
ma14:;
            uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t241 != ((uint8_t)44ULL)) goto mn240;
            {
                void * t243 = gm_l0;
                gm_tl874t2atnlh3 t244 = gm_l1;
                gm_t9qkh0rsooc8d t245;
                GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)2ULL));
                gm_b16_8 t246 = gm_f_empty_slice_2p7d2kv();
                gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
                gm_b16_8 t248 = gm_l5;
                gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t243, t244, t245, ((uint32_t)0ULL), t247, t248);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me3;
mn240:;
ma15:;
            uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t250 != ((uint8_t)45ULL)) goto mn249;
            {
                void * t252 = gm_l0;
                gm_tl874t2atnlh3 t253 = gm_l1;
                gm_t9qkh0rsooc8d t254;
                GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)2ULL));
                gm_b16_8 t255 = gm_l4;
                gm_b16_8 t256 = gm_l5;
                gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t252, t253, t254, ((uint32_t)1ULL), t255, t256);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me3;
mn249:;
ma16:;
            uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t258 != ((uint8_t)46ULL)) goto mn257;
            {
                void * t260 = gm_l0;
                void * t261 = gm_l0;
                uint64_t t262 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t263 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t262 >= (uintptr_t)(t263)) gm_trap_index(t262, t263, gm_srcn3jjc71a2bv3, 196, 52);
                gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t262 * 16));
                gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                unsigned __int128 t266 = gm_f_get_uint_bq8c4b7(t261, t265);
                uint8_t t267 = (uint8_t)t266;
                gm_f_env_exit_prbvpjt((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t260, t267);
                goto gm_fdone;
            }
            goto me3;
mn257:;
ma17:;
            void * t269 = gm_l0;
            gm_tl874t2atnlh3 t270 = gm_l1;
            gm_b16_8 t271;
            GM_ST(void *, (uint8_t *)&t271, (void *)(gm_s27n8348bef64));
            GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(62));
            gm_t76123gfet73g t272;
            gm_f_trap_qh84l9d((void *)&t272, (void *)NULL, cx, NULL, t269, t270, t271);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t272);
            goto gm_fdone;
            goto me3;
mn268:;
ma18:;
            gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 167, 5);
me3:;
        }
        {
            void * t273 = gm_l0;
            gm_b16_8 t274 = gm_l5;
            int64_t t275 = gm_l6;
            gm_f_sys_ret_vpa2k8i(t273, t274, t275);
        }
    }
    gm_b0_1 t276;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t276);
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
inline __attribute__((aligned(4))) void gm_f_sys_src_5svsr6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_src_5svsr6i F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* v */
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_t934k1j7u70ct t9;
        gm_f_read_u6r4vns((void *)&t9, t6, t7, t8, ((uint64_t)1ULL));
        gm_b16_8 t10;
        if (!((((uint8_t *)&t9))[0] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            t10 = t11;
        } else {
            F.l4 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t9) + 8));
            void * t12 = gm_l0;
            gm_tl874t2atnlh3 t13 = gm_l1;
            gm_t76123gfet73g t14;
            gm_f_mem_trap_ecogko0((void *)&t14, (void *)NULL, cx, NULL, t12, t13, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8, t14);
            goto gm_fdone;
        }
        gm_l5 = t10;
    }
    {
        gm_b16_8 t15 = gm_l5;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8, t16);
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
inline __attribute__((aligned(4))) void gm_f_sys_state_cvl1lvl(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_sys_state_cvl1lvl F;
    GM_STACK_CHECK(&F, 296u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t4c7ipjj9s3fp t2;
        gm_f_hash_map_3sndrtb((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t3;
        gm_f_hash_map_irh77t1((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t4;
        gm_f_hash_map_abi2bn7((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t5;
        gm_f_hash_map_ql49boo((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t6;
        gm_f_hash_map_3d5nsdr((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 0, t2);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 56, t3);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 112, t4);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 168, t5);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 224, t6);
        GM_ST(int64_t, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 280, ((int64_t)0LL));
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_ttse10gjbhca2 *)gm_out) + 288, t7);
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_sys_stdio_nsl9o9u(uint32_t a0);
__attribute__((aligned(4))) void gm_f_sys_strings_r85ragc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_sys_strings_r85ragc F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    uint64_t gm_l4; /* ps */
    gm_b16_8 gm_l6; /* items */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* s */
    GM_STACK_CHECK(&F, 719u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
        uint64_t t7 = gm_f_us_cuv01fp(t6);
        gm_l4 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 184);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t12 = gm_l4;
        uint64_t r13; bool o14 = __builtin_mul_overflow(((uint64_t)2ULL), t12, &r13);
        if (o14) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 143, 47);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)t15;
        uint64_t r17; bool o18 = __builtin_mul_overflow(t11, t16, &r17);
        uint64_t t19 = o18 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r17;
        gm_t934k1j7u70ct t20;
        gm_f_read_u6r4vns((void *)&t20, t9, t10, t19, ((uint64_t)1ULL));
        gm_b16_8 t21;
        if (!((((uint8_t *)&t20))[0] != 0)) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            t21 = t22;
        } else {
            F.l5 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t20) + 8));
            void * t23 = gm_l0;
            gm_tl874t2atnlh3 t24 = gm_l1;
            gm_t76123gfet73g t25;
            gm_f_mem_trap_ecogko0((void *)&t25, (void *)NULL, cx, NULL, t23, t24, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        gm_l6 = t21;
    }
    {
        gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t28 = gm_f_us_cuv01fp(t27);
            gm_l8 = t28;
        }
        {
            for (;;) {
                uint64_t t30 = gm_l9;
                uint64_t t31 = gm_l8;
                uint8_t t32 = (t30 < t31);
                if (!t32) goto lb29;
                {
                    uint64_t t35 = gm_l9;
                    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t35));
                    gm_l10 = t38;
                    {
                        void * t40 = gm_l0;
                        gm_tl874t2atnlh3 t41 = gm_l1;
                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        void * t43 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                        uint64_t t44 = gm_l10;
                        uint64_t r45; bool o46 = __builtin_mul_overflow(t44, ((uint64_t)2ULL), &r45);
                        if (o46) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t47 = r45;
                        uint64_t t48 = gm_l4;
                        uint64_t r49; bool o50 = __builtin_mul_overflow(t47, t48, &r49);
                        if (o50) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 40);
                        uint64_t t51 = r49;
                        uint64_t t52 = (uintptr_t)t51;
                        uint64_t t53 = gm_l10;
                        uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                        if (o55) gm_trap("integer overflow in `+`", gm_srcn3jjc71a2bv3, 146, 53);
                        uint64_t t56 = r54;
                        uint64_t r57; bool o58 = __builtin_mul_overflow(t56, ((uint64_t)2ULL), &r57);
                        if (o58) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t59 = r57;
                        uint64_t t60 = gm_l4;
                        uint64_t r61; bool o62 = __builtin_mul_overflow(t59, t60, &r61);
                        if (o62) gm_trap("integer overflow in `*`", gm_srcn3jjc71a2bv3, 146, 52);
                        uint64_t t63 = r61;
                        uint64_t t64 = (uintptr_t)t63;
                        if (t52 > t64 || t64 > (uintptr_t)t42) gm_trap_slice(t52, t64, t42, gm_srcn3jjc71a2bv3, 146, 34);
                        gm_b16_8 t65;
                        GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t43 + t52 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - t52));
                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                        gm_td0ept12e7q8b t67;
                        gm_f_sys_src_5svsr6i((void *)&t67, (void *)NULL, cx, NULL, t40, t41, t66);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        if (((((uint8_t *)&t67))[0] != 0)) {
                            gm_t9922qjstblh1 t68;
                            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t68 + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&t68 + 40, 0);
                            ((uint8_t *)&t68)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t68 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t67) + 8)));
                            F.ret = t68;
                            goto gm_fret;
                        }
                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                        gm_l11 = t69;
                    }
                    {
                        void * t70 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t71 = gm_l11;
                        gm_tftb1odc739t5 t72 = gm_f_push_3ef746b(t70, t71);
                        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcn3jjc71a2bv3, 147, 9);
                        gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_srcn3jjc71a2bv3, 145, 5);
me36:;
                }
lc29:;
                {
                    uint64_t t75 = gm_l9;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l9 = t78;
                }
            }
gm_ll33:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb29; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc29; }
            }
            goto gm_lad;
lb29:;
        }
    }
    {
        const uint8_t *t79 = ((uint8_t *)&F.l7);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_t9922qjstblh1 *)gm_out) + 8, t79, 40);
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
extern __attribute__((aligned(4))) int64_t gm_f_sys_unit_nrsqciq(gm_ttln0d0hd70en a0);

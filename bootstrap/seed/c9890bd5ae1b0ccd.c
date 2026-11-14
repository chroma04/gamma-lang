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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t4rau9abddl2u;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4rau9abddl2u) == 104 && _Alignof(gm_t4rau9abddl2u) == 8 && offsetof(gm_t4rau9abddl2u, f0) == 0 && offsetof(gm_t4rau9abddl2u, f1) == 8 && offsetof(gm_t4rau9abddl2u, f2) == 48 && offsetof(gm_t4rau9abddl2u, f3) == 88, "layout of `SiteConst`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
struct gm_fr_borrowed_params_b39va3r {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_trqfvmjtt4sij l9; /* none */
    gm_trqfvmjtt4sij l12; /* none */
    gm_trqfvmjtt4sij l16; /* touched */
    gm_b16_8 ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cell_fn_refs_6icb85l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_compute_steady_c2v2ffl {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* escaping */
    gm_t4c7ipjj9s3fp l11; /* seen */
    gm_tjcptv9sagfq8 l14; /* c */
    gm_tq7qjpkmqmc5s l19; /* gl */
    gm_trqfvmjtt4sij l26; /* written */
    gm_trqfvmjtt4sij l30; /* w */
    gm_t4c7ipjj9s3fp l33; /* captured */
    gm_trqfvmjtt4sij l34; /* steady */
    gm_trqfvmjtt4sij l41; /* row */
    gm_trqfvmjtt4sij l51; /* calls */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_direct_calls_tgjq7k6 {
    gm_frame h;
    void * l1; /* out */
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_b0_1 ret;
};
struct gm_fr_each_cond_hcnk23r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_each_pat_vg13dgd {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_each_stmt_tt8e8go {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ft2q3st {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hs811oq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jc4kvvv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vub5ih1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_05d3agq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cmb4ji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_count_g9jh9qr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fn_djrq2n1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gnspmq2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_n57683i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p8hr8ki {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_refs_tvld7g2 {
    gm_frame h;
    gm_b16_8 l1; /* out */
    gm_b0_1 ret;
};
struct gm_fr_fn_url0b1k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_v10s5jv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vg5vaqp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_ag7vhtq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_oe801t5 {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_get_s2mpo5a {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_l12of40 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_const_arg_98lfonj {
    gm_frame h;
    gm_t4rau9abddl2u l2; /* v */
    uint8_t ret;
};
struct gm_fr_is_pure_l81kp3k {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_72a1c0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_frr0ceh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_r8qc0er {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_locals_in_aggregates_3jqq5eh {
    gm_frame h;
    void * l2; /* out */
    uint8_t l4; /* in2 */
    gm_b0_1 ret;
};
struct gm_fr_mutated_in_block_26pupm0 {
    gm_frame h;
    void * l0; /* g */
    void * l2; /* out */
    gm_b0_1 ret;
};
struct gm_fr_mutated_locals_tltgkrt {
    gm_frame h;
    void * l0; /* g */
    void * l2; /* out */
    gm_t710lsrq1m8gk l8; /* l */
    gm_b0_1 ret;
};
struct gm_fr_own_place_peh2qe9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pure_block_94oo6k4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pure_expr_fee5b14 {
    gm_frame h;
    void * l0; /* g */
    uint8_t l2; /* ok */
    gm_t710lsrq1m8gk l4; /* l */
    gm_tsth9qu1dv0g3 l6; /* callee */
    uint8_t ret;
};
struct gm_fr_read_during_build_ng10fmi {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* used */
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
struct gm_fr_steady_arg_7904qej {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_steady_params_ng2sd7r {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* s */
    gm_trqfvmjtt4sij l3; /* all */
    gm_trqfvmjtt4sij l4; /* copy */
    gm_b16_8 ret;
};
struct gm_fr_value_fn_refs_jv5jhbq {
    gm_frame h;
    gm_t6evimt84lndq l0; /* v */
    gm_t6evimt84lndq l7; /* x */
    gm_b0_1 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_borrowed_params_b39va3r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_captured_locals_glsvloj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_cell_fn_refs_6icb85l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_compute_steady_c2v2ffl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_const_into_v9nj7g1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_direct_calls_tgjq7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_each_cond_hcnk23r(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_each_pat_vg13dgd(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_each_stmt_tt8e8go(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ft2q3st(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hs811oq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jc4kvvv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vub5ih1(void);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_0q6v4m5(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_tajhkla(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05d3agq(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmb4ji5(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djrq2n1(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gnspmq2(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n57683i(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8hr8ki(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_fn_refs_tvld7g2(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_url0b1k(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v10s5jv(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg5vaqp(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_6bqup89(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_oe801t5(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_s2mpo5a(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_l12of40(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_is_const_arg_98lfonj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_pure_l81kp3k(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_72a1c0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_frr0ceh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_r8qc0er(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_locals_in_aggregates_3jqq5eh(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_in_block_26pupm0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_locals_tltgkrt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_new_site_const_qfbs2k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_own_place_peh2qe9(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pure_block_94oo6k4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pure_expr_fee5b14(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_69vvlrm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f8cmgo9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o6sr5eu(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_15gklke(void * a0, uint64_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_iucnhfg(void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_p2039fp(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_during_build_ng10fmi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_root_local_40frg7f(void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_steady_arg_7904qej(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_steady_params_ng2sd7r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_value_fn_refs_jv5jhbq(void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, void * a2);
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srca29n3fkrpbdt[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 97u);
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
    gm_t5v8n6qrn4rh4 t5 = t4;
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)6ULL)));
    if (t6) {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        uint64_t t9 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (t9 > ((uint64_t)16ULL));
        t1 = t10;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ft2q3st(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ft2q3st F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hs811oq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hs811oq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jc4kvvv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jc4kvvv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vub5ih1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vub5ih1 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05d3agq(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_05d3agq F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_in_block_26pupm0 *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_in_block_26pupm0 *)((&F.h)->up))->l2));
        gm_b0_1 t5 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmb4ji5(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_cmb4ji5 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_direct_calls_tgjq7k6 *)((&F.h)->up))->l1));
        gm_b0_1 t4 = gm_f_direct_calls_tgjq7k6((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_count_g9jh9qr F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 344) + 0) + 8);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djrq2n1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_djrq2n1 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l2));
        gm_b0_1 t5 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gnspmq2(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_gnspmq2 F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_each_pat_vg13dgd *)((&F.h)->up))->l1));
        gm_f_each_pat_vg13dgd(t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n57683i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_n57683i F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_mutated_locals_tltgkrt *)((&F.h)->up))->l2));
        gm_b0_1 t5 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8hr8ki(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_p8hr8ki F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2));
        if (t3) {
            void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l0));
            void * t5 = gm_l0;
            uint8_t t6 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2), t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_url0b1k(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_url0b1k F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2));
        if (t3) {
            void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l0));
            void * t5 = gm_l0;
            uint8_t t6 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_pure_expr_fee5b14 *)((&F.h)->up))->l2), t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v10s5jv(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_v10s5jv F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_locals_in_aggregates_3jqq5eh *)((&F.h)->up))->l4));
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_locals_in_aggregates_3jqq5eh *)((&F.h)->up))->l2));
        gm_b0_1 t5 = gm_f_locals_in_aggregates_3jqq5eh((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg5vaqp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_vg5vaqp F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_fn_refs_tvld7g2 *)((&F.h)->up))->l1));
        gm_f_fn_refs_tvld7g2(t2, t3);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ag7vhtq F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_efqiqfc(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tqs5p4n2tq19v t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint64_t t10 = gm_l2;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 0) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 16) + 12));
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[1] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_oe801t5(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_oe801t5 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_0q6v4m5(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tqs5p4n2tq19v t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint64_t t10 = gm_l2;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 0) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 12) + 8));
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[1] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_s2mpo5a(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_s2mpo5a F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_tajhkla(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tqs5p4n2tq19v t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint64_t t10 = gm_l2;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 0) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 24) + 16));
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[1] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_l12of40(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_l12of40 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jc4kvvv();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_const_arg_98lfonj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_is_const_arg_98lfonj F;
    void * gm_l0; /* g */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 0));
        gm_f_new_site_const_qfbs2k2((void *)&F.l2, (void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        void * t7 = (void *)((uint8_t *)&F.l2);
        uint8_t t8 = gm_f_const_into_v9nj7g1((void *)NULL, cx, NULL, t5, t6, ((uint64_t)0ULL), t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_list_72a1c0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_72a1c0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vub5ih1();
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
inline __attribute__((aligned(4))) void gm_f_list_frr0ceh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_frr0ceh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hs811oq();
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
inline __attribute__((aligned(4))) void gm_f_list_r8qc0er(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_r8qc0er F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ft2q3st();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) uint8_t gm_f_own_place_peh2qe9(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_own_place_peh2qe9 F;
    void * gm_l0; /* g */
    void * gm_l1; /* place */
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = gm_l1;
    gm_t9805qtonbsag t5 = gm_f_root_local_40frg7f(t3, t4);
    gm_t9805qtonbsag t6 = t5;
    uint8_t t7;
    if (((((uint8_t *)&t6))[4] != 0)) goto mn9;
    t7 = ((uint8_t)0);
    goto me8;
mn9:;
    t7 = ((uint8_t)1);
    goto me8;
mn10:;
    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 232, 51);
me8:;
    F.ret = t7;
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
    GM_STACK_CHECK(&F, 120u);
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
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_borrowed_params_b39va3r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_borrowed_params_b39va3r F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    uint8_t gm_l4; /* any */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* p */
    uint8_t gm_l8; /* c */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l15; /* body */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    uint32_t gm_l20; /* p */
    GM_STACK_CHECK(&F, 956u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            void * t7 = gm_l2;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 32));
            gm_l5 = t8;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l6;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l6;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t16 * 4));
                    uint32_t t19 = t18;
                    uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&t19));
                    gm_l7 = t22;
                    {
                        void * t24 = gm_l0;
                        void * t25 = (void *)(((uint8_t *)t24) + 8);
                        uint32_t t26 = gm_l7;
                        uint64_t t27 = (uint64_t)t26;
                        uint64_t t28 = (uintptr_t)t27;
                        void * t29 = gm_l2;
                        uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)t29) + 48) + 8);
                        if (t28 >= (uintptr_t)(t30)) gm_trap_index(t28, t30, gm_srca29n3fkrpbdt, 26, 26);
                        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t29) + 48)) + t28 * 40) + 16));
                        uint8_t t32 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t25, t31);
                        if (gm_unw.kind) {
                            goto gm_ll13;
                        }
                        gm_l8 = t32;
                    }
                    {
                        void * t33 = (void *)((uint8_t *)&F.l3);
                        uint8_t t34 = gm_l8;
                        gm_tftb1odc739t5 t35 = gm_f_push_vd1290p(t33, t34);
                        gm_tftb1odc739t5 t36 = t35;
                        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srca29n3fkrpbdt, 27, 9);
                        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                    }
                    {
                        uint8_t t39 = gm_l8;
                        if (!t39) goto ie38;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 25, 5);
me20:;
                }
lc9:;
                {
                    uint64_t t42 = gm_l6;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l6 = t45;
                }
            }
gm_ll13:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
            }
            goto gm_lad;
lb9:;
        }
    }
    {
        uint8_t t47 = gm_l4;
        uint8_t t48 = (uint8_t)!t47;
        if (!t48) goto ie46;
        {
            gm_f_list_s83jn2q((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t51 = gm_l2;
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t51) + 32));
                gm_l10 = t52;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l11;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l11;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t60 * 4));
                        uint32_t t63 = t62;
                        {
                            void * t67 = (void *)((uint8_t *)&F.l9);
                            gm_tftb1odc739t5 t68 = gm_f_push_vd1290p(t67, ((uint8_t)0));
                            gm_tftb1odc739t5 t69 = t68;
                            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srca29n3fkrpbdt, 32, 31);
                            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
                        }
                        goto me64;
mn65:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 32, 9);
me64:;
                    }
lc53:;
                    {
                        uint64_t t72 = gm_l11;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                        uint64_t t75 = r73;
                        gm_l11 = t75;
                    }
                }
lb53:;
            }
        }
        {
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t79 = gm_l0;
        uint32_t t80 = gm_l1;
        uint8_t t81 = gm_f_is_pure_l81kp3k((void *)NULL, cx, NULL, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t82 = (uint8_t)!t81;
        if (!t82) goto ie78;
        {
            void * t84 = gm_l0;
            uint32_t t85 = gm_l1;
            gm_b16_8 t86 = gm_f_steady_params_ng2sd7r((void *)NULL, cx, NULL, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t87 = 0;
            F.ret = t86;
            goto gm_fret;
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        void * t88 = gm_l2;
        void * t89 = GM_LD(void *, (((uint8_t *)t88) + 152));
        void * t90 = t89;
        void * t91;
        if ((GM_LD(void *, ((uint8_t *)&t90)) != 0)) {
            void * t92 = GM_LD(void *, (((uint8_t *)&t90) + 0));
            t91 = t92;
        } else {
            {
                gm_f_list_s83jn2q((void *)&F.l12, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    void * t95 = gm_l2;
                    gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)t95) + 32));
                    gm_l13 = t96;
                }
                {
                    gm_l14 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t98 = gm_l14;
                        uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        uint8_t t100 = (t98 < t99);
                        if (!t100) goto lb97;
                        {
                            uint64_t t103 = gm_l14;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            uint32_t t106 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t104 * 4));
                            uint32_t t107 = t106;
                            {
                                void * t111 = (void *)((uint8_t *)&F.l12);
                                gm_tftb1odc739t5 t112 = gm_f_push_vd1290p(t111, ((uint8_t)0));
                                gm_tftb1odc739t5 t113 = t112;
                                if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_srca29n3fkrpbdt, 38, 31);
                                gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
                            }
                            goto me108;
mn109:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 38, 9);
me108:;
                        }
lc97:;
                        {
                            uint64_t t116 = gm_l14;
                            uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                            uint64_t t119 = r117;
                            gm_l14 = t119;
                        }
                    }
lb97:;
                }
            }
            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            uint8_t t121 = 0;
            F.ret = t120;
            goto gm_fret;
        }
        gm_l15 = t91;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t122 = gm_l0;
        void * t123 = gm_l15;
        void * t124 = (void *)((uint8_t *)&F.l16);
        gm_b0_1 t125 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t122, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t127 = gm_l2;
            gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)t127) + 32));
            gm_l17 = t128;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t130 = gm_l18;
                uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t132 = (t130 < t131);
                if (!t132) goto lb129;
                {
                    uint64_t t135 = gm_l18;
                    uint64_t t136 = gm_l18;
                    uint64_t t137 = (uintptr_t)t136;
                    uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint32_t t139 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t137 * 4));
                    gm_tn68el8d14vku t140;
                    GM_ST(uint32_t, (uint8_t *)&t140 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t140 + 0), t135);
                    GM_ST(uint32_t, ((uint8_t *)&t140 + 8), t139);
                    gm_tn68el8d14vku t141 = t140;
                    uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&t141) + 0));
                    gm_l19 = t144;
                    uint32_t t145 = GM_LD(uint32_t, (((uint8_t *)&t141) + 8));
                    gm_l20 = t145;
                    {
                        uint64_t t148 = gm_l19;
                        uint64_t t149 = (uintptr_t)t148;
                        uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t149 >= (uintptr_t)(t150)) gm_trap_index(t149, t150, gm_srca29n3fkrpbdt, 44, 12);
                        uint8_t t151 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t149 * 1));
                        if (!t151) goto ie147;
                        uint32_t t152 = gm_l20;
                        gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                        gm_b16_8 t154 = t153;
                        uint8_t t155 = 0;
                        uint32_t t156 = t152;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t154) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t154))) + i * 4, ((uint8_t *)&t156))) { t155 = 1; break; }
                        uint8_t t157 = (uint8_t)(t155);
                        if (!t157) goto ie147;
                        {
                            uint64_t t159 = gm_l19;
                            uint64_t t160 = (uintptr_t)t159;
                            uint64_t t161 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                            if (t160 >= (uintptr_t)(t161)) gm_trap_index(t160, t161, gm_srca29n3fkrpbdt, 44, 50);
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t160 * 1), ((uint8_t)0));
                        }
                        goto ix147;
ie147:;
ix147:;
                    }
                    goto me142;
mn143:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 43, 5);
me142:;
                }
lc129:;
                {
                    uint64_t t163 = gm_l18;
                    uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                    uint64_t t166 = r164;
                    gm_l18 = t166;
                }
            }
lb129:;
        }
    }
    {
        gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_cell_fn_refs_6icb85l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_cell_fn_refs_6icb85l F;
    void * gm_l0; /* cell */
    gm_b16_8 gm_l1; /* out */
    void * gm_l2; /* seen */
    uint64_t gm_l3; /* key */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = (uintptr_t)t2;
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l2;
        uint64_t t6 = gm_l3;
        gm_tqs5p4n2tq19v t7 = gm_f_get_s2mpo5a(t5, t6);
        gm_tqs5p4n2tq19v t8 = t7;
        uint8_t t9;
        if (((((uint8_t *)&t8))[1] != 0)) goto mn11;
        t9 = ((uint8_t)0);
        goto me10;
mn11:;
        t9 = ((uint8_t)1);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 339, 8);
me10:;
        if (!t9) goto ie4;
        {
            gm_b0_1 t14;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t16 = gm_l2;
        uint64_t t17 = gm_l3;
        gm_tftb1odc739t5 t18 = gm_f_put_15gklke(t16, t17, ((uint8_t)1));
        gm_tftb1odc739t5 t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srca29n3fkrpbdt, 340, 5);
        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
    }
    {
        void * t21 = gm_l0;
        gm_b16_8 t22 = gm_l1;
        void * t23 = gm_l2;
        gm_b0_1 t24 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)(((uint8_t *)t21) + 0)), t22, t23);
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
__attribute__((aligned(4))) void gm_f_compute_steady_c2v2ffl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_compute_steady_c2v2ffl F;
    void * gm_l0; /* g */
    void * gm_l1; /* prog */
    uint64_t gm_l2; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* d */
    void * gm_l10; /* b */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l15; /* x */
    void * gm_l16; /* cell */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    void * gm_l20; /* cell */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint32_t gm_l23; /* f */
    uint32_t gm_l24; /* h */
    uint8_t gm_l25; /* debug_info */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    void * gm_l31; /* d */
    void * gm_l32; /* b */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* i */
    void * gm_l38; /* d */
    uint32_t gm_l39; /* f */
    uint8_t gm_l40; /* eligible */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint32_t gm_l44; /* p */
    uint64_t gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* i */
    void * gm_l48; /* d */
    void * gm_l49; /* b */
    uint32_t gm_l50; /* caller */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_tiilmfd16v3fc gm_l54; /* c */
    gm_b16_8 gm_l55; /* row */
    uint64_t gm_l56; /* <tmp> */
    uint64_t gm_l57; /* <tmp> */
    uint64_t gm_l58; /* k */
    GM_STACK_CHECK(&F, 2601u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l1;
        uint64_t t5 = gm_f_fn_count_g9jh9qr(t4);
        gm_l2 = t5;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t7 = gm_l2;
            gm_l4 = t7;
        }
        {
            for (;;) {
                uint64_t t9 = gm_l5;
                uint64_t t10 = gm_l4;
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l5;
                    uint64_t t15 = t14;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t20 = gm_f_push_vd1290p(t19, ((uint8_t)0));
                        gm_tftb1odc739t5 t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srca29n3fkrpbdt, 69, 21);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 69, 5);
me16:;
                }
lc8:;
                {
                    uint64_t t24 = gm_l5;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l5 = t27;
                }
            }
lb8:;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t29 = gm_l2;
            gm_l6 = t29;
        }
        {
            for (;;) {
                uint64_t t31 = gm_l7;
                uint64_t t32 = gm_l6;
                uint8_t t33 = (t31 < t32);
                if (!t33) goto lb30;
                {
                    uint64_t t36 = gm_l7;
                    uint64_t t37 = t36;
                    uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)&t37));
                    gm_l8 = t40;
                    {
                        void * t42 = gm_l1;
                        uint64_t t43 = gm_l8;
                        uint32_t t44 = (uint32_t)t43;
                        void * t45 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t42, t44);
                        if (gm_unw.kind) {
                            goto gm_ll34;
                        }
                        gm_l9 = t45;
                    }
                    {
                        void * t47 = gm_l9;
                        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 152));
                        void * t49 = t48;
                        if (!(GM_LD(void *, ((uint8_t *)&t49)) != 0)) goto ie46;
                        void * t50 = GM_LD(void *, ((uint8_t *)&t49));
                        gm_l10 = t50;
                        {
                            void * t52 = gm_l10;
                            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            gm_f_fn_refs_tvld7g2(t52, t53);
                        }
                        goto ix46;
ie46:;
ix46:;
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 70, 5);
me38:;
                }
lc30:;
                {
                    uint64_t t55 = gm_l7;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l7 = t58;
                }
            }
gm_ll34:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
            }
            goto gm_lad;
lb30:;
        }
    }
    {
        gm_f_hash_map_l12of40((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t60 = gm_l1;
            void * t61 = GM_LD(void *, (((uint8_t *)t60) + 0));
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((((uint8_t *)t61) + 1968) + 0));
            gm_l12 = t62;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t64 = gm_l13;
                uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t66 = (t64 < t65);
                if (!t66) goto lb63;
                {
                    uint64_t t69 = gm_l13;
                    uint64_t t70 = (uintptr_t)t69;
                    uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_tjcptv9sagfq8 t74 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t70 * 88));
                    F.l14 = t74;
                    {
                        void * t77 = GM_LD(void *, (((uint8_t *)&F.l14) + 64));
                        void * t78 = t77;
                        if (!(GM_LD(void *, ((uint8_t *)&t78)) != 0)) goto ie76;
                        void * t79 = GM_LD(void *, ((uint8_t *)&t78));
                        gm_l15 = t79;
                        {
                            void * t81 = gm_l15;
                            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            gm_f_fn_refs_tvld7g2(t81, t82);
                        }
                        goto ix76;
ie76:;
ix76:;
                    }
                    {
                        void * t84 = GM_LD(void *, (((uint8_t *)&F.l14) + 80));
                        void * t85 = t84;
                        if (!(GM_LD(void *, ((uint8_t *)&t85)) != 0)) goto ie83;
                        void * t86 = GM_LD(void *, ((uint8_t *)&t85));
                        gm_l16 = t86;
                        {
                            void * t88 = gm_l16;
                            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            void * t90 = (void *)((uint8_t *)&F.l11);
                            gm_b0_1 t91 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)(((uint8_t *)t88) + 0)), t89, t90);
                            if (gm_unw.kind) {
                                goto gm_ll67;
                            }
                        }
                        goto ix83;
ie83:;
ix83:;
                    }
                    goto me72;
mn73:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 75, 5);
me72:;
                }
lc63:;
                {
                    uint64_t t93 = gm_l13;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    uint64_t t96 = r94;
                    gm_l13 = t96;
                }
            }
gm_ll67:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb63; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc63; }
            }
            goto gm_lad;
lb63:;
        }
    }
    {
        {
            void * t98 = gm_l1;
            void * t99 = GM_LD(void *, (((uint8_t *)t98) + 0));
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((((uint8_t *)t99) + 1448) + 0));
            gm_l17 = t100;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t102 = gm_l18;
                uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t104 = (t102 < t103);
                if (!t104) goto lb101;
                {
                    uint64_t t107 = gm_l18;
                    uint64_t t108 = (uintptr_t)t107;
                    uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    gm_tq7qjpkmqmc5s t112 = GM_LD(gm_tq7qjpkmqmc5s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t108 * 104));
                    F.l19 = t112;
                    {
                        void * t115 = GM_LD(void *, (((uint8_t *)&F.l19) + 96));
                        void * t116 = t115;
                        if (!(GM_LD(void *, ((uint8_t *)&t116)) != 0)) goto ie114;
                        void * t117 = GM_LD(void *, ((uint8_t *)&t116));
                        gm_l20 = t117;
                        {
                            void * t119 = gm_l20;
                            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            void * t121 = (void *)((uint8_t *)&F.l11);
                            gm_b0_1 t122 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)(((uint8_t *)t119) + 0)), t120, t121);
                            if (gm_unw.kind) {
                                goto gm_ll105;
                            }
                        }
                        goto ix114;
ie114:;
ix114:;
                    }
                    goto me110;
mn111:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 79, 5);
me110:;
                }
lc101:;
                {
                    uint64_t t124 = gm_l18;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l18 = t127;
                }
            }
gm_ll105:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb101; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc101; }
            }
            goto gm_lad;
lb101:;
        }
    }
    {
        {
            void * t129 = gm_l1;
            void * t130 = GM_LD(void *, (((uint8_t *)t129) + 0));
            gm_b16_8 t131 = GM_LD(gm_b16_8, ((((uint8_t *)t130) + 920) + 0));
            gm_l21 = t131;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t133 = gm_l22;
                uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t135 = (t133 < t134);
                if (!t135) goto lb132;
                {
                    uint64_t t138 = gm_l22;
                    uint64_t t139 = (uintptr_t)t138;
                    uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    uint32_t t141 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t139 * 4));
                    uint32_t t142 = t141;
                    uint32_t t145 = GM_LD(uint32_t, ((uint8_t *)&t142));
                    gm_l23 = t145;
                    {
                        uint32_t t147 = gm_l23;
                        uint64_t t148 = (uint64_t)t147;
                        uint64_t t149 = (uintptr_t)t148;
                        uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t149 >= (uintptr_t)(t150)) gm_trap_index(t149, t150, gm_srca29n3fkrpbdt, 82, 40);
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t149 * 1), ((uint8_t)1));
                    }
                    goto me143;
mn144:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 82, 5);
me143:;
                }
lc132:;
                {
                    uint64_t t152 = gm_l22;
                    uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                    uint64_t t155 = r153;
                    gm_l22 = t155;
                }
            }
lb132:;
        }
    }
    {
        void * t157 = gm_l1;
        void * t158 = GM_LD(void *, (((uint8_t *)t157) + 0));
        if (!(((((uint8_t *)t158) + 960))[4] != 0)) goto ie156;
        uint32_t t159 = GM_LD(uint32_t, (((uint8_t *)t158) + 960));
        gm_l24 = t159;
        {
            uint32_t t161 = gm_l24;
            uint64_t t162 = (uint64_t)t161;
            uint64_t t163 = (uintptr_t)t162;
            uint64_t t164 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            if (t163 >= (uintptr_t)(t164)) gm_trap_index(t163, t164, gm_srca29n3fkrpbdt, 83, 32);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t163 * 1), ((uint8_t)1));
        }
        goto ix156;
ie156:;
ix156:;
    }
    {
        void * t165 = gm_l0;
        uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)t165) + 1136) + 2));
        gm_l25 = t166;
    }
    {
        gm_f_list_r8qc0er((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            uint64_t t168 = gm_l2;
            gm_l27 = t168;
        }
        {
            for (;;) {
                uint64_t t170 = gm_l28;
                uint64_t t171 = gm_l27;
                uint8_t t172 = (t170 < t171);
                if (!t172) goto lb169;
                {
                    uint64_t t175 = gm_l28;
                    uint64_t t176 = t175;
                    uint64_t t179 = GM_LD(uint64_t, ((uint8_t *)&t176));
                    gm_l29 = t179;
                    {
                        gm_f_list_go2qg0b((void *)&F.l30, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll173;
                        }
                    }
                    {
                        void * t181 = gm_l1;
                        uint64_t t182 = gm_l29;
                        uint32_t t183 = (uint32_t)t182;
                        void * t184 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t181, t183);
                        if (gm_unw.kind) {
                            goto gm_ll173;
                        }
                        gm_l31 = t184;
                    }
                    {
                        void * t186 = gm_l31;
                        void * t187 = GM_LD(void *, (((uint8_t *)t186) + 152));
                        void * t188 = t187;
                        if (!(GM_LD(void *, ((uint8_t *)&t188)) != 0)) goto ie185;
                        void * t189 = GM_LD(void *, ((uint8_t *)&t188));
                        gm_l32 = t189;
                        {
                            void * t191 = gm_l0;
                            void * t192 = gm_l32;
                            void * t193 = (void *)((uint8_t *)&F.l30);
                            gm_b0_1 t194 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t191, t192, t193);
                            if (gm_unw.kind) {
                                goto gm_ll173;
                            }
                        }
                        goto ix185;
ie185:;
ix185:;
                    }
                    {
                        void * t195 = (void *)((uint8_t *)&F.l26);
                        gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
                        gm_tftb1odc739t5 t197 = gm_f_push_o6sr5eu(t195, t196);
                        gm_tftb1odc739t5 t198 = t197;
                        if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 1, gm_srca29n3fkrpbdt, 91, 9);
                        gm_b0_1 t199 = GM_LD(gm_b0_1, (((uint8_t *)&t198) + 1));
                    }
                    goto me177;
mn178:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 87, 5);
me177:;
                }
lc169:;
                {
                    uint64_t t201 = gm_l28;
                    uint64_t r202; bool o203 = __builtin_add_overflow(t201, ((uint64_t)1ULL), &r202);
                    uint64_t t204 = r202;
                    gm_l28 = t204;
                }
            }
gm_ll173:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb169; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc169; }
            }
            goto gm_lad;
lb169:;
        }
    }
    {
        void * t205 = gm_l0;
        gm_f_captured_locals_glsvloj((void *)&F.l33, (void *)NULL, cx, NULL, t205);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_72a1c0b((void *)&F.l34, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l36 = ((uint64_t)0ULL);
        }
        {
            uint64_t t207 = gm_l2;
            gm_l35 = t207;
        }
        {
            for (;;) {
                uint64_t t209 = gm_l36;
                uint64_t t210 = gm_l35;
                uint8_t t211 = (t209 < t210);
                if (!t211) goto lb208;
                {
                    uint64_t t214 = gm_l36;
                    uint64_t t215 = t214;
                    uint64_t t218 = GM_LD(uint64_t, ((uint8_t *)&t215));
                    gm_l37 = t218;
                    {
                        void * t220 = gm_l1;
                        uint64_t t221 = gm_l37;
                        uint32_t t222 = (uint32_t)t221;
                        void * t223 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t220, t222);
                        if (gm_unw.kind) {
                            goto gm_ll212;
                        }
                        gm_l38 = t223;
                    }
                    {
                        uint64_t t224 = gm_l37;
                        uint32_t t225 = (uint32_t)t224;
                        gm_l39 = t225;
                    }
                    {
                        uint8_t t226 = 0;
                        uint8_t t227 = 0;
                        uint8_t t228 = 0;
                        uint8_t t229 = 0;
                        uint8_t t230 = gm_l25;
                        uint8_t t231 = (uint8_t)!t230;
                        if (t231) {
                            void * t232 = gm_l38;
                            uint8_t t233 = ((GM_LD(uint8_t, (((uint8_t *)t232) + 96) + 0) == ((uint8_t)0ULL)));
                            t229 = t233;
                        } else t229 = 0;
                        if (t229) {
                            void * t234 = gm_l38;
                            uint8_t t235;
                            if ((((((uint8_t *)t234) + 88))[4] != 0)) goto mn237;
                            t235 = ((uint8_t)1);
                            goto me236;
mn237:;
                            t235 = ((uint8_t)0);
                            goto me236;
mn238:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 98, 61);
me236:;
                            t228 = t235;
                        } else t228 = 0;
                        if (t228) {
                            void * t239 = gm_l38;
                            void * t240 = GM_LD(void *, (((uint8_t *)t239) + 152));
                            void * t241 = t240;
                            uint8_t t242;
                            if ((GM_LD(void *, ((uint8_t *)&t241)) != 0)) goto mn244;
                            t242 = ((uint8_t)0);
                            goto me243;
mn244:;
                            t242 = ((uint8_t)1);
                            goto me243;
mn245:;
                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 98, 82);
me243:;
                            t227 = t242;
                        } else t227 = 0;
                        if (t227) {
                            uint64_t t246 = gm_l37;
                            uint64_t t247 = (uintptr_t)t246;
                            uint64_t t248 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                            if (t247 >= (uintptr_t)(t248)) gm_trap_index(t247, t248, gm_srca29n3fkrpbdt, 98, 109);
                            uint8_t t249 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t247 * 1));
                            uint8_t t250 = (uint8_t)!t249;
                            t226 = t250;
                        } else t226 = 0;
                        gm_l40 = t226;
                    }
                    {
                        gm_f_list_s83jn2q((void *)&F.l41, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll212;
                        }
                    }
                    {
                        {
                            void * t252 = gm_l38;
                            gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)t252) + 32));
                            gm_l42 = t253;
                        }
                        {
                            gm_l43 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t255 = gm_l43;
                                uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                uint8_t t257 = (t255 < t256);
                                if (!t257) goto lb254;
                                {
                                    uint64_t t260 = gm_l43;
                                    uint64_t t261 = (uintptr_t)t260;
                                    uint64_t t262 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                    uint32_t t263 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t261 * 4));
                                    uint32_t t264 = t263;
                                    uint32_t t267 = GM_LD(uint32_t, ((uint8_t *)&t264));
                                    gm_l44 = t267;
                                    {
                                        void * t269 = (void *)((uint8_t *)&F.l41);
                                        uint8_t t270 = 0;
                                        uint8_t t271 = 0;
                                        uint8_t t272 = 0;
                                        uint8_t t273 = gm_l40;
                                        if (t273) {
                                            void * t274 = gm_l0;
                                            void * t275 = (void *)(((uint8_t *)t274) + 8);
                                            uint32_t t276 = gm_l44;
                                            uint64_t t277 = (uint64_t)t276;
                                            uint64_t t278 = (uintptr_t)t277;
                                            void * t279 = gm_l38;
                                            uint64_t t280 = GM_LD(uintptr_t, (((uint8_t *)t279) + 48) + 8);
                                            if (t278 >= (uintptr_t)(t280)) gm_trap_index(t278, t280, gm_srca29n3fkrpbdt, 101, 47);
                                            gm_t88rcucg1b7uk t281 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t279) + 48)) + t278 * 40) + 16));
                                            uint8_t t282 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t275, t281);
                                            if (gm_unw.kind) {
                                                goto gm_ll258;
                                            }
                                            t272 = t282;
                                        } else t272 = 0;
                                        if (t272) {
                                            uint32_t t283 = gm_l44;
                                            uint64_t t284 = gm_l37;
                                            uint64_t t285 = (uintptr_t)t284;
                                            uint64_t t286 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 0) + 8);
                                            if (t285 >= (uintptr_t)(t286)) gm_trap_index(t285, t286, gm_srca29n3fkrpbdt, 101, 83);
                                            gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 0)) + t285 * 16));
                                            gm_b16_8 t288 = t287;
                                            uint8_t t289 = 0;
                                            uint32_t t290 = t283;
                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t288) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t288))) + i * 4, ((uint8_t *)&t290))) { t289 = 1; break; }
                                            uint8_t t291 = (uint8_t)(!t289);
                                            t271 = t291;
                                        } else t271 = 0;
                                        if (t271) {
                                            void * t292 = (void *)((uint8_t *)&F.l33);
                                            uint32_t t293 = gm_l39;
                                            uint32_t t294 = gm_l44;
                                            gm_tcc1rq4n1avb2 t295;
                                            GM_ST(uint32_t, ((uint8_t *)&t295 + 0), t293);
                                            GM_ST(uint32_t, ((uint8_t *)&t295 + 4), t294);
                                            gm_tqs5p4n2tq19v t296 = gm_f_get_ag7vhtq(t292, t295);
                                            gm_tqs5p4n2tq19v t297 = t296;
                                            uint8_t t298;
                                            if (((((uint8_t *)&t297))[1] != 0)) goto mn300;
                                            t298 = ((uint8_t)1);
                                            goto me299;
mn300:;
                                            t298 = ((uint8_t)0);
                                            goto me299;
mn301:;
                                            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 101, 104);
me299:;
                                            t270 = t298;
                                        } else t270 = 0;
                                        gm_tftb1odc739t5 t302 = gm_f_push_vd1290p(t269, t270);
                                        gm_tftb1odc739t5 t303 = t302;
                                        if (((((uint8_t *)&t303))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t303) + 1, gm_srca29n3fkrpbdt, 101, 13);
                                        gm_b0_1 t304 = GM_LD(gm_b0_1, (((uint8_t *)&t303) + 1));
                                    }
                                    goto me265;
mn266:;
                                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 100, 9);
me265:;
                                }
lc254:;
                                {
                                    uint64_t t306 = gm_l43;
                                    uint64_t r307; bool o308 = __builtin_add_overflow(t306, ((uint64_t)1ULL), &r307);
                                    uint64_t t309 = r307;
                                    gm_l43 = t309;
                                }
                            }
gm_ll258:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb254; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc254; }
                            }
                            goto gm_ll212;
lb254:;
                        }
                    }
                    {
                        void * t310 = (void *)((uint8_t *)&F.l34);
                        gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
                        gm_tftb1odc739t5 t312 = gm_f_push_69vvlrm(t310, t311);
                        gm_tftb1odc739t5 t313 = t312;
                        if (((((uint8_t *)&t313))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t313) + 1, gm_srca29n3fkrpbdt, 103, 9);
                        gm_b0_1 t314 = GM_LD(gm_b0_1, (((uint8_t *)&t313) + 1));
                    }
                    goto me216;
mn217:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 95, 5);
me216:;
                }
lc208:;
                {
                    uint64_t t316 = gm_l36;
                    uint64_t r317; bool o318 = __builtin_add_overflow(t316, ((uint64_t)1ULL), &r317);
                    uint64_t t319 = r317;
                    gm_l36 = t319;
                }
            }
gm_ll212:;
            if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb208; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc208; }
            }
            goto gm_lad;
lb208:;
        }
    }
    {
        {
            gm_l46 = ((uint64_t)0ULL);
        }
        {
            uint64_t t321 = gm_l2;
            gm_l45 = t321;
        }
        {
            for (;;) {
                uint64_t t323 = gm_l46;
                uint64_t t324 = gm_l45;
                uint8_t t325 = (t323 < t324);
                if (!t325) goto lb322;
                {
                    uint64_t t328 = gm_l46;
                    uint64_t t329 = t328;
                    uint64_t t332 = GM_LD(uint64_t, ((uint8_t *)&t329));
                    gm_l47 = t332;
                    {
                        void * t334 = gm_l1;
                        uint64_t t335 = gm_l47;
                        uint32_t t336 = (uint32_t)t335;
                        void * t337 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t334, t336);
                        if (gm_unw.kind) {
                            goto gm_ll326;
                        }
                        gm_l48 = t337;
                    }
                    {
                        void * t338 = gm_l48;
                        void * t339 = GM_LD(void *, (((uint8_t *)t338) + 152));
                        void * t340 = t339;
                        void * t341;
                        if ((GM_LD(void *, ((uint8_t *)&t340)) != 0)) {
                            void * t342 = GM_LD(void *, (((uint8_t *)&t340) + 0));
                            t341 = t342;
                        } else {
                            goto lc322;
                        }
                        gm_l49 = t341;
                    }
                    {
                        uint64_t t343 = gm_l47;
                        uint32_t t344 = (uint32_t)t343;
                        gm_l50 = t344;
                    }
                    {
                        gm_f_list_frr0ceh((void *)&F.l51, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll326;
                        }
                    }
                    {
                        void * t345 = gm_l49;
                        void * t346 = (void *)((uint8_t *)&F.l51);
                        gm_b0_1 t347 = gm_f_direct_calls_tgjq7k6((void *)NULL, cx, NULL, t345, t346);
                        if (gm_unw.kind) {
                            goto gm_ll326;
                        }
                    }
                    {
                        {
                            gm_b16_8 t349 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                            gm_l52 = t349;
                        }
                        {
                            gm_l53 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t351 = gm_l53;
                                uint64_t t352 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                                uint8_t t353 = (t351 < t352);
                                if (!t353) goto lb350;
                                {
                                    uint64_t t356 = gm_l53;
                                    uint64_t t357 = (uintptr_t)t356;
                                    uint64_t t358 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                                    gm_tiilmfd16v3fc t361 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t357 * 24));
                                    gm_l54 = t361;
                                    {
                                        uint32_t t363 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 0));
                                        uint64_t t364 = (uint64_t)t363;
                                        uint64_t t365 = (uintptr_t)t364;
                                        uint64_t t366 = GM_LD(uintptr_t, (((uint8_t *)&F.l34) + 0) + 8);
                                        if (t365 >= (uintptr_t)(t366)) gm_trap_index(t365, t366, gm_srca29n3fkrpbdt, 113, 20);
                                        gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l34) + 0)) + t365 * 16));
                                        gm_l55 = t367;
                                    }
                                    {
                                        {
                                            gm_l57 = ((uint64_t)0ULL);
                                        }
                                        {
                                            uint64_t t369 = GM_LD(uintptr_t, (((uint8_t *)&gm_l54) + 8) + 8);
                                            gm_l56 = t369;
                                        }
                                        {
                                            for (;;) {
                                                uint64_t t371 = gm_l57;
                                                uint64_t t372 = gm_l56;
                                                uint8_t t373 = (t371 < t372);
                                                if (!t373) goto lb370;
                                                {
                                                    uint64_t t376 = gm_l57;
                                                    uint64_t t377 = t376;
                                                    uint64_t t380 = GM_LD(uint64_t, ((uint8_t *)&t377));
                                                    gm_l58 = t380;
                                                    {
                                                        uint64_t t383 = gm_l58;
                                                        uint64_t t384 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                                        uint8_t t385 = (t383 < t384);
                                                        if (!t385) goto ie382;
                                                        uint64_t t386 = gm_l58;
                                                        uint64_t t387 = (uintptr_t)t386;
                                                        uint64_t t388 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                                        if (t387 >= (uintptr_t)(t388)) gm_trap_index(t387, t388, gm_srca29n3fkrpbdt, 115, 36);
                                                        uint8_t t389 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t387 * 1));
                                                        if (!t389) goto ie382;
                                                        void * t390 = gm_l0;
                                                        uint32_t t391 = gm_l50;
                                                        uint64_t t392 = gm_l58;
                                                        uint64_t t393 = (uintptr_t)t392;
                                                        uint64_t t394 = GM_LD(uintptr_t, (((uint8_t *)&gm_l54) + 8) + 8);
                                                        if (t393 >= (uintptr_t)(t394)) gm_trap_index(t393, t394, gm_srca29n3fkrpbdt, 115, 74);
                                                        void * t395 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l54) + 8)) + t393 * 88);
                                                        uint64_t t396 = gm_l47;
                                                        uint64_t t397 = (uintptr_t)t396;
                                                        uint64_t t398 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 0) + 8);
                                                        if (t397 >= (uintptr_t)(t398)) gm_trap_index(t397, t398, gm_srca29n3fkrpbdt, 115, 85);
                                                        gm_b16_8 t399 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 0)) + t397 * 16));
                                                        gm_b16_8 t400 = t399;
                                                        gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&t400));
                                                        void * t402 = (void *)((uint8_t *)&F.l33);
                                                        uint8_t t403 = gm_f_steady_arg_7904qej((void *)NULL, cx, NULL, t390, t391, t395, t401, t402);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll374;
                                                        }
                                                        uint8_t t404 = (uint8_t)!t403;
                                                        if (!t404) goto ie382;
                                                        {
                                                            uint64_t t406 = gm_l58;
                                                            uint64_t t407 = (uintptr_t)t406;
                                                            uint64_t t408 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                                                            if (t407 >= (uintptr_t)(t408)) gm_trap_index(t407, t408, gm_srca29n3fkrpbdt, 116, 21);
                                                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t407 * 1), ((uint8_t)0));
                                                        }
                                                        goto ix382;
ie382:;
ix382:;
                                                    }
                                                    goto me378;
mn379:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 114, 13);
me378:;
                                                }
lc370:;
                                                {
                                                    uint64_t t410 = gm_l57;
                                                    uint64_t r411; bool o412 = __builtin_add_overflow(t410, ((uint64_t)1ULL), &r411);
                                                    uint64_t t413 = r411;
                                                    gm_l57 = t413;
                                                }
                                            }
gm_ll374:;
                                            if (gm_unw.frame == &F.h && gm_unw.label == 10) {
                                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb370; }
                                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc370; }
                                            }
                                            goto gm_ll354;
lb370:;
                                        }
                                    }
                                    goto me359;
mn360:;
                                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 112, 9);
me359:;
                                }
lc350:;
                                {
                                    uint64_t t415 = gm_l53;
                                    uint64_t r416; bool o417 = __builtin_add_overflow(t415, ((uint64_t)1ULL), &r416);
                                    uint64_t t418 = r416;
                                    gm_l53 = t418;
                                }
                            }
gm_ll354:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb350; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc350; }
                            }
                            goto gm_ll326;
lb350:;
                        }
                    }
                    goto me330;
mn331:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 106, 5);
me330:;
                }
lc322:;
                {
                    uint64_t t420 = gm_l46;
                    uint64_t r421; bool o422 = __builtin_add_overflow(t420, ((uint64_t)1ULL), &r421);
                    uint64_t t423 = r421;
                    gm_l46 = t423;
                }
            }
gm_ll326:;
            if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb322; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc322; }
            }
            goto gm_lad;
lb322:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l34), 40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_direct_calls_tgjq7k6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_direct_calls_tgjq7k6 F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l3; /* args */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)34ULL)) goto ie2;
        gm_tsth9qu1dv0g3 t5 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t3) + 8) + 8) + 0));
        F.l2 = t5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 40));
        gm_l3 = t6;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t9 != ((uint8_t)0ULL)) goto ie8;
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 8));
            gm_l4 = t10;
            {
                void * t12 = F.l1;
                gm_tiilmfd16v3fc t13;
                memset((uint8_t *)&t13 + 4, 0, 4);
                uint32_t t14 = gm_l4;
                GM_ST(uint32_t, (uint8_t *)&t13 + 0, t14);
                gm_b16_8 t15 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t13 + 8, t15);
                gm_tftb1odc739t5 t16 = gm_f_push_f8cmgo9(t12, &t13);
                gm_tftb1odc739t5 t17 = t16;
                if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srca29n3fkrpbdt, 353, 31);
                gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        gm_closure t20 = ((gm_closure){(void *)gm_f_fn_cmb4ji5, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t19, t20);
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
inline __attribute__((aligned(4))) void gm_f_each_cond_hcnk23r(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_each_cond_hcnk23r F;
    gm_b16_8 gm_l0; /* cond */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 395u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 24));
                    gm_l4 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l5 = t20;
                        gm_closure t21 = gm_l1;
                        void * t22 = gm_l5;
                        gm_closure t23 = t21;
                        gm_node *ccx24 = t23.cx;
                        void * t25 = t23.code;
                        gm_b0_1 t26;
                        if (GM_PTR_TAGGED(t25)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t25))(t22);
                        } else {
                            t26 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t25)(t23.env, ccx24, NULL, t22);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn18:;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t28 != ((uint8_t)1ULL)) goto mn27;
                        void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l6 = t29;
                        void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l7 = t30;
                        {
                            void * t32 = gm_l6;
                            gm_closure t33 = gm_l1;
                            gm_f_each_pat_vg13dgd(t32, t33);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            gm_closure t34 = gm_l1;
                            void * t35 = gm_l7;
                            gm_closure t36 = t34;
                            gm_node *ccx37 = t36.cx;
                            void * t38 = t36.code;
                            gm_b0_1 t39;
                            if (GM_PTR_TAGGED(t38)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t38))(t35);
                            } else {
                                t39 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t38)(t36.env, ccx37, NULL, t35);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn27:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 237, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 236, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t41 = gm_l3;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l3 = t44;
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
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_each_pat_vg13dgd(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_each_pat_vg13dgd F;
    void * gm_l0; /* p */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)2ULL)) goto ie2;
        void * t5 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            gm_closure t7 = F.l1;
            void * t8 = gm_l2;
            gm_closure t9 = t7;
            gm_node *ccx10 = t9.cx;
            void * t11 = t9.code;
            gm_b0_1 t12;
            if (GM_PTR_TAGGED(t11)) {
                ((void (*)(void *))GM_PTR_UNTAG(t11))(t8);
            } else {
                t12 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t11)(t9.env, ccx10, NULL, t8);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gnspmq2, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t13, t14);
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
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_each_stmt_tt8e8go(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_each_stmt_tt8e8go F;
    void * gm_l0; /* b */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    void * gm_l5; /* init */
    void * gm_l6; /* x */
    void * gm_l7; /* x */
    void * gm_l8; /* p */
    void * gm_l9; /* v */
    gm_b16_8 gm_l10; /* ps */
    void * gm_l11; /* v */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l14; /* p */
    void * gm_l15; /* x */
    void * gm_l16; /* x */
    void * gm_l17; /* x */
    void * gm_l18; /* t */
    GM_STACK_CHECK(&F, 1097u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        switch (t25) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma23; case ((uint8_t)5ULL): goto ma23; default: goto ma24; }
ma19:;
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t27 != ((uint8_t)0ULL)) goto mn26;
                        void * t28 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l5 = t28;
                        void * t30 = gm_l5;
                        void * t31 = t30;
                        if (!(GM_LD(void *, ((uint8_t *)&t31)) != 0)) goto ie29;
                        void * t32 = GM_LD(void *, ((uint8_t *)&t31));
                        gm_l6 = t32;
                        {
                            gm_closure t34 = gm_l1;
                            void * t35 = gm_l6;
                            gm_closure t36 = t34;
                            gm_node *ccx37 = t36.cx;
                            void * t38 = t36.code;
                            gm_b0_1 t39;
                            if (GM_PTR_TAGGED(t38)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t38))(t35);
                            } else {
                                t39 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t38)(t36.env, ccx37, NULL, t35);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix29;
ie29:;
ix29:;
                        goto me18;
mn26:;
ma20:;
                        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t41 != ((uint8_t)1ULL)) goto mn40;
                        void * t42 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l7 = t42;
                        gm_closure t43 = gm_l1;
                        void * t44 = gm_l7;
                        gm_closure t45 = t43;
                        gm_node *ccx46 = t45.cx;
                        void * t47 = t45.code;
                        gm_b0_1 t48;
                        if (GM_PTR_TAGGED(t47)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t47))(t44);
                        } else {
                            t48 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t47)(t45.env, ccx46, NULL, t44);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn40:;
ma21:;
                        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t50 != ((uint8_t)2ULL)) goto mn49;
                        void * t51 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l8 = t51;
                        void * t52 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l9 = t52;
                        {
                            gm_closure t54 = gm_l1;
                            void * t55 = gm_l8;
                            gm_closure t56 = t54;
                            gm_node *ccx57 = t56.cx;
                            void * t58 = t56.code;
                            gm_b0_1 t59;
                            if (GM_PTR_TAGGED(t58)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t58))(t55);
                            } else {
                                t59 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t58)(t56.env, ccx57, NULL, t55);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            gm_closure t60 = gm_l1;
                            void * t61 = gm_l9;
                            gm_closure t62 = t60;
                            gm_node *ccx63 = t62.cx;
                            void * t64 = t62.code;
                            gm_b0_1 t65;
                            if (GM_PTR_TAGGED(t64)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t64))(t61);
                            } else {
                                t65 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t64)(t62.env, ccx63, NULL, t61);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn49:;
ma22:;
                        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t67 != ((uint8_t)3ULL)) goto mn66;
                        gm_b16_8 t68 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l10 = t68;
                        void * t69 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l11 = t69;
                        {
                            {
                                gm_b16_8 t72 = gm_l10;
                                gm_l12 = t72;
                            }
                            {
                                gm_l13 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t74 = gm_l13;
                                    uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    uint8_t t76 = (t74 < t75);
                                    if (!t76) goto lb73;
                                    {
                                        uint64_t t79 = gm_l13;
                                        uint64_t t80 = (uintptr_t)t79;
                                        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        void * t82 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t80 * 8));
                                        void * t83 = t82;
                                        void * t86 = GM_LD(void *, ((uint8_t *)&t83));
                                        gm_l14 = t86;
                                        {
                                            void * t89 = gm_l14;
                                            void * t90 = t89;
                                            if (!(GM_LD(void *, ((uint8_t *)&t90)) != 0)) goto ie88;
                                            void * t91 = GM_LD(void *, ((uint8_t *)&t90));
                                            gm_l15 = t91;
                                            {
                                                gm_closure t93 = gm_l1;
                                                void * t94 = gm_l15;
                                                gm_closure t95 = t93;
                                                gm_node *ccx96 = t95.cx;
                                                void * t97 = t95.code;
                                                gm_b0_1 t98;
                                                if (GM_PTR_TAGGED(t97)) {
                                                    ((void (*)(void *))GM_PTR_UNTAG(t97))(t94);
                                                } else {
                                                    t98 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t97)(t95.env, ccx96, NULL, t94);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_ll77;
                                                }
                                            }
                                            goto ix88;
ie88:;
ix88:;
                                        }
                                        goto me84;
mn85:;
                                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 264, 17);
me84:;
                                    }
lc73:;
                                    {
                                        uint64_t t100 = gm_l13;
                                        uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                                        uint64_t t103 = r101;
                                        gm_l13 = t103;
                                    }
                                }
gm_ll77:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb73; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc73; }
                                }
                                goto gm_ll9;
lb73:;
                            }
                        }
                        {
                            gm_closure t104 = gm_l1;
                            void * t105 = gm_l11;
                            gm_closure t106 = t104;
                            gm_node *ccx107 = t106.cx;
                            void * t108 = t106.code;
                            gm_b0_1 t109;
                            if (GM_PTR_TAGGED(t108)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t108))(t105);
                            } else {
                                t109 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t108)(t106.env, ccx107, NULL, t105);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn66:;
ma23:;
                        uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t113 != ((uint8_t)4ULL)) goto pa112;
                        void * t114 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l16 = t114;
                        goto po111;
pa112:;
                        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t116 != ((uint8_t)5ULL)) goto pa115;
                        void * t117 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l16 = t117;
                        goto po111;
pa115:;
                        goto mn110;
po111:;
                        gm_closure t118 = gm_l1;
                        void * t119 = gm_l16;
                        gm_closure t120 = t118;
                        gm_node *ccx121 = t120.cx;
                        void * t122 = t120.code;
                        gm_b0_1 t123;
                        if (GM_PTR_TAGGED(t122)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t122))(t119);
                        } else {
                            t123 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t122)(t120.env, ccx121, NULL, t119);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn110:;
ma24:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 256, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 255, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t125 = gm_l3;
                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                    uint64_t t128 = r126;
                    gm_l3 = t128;
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
    {
        void * t130 = gm_l0;
        void * t131 = GM_LD(void *, (((uint8_t *)t130) + 16));
        void * t132 = t131;
        if (!(GM_LD(void *, ((uint8_t *)&t132)) != 0)) goto ie129;
        void * t133 = GM_LD(void *, ((uint8_t *)&t132));
        gm_l18 = t133;
        {
            gm_closure t135 = gm_l1;
            void * t136 = gm_l18;
            gm_closure t137 = t135;
            gm_node *ccx138 = t137.cx;
            void * t139 = t137.code;
            gm_b0_1 t140;
            if (GM_PTR_TAGGED(t139)) {
                ((void (*)(void *))GM_PTR_UNTAG(t139))(t136);
            } else {
                t140 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t139)(t137.env, ccx138, NULL, t136);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix129;
ie129:;
ix129:;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05d3agq(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cmb4ji5(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_djrq2n1(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gnspmq2(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_n57683i(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8hr8ki(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_fn_refs_tvld7g2(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_refs_tvld7g2 F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* f */
    uint32_t gm_l3; /* f */
    GM_STACK_CHECK(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma4; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)26ULL)) goto mn9;
        uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t11;
        {
            uint32_t t13 = gm_l2;
            uint64_t t14 = (uint64_t)t13;
            uint64_t t15 = (uintptr_t)t14;
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&F.l1) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srca29n3fkrpbdt, 316, 24);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l1)) + t15 * 1), ((uint8_t)1));
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)27ULL)) goto mn17;
        uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t19;
        {
            uint32_t t21 = gm_l3;
            uint64_t t22 = (uint64_t)t21;
            uint64_t t23 = (uintptr_t)t22;
            uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&F.l1) + 8);
            if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srca29n3fkrpbdt, 317, 26);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l1)) + t23 * 1), ((uint8_t)1));
        }
        goto me3;
mn17:;
ma6:;
        goto me3;
mn25:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 315, 5);
me3:;
    }
    {
        void * t27 = gm_l0;
        gm_closure t28 = ((gm_closure){(void *)gm_f_fn_vg5vaqp, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t27, t28);
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
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_url0b1k(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v10s5jv(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg5vaqp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_pure_l81kp3k(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_is_pure_l81kp3k F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint8_t gm_l2; /* m */
    void * gm_l3; /* def */
    uint8_t gm_l4; /* r */
    void * gm_l5; /* b */
    GM_STACK_CHECK(&F, 239u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 2392);
        uint32_t t5 = gm_l1;
        gm_tqs5p4n2tq19v t6 = gm_f_get_oe801t5(t4, t5);
        gm_tqs5p4n2tq19v t7 = t6;
        if (!((((uint8_t *)&t7))[1] != 0)) goto ie2;
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            uint8_t t10 = gm_l2;
            uint8_t t11 = ((t10 == ((uint8_t)3ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 2392);
        uint32_t t15 = gm_l1;
        gm_tftb1odc739t5 t16 = gm_f_put_iucnhfg(t14, t15, ((uint8_t)1ULL));
        gm_tftb1odc739t5 t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srca29n3fkrpbdt, 168, 5);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint32_t t21 = gm_l1;
        void * t22 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t22;
    }
    {
        uint8_t t23 = 0;
        uint8_t t24 = 0;
        uint8_t t25 = 0;
        void * t26 = gm_l3;
        uint8_t t27 = ((GM_LD(uint8_t, (((uint8_t *)t26) + 96) + 0) == ((uint8_t)0ULL)));
        if (!t27) {
            void * t28 = gm_l3;
            uint8_t t29 = ((GM_LD(uint8_t, (((uint8_t *)t28) + 96) + 0) == ((uint8_t)1ULL)));
            t25 = t29;
        } else t25 = 1;
        if (t25) {
            void * t30 = gm_l3;
            uint8_t t31;
            if ((((((uint8_t *)t30) + 88))[4] != 0)) goto mn33;
            t31 = ((uint8_t)1);
            goto me32;
mn33:;
            t31 = ((uint8_t)0);
            goto me32;
mn34:;
            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 170, 59);
me32:;
            t24 = t31;
        } else t24 = 0;
        if (t24) {
            void * t35 = gm_l3;
            uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 72) + 0) + 8);
            uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
            t23 = t37;
        } else t23 = 0;
        gm_l4 = t23;
    }
    {
        uint8_t t39 = gm_l4;
        if (!t39) goto ie38;
        {
            gm_l4 = ((uint8_t)0);
        }
        {
            void * t42 = gm_l3;
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 152));
            void * t44 = t43;
            if (!(GM_LD(void *, ((uint8_t *)&t44)) != 0)) goto ie41;
            void * t45 = GM_LD(void *, ((uint8_t *)&t44));
            gm_l5 = t45;
            {
                void * t47 = gm_l0;
                void * t48 = gm_l5;
                uint8_t t49 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t47, t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t49;
            }
            goto ix41;
ie41:;
ix41:;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        void * t50 = gm_l0;
        void * t51 = (void *)(((uint8_t *)t50) + 2392);
        uint32_t t52 = gm_l1;
        uint8_t t53;
        uint8_t t55 = gm_l4;
        if (!t55) goto ie54;
        uint8_t t57 = ((uint8_t)3ULL);
        t53 = t57;
        goto ix54;
ie54:;
        uint8_t t59 = ((uint8_t)2ULL);
        t53 = t59;
ix54:;
        gm_tftb1odc739t5 t60 = gm_f_put_iucnhfg(t51, t52, t53);
        gm_tftb1odc739t5 t61 = t60;
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srca29n3fkrpbdt, 175, 5);
        gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
    }
    {
        uint8_t t63 = gm_l4;
        uint8_t t64 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_locals_in_aggregates_3jqq5eh(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2) {
    struct gm_fr_locals_in_aggregates_3jqq5eh F;
    void * gm_l0; /* e */
    uint8_t gm_l1; /* inside */
    uint32_t gm_l3; /* l */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            void * t6 = gm_l0;
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            if (t7 != ((uint8_t)15ULL)) goto ie5;
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)t6) + 8) + 8));
            gm_l3 = t8;
            {
                void * t10 = F.l2;
                uint32_t t11 = gm_l3;
                gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t10, t11);
                gm_tftb1odc739t5 t13 = t12;
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srca29n3fkrpbdt, 361, 34);
                gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
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
        uint8_t t15 = 0;
        uint8_t t16 = 0;
        uint8_t t17 = gm_l1;
        if (!t17) {
            void * t18 = gm_l0;
            uint8_t t19;
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            if (t22 != ((uint8_t)35ULL)) goto mn21;
            t19 = ((uint8_t)1);
            goto me20;
mn21:;
            t19 = ((uint8_t)0);
            goto me20;
mn23:;
            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 363, 22);
me20:;
            t16 = t19;
        } else t16 = 1;
        if (!t16) {
            void * t24 = gm_l0;
            uint8_t t25;
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            if (t28 != ((uint8_t)36ULL)) goto mn27;
            t25 = ((uint8_t)1);
            goto me26;
mn27:;
            t25 = ((uint8_t)0);
            goto me26;
mn29:;
            gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 363, 49);
me26:;
            t15 = t25;
        } else t15 = 1;
        F.l4 = t15;
    }
    {
        void * t30 = gm_l0;
        gm_closure t31 = ((gm_closure){(void *)gm_f_fn_v10s5jv, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t30, t31);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_in_block_26pupm0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mutated_in_block_26pupm0 F;
    void * gm_l1; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* s */
    void * gm_l6; /* place */
    uint32_t gm_l7; /* l */
    gm_b16_8 gm_l8; /* places */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    void * gm_l11; /* p */
    void * gm_l12; /* x */
    uint32_t gm_l13; /* l */
    GM_STACK_CHECK(&F, 565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        switch (t23) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma21; case ((uint8_t)5ULL): goto ma21; default: goto ma21; }
ma19:;
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t25 != ((uint8_t)2ULL)) goto mn24;
                        void * t26 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l6 = t26;
                        {
                            void * t29 = F.l0;
                            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
                            void * t32 = gm_l6;
                            gm_t9805qtonbsag t33 = gm_f_root_local_40frg7f(t31, t32);
                            gm_t9805qtonbsag t34 = t33;
                            if (!((((uint8_t *)&t34))[4] != 0)) goto ie28;
                            uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&t34));
                            gm_l7 = t35;
                            {
                                void * t37 = F.l2;
                                uint32_t t38 = gm_l7;
                                gm_tftb1odc739t5 t39 = gm_f_push_im9d4ro(t37, t38);
                                gm_tftb1odc739t5 t40 = t39;
                                if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srca29n3fkrpbdt, 299, 73);
                                gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto me18;
mn24:;
ma20:;
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t43 != ((uint8_t)3ULL)) goto mn42;
                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l8 = t44;
                        {
                            {
                                gm_b16_8 t47 = gm_l8;
                                gm_l9 = t47;
                            }
                            {
                                gm_l10 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t49 = gm_l10;
                                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    uint8_t t51 = (t49 < t50);
                                    if (!t51) goto lb48;
                                    {
                                        uint64_t t54 = gm_l10;
                                        uint64_t t55 = (uintptr_t)t54;
                                        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                        void * t57 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t55 * 8));
                                        void * t58 = t57;
                                        void * t61 = GM_LD(void *, ((uint8_t *)&t58));
                                        gm_l11 = t61;
                                        {
                                            void * t64 = gm_l11;
                                            void * t65 = t64;
                                            if (!(GM_LD(void *, ((uint8_t *)&t65)) != 0)) goto ie63;
                                            void * t66 = GM_LD(void *, ((uint8_t *)&t65));
                                            gm_l12 = t66;
                                            {
                                                void * t69 = F.l0;
                                                void * t70 = GM_LD(void *, (((uint8_t *)t69) + 0));
                                                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
                                                void * t72 = gm_l12;
                                                gm_t9805qtonbsag t73 = gm_f_root_local_40frg7f(t71, t72);
                                                gm_t9805qtonbsag t74 = t73;
                                                if (!((((uint8_t *)&t74))[4] != 0)) goto ie68;
                                                uint32_t t75 = GM_LD(uint32_t, ((uint8_t *)&t74));
                                                gm_l13 = t75;
                                                {
                                                    void * t77 = F.l2;
                                                    uint32_t t78 = gm_l13;
                                                    gm_tftb1odc739t5 t79 = gm_f_push_im9d4ro(t77, t78);
                                                    gm_tftb1odc739t5 t80 = t79;
                                                    if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srca29n3fkrpbdt, 303, 58);
                                                    gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                                                }
                                                goto ix68;
ie68:;
ix68:;
                                            }
                                            goto ix63;
ie63:;
ix63:;
                                        }
                                        goto me59;
mn60:;
                                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 301, 17);
me59:;
                                    }
lc48:;
                                    {
                                        uint64_t t83 = gm_l10;
                                        uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                                        uint64_t t86 = r84;
                                        gm_l10 = t86;
                                    }
                                }
lb48:;
                            }
                        }
                        goto me18;
mn42:;
ma21:;
                        goto me18;
mn87:;
ma22:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 298, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 297, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t90 = gm_l4;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l4 = t93;
                }
            }
lb5:;
        }
    }
    {
        void * t94 = gm_l1;
        gm_closure t95 = ((gm_closure){(void *)gm_f_fn_05d3agq, (void *)&F.h, cx});
        gm_f_each_stmt_tt8e8go(t94, t95);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_locals_tltgkrt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_mutated_locals_tltgkrt F;
    void * gm_l1; /* e */
    void * gm_l3; /* p */
    uint32_t gm_l4; /* l */
    gm_tao63pk3r18v5 gm_l5; /* sl */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* b */
    void * gm_l9; /* s */
    GM_STACK_CHECK(&F, 479u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma4; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)24ULL)) goto mn11;
        void * t13 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t13;
        {
            void * t16 = F.l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            void * t19 = gm_l3;
            gm_t9805qtonbsag t20 = gm_f_root_local_40frg7f(t18, t19);
            gm_t9805qtonbsag t21 = t20;
            if (!((((uint8_t *)&t21))[4] != 0)) goto ie15;
            uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&t21));
            gm_l4 = t22;
            {
                void * t24 = F.l2;
                uint32_t t25 = gm_l4;
                gm_tftb1odc739t5 t26 = gm_f_push_im9d4ro(t24, t25);
                gm_tftb1odc739t5 t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srca29n3fkrpbdt, 279, 58);
                gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
            }
            goto ix15;
ie15:;
ix15:;
        }
        goto me3;
mn11:;
ma5:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t30 != ((uint8_t)21ULL)) goto mn29;
        gm_tao63pk3r18v5 t31 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t31;
        {
            void * t34 = F.l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
            void * t37 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
            gm_t9805qtonbsag t38 = gm_f_root_local_40frg7f(t36, t37);
            gm_t9805qtonbsag t39 = t38;
            if (!((((uint8_t *)&t39))[4] != 0)) goto ie33;
            uint32_t t40 = GM_LD(uint32_t, ((uint8_t *)&t39));
            gm_l6 = t40;
            {
                void * t42 = F.l2;
                uint32_t t43 = gm_l6;
                gm_tftb1odc739t5 t44 = gm_f_push_im9d4ro(t42, t43);
                gm_tftb1odc739t5 t45 = t44;
                if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srca29n3fkrpbdt, 280, 64);
                gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
            }
            goto ix33;
ie33:;
ix33:;
        }
        goto me3;
mn29:;
ma6:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)46ULL)) goto mn47;
        void * t49 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t49;
        {
            void * t51 = F.l0;
            void * t52 = gm_l7;
            void * t53 = F.l2;
            gm_b0_1 t54 = gm_f_mutated_in_block_26pupm0((void *)NULL, cx, NULL, t51, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t55;
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto me3;
mn47:;
ma7:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)48ULL)) goto mn57;
        gm_t710lsrq1m8gk t59 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l8 = t59;
        {
            gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
            gm_closure t62 = ((gm_closure){(void *)gm_f_fn_n57683i, (void *)&F.h, cx});
            gm_f_each_cond_hcnk23r(t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t63 = F.l0;
            void * t64 = GM_LD(void *, (((uint8_t *)&F.l8) + 24));
            void * t65 = F.l2;
            gm_b0_1 t66 = gm_f_mutated_in_block_26pupm0((void *)NULL, cx, NULL, t63, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t68 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
            void * t69 = t68;
            if (!(GM_LD(void *, ((uint8_t *)&t69)) != 0)) goto ie67;
            void * t70 = GM_LD(void *, ((uint8_t *)&t69));
            gm_l9 = t70;
            {
                void * t72 = F.l0;
                void * t73 = gm_l9;
                void * t74 = F.l2;
                gm_b0_1 t75 = gm_f_mutated_locals_tltgkrt((void *)NULL, cx, NULL, t72, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix67;
ie67:;
ix67:;
        }
        {
            gm_b0_1 t76;
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto me3;
mn57:;
ma8:;
        goto me3;
mn78:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 278, 5);
me3:;
    }
    {
        void * t80 = gm_l1;
        gm_closure t81 = ((gm_closure){(void *)gm_f_fn_djrq2n1, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t80, t81);
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
extern __attribute__((aligned(4))) uint8_t gm_f_own_place_peh2qe9(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pure_block_94oo6k4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_pure_block_94oo6k4 F;
    void * gm_l0; /* g */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    uint8_t gm_l5; /* ok */
    void * gm_l6; /* place */
    void * gm_l7; /* value */
    gm_b16_8 gm_l8; /* places */
    void * gm_l9; /* value */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l12; /* p */
    void * gm_l13; /* x */
    void * gm_l14; /* init */
    void * gm_l15; /* x */
    void * gm_l16; /* init */
    void * gm_l17; /* x */
    void * gm_l18; /* x */
    void * gm_l19; /* t */
    GM_STACK_CHECK(&F, 753u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
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
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        gm_l5 = ((uint8_t)1);
                    }
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        switch (t25) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma23; case ((uint8_t)5ULL): goto ma23; default: goto ma24; }
ma19:;
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t27 != ((uint8_t)2ULL)) goto mn26;
                        void * t28 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l6 = t28;
                        void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l7 = t29;
                        {
                            uint8_t t31 = 0;
                            uint8_t t32 = 0;
                            void * t33 = gm_l0;
                            void * t34 = gm_l6;
                            uint8_t t35 = gm_f_own_place_peh2qe9(t33, t34);
                            if (t35) {
                                void * t36 = gm_l0;
                                void * t37 = gm_l6;
                                uint8_t t38 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t36, t37);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                t32 = t38;
                            } else t32 = 0;
                            if (t32) {
                                void * t39 = gm_l0;
                                void * t40 = gm_l7;
                                uint8_t t41 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t39, t40);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                t31 = t41;
                            } else t31 = 0;
                            gm_l5 = t31;
                        }
                        goto me18;
mn26:;
ma20:;
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t43 != ((uint8_t)3ULL)) goto mn42;
                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l8 = t44;
                        void * t45 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l9 = t45;
                        {
                            {
                                gm_b16_8 t48 = gm_l8;
                                gm_l10 = t48;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t50 = gm_l11;
                                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t52 = (t50 < t51);
                                    if (!t52) goto lb49;
                                    {
                                        uint64_t t55 = gm_l11;
                                        uint64_t t56 = (uintptr_t)t55;
                                        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        void * t58 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t56 * 8));
                                        void * t59 = t58;
                                        void * t62 = GM_LD(void *, ((uint8_t *)&t59));
                                        gm_l12 = t62;
                                        {
                                            void * t65 = gm_l12;
                                            void * t66 = t65;
                                            if (!(GM_LD(void *, ((uint8_t *)&t66)) != 0)) goto ie64;
                                            void * t67 = GM_LD(void *, ((uint8_t *)&t66));
                                            gm_l13 = t67;
                                            {
                                                uint8_t t70 = 0;
                                                void * t71 = gm_l0;
                                                void * t72 = gm_l13;
                                                uint8_t t73 = gm_f_own_place_peh2qe9(t71, t72);
                                                if (t73) {
                                                    void * t74 = gm_l0;
                                                    void * t75 = gm_l13;
                                                    uint8_t t76 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t74, t75);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll53;
                                                    }
                                                    t70 = t76;
                                                } else t70 = 0;
                                                uint8_t t77 = (uint8_t)!t70;
                                                if (!t77) goto ie69;
                                                {
                                                    gm_l5 = ((uint8_t)0);
                                                }
                                                goto ix69;
ie69:;
ix69:;
                                            }
                                            goto ix64;
ie64:;
ix64:;
                                        }
                                        goto me60;
mn61:;
                                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 213, 17);
me60:;
                                    }
lc49:;
                                    {
                                        uint64_t t80 = gm_l11;
                                        uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                                        uint64_t t83 = r81;
                                        gm_l11 = t83;
                                    }
                                }
gm_ll53:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb49; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc49; }
                                }
                                goto gm_ll9;
lb49:;
                            }
                        }
                        {
                            uint8_t t84 = 0;
                            uint8_t t85 = gm_l5;
                            if (t85) {
                                void * t86 = gm_l0;
                                void * t87 = gm_l9;
                                uint8_t t88 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t86, t87);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                t84 = t88;
                            } else t84 = 0;
                            gm_l5 = t84;
                        }
                        goto me18;
mn42:;
ma21:;
                        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t90 != ((uint8_t)0ULL)) goto mn89;
                        void * t91 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l14 = t91;
                        {
                            void * t94 = gm_l14;
                            void * t95 = t94;
                            if (!(GM_LD(void *, ((uint8_t *)&t95)) != 0)) goto ie93;
                            void * t96 = GM_LD(void *, ((uint8_t *)&t95));
                            gm_l15 = t96;
                            {
                                void * t98 = gm_l0;
                                void * t99 = gm_l15;
                                uint8_t t100 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t98, t99);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                                gm_l5 = t100;
                            }
                            goto ix93;
ie93:;
ix93:;
                        }
                        goto me18;
mn89:;
ma22:;
                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t102 != ((uint8_t)1ULL)) goto mn101;
                        void * t103 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l16 = t103;
                        {
                            void * t105 = gm_l0;
                            void * t106 = gm_l16;
                            uint8_t t107 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t105, t106);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l5 = t107;
                        }
                        goto me18;
mn101:;
ma23:;
                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t111 != ((uint8_t)4ULL)) goto pa110;
                        void * t112 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l17 = t112;
                        goto po109;
pa110:;
                        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t114 != ((uint8_t)5ULL)) goto pa113;
                        void * t115 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l17 = t115;
                        goto po109;
pa113:;
                        goto mn108;
po109:;
                        {
                            void * t117 = gm_l0;
                            void * t118 = gm_l17;
                            uint8_t t119 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t117, t118);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l5 = t119;
                        }
                        goto me18;
mn108:;
ma24:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 210, 9);
me18:;
                    }
                    {
                        uint8_t t121 = gm_l5;
                        uint8_t t122 = (uint8_t)!t121;
                        if (!t122) goto ie120;
                        {
                            uint8_t t124 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix120;
ie120:;
ix120:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 208, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t126 = gm_l3;
                    uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                    uint64_t t129 = r127;
                    gm_l3 = t129;
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
    {
        void * t131 = gm_l1;
        void * t132 = GM_LD(void *, (((uint8_t *)t131) + 16));
        void * t133 = t132;
        if (!(GM_LD(void *, ((uint8_t *)&t133)) != 0)) goto ie130;
        void * t134 = GM_LD(void *, ((uint8_t *)&t133));
        gm_l19 = t134;
        {
            void * t136 = gm_l0;
            void * t137 = gm_l19;
            uint8_t t138 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t139 = 0;
            F.ret = t138;
            goto gm_fret;
        }
        goto ix130;
ie130:;
ix130:;
    }
    {
        uint8_t t140 = 0;
        F.ret = ((uint8_t)1);
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
__attribute__((aligned(4))) uint8_t gm_f_pure_expr_fee5b14(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_pure_expr_fee5b14 F;
    void * gm_l1; /* e */
    void * gm_l3; /* b */
    void * gm_l5; /* s */
    uint32_t gm_l7; /* h */
    gm_tvdg94bk7aa22 gm_l8; /* op */
    GM_STACK_CHECK(&F, 356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)1);
    }
    {
        void * t2 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma4; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma9; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t13 != ((uint8_t)46ULL)) goto mn12;
        void * t14 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t14;
        void * t15 = F.l0;
        void * t16 = gm_l3;
        uint8_t t17 = gm_f_pure_block_94oo6k4((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = 0;
        F.ret = t17;
        goto gm_fret;
        goto me3;
mn12:;
ma5:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)48ULL)) goto mn19;
        gm_t710lsrq1m8gk t21 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l4 = t21;
        {
            F.l2 = ((uint8_t)1);
        }
        {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
            gm_closure t24 = ((gm_closure){(void *)gm_f_fn_url0b1k, (void *)&F.h, cx});
            gm_f_each_cond_hcnk23r(t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t26 = F.l2;
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) goto ie25;
            {
                uint8_t t29 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            void * t31 = F.l0;
            void * t32 = GM_LD(void *, (((uint8_t *)&F.l4) + 24));
            uint8_t t33 = gm_f_pure_block_94oo6k4((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t34 = (uint8_t)!t33;
            if (!t34) goto ie30;
            {
                uint8_t t36 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix30;
ie30:;
ix30:;
        }
        {
            void * t38 = GM_LD(void *, (((uint8_t *)&F.l4) + 32));
            void * t39 = t38;
            if (!(GM_LD(void *, ((uint8_t *)&t39)) != 0)) goto ie37;
            void * t40 = GM_LD(void *, ((uint8_t *)&t39));
            gm_l5 = t40;
            {
                void * t42 = F.l0;
                void * t43 = gm_l5;
                uint8_t t44 = gm_f_pure_expr_fee5b14((void *)NULL, cx, NULL, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            uint8_t t46 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn19:;
ma6:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)34ULL)) goto mn47;
        gm_tsth9qu1dv0g3 t49 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l6 = t49;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
        if (t52 != ((uint8_t)0ULL)) goto mn51;
        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
        gm_l7 = t53;
        {
            void * t55 = F.l0;
            uint32_t t56 = gm_l7;
            uint8_t t57 = gm_f_is_pure_l81kp3k((void *)NULL, cx, NULL, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t57;
        }
        goto me50;
mn51:;
        {
            F.l2 = ((uint8_t)0);
        }
        goto me50;
mn58:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 191, 29);
me50:;
        goto me3;
mn47:;
ma7:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)58ULL)) goto pa62;
        goto po61;
pa62:;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t65 != ((uint8_t)27ULL)) goto pa64;
        goto po61;
pa64:;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t67 != ((uint8_t)28ULL)) goto pa66;
        goto po61;
pa66:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)53ULL)) goto pa68;
        goto po61;
pa68:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t71 != ((uint8_t)54ULL)) goto pa70;
        goto po61;
pa70:;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t73 != ((uint8_t)55ULL)) goto pa72;
        goto po61;
pa72:;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t75 != ((uint8_t)56ULL)) goto pa74;
        goto po61;
pa74:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t77 != ((uint8_t)57ULL)) goto pa76;
        goto po61;
pa76:;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t79 != ((uint8_t)17ULL)) goto pa78;
        goto po61;
pa78:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t81 != ((uint8_t)16ULL)) goto pa80;
        goto po61;
pa80:;
        goto mn60;
po61:;
        {
            F.l2 = ((uint8_t)0);
        }
        goto me3;
mn60:;
ma8:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t84 != ((uint8_t)33ULL)) goto mn83;
        gm_tvdg94bk7aa22 t85 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l8 = t85;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t90 != ((uint8_t)65ULL)) goto pa89;
        goto po88;
pa89:;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t92 != ((uint8_t)66ULL)) goto pa91;
        goto po88;
pa91:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t94 != ((uint8_t)67ULL)) goto pa93;
        goto po88;
pa93:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t96 != ((uint8_t)68ULL)) goto pa95;
        goto po88;
pa95:;
        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t98 != ((uint8_t)71ULL)) goto pa97;
        goto po88;
pa97:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t100 != ((uint8_t)72ULL)) goto pa99;
        goto po88;
pa99:;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t102 != ((uint8_t)73ULL)) goto pa101;
        goto po88;
pa101:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t104 != ((uint8_t)74ULL)) goto pa103;
        goto po88;
pa103:;
        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t106 != ((uint8_t)75ULL)) goto pa105;
        goto po88;
pa105:;
        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t108 != ((uint8_t)76ULL)) goto pa107;
        goto po88;
pa107:;
        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t110 != ((uint8_t)77ULL)) goto pa109;
        goto po88;
pa109:;
        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t112 != ((uint8_t)78ULL)) goto pa111;
        goto po88;
pa111:;
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
        if (t114 != ((uint8_t)79ULL)) goto pa113;
        goto po88;
pa113:;
        goto mn87;
po88:;
        {
            F.l2 = ((uint8_t)0);
        }
        goto me86;
mn87:;
        goto me86;
mn116:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 196, 28);
me86:;
        goto me3;
mn83:;
ma9:;
        goto me3;
mn118:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 181, 5);
me3:;
    }
    {
        uint8_t t121 = F.l2;
        uint8_t t122 = (uint8_t)!t121;
        if (!t122) goto ie120;
        {
            uint8_t t124 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix120;
ie120:;
ix120:;
    }
    {
        void * t125 = gm_l1;
        gm_closure t126 = ((gm_closure){(void *)gm_f_fn_p8hr8ki, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t127 = F.l2;
        uint8_t t128 = 0;
        F.ret = t127;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_read_during_build_ng10fmi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_read_during_build_ng10fmi F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* r */
    gm_b16_8 gm_l3; /* r */
    uint8_t gm_l4; /* any */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* x */
    void * gm_l8; /* def */
    void * gm_l10; /* body */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    uint32_t gm_l14; /* p */
    GM_STACK_CHECK(&F, 722u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 2448);
        uint32_t t5 = gm_l1;
        gm_th2lqbo77pvsc t6;
        gm_f_get_6bqup89((void *)&t6, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l2 = t8;
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
        void * t12 = gm_l0;
        uint32_t t13 = gm_l1;
        gm_b16_8 t14 = gm_f_borrowed_params_b39va3r((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t14;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t16 = gm_l3;
            gm_l5 = t16;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l6;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t24 * 1));
                    uint8_t t27 = t26;
                    uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t27));
                    gm_l7 = t30;
                    {
                        uint8_t t33 = gm_l7;
                        if (!t33) goto ie32;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 147, 5);
me28:;
                }
lc17:;
                {
                    uint64_t t36 = gm_l6;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l6 = t39;
                }
            }
lb17:;
        }
    }
    {
        uint8_t t41 = gm_l4;
        if (!t41) goto ie40;
        {
            void * t43 = gm_l0;
            void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
            uint32_t t45 = gm_l1;
            void * t46 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t46;
        }
        {
            gm_f_list_go2qg0b((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t48 = gm_l8;
            void * t49 = GM_LD(void *, (((uint8_t *)t48) + 152));
            void * t50 = t49;
            if (!(GM_LD(void *, ((uint8_t *)&t50)) != 0)) goto ie47;
            void * t51 = GM_LD(void *, ((uint8_t *)&t50));
            gm_l10 = t51;
            {
                void * t53 = gm_l10;
                void * t54 = (void *)((uint8_t *)&F.l9);
                gm_b0_1 t55 = gm_f_locals_in_aggregates_3jqq5eh((void *)NULL, cx, NULL, t53, ((uint8_t)0), t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix47;
ie47:;
ix47:;
        }
        {
            {
                void * t57 = gm_l8;
                gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t57) + 32));
                gm_l11 = t58;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t60 = gm_l12;
                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t62 = (t60 < t61);
                    if (!t62) goto lb59;
                    {
                        uint64_t t65 = gm_l12;
                        uint64_t t66 = gm_l12;
                        uint64_t t67 = (uintptr_t)t66;
                        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        uint32_t t69 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t67 * 4));
                        gm_tn68el8d14vku t70;
                        GM_ST(uint32_t, (uint8_t *)&t70 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t70 + 0), t65);
                        GM_ST(uint32_t, ((uint8_t *)&t70 + 8), t69);
                        gm_tn68el8d14vku t71 = t70;
                        uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&t71) + 0));
                        gm_l13 = t74;
                        uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&t71) + 8));
                        gm_l14 = t75;
                        {
                            uint64_t t78 = gm_l13;
                            uint64_t t79 = (uintptr_t)t78;
                            uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_srca29n3fkrpbdt, 155, 16);
                            uint8_t t81 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t79 * 1));
                            if (!t81) goto ie77;
                            uint32_t t82 = gm_l14;
                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                            gm_b16_8 t84 = t83;
                            uint8_t t85 = 0;
                            uint32_t t86 = t82;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t84) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t84))) + i * 4, ((uint8_t *)&t86))) { t85 = 1; break; }
                            uint8_t t87 = (uint8_t)(!t85);
                            if (!t87) goto ie77;
                            {
                                uint64_t t89 = gm_l13;
                                uint64_t t90 = (uintptr_t)t89;
                                uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t90 >= (uintptr_t)(t91)) gm_trap_index(t90, t91, gm_srca29n3fkrpbdt, 155, 47);
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t90 * 1), ((uint8_t)0));
                            }
                            goto ix77;
ie77:;
ix77:;
                        }
                        goto me72;
mn73:;
                        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 154, 9);
me72:;
                    }
lc59:;
                    {
                        uint64_t t93 = gm_l12;
                        uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                        uint64_t t96 = r94;
                        gm_l12 = t96;
                    }
                }
lb59:;
            }
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        void * t97 = gm_l0;
        void * t98 = (void *)(((uint8_t *)t97) + 2448);
        uint32_t t99 = gm_l1;
        gm_b16_8 t100 = gm_l3;
        gm_tftb1odc739t5 t101 = gm_f_put_p2039fp(t98, t99, t100);
        gm_tftb1odc739t5 t102 = t101;
        if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_srca29n3fkrpbdt, 158, 5);
        gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
    }
    {
        gm_b16_8 t104 = gm_l3;
        uint8_t t105 = 0;
        F.ret = t104;
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
inline __attribute__((aligned(4))) uint8_t gm_f_steady_arg_7904qej(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_steady_arg_7904qej F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* caller */
    void * gm_l2; /* a */
    gm_b16_8 gm_l3; /* written */
    void * gm_l4; /* captured */
    uint32_t gm_l5; /* l */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l2;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t7 != ((uint8_t)35ULL)) goto pa6;
        goto po5;
pa6:;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t9 != ((uint8_t)36ULL)) goto pa8;
        goto po5;
pa8:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)34ULL)) goto pa10;
        goto po5;
pa10:;
        goto mn4;
po5:;
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn4:;
        goto me3;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 127, 5);
me3:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = gm_l2;
        gm_t9805qtonbsag t20 = gm_f_root_local_40frg7f(t18, t19);
        gm_t9805qtonbsag t21 = t20;
        if (!((((uint8_t *)&t21))[4] != 0)) goto ie15;
        uint32_t t22 = GM_LD(uint32_t, ((uint8_t *)&t21));
        gm_l5 = t22;
        {
            uint8_t t24 = 0;
            uint32_t t25 = gm_l5;
            gm_b16_8 t26 = gm_l3;
            gm_b16_8 t27 = t26;
            uint8_t t28 = 0;
            uint32_t t29 = t25;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t27) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t27))) + i * 4, ((uint8_t *)&t29))) { t28 = 1; break; }
            uint8_t t30 = (uint8_t)(!t28);
            if (t30) {
                void * t31 = gm_l4;
                uint32_t t32 = gm_l1;
                uint32_t t33 = gm_l5;
                gm_tcc1rq4n1avb2 t34;
                GM_ST(uint32_t, ((uint8_t *)&t34 + 0), t32);
                GM_ST(uint32_t, ((uint8_t *)&t34 + 4), t33);
                gm_tqs5p4n2tq19v t35 = gm_f_get_ag7vhtq(t31, t34);
                gm_tqs5p4n2tq19v t36 = t35;
                uint8_t t37;
                if (((((uint8_t *)&t36))[1] != 0)) goto mn39;
                t37 = ((uint8_t)1);
                goto me38;
mn39:;
                t37 = ((uint8_t)0);
                goto me38;
mn40:;
                gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 135, 66);
me38:;
                t24 = t37;
            } else t24 = 0;
            uint8_t t41 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t42 = gm_l0;
        void * t43 = gm_l2;
        uint8_t t44 = gm_f_is_const_arg_98lfonj((void *)NULL, cx, NULL, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t45 = 0;
        F.ret = t44;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_steady_params_ng2sd7r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_steady_params_ng2sd7r F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* x */
    GM_STACK_CHECK(&F, 460u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4;
        if ((((((uint8_t *)t3) + 2504))[40] != 0)) goto mn6;
        t4 = ((uint8_t)1);
        goto me5;
mn6:;
        t4 = ((uint8_t)0);
        goto me5;
mn7:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 53, 8);
me5:;
        if (!t4) goto ie2;
        {
            void * t9 = gm_l0;
            gm_f_compute_steady_c2v2ffl((void *)&F.l2, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t10 = gm_l0;
            gm_trqfvmjtt4sij t11 = F.l2;
            gm_tqetrgo0q24b5 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 44, 0);
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[40] = 1;
            GM_ST(gm_tqetrgo0q24b5, (((uint8_t *)t10) + 2504), t12);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        gm_tqetrgo0q24b5 t14 = GM_LD(gm_tqetrgo0q24b5, (((uint8_t *)t13) + 2504));
        gm_tqetrgo0q24b5 t15 = t14;
        if (!((((uint8_t *)&t15))[40] != 0)) gm_trap("unwrap of null", gm_srca29n3fkrpbdt, 57, 12);
        gm_trqfvmjtt4sij t16 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t15));
        F.l3 = t16;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint32_t t18 = gm_l1;
            uint64_t t19 = (uint64_t)t18;
            uint64_t t20 = (uintptr_t)t19;
            uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srca29n3fkrpbdt, 60, 14);
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t20 * 16));
            gm_l5 = t22;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t24 = gm_l6;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t26 = (t24 < t25);
                if (!t26) goto lb23;
                {
                    uint64_t t29 = gm_l6;
                    uint64_t t30 = (uintptr_t)t29;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t30 * 1));
                    uint8_t t33 = t32;
                    uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t33));
                    gm_l7 = t36;
                    {
                        void * t38 = (void *)((uint8_t *)&F.l4);
                        uint8_t t39 = gm_l7;
                        gm_tftb1odc739t5 t40 = gm_f_push_vd1290p(t38, t39);
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srca29n3fkrpbdt, 60, 36);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 60, 5);
me34:;
                }
lc23:;
                {
                    uint64_t t44 = gm_l6;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l6 = t47;
                }
            }
lb23:;
        }
    }
    {
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        uint8_t t49 = 0;
        F.ret = t48;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_value_fn_refs_jv5jhbq(void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_value_fn_refs_jv5jhbq F;
    gm_b16_8 gm_l1; /* out */
    void * gm_l2; /* seen */
    uint32_t gm_l3; /* f */
    void * gm_l4; /* a */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tkaapcii86udk gm_l8; /* pl */
    gm_tkaapcii86udk gm_l9; /* start */
    void * gm_l10; /* x */
    void * gm_l11; /* x */
    void * gm_l12; /* x */
    void * gm_l13; /* payload */
    void * gm_l14; /* x */
    GM_STACK_CHECK(&F, 582u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma3; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; default: goto ma9; }
ma3:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t13 != ((uint8_t)11ULL)) goto mn12;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&F.l0) + 16));
        gm_l3 = t14;
        {
            uint32_t t16 = gm_l3;
            uint64_t t17 = (uint64_t)t16;
            uint64_t t18 = (uintptr_t)t17;
            uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srca29n3fkrpbdt, 327, 21);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 1), ((uint8_t)1));
        }
        goto me2;
mn12:;
ma4:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t21 != ((uint8_t)3ULL)) goto mn20;
        void * t22 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l4 = t22;
        {
            void * t24 = gm_l4;
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 0));
            gm_l5 = t25;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t27 = gm_l6;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t29 = (t27 < t28);
                if (!t29) goto lb26;
                {
                    uint64_t t32 = gm_l6;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t6evimt84lndq t37 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t33 * 48));
                    F.l7 = t37;
                    {
                        gm_b16_8 t39 = gm_l1;
                        void * t40 = gm_l2;
                        gm_b0_1 t41 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l7)), t39, t40);
                        if (gm_unw.kind) {
                            goto gm_ll30;
                        }
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 328, 20);
me35:;
                }
lc26:;
                {
                    uint64_t t43 = gm_l6;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l6 = t46;
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
        goto me2;
mn20:;
ma5:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t48 != ((uint8_t)4ULL)) goto mn47;
        gm_tkaapcii86udk t49 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l0) + 16));
        gm_l8 = t49;
        void * t50 = GM_LD(void *, (((uint8_t *)&gm_l8) + 0));
        gm_b16_8 t51 = gm_l1;
        void * t52 = gm_l2;
        gm_b0_1 t53 = gm_f_cell_fn_refs_6icb85l((void *)NULL, cx, NULL, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn47:;
ma6:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t55 != ((uint8_t)5ULL)) goto mn54;
        gm_tkaapcii86udk t56 = GM_LD(gm_tkaapcii86udk, ((((uint8_t *)&F.l0) + 16) + 0));
        gm_l9 = t56;
        void * t57 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
        gm_b16_8 t58 = gm_l1;
        void * t59 = gm_l2;
        gm_b0_1 t60 = gm_f_cell_fn_refs_6icb85l((void *)NULL, cx, NULL, t57, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn54:;
ma7:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t64 != ((uint8_t)7ULL)) goto pa63;
        void * t65 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l10 = t65;
        goto po62;
pa63:;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t67 != ((uint8_t)8ULL)) goto pa66;
        void * t68 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l10 = t68;
        goto po62;
pa66:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t70 != ((uint8_t)9ULL)) goto pa69;
        void * t71 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l10 = t71;
        goto po62;
pa69:;
        goto mn61;
po62:;
        void * t72 = gm_l10;
        gm_b16_8 t73 = gm_l1;
        void * t74 = gm_l2;
        gm_b0_1 t75 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t72)), t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn61:;
ma8:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t77 != ((uint8_t)10ULL)) goto mn76;
        void * t78 = GM_LD(void *, ((((uint8_t *)&F.l0) + 16) + 8));
        gm_l13 = t78;
        void * t80 = gm_l13;
        void * t81 = t80;
        if (!(GM_LD(void *, ((uint8_t *)&t81)) != 0)) goto ie79;
        void * t82 = GM_LD(void *, ((uint8_t *)&t81));
        gm_l14 = t82;
        {
            void * t84 = gm_l14;
            gm_b16_8 t85 = gm_l1;
            void * t86 = gm_l2;
            gm_b0_1 t87 = gm_f_value_fn_refs_jv5jhbq((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t84)), t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix79;
ie79:;
ix79:;
        goto me2;
mn76:;
ma9:;
        goto me2;
mn88:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srca29n3fkrpbdt, 326, 5);
me2:;
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

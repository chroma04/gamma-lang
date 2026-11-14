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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
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
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tdu0qimd4ae4c;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint64_t f0; gm_t08olq97rscn3 f1; } gm_t4u0l4lf5utrg;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdu0qimd4ae4c) == 32 && _Alignof(gm_tdu0qimd4ae4c) == 8 && offsetof(gm_tdu0qimd4ae4c, f0) == 0 && offsetof(gm_tdu0qimd4ae4c, f1) == 16 && offsetof(gm_tdu0qimd4ae4c, f2) == 20, "layout of `PatBind`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4u0l4lf5utrg) == 40 && _Alignof(gm_t4u0l4lf5utrg) == 8 && offsetof(gm_t4u0l4lf5utrg, f0) == 0 && offsetof(gm_t4u0l4lf5utrg, f1) == 8, "layout of `(i: usize, x: Ident)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_b16_8 f0; gm_t930q5l38hdap f1; } gm_tmhicb6huo2ra;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tmhicb6huo2ra) == 224 && _Alignof(gm_tmhicb6huo2ra) == 8 && offsetof(gm_tmhicb6huo2ra, f0) == 0 && offsetof(gm_tmhicb6huo2ra, f1) == 16, "layout of `Binding`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
struct gm_fr_binding_igkhmn1 {
    gm_frame h;
    gm_t5vbkdq81qfs7 l7; /* r */
    gm_t0ialelq3ql1k l8; /* v */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_closed_const_0aif03i {
    gm_frame h;
    void * l0; /* s */
    uint8_t l2; /* ok */
    uint8_t ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_destructure_2jra5iu {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* elems */
    gm_trqfvmjtt4sij l14; /* locals */
    gm_trqfvmjtt4sij l15; /* declared */
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_90pnsb9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hh33a3d {
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
struct gm_fr_fn_6339gg7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_eno8frt {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_b0_1 ret;
};
struct gm_fr_fn_meq413i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_is_construction_dbq4h7o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rb5mlsd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_sh8m0m0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_adt_binding_j8q6s4a {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_local_is_type_gakgl1b {
    gm_frame h;
    gm_tv1o1j44ii4db l5; /* el */
    uint8_t ret;
};
struct gm_fr_local_type_init_6it5555 {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
    gm_t930q5l38hdap l6; /* b */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_nominal_lit_aiq6n9c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
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
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsettled_pmdpogr {
    gm_frame h;
    void * l0; /* cx */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_visit_e3tpkkh {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
__attribute__((aligned(4))) void gm_f_binding_igkhmn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_closed_const_0aif03i(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_local_adt_6j4pdai(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_destructure_2jra5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_90pnsb9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_6339gg7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_eno8frt(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_7kp9jno(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_9i45r57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gb7r1is(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hm2bsgr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q32bjcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_handler_lit_u1lbff2(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_construction_dbq4h7o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_sh8m0m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_adt_binding_j8q6s4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_local_const_b6dhg2g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) uint8_t gm_f_local_is_type_gakgl1b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_local_type_init_6it5555(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_type_rjdgib9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_nested_fn_binding_0cl95ol(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_nominal_lit_aiq6n9c(void * a0);
__attribute__((aligned(4))) void * gm_f_pending_handler_literal_3hqdgg4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hign4kd(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nr0n00f(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r4haklv(void * a0, gm_t9805qtonbsag a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_unsettled_pmdpogr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_sd7vrpqmmkbm0[111];
extern const uint8_t gm_se5bopd2dbamm[30];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_shqseub127akh[47];
extern const uint8_t gm_sj3fn9etjoihf[116];
extern const uint8_t gm_sjsokj7ll1qcv[76];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcm8m4ndqum1bp[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_svrjcmg2c5dio[78];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_closed_const_0aif03i(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closed_const_0aif03i F;
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 44u);
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
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_6339gg7, (void *)&F.h, cx});
        gm_f_visit_e3tpkkh(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src6lm8esmv16p1, 176, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
        void * t11 = gm_l3;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_l2;
        uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
        uint8_t t15 = 0;
        F.ret = t14;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_90pnsb9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_90pnsb9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hh33a3d F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_meq413i F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_visit_e3tpkkh *)((&F.h)->up))->l1));
        gm_f_visit_e3tpkkh(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
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
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_hoists_13k2a2s(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        void * t10 = gm_f_prefix_operand_3n5jchh(t9);
        void * t11 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t8, t10);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (!t6) {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        t1 = t10;
    } else t1 = 1;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void gm_f_list_rb5mlsd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rb5mlsd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hh33a3d();
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
inline __attribute__((aligned(4))) void gm_f_list_sh8m0m0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_sh8m0m0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_90pnsb9();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        gm_tep2drsl9408b t4;
        memset((uint8_t *)&t4 + 20, 0, 4);
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_t88rcucg1b7uk t6 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_hv483nm(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcsffver18si54, 63, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
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
    gm_t88rcucg1b7uk t3 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_unsettled_pmdpogr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_unsettled_pmdpogr F;
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l1;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_eno8frt, (void *)&F.h, cx});
        gm_f_visit_e3tpkkh(t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma3; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    t1 = ((uint8_t)1);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_same_package_vmps24r(t12, t13, t14);
    t1 = t15;
    goto me2;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l3;
    uint8_t t20 = ((t18 == t19));
    t1 = t20;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 168, 103);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visit_e3tpkkh F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        gm_closure t2 = F.l1;
        void * t3 = gm_l0;
        gm_closure t4 = t2;
        gm_node *ccx5 = t4.cx;
        void * t6 = t4.code;
        gm_b0_1 t7;
        if (GM_PTR_TAGGED(t6)) {
            ((void (*)(void *))GM_PTR_UNTAG(t6))(t3);
        } else {
            t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t6)(t4.env, ccx5, NULL, t3);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = gm_l0;
        gm_closure t9 = ((gm_closure){(void *)gm_f_fn_meq413i, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t8, t9);
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
__attribute__((aligned(4))) void gm_f_binding_igkhmn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_binding_igkhmn1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* lit */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_tkn42u7na3t81 gm_l5; /* annot */
    void * gm_l6; /* t */
    uint8_t gm_l9; /* mutable */
    uint64_t gm_l10; /* errors_before */
    gm_t88rcucg1b7uk gm_l11; /* t */
    void * gm_l12; /* lit */
    void * gm_l13; /* ex */
    gm_t88rcucg1b7uk gm_l14; /* t */
    void * gm_l15; /* init */
    uint32_t gm_l16; /* id */
    gm_t88rcucg1b7uk gm_l17; /* ty */
    uint32_t gm_l18; /* id */
    GM_STACK_CHECK(&F, 4380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 8) + 8);
        uint8_t t5 = (!(t4 == ((uint64_t)1ULL)));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = gm_l1;
            gm_f_destructure_2jra5iu((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t9 = (uintptr_t)((uint64_t)0ULL);
        void * t10 = gm_l1;
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 8) + 8);
        if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcm8m4ndqum1bp, 5, 13);
        gm_t08olq97rscn3 t12 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 8)) + t9 * 32));
        gm_l2 = t12;
    }
    {
        void * t14 = gm_l1;
        uint8_t t15 = ((GM_LD(uint8_t, (((uint8_t *)t14) + 32) + 0) == ((uint8_t)0ULL)));
        if (!t15) goto ie13;
        void * t16 = gm_l1;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 24));
        void * t18 = t17;
        uint8_t t19;
        if ((GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto mn21;
        t19 = ((uint8_t)1);
        goto me20;
mn21:;
        t19 = ((uint8_t)0);
        goto me20;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 6, 29);
me20:;
        if (!t19) goto ie13;
        {
            void * t25 = gm_l1;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 40));
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            if (t27 != ((uint8_t)36ULL)) goto ie24;
            void * t28 = GM_LD(void *, ((((uint8_t *)t26) + 8) + 8));
            gm_l3 = t28;
            {
                void * t30 = gm_l0;
                void * t31 = gm_l3;
                void * t32 = gm_l1;
                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 48));
                gm_f_nested_fn_binding_0cl95ol((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t30, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t31, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix24;
ie24:;
ix24:;
        }
        {
            void * t35 = gm_l1;
            void * t36 = GM_LD(void *, (((uint8_t *)t35) + 40));
            uint8_t t37 = gm_f_nominal_lit_aiq6n9c(t36);
            if (!t37) goto ie34;
            {
                void * t39 = gm_l0;
                void * t40 = gm_l1;
                gm_f_local_adt_binding_j8q6s4a((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t39, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            void * t42 = gm_l0;
            void * t43 = gm_l1;
            void * t44 = GM_LD(void *, (((uint8_t *)t43) + 40));
            gm_tkn42u7na3t81 t45 = gm_f_local_type_init_6it5555((void *)NULL, cx, NULL, t42, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t46 = t45;
            if (!((((uint8_t *)&t46))[4] != 0)) goto ie41;
            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t46));
            gm_l4 = t47;
            {
                void * t49 = gm_l0;
                gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_t88rcucg1b7uk t51 = gm_l4;
                gm_t930q5l38hdap t52;
                memset(&t52, 0, sizeof t52);
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)5ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t52 + 8), t51);
                gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                gm_b0_1 t54 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t49, t50, &t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t55 = gm_l0;
                void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_t88rcucg1b7uk t58 = gm_l4;
                gm_b0_1 t59 = gm_f_push_type_alias_rr9mg3i((void *)NULL, cx, NULL, t56, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t60 = gm_l0;
                void * t61 = GM_LD(void *, (((uint8_t *)t60) + 0));
                void * t62 = gm_l1;
                gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 48));
                void * t64 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t61, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trcmmd12cefb1 t65;
                GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t65 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t65 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t65 + 8), t64);
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            goto ix41;
ie41:;
ix41:;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_tkn42u7na3t81 t67;
        memset(&t67, 0, sizeof t67);
        gm_l5 = t67;
    }
    {
        void * t69 = gm_l1;
        void * t70 = GM_LD(void *, (((uint8_t *)t69) + 24));
        void * t71 = t70;
        if (!(GM_LD(void *, ((uint8_t *)&t71)) != 0)) goto ie68;
        void * t72 = GM_LD(void *, ((uint8_t *)&t71));
        gm_l6 = t72;
        {
            void * t74 = gm_l0;
            void * t75 = gm_l6;
            gm_t88rcucg1b7uk t76 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t77), t76);
            ((uint8_t *)&t77)[4] = 1;
            gm_l5 = t77;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        void * t79 = gm_l1;
        uint8_t t80 = ((GM_LD(uint8_t, (((uint8_t *)t79) + 32) + 0) == ((uint8_t)0ULL)));
        if (!t80) goto ie78;
        uint8_t t81;
        if (((((uint8_t *)&gm_l5))[4] != 0)) goto mn83;
        t81 = ((uint8_t)1);
        goto me82;
mn83:;
        t81 = ((uint8_t)0);
        goto me82;
mn84:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 20, 29);
me82:;
        if (!t81) goto ie78;
        {
            void * t87 = gm_l0;
            void * t88 = gm_l1;
            void * t89 = GM_LD(void *, (((uint8_t *)t88) + 40));
            gm_tkv1vap90vht2 t90;
            gm_f_try_untyped_8j8up35((void *)&t90, (void *)NULL, cx, NULL, t87, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t91 = t90;
            if (!((((uint8_t *)&t91))[208] != 0)) goto ie86;
            gm_t5vbkdq81qfs7 t92 = GM_LD(gm_t5vbkdq81qfs7, ((uint8_t *)&t91));
            F.l7 = t92;
            {
                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                if (t95 != ((uint8_t)0ULL)) goto ie94;
                gm_t0ialelq3ql1k t96 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l7) + 8));
                F.l8 = t96;
                {
                    void * t98 = gm_l0;
                    gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                    gm_t0ialelq3ql1k t100 = F.l8;
                    gm_t930q5l38hdap t101;
                    GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t101 + 8), t100);
                    gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                    gm_b0_1 t103 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t98, t99, &t101, t102);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix94;
ie94:;
ix94:;
            }
            {
                void * t104 = gm_l0;
                void * t105 = GM_LD(void *, (((uint8_t *)t104) + 0));
                void * t106 = gm_l1;
                gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 48));
                void * t108 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t105, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trcmmd12cefb1 t109;
                GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t109 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t109 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t109 + 8), t108);
                uint8_t t110 = 0;
                F.ret = t109;
                goto gm_fret;
            }
            goto ix86;
ie86:;
ix86:;
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        void * t111 = gm_l1;
        uint8_t t112 = ((GM_LD(uint8_t, (((uint8_t *)t111) + 32) + 0) == ((uint8_t)1ULL)));
        gm_l9 = t112;
    }
    {
        void * t113 = gm_l0;
        void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
        void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
        void * t116 = GM_LD(void *, (((uint8_t *)t115) + 8));
        uint64_t t117 = GM_LD(uintptr_t, ((((uint8_t *)t116) + 0) + 0) + 8);
        gm_l10 = t117;
    }
    {
        void * t118;
        void * t120 = gm_l1;
        void * t121 = GM_LD(void *, (((uint8_t *)t120) + 40));
        uint8_t t122 = ((GM_LD(uint8_t, (((uint8_t *)t121) + 8) + 0) == ((uint8_t)5ULL)));
        if (!t122) goto ie119;
        {
            uint8_t t125 = gm_l9;
            uint8_t t126 = (uint8_t)!t125;
            if (!t126) goto ie124;
            {
                void * t128 = gm_l0;
                void * t129 = gm_l1;
                void * t130 = GM_LD(void *, (((uint8_t *)t129) + 40));
                gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t130) + 72));
                gm_b16_8 t132;
                GM_ST(void *, (uint8_t *)&t132, (void *)(gm_svrjcmg2c5dio));
                GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(77));
                gm_b0_1 t133 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t128, t131, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix124;
ie124:;
ix124:;
        }
        void * t134;
        if (!((((uint8_t *)&gm_l5))[4] != 0)) goto ie135;
        gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
        gm_l11 = t136;
        {
            void * t139 = gm_l0;
            gm_t88rcucg1b7uk t140 = gm_l11;
            uint8_t t141 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t142 = (uint8_t)!t141;
            if (!t142) goto ie138;
            {
                void * t144 = gm_l0;
                void * t145 = gm_l1;
                void * t146 = GM_LD(void *, (((uint8_t *)t145) + 40));
                gm_tl874t2atnlh3 t147 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t146) + 72));
                void * t148 = gm_l0;
                void * t149 = GM_LD(void *, (((uint8_t *)t148) + 0));
                gm_t88rcucg1b7uk t150 = gm_l11;
                gm_b16_8 t151 = gm_f_uninit_message_u2c3peu((void *)NULL, cx, NULL, t149, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t152 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t144, t147, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix138;
ie138:;
ix138:;
        }
        gm_t88rcucg1b7uk t153 = gm_l11;
        gm_teti411eujvf4 t154;
        memset(&t154, 0, sizeof t154);
        GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)8ULL));
        void * t155 = gm_l1;
        void * t156 = GM_LD(void *, (((uint8_t *)t155) + 40));
        gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 72));
        void * t158 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t153, &t154, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t159 = t158;
        t134 = t159;
        goto ix135;
ie135:;
        {
            void * t161 = gm_l0;
            void * t162 = gm_l1;
            void * t163 = GM_LD(void *, (((uint8_t *)t162) + 40));
            gm_tl874t2atnlh3 t164 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t163) + 72));
            gm_b16_8 t165;
            GM_ST(void *, (uint8_t *)&t165, (void *)(gm_se5bopd2dbamm));
            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(29));
            gm_b0_1 t166 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t161, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t167 = gm_l0;
        void * t168 = GM_LD(void *, (((uint8_t *)t167) + 0));
        void * t169 = gm_l1;
        void * t170 = GM_LD(void *, (((uint8_t *)t169) + 40));
        gm_tl874t2atnlh3 t171 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t170) + 72));
        void * t172 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t168, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t173 = t172;
        t134 = t173;
ix135:;
        void * t174 = t134;
        t118 = t174;
        goto ix119;
ie119:;
        void * t175;
        void * t177 = gm_l1;
        void * t178 = GM_LD(void *, (((uint8_t *)t177) + 40));
        void * t179 = gm_f_handler_lit_u1lbff2(t178);
        void * t180 = t179;
        if (!(GM_LD(void *, ((uint8_t *)&t180)) != 0)) goto ie176;
        void * t181 = GM_LD(void *, ((uint8_t *)&t180));
        gm_l12 = t181;
        void * t182 = gm_l1;
        uint8_t t183 = ((GM_LD(uint8_t, (((uint8_t *)t182) + 32) + 0) == ((uint8_t)0ULL)));
        if (!t183) goto ie176;
        uint8_t t184;
        if (((((uint8_t *)&gm_l5))[4] != 0)) goto mn186;
        t184 = ((uint8_t)1);
        goto me185;
mn186:;
        t184 = ((uint8_t)0);
        goto me185;
mn187:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 37, 67);
me185:;
        if (!t184) goto ie176;
        void * t188 = gm_l0;
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)t188) + 520));
        uint8_t t190 = (uint8_t)!t189;
        if (!t190) goto ie176;
        void * t192 = gm_l0;
        void * t193 = gm_l12;
        void * t194 = gm_l1;
        void * t195 = GM_LD(void *, (((uint8_t *)t194) + 40));
        gm_tl874t2atnlh3 t196 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t195) + 72));
        void * t197 = gm_f_pending_handler_literal_3hqdgg4((void *)NULL, cx, NULL, t192, t193, t196);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t198 = t197;
        t175 = t198;
        goto ix176;
ie176:;
        {
            void * t200 = gm_l0;
            void * t201 = gm_l1;
            void * t202 = GM_LD(void *, (((uint8_t *)t201) + 40));
            gm_tkn42u7na3t81 t203 = gm_l5;
            void * t204 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t200, t202, t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t204;
        }
        void * t205;
        if (!((((uint8_t *)&gm_l5))[4] != 0)) goto ie206;
        gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
        gm_l14 = t207;
        void * t209 = gm_l0;
        void * t210 = gm_l13;
        gm_t88rcucg1b7uk t211 = gm_l14;
        void * t212 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t209, t210, t211);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t213 = t212;
        t205 = t213;
        goto ix206;
ie206:;
        void * t215 = gm_l13;
        void * t216 = t215;
        t205 = t216;
ix206:;
        void * t217 = t205;
        t175 = t217;
ix176:;
        t118 = t175;
ix119:;
        gm_l15 = t118;
    }
    {
        void * t219 = gm_l1;
        uint8_t t220 = ((GM_LD(uint8_t, (((uint8_t *)t219) + 32) + 0) == ((uint8_t)0ULL)));
        if (!t220) goto ie218;
        uint64_t t221 = gm_l10;
        void * t222 = gm_l0;
        void * t223 = GM_LD(void *, (((uint8_t *)t222) + 0));
        void * t224 = GM_LD(void *, (((uint8_t *)t223) + 0));
        void * t225 = GM_LD(void *, (((uint8_t *)t224) + 8));
        uint64_t t226 = GM_LD(uintptr_t, ((((uint8_t *)t225) + 0) + 0) + 8);
        uint8_t t227 = ((t221 == t226));
        if (!t227) goto ie218;
        void * t228 = gm_l1;
        void * t229 = GM_LD(void *, (((uint8_t *)t228) + 40));
        uint8_t t230 = (!(GM_LD(uint8_t, (((uint8_t *)t229) + 8) + 0) == ((uint8_t)5ULL)));
        if (!t230) goto ie218;
        void * t231 = gm_l0;
        void * t232 = GM_LD(void *, (((uint8_t *)t231) + 0));
        void * t233 = gm_l15;
        uint8_t t234 = gm_f_closed_const_0aif03i(t232, t233);
        if (!t234) goto ie218;
        void * t235 = gm_l0;
        void * t236 = gm_l15;
        uint8_t t237 = gm_f_unsettled_pmdpogr((void *)NULL, cx, NULL, t235, t236);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t238 = (uint8_t)!t237;
        if (!t238) goto ie218;
        {
            void * t241 = gm_l0;
            void * t242 = GM_LD(void *, (((uint8_t *)t241) + 0));
            void * t243 = gm_l0;
            uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)t243) + 8));
            void * t245 = gm_l1;
            void * t246 = GM_LD(void *, (((uint8_t *)t245) + 40));
            gm_tkn42u7na3t81 t247 = gm_l5;
            gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tl874t2atnlh3 t249 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
            gm_t9805qtonbsag t250 = gm_f_local_const_b6dhg2g((void *)NULL, cx, NULL, t242, t244, t246, t247, t248, t249);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t251 = t250;
            if (!((((uint8_t *)&t251))[4] != 0)) goto ie240;
            uint32_t t252 = GM_LD(uint32_t, ((uint8_t *)&t251));
            gm_l16 = t252;
            {
                void * t254 = gm_l0;
                gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_ts78ca2amthgu t256;
                memset((uint8_t *)&t256 + 4, 0, 4);
                memset((uint8_t *)&t256 + 36, 0, 4);
                uint32_t t257 = gm_l16;
                uint64_t t258 = (uint64_t)t257;
                uint64_t t259 = (uintptr_t)t258;
                void * t260 = gm_l0;
                void * t261 = GM_LD(void *, (((uint8_t *)t260) + 0));
                uint64_t t262 = GM_LD(uintptr_t, ((((uint8_t *)t261) + 1968) + 0) + 8);
                if (t259 >= (uintptr_t)(t262)) gm_trap_index(t259, t262, gm_srcm8m4ndqum1bp, 47, 46);
                gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t261) + 1968) + 0)) + t259 * 88) + 28));
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t256 + 0, t263);
                gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t256 + 8, t264);
                gm_tl874t2atnlh3 t265 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t256 + 24, t265);
                gm_t930q5l38hdap t266;
                memset(&t266, 0, sizeof t266);
                GM_ST(uint8_t, ((uint8_t *)&t266 + 0), ((uint8_t)4ULL));
                GM_ST(gm_ts78ca2amthgu, ((uint8_t *)&t266 + 8), t256);
                gm_tl874t2atnlh3 t267 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
                gm_b0_1 t268 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t254, t255, &t266, t267);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t269 = gm_l0;
                void * t270 = GM_LD(void *, (((uint8_t *)t269) + 0));
                void * t271 = gm_l1;
                gm_tl874t2atnlh3 t272 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t271) + 48));
                void * t273 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t270, t272);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trcmmd12cefb1 t274;
                GM_ST(uint8_t, (uint8_t *)&t274 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t274 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t274 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t274 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t274 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t274 + 8), t273);
                uint8_t t275 = 0;
                F.ret = t274;
                goto gm_fret;
            }
            goto ix240;
ie240:;
ix240:;
        }
        goto ix218;
ie218:;
ix218:;
    }
    {
        gm_tkn42u7na3t81 t276 = gm_l5;
        gm_tkn42u7na3t81 t277 = t276;
        gm_t88rcucg1b7uk t278;
        if (((((uint8_t *)&t277))[4] != 0)) {
            gm_t88rcucg1b7uk t279 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t277) + 0));
            t278 = t279;
        } else {
            void * t280 = gm_l15;
            gm_t88rcucg1b7uk t281 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t280) + 0));
            t278 = t281;
        }
        gm_l17 = t278;
    }
    {
        void * t283 = gm_l0;
        void * t284 = GM_LD(void *, (((uint8_t *)t283) + 0));
        void * t285 = (void *)(((uint8_t *)t284) + 8);
        gm_t88rcucg1b7uk t286 = gm_l17;
        gm_t53at80pfdus3 t287;
        gm_f_kind_jcnt4hm((void *)&t287, t285, GM_CONV(gm_csguankko4lmhd, t286));
        gm_t53at80pfdus3 t288 = t287;
        uint8_t t289 = ((GM_LD(uint8_t, ((uint8_t *)&t288) + 0) == ((uint8_t)6ULL)));
        if (!t289) goto ie282;
        {
            void * t291 = gm_l0;
            void * t292 = gm_l1;
            void * t293 = GM_LD(void *, (((uint8_t *)t292) + 40));
            gm_tl874t2atnlh3 t294 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t293) + 72));
            gm_b16_8 t295;
            GM_ST(void *, (uint8_t *)&t295, (void *)(gm_sjsokj7ll1qcv));
            GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(75));
            gm_b0_1 t296 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t291, t294, t295);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix282;
ie282:;
ix282:;
    }
    {
        uint8_t t298 = gm_l9;
        if (!t298) goto ie297;
        void * t299 = gm_l0;
        void * t300 = GM_LD(void *, (((uint8_t *)t299) + 0));
        void * t301 = (void *)(((uint8_t *)t300) + 8);
        void * t302 = gm_l0;
        void * t303 = (void *)(((uint8_t *)t302) + 96);
        void * t304 = gm_l0;
        void * t305 = GM_LD(void *, (((uint8_t *)t304) + 0));
        gm_t88rcucg1b7uk t306 = gm_l17;
        gm_t88rcucg1b7uk t307 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t303, t305, t306);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t308;
        gm_f_kind_jcnt4hm((void *)&t308, t301, GM_CONV(gm_csguankko4lmhd, t307));
        gm_t53at80pfdus3 t309 = t308;
        uint8_t t310;
        uint8_t t316 = GM_LD(uint8_t, (((uint8_t *)&t309) + 0));
        switch (t316) { case ((uint8_t)0ULL): goto ma314; case ((uint8_t)1ULL): goto ma314; case ((uint8_t)2ULL): goto ma314; case ((uint8_t)3ULL): goto ma314; case ((uint8_t)4ULL): goto ma314; case ((uint8_t)5ULL): goto ma314; case ((uint8_t)6ULL): goto ma314; case ((uint8_t)7ULL): goto ma314; case ((uint8_t)8ULL): goto ma314; case ((uint8_t)9ULL): goto ma314; case ((uint8_t)10ULL): goto ma314; case ((uint8_t)11ULL): goto ma314; case ((uint8_t)12ULL): goto ma314; case ((uint8_t)13ULL): goto ma314; case ((uint8_t)14ULL): goto ma314; case ((uint8_t)15ULL): goto ma312; case ((uint8_t)16ULL): goto ma314; case ((uint8_t)17ULL): goto ma314; case ((uint8_t)18ULL): goto ma314; case ((uint8_t)19ULL): goto ma314; case ((uint8_t)20ULL): goto ma314; case ((uint8_t)21ULL): goto ma314; case ((uint8_t)22ULL): goto ma314; case ((uint8_t)23ULL): goto ma314; case ((uint8_t)24ULL): goto ma314; case ((uint8_t)25ULL): goto ma313; case ((uint8_t)26ULL): goto ma314; default: goto ma314; }
ma312:;
        uint8_t t318 = GM_LD(uint8_t, (((uint8_t *)&t309) + 0));
        if (t318 != ((uint8_t)15ULL)) goto mn317;
        t310 = ((uint8_t)1);
        goto me311;
mn317:;
ma313:;
        uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&t309) + 0));
        if (t320 != ((uint8_t)25ULL)) goto mn319;
        t310 = ((uint8_t)1);
        goto me311;
mn319:;
ma314:;
        t310 = ((uint8_t)0);
        goto me311;
mn321:;
ma315:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 55, 20);
me311:;
        if (!t310) goto ie297;
        {
            void * t323 = gm_l0;
            void * t324 = gm_l1;
            gm_tl874t2atnlh3 t325 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t324) + 48));
            gm_b16_8 t326;
            GM_ST(void *, (uint8_t *)&t326, (void *)(gm_sj3fn9etjoihf));
            GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(115));
            gm_b0_1 t327 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t323, t325, t326);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix297;
ie297:;
ix297:;
    }
    {
        void * t328 = gm_l0;
        gm_b16_8 t329 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_t88rcucg1b7uk t330 = gm_l17;
        uint8_t t331 = gm_l9;
        gm_tl874t2atnlh3 t332 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
        uint32_t t333 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t328, t329, t330, t331, t332);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t333;
    }
    {
        void * t334 = gm_l0;
        gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        uint32_t t336 = gm_l18;
        gm_t930q5l38hdap t337;
        memset(&t337, 0, sizeof t337);
        GM_ST(uint8_t, ((uint8_t *)&t337 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t337 + 8), t336);
        gm_tl874t2atnlh3 t338 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
        gm_b0_1 t339 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t334, t335, &t337, t338);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t340 = gm_l18;
        void * t341 = gm_l15;
        gm_ttggq7ng4kp3p t342;
        GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t342 + 0), t340);
        GM_ST(void *, ((uint8_t *)&t342 + 8), t341);
        gm_trcmmd12cefb1 t343;
        GM_ST(uint8_t, (uint8_t *)&t343 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t343 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t343 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t343 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t343 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t343 + 8), t342);
        uint8_t t344 = 0;
        F.ret = t343;
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
extern __attribute__((aligned(4))) uint8_t gm_f_closed_const_0aif03i(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_destructure_2jra5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_destructure_2jra5iu F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    void * gm_l2; /* tys */
    gm_tkn42u7na3t81 gm_l3; /* annot */
    void * gm_l4; /* t */
    void * gm_l5; /* init */
    gm_t88rcucg1b7uk gm_l6; /* t */
    void * gm_l7; /* ex */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* t */
    uint64_t gm_l11; /* n */
    gm_b16_8 gm_l12; /* es */
    uint8_t gm_l13; /* mutable */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_t08olq97rscn3 gm_l19; /* name */
    uint32_t gm_l20; /* id */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l23; /* d */
    GM_STACK_CHECK(&F, 2215u);
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
        gm_tkn42u7na3t81 t5;
        memset(&t5, 0, sizeof t5);
        gm_l3 = t5;
    }
    {
        void * t7 = gm_l1;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 24));
        void * t9 = t8;
        if (!(GM_LD(void *, ((uint8_t *)&t9)) != 0)) goto ie6;
        void * t10 = GM_LD(void *, ((uint8_t *)&t9));
        gm_l4 = t10;
        {
            void * t12 = gm_l0;
            void * t13 = gm_l4;
            gm_t88rcucg1b7uk t14 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[4] = 1;
            gm_l3 = t15;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        void * t18 = gm_l1;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 40));
        gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t19) + 72));
        void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t17, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t21;
    }
    {
        void * t23 = gm_l1;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 40));
        uint8_t t25 = ((GM_LD(uint8_t, (((uint8_t *)t24) + 8) + 0) == ((uint8_t)5ULL)));
        if (!t25) goto ie22;
        {
            void * t28 = gm_l1;
            uint8_t t29 = (!(GM_LD(uint8_t, (((uint8_t *)t28) + 32) + 0) == ((uint8_t)1ULL)));
            if (!t29) goto ie27;
            {
                void * t31 = gm_l0;
                void * t32 = gm_l1;
                void * t33 = GM_LD(void *, (((uint8_t *)t32) + 40));
                gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_svrjcmg2c5dio));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(77));
                gm_b0_1 t36 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t31, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie37;
            gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l6 = t38;
            void * t39 = gm_l0;
            gm_t88rcucg1b7uk t40 = gm_l6;
            uint8_t t41 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t41) goto ie37;
            {
                gm_t88rcucg1b7uk t43 = gm_l6;
                gm_teti411eujvf4 t44;
                memset(&t44, 0, sizeof t44);
                GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)8ULL));
                void * t45 = gm_l1;
                void * t46 = GM_LD(void *, (((uint8_t *)t45) + 40));
                gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 72));
                void * t48 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t43, &t44, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t48;
            }
            goto ix37;
ie37:;
            {
                void * t50 = gm_l0;
                void * t51 = gm_l1;
                void * t52 = GM_LD(void *, (((uint8_t *)t51) + 40));
                gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 72));
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_shqseub127akh));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(46));
                gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t50, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix37:;
        }
        goto ix22;
ie22:;
        {
            void * t57 = gm_l0;
            void * t58 = gm_l1;
            void * t59 = GM_LD(void *, (((uint8_t *)t58) + 40));
            gm_tkn42u7na3t81 t60 = gm_l3;
            void * t61 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t57, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t61;
        }
        {
            void * t62;
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie63;
            gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l8 = t64;
            void * t66 = gm_l0;
            void * t67 = gm_l7;
            gm_t88rcucg1b7uk t68 = gm_l8;
            void * t69 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t70 = t69;
            t62 = t70;
            goto ix63;
ie63:;
            void * t72 = gm_l7;
            void * t73 = t72;
            t62 = t73;
ix63:;
            gm_l5 = t62;
        }
ix22:;
    }
    {
        void * t74 = gm_l0;
        void * t75 = (void *)(((uint8_t *)t74) + 96);
        void * t76 = gm_l0;
        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
        void * t78 = gm_l5;
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t78) + 0));
        gm_t88rcucg1b7uk t80 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t75, t77, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t80;
    }
    {
        gm_f_list_nof7vfj((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t81 = gm_l1;
        uint64_t t82 = GM_LD(uintptr_t, (((uint8_t *)t81) + 8) + 8);
        gm_l11 = t82;
    }
    {
        void * t83 = gm_l2;
        gm_t88rcucg1b7uk t84 = gm_l9;
        gm_t53at80pfdus3 t85;
        gm_f_kind_jcnt4hm((void *)&t85, t83, GM_CONV(gm_csguankko4lmhd, t84));
        gm_t53at80pfdus3 t86 = t85;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t86) + 0));
        switch (t92) { case ((uint8_t)0ULL): goto ma90; case ((uint8_t)1ULL): goto ma90; case ((uint8_t)2ULL): goto ma90; case ((uint8_t)3ULL): goto ma90; case ((uint8_t)4ULL): goto ma90; case ((uint8_t)5ULL): goto ma90; case ((uint8_t)6ULL): goto ma90; case ((uint8_t)7ULL): goto ma90; case ((uint8_t)8ULL): goto ma90; case ((uint8_t)9ULL): goto ma90; case ((uint8_t)10ULL): goto ma90; case ((uint8_t)11ULL): goto ma90; case ((uint8_t)12ULL): goto ma88; case ((uint8_t)13ULL): goto ma90; case ((uint8_t)14ULL): goto ma90; case ((uint8_t)15ULL): goto ma90; case ((uint8_t)16ULL): goto ma90; case ((uint8_t)17ULL): goto ma90; case ((uint8_t)18ULL): goto ma90; case ((uint8_t)19ULL): goto ma90; case ((uint8_t)20ULL): goto ma90; case ((uint8_t)21ULL): goto ma90; case ((uint8_t)22ULL): goto ma90; case ((uint8_t)23ULL): goto ma90; case ((uint8_t)24ULL): goto ma90; case ((uint8_t)25ULL): goto ma90; case ((uint8_t)26ULL): goto ma89; default: goto ma90; }
ma88:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t86) + 0));
        if (t94 != ((uint8_t)12ULL)) goto mn93;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((((uint8_t *)&t86) + 8) + 0));
        gm_l12 = t95;
        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
        uint64_t t98 = gm_l11;
        uint8_t t99 = ((t97 == t98));
        if (!t99) goto ie96;
        {
            void * t101 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t102 = gm_l12;
            gm_tftb1odc739t5 t103 = gm_f_extend_lfqasgq(t101, t102);
            gm_tftb1odc739t5 t104 = t103;
            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcm8m4ndqum1bp, 86, 13);
            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
        }
        goto ix96;
ie96:;
        {
            void * t107 = gm_l0;
            void * t108 = gm_l1;
            gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t108) + 48));
            uint64_t t110 = gm_l11;
            uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
            gm_t2lqu8jsj511d t112;
            GM_ST(uint64_t, ((uint8_t *)&t112 + 0), t110);
            GM_ST(uint64_t, ((uint8_t *)&t112 + 8), t111);
            gm_tabb710d9mq2e t113;
            gm_f_format_7kp9jno((void *)&t113, (void *)NULL, cx, NULL, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t114 = t113;
            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srcm8m4ndqum1bp, 88, 30);
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
            gm_b16_8 t116 = t115;
            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
            gm_b0_1 t118 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t107, t109, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix96:;
        goto me87;
mn93:;
ma89:;
        uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&t86) + 0));
        if (t120 != ((uint8_t)26ULL)) goto mn119;
        goto me87;
mn119:;
ma90:;
        void * t123 = gm_l0;
        void * t124 = gm_l1;
        void * t125 = GM_LD(void *, (((uint8_t *)t124) + 40));
        gm_tl874t2atnlh3 t126 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t125) + 72));
        void * t127 = gm_l0;
        gm_t88rcucg1b7uk t128 = gm_l9;
        gm_b16_8 t129 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t127, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t130;
        GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t129);
        gm_tabb710d9mq2e t131;
        gm_f_format_gb7r1is((void *)&t131, (void *)NULL, cx, NULL, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t132 = t131;
        if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 8, gm_srcm8m4ndqum1bp, 91, 36);
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
        gm_b16_8 t134 = t133;
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_b0_1 t136 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t123, t126, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me87;
mn122:;
ma91:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 84, 5);
me87:;
    }
    {
        for (;;) {
            uint64_t t138 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
            uint64_t t139 = gm_l11;
            uint8_t t140 = (t138 < t139);
            if (!t140) goto lb137;
            {
                void * t143 = (void *)((uint8_t *)&F.l10);
                void * t144 = gm_l2;
                gm_t88rcucg1b7uk t145 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t144);
                if (gm_unw.kind) {
                    goto gm_ll141;
                }
                gm_tftb1odc739t5 t146 = gm_f_push_k3l9nic(t143, GM_CONV(gm_csguankko4lmhd, t145));
                gm_tftb1odc739t5 t147 = t146;
                if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_srcm8m4ndqum1bp, 93, 33);
                gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
            }
lc137:;
        }
gm_ll141:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb137; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc137; }
        }
        goto gm_lad;
lb137:;
    }
    {
        void * t149 = gm_l1;
        uint8_t t150 = ((GM_LD(uint8_t, (((uint8_t *)t149) + 32) + 0) == ((uint8_t)1ULL)));
        gm_l13 = t150;
    }
    {
        gm_f_list_rb5mlsd((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_sh8m0m0((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t152 = gm_l1;
            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)t152) + 8));
            gm_b16_8 t154 = t153;
            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
            gm_l16 = t155;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t157 = gm_l17;
                uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t159 = (t157 < t158);
                if (!t159) goto lb156;
                {
                    gm_t4u0l4lf5utrg t162;
                    uint64_t t163 = gm_l17;
                    GM_ST(uint64_t, (uint8_t *)&t162 + 0, t163);
                    uint64_t t164 = gm_l17;
                    uint64_t t165 = (uintptr_t)t164;
                    uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    memcpy((uint8_t *)&t162 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t165 * 32), 32);
                    gm_t4u0l4lf5utrg t167 = t162;
                    uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&t167) + 0));
                    gm_l18 = t170;
                    gm_t08olq97rscn3 t171 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t167) + 8));
                    gm_l19 = t171;
                    {
                        uint8_t t174 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l19) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l19) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                        if (!t174) goto ie173;
                        {
                            void * t176 = (void *)((uint8_t *)&F.l14);
                            gm_t9805qtonbsag t177;
                            memset(&t177, 0, sizeof t177);
                            gm_tftb1odc739t5 t178 = gm_f_push_r4haklv(t176, t177);
                            gm_tftb1odc739t5 t179 = t178;
                            if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srcm8m4ndqum1bp, 99, 13);
                            gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
                        }
                        {
                            goto lc156;
                        }
                        goto ix173;
ie173:;
ix173:;
                    }
                    {
                        void * t181 = gm_l0;
                        gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 0));
                        uint64_t t183 = gm_l18;
                        uint64_t t184 = (uintptr_t)t183;
                        uint64_t t185 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                        if (t184 >= (uintptr_t)(t185)) gm_trap_index(t184, t185, gm_srcm8m4ndqum1bp, 102, 39);
                        gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t184 * 4));
                        uint8_t t187 = gm_l13;
                        gm_tl874t2atnlh3 t188 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l19) + 16));
                        uint32_t t189 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t181, t182, t186, t187, t188);
                        if (gm_unw.kind) {
                            goto gm_ll160;
                        }
                        gm_l20 = t189;
                    }
                    {
                        void * t190 = (void *)((uint8_t *)&F.l14);
                        uint32_t t191 = gm_l20;
                        gm_t9805qtonbsag t192;
                        GM_ST(uint8_t, (uint8_t *)&t192 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t192 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t192), t191);
                        ((uint8_t *)&t192)[4] = 1;
                        gm_tftb1odc739t5 t193 = gm_f_push_r4haklv(t190, t192);
                        gm_tftb1odc739t5 t194 = t193;
                        if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 1, gm_srcm8m4ndqum1bp, 103, 9);
                        gm_b0_1 t195 = GM_LD(gm_b0_1, (((uint8_t *)&t194) + 1));
                    }
                    {
                        void * t196 = (void *)((uint8_t *)&F.l15);
                        gm_tdu0qimd4ae4c t197;
                        gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t197 + 0, t198);
                        uint32_t t199 = gm_l20;
                        GM_ST(uint32_t, (uint8_t *)&t197 + 16, t199);
                        gm_tl874t2atnlh3 t200 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l19) + 16));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t197 + 20, t200);
                        gm_tftb1odc739t5 t201 = gm_f_push_hign4kd(t196, &t197);
                        gm_tftb1odc739t5 t202 = t201;
                        if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 1, gm_srcm8m4ndqum1bp, 104, 9);
                        gm_b0_1 t203 = GM_LD(gm_b0_1, (((uint8_t *)&t202) + 1));
                    }
                    goto me168;
mn169:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 97, 5);
me168:;
                }
lc156:;
                {
                    uint64_t t205 = gm_l17;
                    uint64_t r206; bool o207 = __builtin_add_overflow(t205, ((uint64_t)1ULL), &r206);
                    uint64_t t208 = r206;
                    gm_l17 = t208;
                }
            }
gm_ll160:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb156; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc156; }
            }
            goto gm_lad;
lb156:;
        }
    }
    {
        {
            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_l21 = t210;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t212 = gm_l22;
                uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t214 = (t212 < t213);
                if (!t214) goto lb211;
                {
                    uint64_t t217 = gm_l22;
                    uint64_t t218 = (uintptr_t)t217;
                    uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_tdu0qimd4ae4c t222 = GM_LD(gm_tdu0qimd4ae4c, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t218 * 32));
                    gm_l23 = t222;
                    {
                        void * t224 = gm_l0;
                        gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 0));
                        uint32_t t226 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 16));
                        gm_t930q5l38hdap t227;
                        memset(&t227, 0, sizeof t227);
                        GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t227 + 8), t226);
                        gm_tl874t2atnlh3 t228 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l23) + 20));
                        gm_b0_1 t229 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t224, t225, &t227, t228);
                        if (gm_unw.kind) {
                            goto gm_ll215;
                        }
                    }
                    goto me220;
mn221:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 107, 5);
me220:;
                }
lc211:;
                {
                    uint64_t t231 = gm_l22;
                    uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                    uint64_t t234 = r232;
                    gm_l22 = t234;
                }
            }
gm_ll215:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb211; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc211; }
            }
            goto gm_lad;
lb211:;
        }
    }
    {
        gm_tpe1hv3fekn05 t235;
        gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t235 + 0, t236);
        void * t237 = gm_l5;
        GM_ST(void *, (uint8_t *)&t235 + 16, t237);
        gm_trcmmd12cefb1 t238;
        GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t238 + 8), t235);
        uint8_t t239 = 0;
        F.ret = t238;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6339gg7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_6339gg7 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_closed_const_0aif03i *)((&F.h)->up))->l0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_has_param_u9t0efs(t5, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) {
            void * t9 = gm_l0;
            uint8_t t10;
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            switch (t23) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma21; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma21; case ((uint8_t)7ULL): goto ma21; case ((uint8_t)8ULL): goto ma21; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma17; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma21; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma21; case ((uint8_t)21ULL): goto ma21; case ((uint8_t)22ULL): goto ma21; case ((uint8_t)23ULL): goto ma21; case ((uint8_t)24ULL): goto ma21; case ((uint8_t)25ULL): goto ma21; case ((uint8_t)26ULL): goto ma21; case ((uint8_t)27ULL): goto ma19; case ((uint8_t)28ULL): goto ma20; case ((uint8_t)29ULL): goto ma21; case ((uint8_t)30ULL): goto ma21; case ((uint8_t)31ULL): goto ma21; case ((uint8_t)32ULL): goto ma21; case ((uint8_t)33ULL): goto ma21; case ((uint8_t)34ULL): goto ma21; case ((uint8_t)35ULL): goto ma21; case ((uint8_t)36ULL): goto ma21; case ((uint8_t)37ULL): goto ma21; case ((uint8_t)38ULL): goto ma21; case ((uint8_t)39ULL): goto ma21; case ((uint8_t)40ULL): goto ma21; case ((uint8_t)41ULL): goto ma21; case ((uint8_t)42ULL): goto ma21; case ((uint8_t)43ULL): goto ma21; case ((uint8_t)44ULL): goto ma21; case ((uint8_t)45ULL): goto ma21; case ((uint8_t)46ULL): goto ma21; case ((uint8_t)47ULL): goto ma21; case ((uint8_t)48ULL): goto ma21; case ((uint8_t)49ULL): goto ma21; case ((uint8_t)50ULL): goto ma21; case ((uint8_t)51ULL): goto ma21; case ((uint8_t)52ULL): goto ma21; case ((uint8_t)53ULL): goto ma21; case ((uint8_t)54ULL): goto ma21; case ((uint8_t)55ULL): goto ma21; case ((uint8_t)56ULL): goto ma21; case ((uint8_t)57ULL): goto ma21; case ((uint8_t)58ULL): goto ma21; default: goto ma21; }
ma12:;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t25 != ((uint8_t)15ULL)) goto mn24;
            t10 = ((uint8_t)1);
            goto me11;
mn24:;
ma13:;
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t27 != ((uint8_t)16ULL)) goto mn26;
            t10 = ((uint8_t)1);
            goto me11;
mn26:;
ma14:;
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t29 != ((uint8_t)9ULL)) goto mn28;
            t10 = ((uint8_t)1);
            goto me11;
mn28:;
ma15:;
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t31 != ((uint8_t)10ULL)) goto mn30;
            t10 = ((uint8_t)1);
            goto me11;
mn30:;
ma16:;
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t33 != ((uint8_t)11ULL)) goto mn32;
            t10 = ((uint8_t)1);
            goto me11;
mn32:;
ma17:;
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t35 != ((uint8_t)12ULL)) goto mn34;
            t10 = ((uint8_t)1);
            goto me11;
mn34:;
ma18:;
            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t37 != ((uint8_t)13ULL)) goto mn36;
            t10 = ((uint8_t)1);
            goto me11;
mn36:;
ma19:;
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t39 != ((uint8_t)27ULL)) goto mn38;
            t10 = ((uint8_t)1);
            goto me11;
mn38:;
ma20:;
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
            if (t41 != ((uint8_t)28ULL)) goto mn40;
            t10 = ((uint8_t)1);
            goto me11;
mn40:;
ma21:;
            t10 = ((uint8_t)0);
            goto me11;
mn42:;
ma22:;
            gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 251, 37);
me11:;
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_closed_const_0aif03i *)((&F.h)->up))->l2), ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_eno8frt(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_eno8frt F;
    void * gm_l0; /* x */
    gm_t9805qtonbsag gm_l1; /* f */
    uint32_t gm_l3; /* g */
    uint32_t gm_l4; /* g */
    uint32_t gm_l5; /* g */
    void * gm_l6; /* sig */
    GM_STACK_CHECK_SHALLOW(&F, 221u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t9805qtonbsag t2;
        memset(&t2, 0, sizeof t2);
        gm_l1 = t2;
    }
    {
        void * t3 = gm_l0;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma5; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma5:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t11 != ((uint8_t)34ULL)) goto mn10;
        gm_tsth9qu1dv0g3 t12 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t3) + 8) + 8) + 0));
        F.l2 = t12;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t14 != ((uint8_t)0ULL)) goto ie13;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 8));
        gm_l3 = t15;
        {
            uint32_t t17 = gm_l3;
            gm_t9805qtonbsag t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t18), t17);
            ((uint8_t *)&t18)[4] = 1;
            gm_l1 = t18;
        }
        goto ix13;
ie13:;
ix13:;
        goto me4;
mn10:;
ma6:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t20 != ((uint8_t)26ULL)) goto mn19;
        uint32_t t21 = GM_LD(uint32_t, ((((uint8_t *)t3) + 8) + 8));
        gm_l4 = t21;
        {
            uint32_t t23 = gm_l4;
            gm_t9805qtonbsag t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[4] = 1;
            gm_l1 = t24;
        }
        goto me4;
mn19:;
ma7:;
        goto me4;
mn25:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 228, 9);
me4:;
    }
    {
        if (!((((uint8_t *)&gm_l1))[4] != 0)) goto ie27;
        uint32_t t28 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_l5 = t28;
        {
            uint32_t t30 = gm_l5;
            uint64_t t31 = (uint64_t)t30;
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_unsettled_pmdpogr *)((&F.h)->up))->l0));
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
            uint64_t t35 = GM_LD(uintptr_t, ((((uint8_t *)t34) + 344) + 0) + 8);
            if (t32 >= (uintptr_t)(t35)) gm_trap_index(t32, t35, gm_srcm8m4ndqum1bp, 234, 21);
            void * t36 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t34) + 344) + 0)) + t32 * 136);
            gm_l6 = t36;
        }
        {
            void * t38 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_unsettled_pmdpogr *)((&F.h)->up))->l0));
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            void * t41 = gm_l6;
            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 56));
            uint8_t t43 = gm_f_has_infer_3o82a0b(t40, GM_CONV(gm_csguankko4lmhd, t42));
            if (!t43) goto ie37;
            {
                GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_unsettled_pmdpogr *)((&F.h)->up))->l2), ((uint8_t)1));
            }
            goto ix37;
ie37:;
ix37:;
        }
        goto ix27;
ie27:;
ix27:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_construction_dbq4h7o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_is_construction_dbq4h7o F;
    void * gm_l0; /* e */
    gm_t3mf58gv9kf3n gm_l1; /* a */
    void * gm_l2; /* x */
    void * gm_l3; /* inner */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma5:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)28ULL)) goto mn10;
        gm_t3mf58gv9kf3n t12 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t12;
        void * t13 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
        t3 = t13;
        goto me4;
mn10:;
ma6:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)24ULL)) goto mn14;
        void * t16 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t16;
        void * t17 = gm_l2;
        t3 = t17;
        goto me4;
mn14:;
ma7:;
        uint8_t t19 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me4;
mn18:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 197, 14);
me4:;
        gm_l3 = t3;
    }
    {
        uint8_t t20 = 0;
        void * t21 = gm_l3;
        void * t22 = gm_f_construction_call_ktqtrpo(t21);
        void * t23 = t22;
        uint8_t t24;
        if ((GM_LD(void *, ((uint8_t *)&t23)) != 0)) goto mn26;
        t24 = ((uint8_t)0);
        goto me25;
mn26:;
        t24 = ((uint8_t)1);
        goto me25;
mn27:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 202, 12);
me25:;
        if (!t24) {
            void * t28 = gm_l0;
            void * t29 = gm_f_hoist_prefix_r1mevpl((void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t30 = t29;
            uint8_t t31;
            if ((GM_LD(void *, ((uint8_t *)&t30)) != 0)) goto mn33;
            t31 = ((uint8_t)0);
            goto me32;
mn33:;
            t31 = ((uint8_t)1);
            goto me32;
mn34:;
            gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 202, 57);
me32:;
            t20 = t31;
        } else t20 = 1;
        uint8_t t35 = 0;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_local_adt_binding_j8q6s4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2) {
    struct gm_fr_local_adt_binding_j8q6s4a F;
    void * gm_l0; /* cx */
    gm_t08olq97rscn3 gm_l1; /* name */
    void * gm_l2; /* b */
    gm_b16_8 gm_l3; /* n */
    gm_t88rcucg1b7uk gm_l4; /* t */
    GM_STACK_CHECK(&F, 1708u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_l3 = t2;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l3;
        gm_tqvu78rpi6q1h t6;
        gm_f_lookup_local_6f38t7p((void *)&t6, t4, t5);
        gm_tqvu78rpi6q1h t7 = t6;
        uint8_t t8;
        if (((((uint8_t *)&t7))[208] != 0)) goto mn10;
        t8 = ((uint8_t)0);
        goto me9;
mn10:;
        t8 = ((uint8_t)1);
        goto me9;
mn11:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 131, 8);
me9:;
        if (!t8) goto ie3;
        {
            void * t13 = gm_l0;
            gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_b16_8 t15 = gm_l3;
            gm_tnpifv20irnph t16;
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
            gm_tabb710d9mq2e t17;
            gm_f_format_q32bjcv((void *)&t17, (void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcm8m4ndqum1bp, 132, 29);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_b0_1 t22 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t13, t14, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix3;
ie3:;
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 8));
        gm_b16_8 t28 = gm_l3;
        uint8_t t29 = gm_f_names_type_rjdgib9((void *)NULL, cx, NULL, t25, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t29) goto ie23;
        {
            void * t31 = gm_l0;
            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_b16_8 t33 = gm_l3;
            gm_tnpifv20irnph t34;
            GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
            gm_tabb710d9mq2e t35;
            gm_f_format_hm2bsgr((void *)&t35, (void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcm8m4ndqum1bp, 134, 29);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_b0_1 t40 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t31, t32, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix23;
ie23:;
        gm_b16_8 t42 = gm_l3;
        uint8_t t43 = gm_f_is_reserved_type_name_tuq4qsj(t42);
        if (!t43) goto ie41;
        {
            void * t45 = gm_l0;
            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_b16_8 t47 = gm_l3;
            gm_tnpifv20irnph t48;
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
            gm_tabb710d9mq2e t49;
            gm_f_format_9i45r57((void *)&t49, (void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t50 = t49;
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcm8m4ndqum1bp, 136, 29);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
            gm_b16_8 t52 = t51;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_b0_1 t54 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t45, t46, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix41;
ie41:;
ix41:;
ix23:;
ix3:;
    }
    {
        void * t55 = gm_l0;
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
        void * t57 = gm_l0;
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 8));
        gm_b16_8 t59 = gm_l3;
        void * t60 = gm_l2;
        void * t61 = GM_LD(void *, (((uint8_t *)t60) + 40));
        gm_t88rcucg1b7uk t62 = gm_f_declare_local_adt_6j4pdai((void *)NULL, cx, NULL, t56, t58, t59, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t62;
    }
    {
        void * t63 = gm_l0;
        uint64_t t64 = GM_LD(uintptr_t, ((((uint8_t *)t63) + 56) + 0) + 8);
        uint64_t r65; bool o66 = __builtin_sub_overflow(t64, ((uint64_t)1ULL), &r65);
        if (o66) gm_trap("integer overflow in `-`", gm_srcm8m4ndqum1bp, 140, 21);
        uint64_t t67 = r65;
        uint64_t t68 = (uintptr_t)t67;
        void * t69 = gm_l0;
        uint64_t t70 = GM_LD(uintptr_t, ((((uint8_t *)t69) + 56) + 0) + 8);
        if (t68 >= (uintptr_t)(t70)) gm_trap_index(t68, t70, gm_srcm8m4ndqum1bp, 140, 5);
        void * t71 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t69) + 56) + 0)) + t68 * 40);
        gm_tmhicb6huo2ra t72;
        gm_b16_8 t73 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 0, t73);
        gm_t88rcucg1b7uk t74 = gm_l4;
        gm_t930q5l38hdap t75;
        memset(&t75, 0, sizeof t75);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)5ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t75 + 8), t74);
        GM_ST(gm_t930q5l38hdap, (uint8_t *)&t72 + 16, t75);
        gm_tftb1odc739t5 t76 = gm_f_push_nr0n00f(t71, &t72);
        gm_tftb1odc739t5 t77 = t76;
        if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcm8m4ndqum1bp, 140, 5);
        gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
    }
    {
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
        void * t81 = gm_l2;
        gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t81) + 48));
        void * t83 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t80, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t84;
        GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t84 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t84 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t84 + 8), t83);
        uint8_t t85 = 0;
        F.ret = t84;
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
__attribute__((aligned(4))) uint8_t gm_f_local_is_type_gakgl1b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_local_is_type_gakgl1b F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* elems */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l6; /* inner */
    GM_STACK_CHECK(&F, 577u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_t2gtbh39ptekd t3 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t2) + 8));
        gm_t2gtbh39ptekd t4 = t3;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma6:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t15 != ((uint8_t)10ULL)) goto mn14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l2 = t16;
        {
            {
                gm_b16_8 t19 = gm_l2;
                gm_l3 = t19;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t21 = gm_l4;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t23 = (t21 < t22);
                    if (!t23) goto lb20;
                    {
                        uint64_t t26 = gm_l4;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_tv1o1j44ii4db t31 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t27 * 136));
                        F.l5 = t31;
                        {
                            uint8_t t34 = 0;
                            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 128));
                            if (!t35) {
                                void * t36 = gm_l0;
                                void * t37 = (void *)(((uint8_t *)&F.l5) + 40);
                                uint8_t t38 = gm_f_local_is_type_gakgl1b((void *)NULL, cx, NULL, t36, t37);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                uint8_t t39 = (uint8_t)!t38;
                                t34 = t39;
                            } else t34 = 1;
                            if (!t34) goto ie33;
                            {
                                uint8_t t41 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix33;
ie33:;
ix33:;
                        }
                        goto me29;
mn30:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 210, 13);
me29:;
                    }
lc20:;
                    {
                        uint64_t t43 = gm_l4;
                        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                        uint64_t t46 = r44;
                        gm_l4 = t46;
                    }
                }
gm_ll24:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
                }
                goto gm_lad;
lb20:;
            }
        }
        {
            uint8_t t47 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me5;
mn14:;
ma7:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t49 != ((uint8_t)9ULL)) goto mn48;
        void * t50 = GM_LD(void *, (((uint8_t *)&t4) + 8));
        gm_l6 = t50;
        void * t51 = gm_l0;
        void * t52 = gm_l6;
        uint8_t t53 = gm_f_local_is_type_gakgl1b((void *)NULL, cx, NULL, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t54 = 0;
        F.ret = t53;
        goto gm_fret;
        goto me5;
mn48:;
ma8:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t58 != ((uint8_t)6ULL)) goto pa57;
        goto po56;
pa57:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t60 != ((uint8_t)22ULL)) goto pa59;
        goto po56;
pa59:;
        goto mn55;
po56:;
        void * t61 = gm_l0;
        void * t62 = gm_l1;
        gm_tkn42u7na3t81 t63 = gm_f_local_type_init_6it5555((void *)NULL, cx, NULL, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t64 = t63;
        uint8_t t65;
        if (((((uint8_t *)&t64))[4] != 0)) goto mn67;
        t65 = ((uint8_t)0);
        goto me66;
mn67:;
        t65 = ((uint8_t)1);
        goto me66;
mn68:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 216, 40);
me66:;
        uint8_t t69 = 0;
        F.ret = t65;
        goto gm_fret;
        goto me5;
mn55:;
ma9:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t73 != ((uint8_t)28ULL)) goto pa72;
        goto po71;
pa72:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t75 != ((uint8_t)24ULL)) goto pa74;
        goto po71;
pa74:;
        goto mn70;
po71:;
        void * t76 = gm_l1;
        uint8_t t77 = gm_f_is_construction_dbq4h7o((void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t77) goto mn70;
        void * t78 = gm_l0;
        void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
        void * t80 = gm_l0;
        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)t80) + 8));
        void * t82 = gm_l1;
        uint8_t t83 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t79, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t84 = 0;
        F.ret = t83;
        goto gm_fret;
        goto me5;
mn70:;
ma10:;
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t88 != ((uint8_t)24ULL)) goto pa87;
        goto po86;
pa87:;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t90 != ((uint8_t)25ULL)) goto pa89;
        goto po86;
pa89:;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t92 != ((uint8_t)26ULL)) goto pa91;
        goto po86;
pa91:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t94 != ((uint8_t)27ULL)) goto pa93;
        goto po86;
pa93:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t96 != ((uint8_t)28ULL)) goto pa95;
        goto po86;
pa95:;
        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t98 != ((uint8_t)29ULL)) goto pa97;
        goto po86;
pa97:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t100 != ((uint8_t)30ULL)) goto pa99;
        goto po86;
pa99:;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t102 != ((uint8_t)31ULL)) goto pa101;
        goto po86;
pa101:;
        goto mn85;
po86:;
        uint8_t t103 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me5;
mn85:;
ma11:;
        uint8_t t105 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me5;
mn104:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 208, 5);
me5:;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_local_type_init_6it5555(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_local_type_init_6it5555 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    gm_t08olq97rscn3 gm_l5; /* id */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_t88rcucg1b7uk gm_l8; /* t */
    uint64_t gm_l9; /* d */
    gm_t88rcucg1b7uk gm_l10; /* t */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    uint64_t gm_l13; /* m */
    uint64_t gm_l14; /* d */
    gm_t88rcucg1b7uk gm_l15; /* t */
    GM_STACK_CHECK(&F, 2926u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_t2gtbh39ptekd t3 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t2) + 8));
        gm_t2gtbh39ptekd t4 = t3;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma15; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma15; case ((uint8_t)33ULL): goto ma15; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma15; case ((uint8_t)37ULL): goto ma15; case ((uint8_t)38ULL): goto ma15; case ((uint8_t)39ULL): goto ma15; case ((uint8_t)40ULL): goto ma15; case ((uint8_t)41ULL): goto ma15; case ((uint8_t)42ULL): goto ma15; case ((uint8_t)43ULL): goto ma15; case ((uint8_t)44ULL): goto ma15; case ((uint8_t)45ULL): goto ma15; case ((uint8_t)46ULL): goto ma15; case ((uint8_t)47ULL): goto ma15; case ((uint8_t)48ULL): goto ma15; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma15; default: goto ma15; }
ma6:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t21 != ((uint8_t)28ULL)) goto pa20;
        goto po19;
pa20:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t23 != ((uint8_t)24ULL)) goto pa22;
        goto po19;
pa22:;
        goto mn18;
po19:;
        void * t24 = gm_l1;
        uint8_t t25 = gm_f_is_construction_dbq4h7o((void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t25) goto mn18;
        {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 8));
            void * t32 = gm_l1;
            uint8_t t33 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t29, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t33) goto ie27;
            {
                void * t35 = gm_l0;
                void * t36 = gm_l1;
                gm_t88rcucg1b7uk t37 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t38), t37);
                ((uint8_t *)&t38)[4] = 1;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            gm_tkn42u7na3t81 t40;
            memset(&t40, 0, sizeof t40);
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto me5;
mn18:;
ma7:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t43 != ((uint8_t)10ULL)) goto mn42;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l1;
            uint8_t t48 = gm_f_local_is_type_gakgl1b((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t48) goto ie45;
            {
                void * t50 = gm_l0;
                void * t51 = gm_l1;
                gm_t88rcucg1b7uk t52 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t50, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t53;
                GM_ST(uint8_t, (uint8_t *)&t53 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t53 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t53), t52);
                ((uint8_t *)&t53)[4] = 1;
                uint8_t t54 = 0;
                F.ret = t53;
                goto gm_fret;
            }
            goto ix45;
ie45:;
ix45:;
        }
        {
            gm_tkn42u7na3t81 t55;
            memset(&t55, 0, sizeof t55);
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto me5;
mn42:;
ma8:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t60 != ((uint8_t)24ULL)) goto pa59;
        goto po58;
pa59:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t62 != ((uint8_t)25ULL)) goto pa61;
        goto po58;
pa61:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t64 != ((uint8_t)26ULL)) goto pa63;
        goto po58;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t66 != ((uint8_t)27ULL)) goto pa65;
        goto po58;
pa65:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t68 != ((uint8_t)28ULL)) goto pa67;
        goto po58;
pa67:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t70 != ((uint8_t)29ULL)) goto pa69;
        goto po58;
pa69:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t72 != ((uint8_t)30ULL)) goto pa71;
        goto po58;
pa71:;
        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t74 != ((uint8_t)31ULL)) goto pa73;
        goto po58;
pa73:;
        goto mn57;
po58:;
        void * t75 = gm_l0;
        void * t76 = gm_l1;
        gm_t88rcucg1b7uk t77 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t78 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t78), t77);
        ((uint8_t *)&t78)[4] = 1;
        uint8_t t79 = 0;
        F.ret = t78;
        goto gm_fret;
        goto me5;
mn57:;
ma9:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t81 != ((uint8_t)49ULL)) goto mn80;
        gm_tet9guhev5r4u t82 = GM_LD(gm_tet9guhev5r4u, (((uint8_t *)&t4) + 8));
        F.l2 = t82;
        {
            uint8_t t85 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l2) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l2) + 0) + 0)), gm_slbgk65sat4bm, 7) == 0));
            if (!t85) goto ie84;
            {
                void * t87 = gm_l0;
                void * t88 = gm_l1;
                gm_t88rcucg1b7uk t89 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t90;
                GM_ST(uint8_t, (uint8_t *)&t90 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t90 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t90), t89);
                ((uint8_t *)&t90)[4] = 1;
                uint8_t t91 = 0;
                F.ret = t90;
                goto gm_fret;
            }
            goto ix84;
ie84:;
ix84:;
        }
        {
            gm_tkn42u7na3t81 t92;
            memset(&t92, 0, sizeof t92);
            uint8_t t93 = 0;
            F.ret = t92;
            goto gm_fret;
        }
        goto me5;
mn80:;
ma10:;
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t95 != ((uint8_t)35ULL)) goto mn94;
        void * t96 = GM_LD(void *, ((((uint8_t *)&t4) + 8) + 16));
        gm_l3 = t96;
        {
            uint8_t t99 = 0;
            void * t100 = gm_l3;
            uint8_t t101;
            uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t100) + 8) + 0));
            if (t104 != ((uint8_t)26ULL)) goto mn103;
            t101 = ((uint8_t)1);
            goto me102;
mn103:;
            t101 = ((uint8_t)0);
            goto me102;
mn105:;
            gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 163, 16);
me102:;
            if (!t101) {
                uint8_t t106 = 0;
                void * t107 = gm_l3;
                uint8_t t108;
                uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 0));
                if (t111 != ((uint8_t)28ULL)) goto mn110;
                t108 = ((uint8_t)1);
                goto me109;
mn110:;
                t108 = ((uint8_t)0);
                goto me109;
mn112:;
                gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 163, 48);
me109:;
                if (t108) {
                    void * t113 = gm_l3;
                    uint8_t t114 = gm_f_is_construction_dbq4h7o((void *)NULL, cx, NULL, t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t115 = (uint8_t)!t114;
                    t106 = t115;
                } else t106 = 0;
                t99 = t106;
            } else t99 = 1;
            if (!t99) goto ie98;
            {
                void * t117 = gm_l0;
                void * t118 = gm_l1;
                gm_t88rcucg1b7uk t119 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t117, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t120;
                GM_ST(uint8_t, (uint8_t *)&t120 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t120 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t120), t119);
                ((uint8_t *)&t120)[4] = 1;
                uint8_t t121 = 0;
                F.ret = t120;
                goto gm_fret;
            }
            goto ix98;
ie98:;
ix98:;
        }
        {
            gm_tkn42u7na3t81 t122;
            memset(&t122, 0, sizeof t122);
            uint8_t t123 = 0;
            F.ret = t122;
            goto gm_fret;
        }
        goto me5;
mn94:;
ma11:;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t125 != ((uint8_t)34ULL)) goto mn124;
        {
            void * t127 = gm_l0;
            void * t128 = gm_l1;
            gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t128) + 72));
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sd7vrpqmmkbm0));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(110));
            gm_b0_1 t131 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t127, t129, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t132 = gm_l0;
            void * t133 = GM_LD(void *, (((uint8_t *)t132) + 0));
            void * t134 = (void *)(((uint8_t *)t133) + 8);
            gm_t88rcucg1b7uk t135 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t136;
            GM_ST(uint8_t, (uint8_t *)&t136 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t136 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136), t135);
            ((uint8_t *)&t136)[4] = 1;
            uint8_t t137 = 0;
            F.ret = t136;
            goto gm_fret;
        }
        goto me5;
mn124:;
ma12:;
        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t139 != ((uint8_t)9ULL)) goto mn138;
        void * t140 = GM_LD(void *, (((uint8_t *)&t4) + 8));
        gm_l4 = t140;
        void * t141 = gm_l0;
        void * t142 = gm_l4;
        gm_tkn42u7na3t81 t143 = gm_f_local_type_init_6it5555((void *)NULL, cx, NULL, t141, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t144 = 0;
        F.ret = t143;
        goto gm_fret;
        goto me5;
mn138:;
ma13:;
        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t146 != ((uint8_t)6ULL)) goto mn145;
        gm_t08olq97rscn3 t147 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t4) + 8));
        gm_l5 = t147;
        {
            void * t150 = gm_l0;
            gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tqvu78rpi6q1h t152;
            gm_f_lookup_local_6f38t7p((void *)&t152, t150, t151);
            gm_tqvu78rpi6q1h t153 = t152;
            if (!((((uint8_t *)&t153))[208] != 0)) goto ie149;
            gm_t930q5l38hdap t154 = GM_LD(gm_t930q5l38hdap, ((uint8_t *)&t153));
            F.l6 = t154;
            {
                uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                if (t157 != ((uint8_t)5ULL)) goto ie156;
                gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                gm_l7 = t158;
                {
                    gm_t88rcucg1b7uk t160 = gm_l7;
                    gm_tkn42u7na3t81 t161;
                    GM_ST(uint8_t, (uint8_t *)&t161 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t161 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t161), t160);
                    ((uint8_t *)&t161)[4] = 1;
                    uint8_t t162 = 0;
                    F.ret = t161;
                    goto gm_fret;
                }
                goto ix156;
ie156:;
ix156:;
            }
            {
                gm_tkn42u7na3t81 t163;
                memset(&t163, 0, sizeof t163);
                uint8_t t164 = 0;
                F.ret = t163;
                goto gm_fret;
            }
            goto ix149;
ie149:;
ix149:;
        }
        {
            void * t166 = gm_l0;
            void * t167 = GM_LD(void *, (((uint8_t *)t166) + 0));
            gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tkn42u7na3t81 t169 = gm_f_type_param_3av4r8i(t167, t168);
            gm_tkn42u7na3t81 t170 = t169;
            if (!((((uint8_t *)&t170))[4] != 0)) goto ie165;
            gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t170));
            gm_l8 = t171;
            {
                gm_t88rcucg1b7uk t173 = gm_l8;
                gm_tkn42u7na3t81 t174;
                GM_ST(uint8_t, (uint8_t *)&t174 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t174 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t174), t173);
                ((uint8_t *)&t174)[4] = 1;
                uint8_t t175 = 0;
                F.ret = t174;
                goto gm_fret;
            }
            goto ix165;
ie165:;
ix165:;
        }
        {
            void * t177 = gm_l0;
            void * t178 = GM_LD(void *, (((uint8_t *)t177) + 0));
            void * t179 = gm_l0;
            uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)t179) + 8));
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tf5srh4mp4b3i t182 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t178, t180, t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t183 = t182;
            if (!((((uint8_t *)&t183))[8] != 0)) goto ie176;
            uint64_t t184 = GM_LD(uint64_t, ((uint8_t *)&t183));
            gm_l9 = t184;
            {
                void * t187 = gm_l0;
                void * t188 = GM_LD(void *, (((uint8_t *)t187) + 0));
                uint64_t t189 = gm_l9;
                gm_t6qooom8qa2do t190;
                gm_f_resolve_decl_pus042v((void *)&t190, (void *)NULL, cx, NULL, t188, t189);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6qooom8qa2do t191 = t190;
                uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&t191) + 0));
                if (t192 != ((uint8_t)0ULL)) goto ie186;
                gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t191) + 8));
                gm_l10 = t193;
                {
                    gm_t88rcucg1b7uk t195 = gm_l10;
                    gm_tkn42u7na3t81 t196;
                    GM_ST(uint8_t, (uint8_t *)&t196 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t196 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t196), t195);
                    ((uint8_t *)&t196)[4] = 1;
                    uint8_t t197 = 0;
                    F.ret = t196;
                    goto gm_fret;
                }
                goto ix186;
ie186:;
ix186:;
            }
            {
                gm_tkn42u7na3t81 t198;
                memset(&t198, 0, sizeof t198);
                uint8_t t199 = 0;
                F.ret = t198;
                goto gm_fret;
            }
            goto ix176;
ie176:;
ix176:;
        }
        {
            void * t200 = gm_l0;
            void * t201 = GM_LD(void *, (((uint8_t *)t200) + 0));
            gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tkn42u7na3t81 t203 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t201, t202);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t204 = 0;
            F.ret = t203;
            goto gm_fret;
        }
        goto me5;
mn145:;
ma14:;
        uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t206 != ((uint8_t)22ULL)) goto mn205;
        void * t207 = GM_LD(void *, ((((uint8_t *)&t4) + 8) + 0));
        gm_l11 = t207;
        gm_t08olq97rscn3 t208 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)&t4) + 8) + 8));
        gm_l12 = t208;
        {
            void * t210 = gm_l0;
            void * t211 = GM_LD(void *, (((uint8_t *)t210) + 0));
            void * t212 = gm_l0;
            uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)t212) + 8));
            void * t214 = gm_l11;
            gm_tf5srh4mp4b3i t215 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t211, t213, t214);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t216 = t215;
            uint64_t t217;
            if (((((uint8_t *)&t216))[8] != 0)) {
                uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&t216) + 0));
                t217 = t218;
            } else {
                gm_tkn42u7na3t81 t219;
                memset(&t219, 0, sizeof t219);
                uint8_t t220 = 0;
                F.ret = t219;
                goto gm_fret;
            }
            gm_l13 = t217;
        }
        {
            void * t221 = gm_l0;
            void * t222 = GM_LD(void *, (((uint8_t *)t221) + 0));
            void * t223 = GM_LD(void *, (((uint8_t *)t222) + 0));
            uint64_t t224 = gm_l13;
            gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
            gm_tf5srh4mp4b3i t226 = gm_f_lookup_59juqoo(t223, t224, t225);
            gm_tf5srh4mp4b3i t227 = t226;
            uint64_t t228;
            if (((((uint8_t *)&t227))[8] != 0)) {
                uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&t227) + 0));
                t228 = t229;
            } else {
                gm_tkn42u7na3t81 t230;
                memset(&t230, 0, sizeof t230);
                uint8_t t231 = 0;
                F.ret = t230;
                goto gm_fret;
            }
            gm_l14 = t228;
        }
        {
            void * t233 = gm_l0;
            void * t234 = GM_LD(void *, (((uint8_t *)t233) + 0));
            uint64_t t235 = gm_l14;
            void * t236 = gm_l0;
            uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)t236) + 8));
            uint8_t t238 = gm_f_decl_visible_bd168ti(t234, t235, t237);
            uint8_t t239 = (uint8_t)!t238;
            if (!t239) goto ie232;
            {
                gm_tkn42u7na3t81 t241;
                memset(&t241, 0, sizeof t241);
                uint8_t t242 = 0;
                F.ret = t241;
                goto gm_fret;
            }
            goto ix232;
ie232:;
ix232:;
        }
        {
            void * t244 = gm_l0;
            void * t245 = GM_LD(void *, (((uint8_t *)t244) + 0));
            uint64_t t246 = gm_l14;
            gm_t6qooom8qa2do t247;
            gm_f_resolve_decl_pus042v((void *)&t247, (void *)NULL, cx, NULL, t245, t246);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t248 = t247;
            uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
            if (t249 != ((uint8_t)0ULL)) goto ie243;
            gm_t88rcucg1b7uk t250 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t248) + 8));
            gm_l15 = t250;
            {
                gm_t88rcucg1b7uk t252 = gm_l15;
                gm_tkn42u7na3t81 t253;
                GM_ST(uint8_t, (uint8_t *)&t253 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t253 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t253), t252);
                ((uint8_t *)&t253)[4] = 1;
                uint8_t t254 = 0;
                F.ret = t253;
                goto gm_fret;
            }
            goto ix243;
ie243:;
ix243:;
        }
        {
            gm_tkn42u7na3t81 t255;
            memset(&t255, 0, sizeof t255);
            uint8_t t256 = 0;
            F.ret = t255;
            goto gm_fret;
        }
        goto me5;
mn205:;
ma15:;
        gm_tkn42u7na3t81 t258;
        memset(&t258, 0, sizeof t258);
        uint8_t t259 = 0;
        F.ret = t258;
        goto gm_fret;
        goto me5;
mn257:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 147, 5);
me5:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_nominal_lit_aiq6n9c(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nominal_lit_aiq6n9c F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma4; case ((uint8_t)33ULL): goto ma4; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma5; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)32ULL)) goto pa11;
        goto po10;
pa11:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)33ULL)) goto pa13;
        goto po10;
pa13:;
        goto mn9;
po10:;
        uint8_t t15 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn9:;
ma5:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)35ULL)) goto mn16;
        void * t18 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l1 = t18;
        void * t19 = gm_l1;
        uint8_t t20;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma24; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma24; case ((uint8_t)17ULL): goto ma24; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma24; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma24; case ((uint8_t)23ULL): goto ma24; case ((uint8_t)24ULL): goto ma24; case ((uint8_t)25ULL): goto ma24; case ((uint8_t)26ULL): goto ma24; case ((uint8_t)27ULL): goto ma24; case ((uint8_t)28ULL): goto ma24; case ((uint8_t)29ULL): goto ma24; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma24; case ((uint8_t)32ULL): goto ma22; case ((uint8_t)33ULL): goto ma23; case ((uint8_t)34ULL): goto ma24; case ((uint8_t)35ULL): goto ma24; case ((uint8_t)36ULL): goto ma24; case ((uint8_t)37ULL): goto ma24; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma24; case ((uint8_t)40ULL): goto ma24; case ((uint8_t)41ULL): goto ma24; case ((uint8_t)42ULL): goto ma24; case ((uint8_t)43ULL): goto ma24; case ((uint8_t)44ULL): goto ma24; case ((uint8_t)45ULL): goto ma24; case ((uint8_t)46ULL): goto ma24; case ((uint8_t)47ULL): goto ma24; case ((uint8_t)48ULL): goto ma24; case ((uint8_t)49ULL): goto ma24; case ((uint8_t)50ULL): goto ma24; default: goto ma24; }
ma22:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t28 != ((uint8_t)32ULL)) goto mn27;
        t20 = ((uint8_t)1);
        goto me21;
mn27:;
ma23:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t30 != ((uint8_t)33ULL)) goto mn29;
        t20 = ((uint8_t)1);
        goto me21;
mn29:;
ma24:;
        t20 = ((uint8_t)0);
        goto me21;
mn31:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 123, 41);
me21:;
        uint8_t t32 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me3;
mn16:;
ma6:;
        uint8_t t34 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn33:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcm8m4ndqum1bp, 121, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_unsettled_pmdpogr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1);

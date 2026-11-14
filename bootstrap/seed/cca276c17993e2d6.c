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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_temv198mna2jp;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tgat3d8836orb;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_temv198mna2jp) == 40 && _Alignof(gm_temv198mna2jp) == 8 && offsetof(gm_temv198mna2jp, f0) == 0 && offsetof(gm_temv198mna2jp, f1) == 16 && offsetof(gm_temv198mna2jp, f2) == 24, "layout of `(string, u32, string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgat3d8836orb) == 40 && _Alignof(gm_tgat3d8836orb) == 8 && offsetof(gm_tgat3d8836orb, f0) == 0 && offsetof(gm_tgat3d8836orb, f1) == 4 && offsetof(gm_tgat3d8836orb, f2) == 8 && offsetof(gm_tgat3d8836orb, f3) == 24, "layout of `(u32, u32, string, string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
struct gm_fr_digits_nifrtn2 {
    gm_frame h;
    gm_tutnubt6u4hoq ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_02cfiej {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0ate5so {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0k2jf0i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0vlmusu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_15a92hj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1cidrae {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_28ficoh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2o1nhi6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2s6ipoc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4dgk612 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5496c1e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6ddn3qo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6oohrk4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_74e5eio {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7gm8pem {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7vtuqmi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_832dk9h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8f2l88f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8sr780m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8teadog {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9ucic2l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_au7ud7s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b167hrs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b2dbnlv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b4dmecl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b57jit1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_btksa2s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_bvc0aop {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ceklcts {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cpqsmoj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_df1ahdv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dkgoijb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dl3ql1v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_efi64at {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_enbh8tr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_er13m96 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_f2q0d2h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fqcc8o9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fs297i6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ftetcsa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_g4oqgn7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_g95p62j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gdnk1k0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hgesl4k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ifongqa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ilp2hnl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ims1cqb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j987nbl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_job5dm8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_k00ktgh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_k0530tt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kpi5c9e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kvgfhs9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l2c7krn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lrva822 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lu1rlo4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m483hp0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mvq4hoe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nrfsbvc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o6goqq3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_o6jinns {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p0m61o8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p997tra {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pcpohkc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pn5qncg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_prtmvlf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ptl47cm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qoru7gf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ra0mede {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rsp2ou4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sng0gs3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_t0k15up {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_t9kg9ja {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tsvbhno {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tuebjvs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uaj2k5o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_va5nmdt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vc4i3o1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vg4ggqa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vq4h0uo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_729oo46 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_72n0e07 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7316bju {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_744g2li {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_74vh99o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_78j0ecp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_79gt991 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7a5f58b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7b9kq2t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7c4i91d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7cgva50 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7cklfe7 {
    gm_frame h;
    gm_tttl9nsdo1iu6 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tttl9nsdo1iu6 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7dijd7n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7duae7m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7e5ha9c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7ejl94l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7f83ds0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7f8ekea {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7fi363q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7g5nhjp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7gn9rqu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7gtji42 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7h4sakv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7h6ef8t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7her5m9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7hpupod {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7jgfnob {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7kp9jno {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7lopc28 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7lru3c2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7nu71mn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7nv0e8m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7os4s22 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7p2ook8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7peeku4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7plp021 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7r64gap {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7sbr40s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7sll6e8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7tqfnpi {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7u7i1da {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n0787fu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n0hp6t5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n18n34e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n1doesn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n1il1vf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n1vqu08 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n3gtmrf {
    gm_frame h;
    gm_temv198mna2jp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_temv198mna2jp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n537vp2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6eprd4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6tblha {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n7840ue {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n8a3chu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n8h20c1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nacqkaa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nag8m5u {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nbg44fl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nbjjmv4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nd352q9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nf4pnfb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nhoqqfq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nhrpa25 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nivudut {
    gm_frame h;
    gm_tgat3d8836orb l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tgat3d8836orb l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_njau13r {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_njg1saa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_njo4244 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nk3ghj0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nk7pf4i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nkjq1if {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nll5o74 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nlu0p4d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nm3fl8o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_norassj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_npjtqgc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nptl5af {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ns6dben {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nsoplg3 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nva8q4r {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nvg3me5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nvhh8d2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_digit_value_2b3md79(uint8_t a0);
__attribute__((aligned(4))) void gm_f_digits_nifrtn2(void *gm_out, gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3);
__attribute__((aligned(4))) gm_tutnubt6u4hoq gm_f_digits_nifrtn2_c(gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_02cfiej(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0ate5so(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0k2jf0i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0vlmusu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_15a92hj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1cidrae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_28ficoh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2o1nhi6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2s6ipoc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4dgk612(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5496c1e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6ddn3qo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6oohrk4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_74e5eio(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7gm8pem(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7vtuqmi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_832dk9h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8f2l88f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8sr780m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8teadog(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9ucic2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_au7ud7s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b167hrs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b2dbnlv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b4dmecl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b57jit1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_btksa2s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bvc0aop(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ceklcts(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cpqsmoj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_df1ahdv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dkgoijb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dl3ql1v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_efi64at(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_enbh8tr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_er13m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f2q0d2h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fqcc8o9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fs297i6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ftetcsa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4oqgn7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g95p62j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gdnk1k0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hgesl4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ifongqa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ilp2hnl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ims1cqb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j987nbl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_job5dm8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k00ktgh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0530tt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kpi5c9e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kvgfhs9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l2c7krn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lrva822(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lu1rlo4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m483hp0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mvq4hoe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nrfsbvc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6goqq3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6jinns(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p0m61o8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p997tra(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pcpohkc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pn5qncg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_prtmvlf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ptl47cm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qoru7gf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ra0mede(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rsp2ou4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sng0gs3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t0k15up(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9kg9ja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tsvbhno(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tuebjvs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uaj2k5o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_va5nmdt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vc4i3o1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg4ggqa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vq4h0uo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_729oo46(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_72n0e07(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7316bju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_744g2li(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_74vh99o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_78j0ecp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_79gt991(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7a5f58b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7b9kq2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7c4i91d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7cklfe7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7dijd7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7duae7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7e5ha9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7ejl94l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7f83ds0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7f8ekea(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7fi363q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_7g5nhjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7gn9rqu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7gtji42(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7h4sakv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7h6ef8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7her5m9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7hpupod(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7jgfnob(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7kp9jno(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_7lopc28(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7nu71mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7nv0e8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_7os4s22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7p2ook8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7peeku4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7plp021(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7sbr40s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7sll6e8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7tqfnpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7u7i1da(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n0787fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_n0hp6t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n18n34e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n1doesn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n1il1vf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n1vqu08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n3gtmrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n537vp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n7840ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n8a3chu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_n8h20c1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nacqkaa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_nag8m5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nbg44fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nd352q9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nf4pnfb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nhoqqfq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nhrpa25(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nivudut(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_njau13r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_njg1saa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_njo4244(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nk3ghj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nk7pf4i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nkjq1if(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nll5o74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nlu0p4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nm3fl8o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_norassj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_npjtqgc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nptl5af(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nsoplg3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nva8q4r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nvg3me5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nvhh8d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s0dhnude2n5fe[8];
extern const uint8_t gm_s0hcm1974rbe7[34];
extern const uint8_t gm_s0iditmthmvpu[21];
extern const uint8_t gm_s0t5omnh472sf[21];
extern const uint8_t gm_s12scbfvbvl4s[3];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1hbm05s14qo4[30];
extern const uint8_t gm_s230fdcusa4us[33];
extern const uint8_t gm_s24s9g8r9gc0v[7];
extern const uint8_t gm_s2aov9df7ef50[53];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2kc17j0uoe9m[23];
extern const uint8_t gm_s2m4r2316r14d[11];
extern const uint8_t gm_s2t2dldl583td[24];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s2umangg4hn1g[15];
extern const uint8_t gm_s312ibu3ccl18[12];
extern const uint8_t gm_s33h2lce46bnm[9];
extern const uint8_t gm_s37otbiaffdaa[25];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3bgi1rlb5ne1[8];
extern const uint8_t gm_s3d91jdjen8gc[8];
extern const uint8_t gm_s3e9133b3pasn[3];
extern const uint8_t gm_s3ea3k3og84ri[9];
extern const uint8_t gm_s3kdrrcff51rt[69];
extern const uint8_t gm_s3ndmfc1bk22f[23];
extern const uint8_t gm_s3sgbpnnlr6sb[5];
extern const uint8_t gm_s43j3q24pq7de[26];
extern const uint8_t gm_s472t5rs14ilt[6];
extern const uint8_t gm_s480gcu6od53p[61];
extern const uint8_t gm_s487q6bd4pfu3[50];
extern const uint8_t gm_s4a6o0t8isvlm[17];
extern const uint8_t gm_s4k36hl9k22np[63];
extern const uint8_t gm_s4k57chtk2mkc[88];
extern const uint8_t gm_s4nikb38gk9t0[22];
extern const uint8_t gm_s4v8j7s5sr5c1[66];
extern const uint8_t gm_s4vnlpe2ovpj8[6];
extern const uint8_t gm_s542rn9shphg8[16];
extern const uint8_t gm_s585om8jb9mop[25];
extern const uint8_t gm_s5hr2c7nkokp9[70];
extern const uint8_t gm_s5j185b1e4leb[89];
extern const uint8_t gm_s5oredgotak9m[58];
extern const uint8_t gm_s6cchf3tcs9e4[21];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s6uo8vuok1rk4[10];
extern const uint8_t gm_s77fggjckb5ln[41];
extern const uint8_t gm_s7bq3dilmfq6u[47];
extern const uint8_t gm_s7f8crhgefl4u[28];
extern const uint8_t gm_s7jb41578vpai[24];
extern const uint8_t gm_s7knd69l0mhc0[36];
extern const uint8_t gm_s7md88e2dla9a[9];
extern const uint8_t gm_s8abg3vemofr6[4];
extern const uint8_t gm_s8bgt7glif1c9[34];
extern const uint8_t gm_s8cr606ter6p9[39];
extern const uint8_t gm_s8ooubi1nm7vq[5];
extern const uint8_t gm_s8qpjmalo4drt[13];
extern const uint8_t gm_s8u6lv8hq3sn2[23];
extern const uint8_t gm_s91vt5u8rghg3[3];
extern const uint8_t gm_s97bho1civrt3[16];
extern const uint8_t gm_s9bggu8oepdvs[19];
extern const uint8_t gm_s9c2oj94op2h6[10];
extern const uint8_t gm_s9ol82jrvi3ma[4];
extern const uint8_t gm_s9pljh6s0n7in[17];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sabcps3pq8iv7[15];
extern const uint8_t gm_sabq7k0io9ql3[66];
extern const uint8_t gm_sahim4484f9l7[3];
extern const uint8_t gm_saja8vc684oud[41];
extern const uint8_t gm_saq0ier99o235[35];
extern const uint8_t gm_sasdd0129vf8p[20];
extern const uint8_t gm_sat505mlqphna[26];
extern const uint8_t gm_satil70qt25hp[52];
extern const uint8_t gm_satkhe9qra7p5[13];
extern const uint8_t gm_sav0q47tkm7j2[20];
extern const uint8_t gm_savi5cfvaqp3a[15];
extern const uint8_t gm_sb1nc8gh4as5h[15];
extern const uint8_t gm_sbg03hl2obk72[10];
extern const uint8_t gm_sbm73uc3nf0c8[6];
extern const uint8_t gm_sbvi33ihvufj1[20];
extern const uint8_t gm_sc21u906vi2gp[72];
extern const uint8_t gm_sc2u7rsavnn5v[47];
extern const uint8_t gm_sc4hsc9atc69r[33];
extern const uint8_t gm_sc61gdfjf1pvd[38];
extern const uint8_t gm_scj5qlamj2bll[39];
extern const uint8_t gm_scn0a8pnba0ho[41];
extern const uint8_t gm_scs1evojsik8q[4];
extern const uint8_t gm_sctlci97unb16[36];
extern const uint8_t gm_sd5g4r2t5gcbk[25];
extern const uint8_t gm_sd70ob31745fn[4];
extern const uint8_t gm_sddniufplfkku[66];
extern const uint8_t gm_sdi3k3oc2kq9i[15];
extern const uint8_t gm_sdqgviehbfoad[6];
extern const uint8_t gm_sdqssko83l66b[28];
extern const uint8_t gm_sdtjos7av9cn7[22];
extern const uint8_t gm_sdugj3qbhl84q[5];
extern const uint8_t gm_sdvrfjnpjiquk[20];
extern const uint8_t gm_se0oi28sapu01[11];
extern const uint8_t gm_se3b4jalim8at[6];
extern const uint8_t gm_se4r520bkfair[18];
extern const uint8_t gm_sedq8b22f9agn[91];
extern const uint8_t gm_sehat3g6ic3fn[50];
extern const uint8_t gm_sehvo63gl3eu0[26];
extern const uint8_t gm_sett0rbmg4jjf[60];
extern const uint8_t gm_sf6chrlv83m77[22];
extern const uint8_t gm_sf6i3pjks8s6u[21];
extern const uint8_t gm_sf9difklo6omg[17];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfjuf1jo6fusr[8];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sg0bl57kiha8j[27];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg9vhq4ehlfsf[18];
extern const uint8_t gm_sge9mrih5euc5[13];
extern const uint8_t gm_sgls4l5hb5j8p[75];
extern const uint8_t gm_sgpahsg79gte4[26];
extern const uint8_t gm_sgtlutvecf7kf[46];
extern const uint8_t gm_sh09ul4fvjk00[36];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sh74p5jjd1e00[4];
extern const uint8_t gm_sh7dg9dli11hk[50];
extern const uint8_t gm_shbfu7hj4uhkt[16];
extern const uint8_t gm_shbtdicfd8lcc[12];
extern const uint8_t gm_shc8a6qgqrqaf[42];
extern const uint8_t gm_shja5bmmh8ag2[30];
extern const uint8_t gm_shmdne5cle16i[8];
extern const uint8_t gm_sic6g350stk7h[21];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_siotufd8s66oj[21];
extern const uint8_t gm_sirg36m1q4cjm[55];
extern const uint8_t gm_sirqj5pqb9rph[58];
extern const uint8_t gm_sj6oj7rmit9sk[93];
extern const uint8_t gm_sjc00l2ljprs2[4];
extern const uint8_t gm_sjm54r6h90i7e[63];
extern const uint8_t gm_sjpvc4ek9dber[39];
extern const uint8_t gm_sjqf7m3o9alo0[9];
extern const uint8_t gm_sk30h53vgth78[23];
extern const uint8_t gm_sk7ckkvc4fg11[28];
extern const uint8_t gm_skat03ddcievj[39];
extern const uint8_t gm_skmq6kit541c7[6];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl7doakbaoimv[7];
extern const uint8_t gm_slfk6iuc3d2pa[14];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_sltjg0sbhbo73[13];
extern const uint8_t gm_slv9mko23s9sn[33];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_sm8uf6b4m0n7p[8];
extern const uint8_t gm_smb8nsuj9ie93[15];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smptjqchq7bt0[47];
extern const uint8_t gm_smrla0cd2esrp[14];
extern const uint8_t gm_sn2t7jm5mf8pm[5];
extern const uint8_t gm_sn88c2jg5b9gv[4];
extern const uint8_t gm_snhskkd5kfgfa[31];
extern const uint8_t gm_snomh93h7tumv[14];
extern const uint8_t gm_snrndjerbtt58[6];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_snvn4jaklkik3[65];
extern const uint8_t gm_soeo46rfuspd5[32];
extern const uint8_t gm_sopjc7gjg0pgu[32];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp525daee5j3r[85];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_spg9vuapom9qd[11];
extern const uint8_t gm_spi5fq66572ic[20];
extern const uint8_t gm_spnr89edksdqp[4];
extern const uint8_t gm_sq4a10jk2jerj[51];
extern const uint8_t gm_sqalimuc6s35k[32];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqen1asili489[52];
extern const uint8_t gm_sqfe8a5cd6b60[7];
extern const uint8_t gm_sqmf3k0nnjf4r[4];
extern const uint8_t gm_sqqdirgtqa30r[8];
extern const uint8_t gm_sqssndasqkc4l[17];
extern const uint8_t gm_sqtvhnbm32dvp[23];
extern const uint8_t gm_squr90ahaf9ht[13];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr88r273kmptc[21];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srhs1fn12neea[33];
extern const uint8_t gm_srp5cji88najc[10];
extern const uint8_t gm_srv5rbhjb8ns5[15];
extern const uint8_t gm_ss2v2ursd8pms[4];
extern const uint8_t gm_ssevkle1ss77c[13];
extern const uint8_t gm_ssjhh9bdn23jf[17];
extern const uint8_t gm_ssld67umkdenq[20];
extern const uint8_t gm_ssm3bl5los9iv[5];
extern const uint8_t gm_ssr91slt3ah1c[11];
extern const uint8_t gm_sssjusbjeb8fr[7];
extern const uint8_t gm_sstr4ffsnqfee[6];
extern const uint8_t gm_st547kc8a6rg5[16];
extern const uint8_t gm_st6hj8v0h80gt[6];
extern const uint8_t gm_stbno5snno6c5[8];
extern const uint8_t gm_stbt78k7uqc0m[24];
extern const uint8_t gm_stdvq0tcga0vp[8];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_sto6ssj6r66e3[30];
extern const uint8_t gm_stobu0djed6ck[10];
extern const uint8_t gm_stol2i9gi9a35[18];
extern const uint8_t gm_su4op6lvvdiua[16];
extern const uint8_t gm_su8f8u3vs6aep[110];
extern const uint8_t gm_sujs43rrijg57[102];
extern const uint8_t gm_suki713bapaoe[7];
extern const uint8_t gm_svkkls864r4t4[30];
extern const uint8_t gm_svnpflkpvd513[5];
extern const gm_type gm_ty1ak5vhpqj4;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyqa6gfa8omk;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_02cfiej(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_02cfiej F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7g5nhjp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7g5nhjp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7g5nhjp *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0ate5so(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0ate5so F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nhoqqfq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nhoqqfq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nhoqqfq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0k2jf0i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0k2jf0i F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7f83ds0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7f83ds0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7f83ds0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0vlmusu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0vlmusu F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7kp9jno *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7kp9jno *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7kp9jno *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_15a92hj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_15a92hj F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7f8ekea *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7f8ekea *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7f8ekea *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1cidrae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1cidrae F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_729oo46 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_729oo46 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_729oo46 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_28ficoh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_28ficoh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7nv0e8m *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7nv0e8m *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7nv0e8m *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2o1nhi6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2o1nhi6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_79gt991 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_79gt991 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_79gt991 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2s6ipoc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2s6ipoc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7ejl94l *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7ejl94l *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7ejl94l *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4dgk612(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4dgk612 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7u7i1da *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7u7i1da *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7u7i1da *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5496c1e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5496c1e F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7peeku4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7peeku4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7peeku4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6ddn3qo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6ddn3qo F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_744g2li *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_744g2li *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_744g2li *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6oohrk4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6oohrk4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nsoplg3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nsoplg3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nsoplg3 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_74e5eio(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_74e5eio F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nptl5af *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nptl5af *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nptl5af *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7gm8pem(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7gm8pem F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7jgfnob *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7jgfnob *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7jgfnob *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7vtuqmi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7vtuqmi F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nm3fl8o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nm3fl8o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nm3fl8o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_832dk9h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_832dk9h F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7b9kq2t *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7b9kq2t *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7b9kq2t *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8f2l88f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8f2l88f F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7dijd7n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7dijd7n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7dijd7n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8sr780m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8sr780m F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nll5o74 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nll5o74 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nll5o74 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8teadog(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8teadog F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n0787fu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n0787fu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n0787fu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9ucic2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9ucic2l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nvg3me5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nvg3me5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nvg3me5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_au7ud7s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_au7ud7s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7h4sakv *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7h4sakv *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7h4sakv *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b167hrs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b167hrs F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7duae7m *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7duae7m *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7duae7m *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b2dbnlv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b2dbnlv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n537vp2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n537vp2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n537vp2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b4dmecl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b4dmecl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_njau13r *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_njau13r *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_njau13r *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b57jit1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b57jit1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n1doesn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n1doesn *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n1doesn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_btksa2s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_btksa2s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7h6ef8t *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7h6ef8t *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7h6ef8t *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bvc0aop(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_bvc0aop F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nd352q9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nd352q9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nd352q9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ceklcts(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ceklcts F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7a5f58b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7a5f58b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7a5f58b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cpqsmoj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cpqsmoj F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nhrpa25 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nhrpa25 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nhrpa25 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_df1ahdv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_df1ahdv F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nva8q4r *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nva8q4r *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nva8q4r *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dkgoijb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dkgoijb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7cklfe7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7cklfe7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7cklfe7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dl3ql1v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dl3ql1v F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7hpupod *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7hpupod *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7hpupod *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_efi64at(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_efi64at F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_78j0ecp *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_78j0ecp *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_78j0ecp *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_enbh8tr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_enbh8tr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7sbr40s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7sbr40s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7sbr40s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_er13m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_er13m96 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nivudut *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nivudut *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nivudut *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f2q0d2h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_f2q0d2h F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7cgva50 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7cgva50 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7cgva50 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fqcc8o9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fqcc8o9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nf4pnfb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nf4pnfb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nf4pnfb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fs297i6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fs297i6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_njo4244 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_njo4244 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_njo4244 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ftetcsa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ftetcsa F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_njg1saa *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_njg1saa *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_njg1saa *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4oqgn7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_g4oqgn7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n8a3chu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n8a3chu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n8a3chu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g95p62j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_g95p62j F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n0hp6t5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n0hp6t5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n0hp6t5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gdnk1k0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gdnk1k0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7316bju *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7316bju *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7316bju *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hgesl4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_hgesl4k F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_74vh99o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_74vh99o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_74vh99o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ifongqa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ifongqa F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nbjjmv4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nbjjmv4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nbjjmv4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ilp2hnl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ilp2hnl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7r64gap *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7r64gap *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7r64gap *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ims1cqb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ims1cqb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nkjq1if *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nkjq1if *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nkjq1if *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j987nbl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_j987nbl F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7c4i91d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7c4i91d *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7c4i91d *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_job5dm8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_job5dm8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7os4s22 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7os4s22 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7os4s22 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k00ktgh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_k00ktgh F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7plp021 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7plp021 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7plp021 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0530tt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_k0530tt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ns6dben *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ns6dben *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ns6dben *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kpi5c9e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kpi5c9e F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7tqfnpi *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7tqfnpi *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7tqfnpi *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kvgfhs9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kvgfhs9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n18n34e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n18n34e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n18n34e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l2c7krn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l2c7krn F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7fi363q *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7fi363q *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7fi363q *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lrva822(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lrva822 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nvhh8d2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nvhh8d2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nvhh8d2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lu1rlo4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_lu1rlo4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7lopc28 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7lopc28 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7lopc28 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m483hp0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_m483hp0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_72n0e07 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_72n0e07 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_72n0e07 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mvq4hoe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mvq4hoe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nbg44fl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nbg44fl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nbg44fl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nrfsbvc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nrfsbvc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7her5m9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7her5m9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7her5m9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6goqq3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o6goqq3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n6tblha *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n6tblha *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n6tblha *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6jinns(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_o6jinns F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nag8m5u *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nag8m5u *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nag8m5u *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p0m61o8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_p0m61o8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n1il1vf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n1il1vf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n1il1vf *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p997tra(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_p997tra F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n3gtmrf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n3gtmrf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n3gtmrf *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pcpohkc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pcpohkc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_npjtqgc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_npjtqgc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_npjtqgc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pn5qncg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pn5qncg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7lru3c2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7lru3c2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7lru3c2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_prtmvlf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_prtmvlf F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n8h20c1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n8h20c1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n8h20c1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ptl47cm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ptl47cm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7nu71mn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7nu71mn *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7nu71mn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qoru7gf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_qoru7gf F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n6eprd4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n6eprd4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n6eprd4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ra0mede(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ra0mede F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7gn9rqu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7gn9rqu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7gn9rqu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rsp2ou4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rsp2ou4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7sll6e8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7sll6e8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7sll6e8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sng0gs3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sng0gs3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7e5ha9c *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7e5ha9c *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7e5ha9c *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t0k15up(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_t0k15up F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_norassj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_norassj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_norassj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9kg9ja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_t9kg9ja F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nk3ghj0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nk3ghj0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nk3ghj0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tsvbhno(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tsvbhno F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7gtji42 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7gtji42 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7gtji42 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tuebjvs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tuebjvs F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nacqkaa *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nacqkaa *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nacqkaa *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uaj2k5o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_uaj2k5o F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_7p2ook8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_7p2ook8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_7p2ook8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_va5nmdt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_va5nmdt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n1vqu08 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n1vqu08 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n1vqu08 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vc4i3o1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vc4i3o1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_n7840ue *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_n7840ue *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_n7840ue *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg4ggqa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vg4ggqa F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nlu0p4d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nlu0p4d *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nlu0p4d *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vq4h0uo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vq4h0uo F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_nk7pf4i *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_nk7pf4i *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_b0_1 t9;
            if (!((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
                t9 = t10;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_nk7pf4i *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t9 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_digits_nifrtn2(void *gm_out, gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digits_nifrtn2 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* neg */
    uint32_t gm_l3; /* base */
    gm_tf5srh4mp4b3i gm_l4; /* tb */
    uint64_t gm_l5; /* acc */
    uint8_t gm_l6; /* after_digit */
    uint64_t gm_l7; /* i */
    uint8_t gm_l8; /* c */
    uint8_t gm_l9; /* d */
    uint64_t gm_l10; /* dv */
    uint64_t gm_l11; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l3;
        gm_tf5srh4mp4b3i t3;
        uint64_t t4 = (uint64_t)t2;
        gm_tf5srh4mp4b3i t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[8] = 1;
        gm_tf5srh4mp4b3i t6;
        memset(&t6, 0, sizeof t6);
        t3 = ((unsigned __int128)(t2) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t5 : t6;
        gm_l4 = t3;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        gm_l6 = ((uint8_t)0);
    }
    {
        uint64_t t7 = gm_l1;
        gm_l7 = t7;
    }
    {
        for (;;) {
            uint64_t t9 = gm_l7;
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t11 = (t9 < t10);
            if (!t11) goto lb8;
            {
                uint64_t t14 = gm_l7;
                uint64_t t15 = (uintptr_t)t14;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_src3d0v42kavhv7, 336, 14);
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t15 * 1));
                gm_l8 = t17;
            }
            {
                uint8_t t19 = gm_l8;
                uint8_t t20 = ((t19 == ((uint8_t)95ULL)));
                if (!t20) goto ie18;
                {
                    uint8_t t23 = gm_l6;
                    uint8_t t24 = (uint8_t)!t23;
                    if (!t24) goto ie22;
                    {
                        uint64_t t26 = gm_l7;
                        gm_t4ce0jd3tdro4 t27;
                        GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t26);
                        gm_ttiiv5vhiuvvr t28;
                        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t28 + 8), t27);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t28);
                        goto gm_fdone;
                    }
                    goto ix22;
ie22:;
ix22:;
                }
                {
                    gm_l6 = ((uint8_t)0);
                }
                {
                    uint64_t t29 = gm_l7;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 340, 13);
                    uint64_t t32 = r30;
                    gm_l7 = t32;
                }
                {
                    goto lc8;
                }
                goto ix18;
ie18:;
ix18:;
            }
            {
                uint8_t t33 = gm_l8;
                gm_tqs5p4n2tq19v t34 = gm_f_digit_value_2b3md79(t33);
                uint8_t t35;
                if (((((uint8_t *)&t34))[1] != 0)) {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
                    t35 = t36;
                } else {
                    uint64_t t37 = gm_l7;
                    gm_t4ce0jd3tdro4 t38;
                    GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t37);
                    gm_ttiiv5vhiuvvr t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t39 + 8), t38);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                    GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t39);
                    goto gm_fdone;
                }
                gm_l9 = t35;
            }
            {
                uint8_t t41 = gm_l9;
                uint32_t t42 = (uint32_t)t41;
                uint32_t t43 = gm_l3;
                uint8_t t44 = (t42 >= t43);
                if (!t44) goto ie40;
                {
                    uint64_t t46 = gm_l7;
                    gm_t4ce0jd3tdro4 t47;
                    GM_ST(uint64_t, ((uint8_t *)&t47 + 0), t46);
                    gm_ttiiv5vhiuvvr t48;
                    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t48 + 8), t47);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                    GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t48);
                    goto gm_fdone;
                }
                goto ix40;
ie40:;
ix40:;
            }
            {
                uint8_t t49 = gm_l9;
                gm_tf5srh4mp4b3i t50;
                uint64_t t51 = (uint64_t)t49;
                gm_tf5srh4mp4b3i t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t52), t51);
                ((uint8_t *)&t52)[8] = 1;
                gm_tf5srh4mp4b3i t53;
                memset(&t53, 0, sizeof t53);
                t50 = ((unsigned __int128)(t49) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t52 : t53;
                uint64_t t54;
                if (((((uint8_t *)&t50))[8] != 0)) {
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&t50) + 0));
                    t54 = t55;
                } else {
                    gm_ttiiv5vhiuvvr t56;
                    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t56 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)2ULL));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                    GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t56);
                    goto gm_fdone;
                }
                gm_l10 = t54;
            }
            {
                uint64_t t58 = gm_l5;
                uint8_t t59 = (!(t58 == ((uint64_t)0ULL)));
                if (!t59) goto ie57;
                {
                    gm_tf5srh4mp4b3i t61 = gm_l4;
                    uint64_t t62;
                    if (((((uint8_t *)&t61))[8] != 0)) {
                        uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&t61) + 0));
                        t62 = t63;
                    } else {
                        gm_ttiiv5vhiuvvr t64;
                        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t64 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t64);
                        goto gm_fdone;
                    }
                    gm_l11 = t62;
                }
                {
                    uint64_t t65 = gm_l5;
                    uint64_t t66 = gm_l11;
                    uint64_t r67; bool o68 = __builtin_mul_overflow(t65, t66, &r67);
                    gm_tf5srh4mp4b3i t69;
                    if (o68) {
                    gm_tf5srh4mp4b3i t70;
                    memset(&t70, 0, sizeof t70);
                        t69 = t70;
                    } else {
                    uint64_t t71 = r67;
                    gm_tf5srh4mp4b3i t72;
                    GM_ST(uint8_t, (uint8_t *)&t72 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t72 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t72 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t72), t71);
                    ((uint8_t *)&t72)[8] = 1;
                        t69 = t72;
                    }
                    uint64_t t73;
                    if (((((uint8_t *)&t69))[8] != 0)) {
                        uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&t69) + 0));
                        t73 = t74;
                    } else {
                        gm_ttiiv5vhiuvvr t75;
                        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t75);
                        goto gm_fdone;
                    }
                    gm_l5 = t73;
                }
                goto ix57;
ie57:;
ix57:;
            }
            {
                uint8_t t77 = gm_l2;
                if (!t77) goto ie76;
                {
                    uint64_t t79 = gm_l5;
                    uint64_t t80 = gm_l10;
                    uint64_t r81; bool o82 = __builtin_sub_overflow(t79, t80, &r81);
                    gm_tf5srh4mp4b3i t83;
                    if (o82) {
                    gm_tf5srh4mp4b3i t84;
                    memset(&t84, 0, sizeof t84);
                        t83 = t84;
                    } else {
                    uint64_t t85 = r81;
                    gm_tf5srh4mp4b3i t86;
                    GM_ST(uint8_t, (uint8_t *)&t86 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t86 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t86), t85);
                    ((uint8_t *)&t86)[8] = 1;
                        t83 = t86;
                    }
                    uint64_t t87;
                    if (((((uint8_t *)&t83))[8] != 0)) {
                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&t83) + 0));
                        t87 = t88;
                    } else {
                        gm_ttiiv5vhiuvvr t89;
                        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t89 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t89);
                        goto gm_fdone;
                    }
                    gm_l5 = t87;
                }
                goto ix76;
ie76:;
                {
                    uint64_t t91 = gm_l5;
                    uint64_t t92 = gm_l10;
                    uint64_t r93; bool o94 = __builtin_add_overflow(t91, t92, &r93);
                    gm_tf5srh4mp4b3i t95;
                    if (o94) {
                    gm_tf5srh4mp4b3i t96;
                    memset(&t96, 0, sizeof t96);
                        t95 = t96;
                    } else {
                    uint64_t t97 = r93;
                    gm_tf5srh4mp4b3i t98;
                    GM_ST(uint8_t, (uint8_t *)&t98 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t98 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t98 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t98), t97);
                    ((uint8_t *)&t98)[8] = 1;
                        t95 = t98;
                    }
                    uint64_t t99;
                    if (((((uint8_t *)&t95))[8] != 0)) {
                        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&t95) + 0));
                        t99 = t100;
                    } else {
                        gm_ttiiv5vhiuvvr t101;
                        GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t101 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)2ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
                        GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t101);
                        goto gm_fdone;
                    }
                    gm_l5 = t99;
                }
ix76:;
            }
            {
                gm_l6 = ((uint8_t)1);
            }
            {
                uint64_t t102 = gm_l7;
                uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                if (o104) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 357, 9);
                uint64_t t105 = r103;
                gm_l7 = t105;
            }
lc8:;
        }
lb8:;
    }
    {
        uint8_t t107 = gm_l6;
        uint8_t t108 = (uint8_t)!t107;
        if (!t108) goto ie106;
        {
            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_t4ce0jd3tdro4 t111;
            GM_ST(uint64_t, ((uint8_t *)&t111 + 0), t110);
            gm_ttiiv5vhiuvvr t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t4ce0jd3tdro4, ((uint8_t *)&t112 + 8), t111);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 1;
            GM_ST(gm_ttiiv5vhiuvvr, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t112);
            goto gm_fdone;
        }
        goto ix106;
ie106:;
ix106:;
    }
    {
        uint64_t t113 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tutnubt6u4hoq *)gm_out) + 8, t113);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tutnubt6u4hoq gm_f_digits_nifrtn2_c(gm_b16_8 a0, uint64_t a1, uint8_t a2, uint32_t a3) { gm_tutnubt6u4hoq gm_r; gm_f_digits_nifrtn2(&gm_r, a0, a1, a2, a3); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_02cfiej(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0ate5so(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0k2jf0i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0vlmusu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_15a92hj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1cidrae(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_28ficoh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2o1nhi6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2s6ipoc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4dgk612(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5496c1e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6ddn3qo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6oohrk4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_74e5eio(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7gm8pem(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7vtuqmi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_832dk9h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8f2l88f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8sr780m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8teadog(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9ucic2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_au7ud7s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b167hrs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b2dbnlv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b4dmecl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b57jit1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_btksa2s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bvc0aop(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ceklcts(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cpqsmoj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_df1ahdv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dkgoijb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dl3ql1v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_efi64at(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_enbh8tr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_er13m96(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f2q0d2h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fqcc8o9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fs297i6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ftetcsa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g4oqgn7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_g95p62j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gdnk1k0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hgesl4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ifongqa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ilp2hnl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ims1cqb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j987nbl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_job5dm8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k00ktgh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k0530tt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kpi5c9e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kvgfhs9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l2c7krn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lrva822(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lu1rlo4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m483hp0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mvq4hoe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nrfsbvc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6goqq3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_o6jinns(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p0m61o8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p997tra(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pcpohkc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pn5qncg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_prtmvlf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ptl47cm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qoru7gf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ra0mede(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rsp2ou4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sng0gs3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t0k15up(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9kg9ja(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tsvbhno(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tuebjvs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uaj2k5o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_va5nmdt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vc4i3o1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vg4ggqa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vq4h0uo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_729oo46(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_729oo46 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siotufd8s66oj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_1cidrae, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_srp5cji88najc));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(9));
            gm_f_fn_1cidrae(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3d91jdjen8gc));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
            gm_f_fn_1cidrae(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_72n0e07(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_72n0e07 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8u6lv8hq3sn2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_m483hp0, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sasdd0129vf8p));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(19));
            gm_f_fn_m483hp0(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_m483hp0(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_m483hp0(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7316bju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7316bju F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3ea3k3og84ri));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gdnk1k0, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sqfe8a5cd6b60));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
            gm_f_fn_gdnk1k0(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_744g2li(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_744g2li F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slv9mko23s9sn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_6ddn3qo, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sdtjos7av9cn7));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(21));
            gm_f_fn_6ddn3qo(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_6ddn3qo(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_stdvq0tcga0vp));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
            gm_f_fn_6ddn3qo(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_6ddn3qo(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_74vh99o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_74vh99o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg0bl57kiha8j));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_hgesl4k, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_hgesl4k(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_hgesl4k(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s6cchf3tcs9e4));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(20));
            gm_f_fn_hgesl4k(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_hgesl4k(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_hgesl4k(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_78j0ecp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_78j0ecp F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh74p5jjd1e00));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_efi64at, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_efi64at(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_efi64at(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_79gt991(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_79gt991 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjqf7m3o9alo0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_2o1nhi6, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_skmq6kit541c7));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
            gm_f_fn_2o1nhi6(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_2o1nhi6(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_2o1nhi6(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7a5f58b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7a5f58b F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8cr606ter6p9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ceklcts, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sl7doakbaoimv));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
            gm_f_fn_ceklcts(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ceklcts(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s7f8crhgefl4u));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(27));
            gm_f_fn_ceklcts(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_ceklcts(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_ceklcts(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_7b9kq2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7b9kq2t F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbm73uc3nf0c8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_832dk9h, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_stmsadvf54k73));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
            gm_f_fn_832dk9h(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_832dk9h(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_832dk9h(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7c4i91d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_7c4i91d F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqmf3k0nnjf4r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_j987nbl, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_j987nbl(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7cgva50 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_satil70qt25hp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_f2q0d2h, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sh7dg9dli11hk));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(49));
            gm_f_fn_f2q0d2h(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_f2q0d2h(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7cklfe7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7cklfe7 F;
    GM_STACK_CHECK(&F, 301u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7bq3dilmfq6u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)56ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tttl9nsdo1iu6 t13 = GM_LD(gm_tttl9nsdo1iu6, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_dkgoijb, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 48));
            gm_closure t19 = t14;
            gm_node *ccx20 = t19.cx;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_smooaouqmm8dj));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_dkgoijb(t19.env, ccx20, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sehvo63gl3eu0));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(25));
            gm_f_fn_dkgoijb(t19.env, ccx20, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_dkgoijb(t19.env, ccx20, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_spnr89edksdqp));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
            gm_f_fn_dkgoijb(t19.env, ccx20, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_dkgoijb(t19.env, ccx20, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(2));
            gm_f_fn_dkgoijb(t19.env, ccx20, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_ty6groq37ram, ((uint8_t *)&t18), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sqqdirgtqa30r));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(7));
            gm_f_fn_dkgoijb(t19.env, ccx20, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t30);
            goto gm_fdone;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t31 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t31);
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
inline __attribute__((aligned(4))) void gm_f_format_7dijd7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7dijd7n F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7jb41578vpai));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8f2l88f, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sf6chrlv83m77));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(21));
            gm_f_fn_8f2l88f(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_8f2l88f(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7duae7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7duae7m F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s230fdcusa4us));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_b167hrs, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sat505mlqphna));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(25));
            gm_f_fn_b167hrs(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_b167hrs(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sqdfr4k3vea5p));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
            gm_f_fn_b167hrs(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_b167hrs(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_7e5ha9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7e5ha9c F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sirqj5pqb9rph));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_sng0gs3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s2umangg4hn1g));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(14));
            gm_f_fn_sng0gs3(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_sng0gs3(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_scj5qlamj2bll));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(38));
            gm_f_fn_sng0gs3(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_sng0gs3(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_sng0gs3(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_7ejl94l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7ejl94l F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stbno5snno6c5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_2s6ipoc, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_snrndjerbtt58));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
            gm_f_fn_2s6ipoc(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_2s6ipoc(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7f83ds0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7f83ds0 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sedq8b22f9agn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(90));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_0k2jf0i, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s487q6bd4pfu3));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(49));
            gm_f_fn_0k2jf0i(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_0k2jf0i(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s24s9g8r9gc0v));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
            gm_f_fn_0k2jf0i(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_0k2jf0i(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sqalimuc6s35k));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(31));
            gm_f_fn_0k2jf0i(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_7f8ekea(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7f8ekea F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgls4l5hb5j8p));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(74));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_15a92hj, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sc21u906vi2gp));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(71));
            gm_f_fn_15a92hj(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_15a92hj(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_15a92hj(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7fi363q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_7fi363q F;
    gm_t19enj90vg8bd gm_l0; /* args */
    gm_t19enj90vg8bd gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdqssko83l66b));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t19enj90vg8bd t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_l2c7krn, (void *)&F.h, cx});
            int32_t t15 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_stbt78k7uqc0m));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(23));
            gm_f_fn_l2c7krn(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tyqa6gfa8omk, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s6t4r831mukdr));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
            gm_f_fn_l2c7krn(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7g5nhjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7g5nhjp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4nikb38gk9t0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_02cfiej, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sabcps3pq8iv7));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(14));
            gm_f_fn_02cfiej(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_02cfiej(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_f_fn_02cfiej(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_02cfiej(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_02cfiej(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_7gn9rqu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7gn9rqu F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s472t5rs14ilt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ra0mede, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ra0mede(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sh0j2ggs1vvoa));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_ra0mede(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tybihgc69jv3, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7gtji42(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7gtji42 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spi5fq66572ic));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_tsvbhno, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sqssndasqkc4l));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(16));
            gm_f_fn_tsvbhno(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_tsvbhno(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_tsvbhno(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7h4sakv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7h4sakv F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shja5bmmh8ag2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_au7ud7s, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sge9mrih5euc5));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(12));
            gm_f_fn_au7ud7s(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_au7ud7s(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s542rn9shphg8));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(15));
            gm_f_fn_au7ud7s(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7h6ef8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7h6ef8t F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgtlutvecf7kf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_btksa2s, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_shbtdicfd8lcc));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(11));
            gm_f_fn_btksa2s(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_btksa2s(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_srhs1fn12neea));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(32));
            gm_f_fn_btksa2s(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7her5m9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7her5m9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smrla0cd2esrp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_nrfsbvc, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_shmdne5cle16i));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(7));
            gm_f_fn_nrfsbvc(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_nrfsbvc(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_nrfsbvc(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_nrfsbvc(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_nrfsbvc(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_7hpupod(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7hpupod F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3kdrrcff51rt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(68));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_dl3ql1v, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sabq7k0io9ql3));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(65));
            gm_f_fn_dl3ql1v(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_dl3ql1v(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_dl3ql1v(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7jgfnob(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7jgfnob F;
    GM_STACK_CHECK(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sirg36m1q4cjm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)40ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tftksr4op114e t13 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_7gm8pem, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sm55k23067j17));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(15));
            gm_f_fn_7gm8pem(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_7gm8pem(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_ssevkle1ss77c));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(12));
            gm_f_fn_7gm8pem(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_7gm8pem(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s39mljh669qgl));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
            gm_f_fn_7gm8pem(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_ty6groq37ram, ((uint8_t *)&t17), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sf6i3pjks8s6u));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(20));
            gm_f_fn_7gm8pem(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_7kp9jno(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_7kp9jno F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq4a10jk2jerj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2lqu8jsj511d t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_0vlmusu, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sc61gdfjf1pvd));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(37));
            gm_f_fn_0vlmusu(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_stobu0djed6ck));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(9));
            gm_f_fn_0vlmusu(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_7lopc28(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7lopc28 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s77fggjckb5ln));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_lu1rlo4, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sb1nc8gh4as5h));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
            gm_f_fn_lu1rlo4(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_lu1rlo4(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s585om8jb9mop));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(24));
            gm_f_fn_lu1rlo4(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7lru3c2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stol2i9gi9a35));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_pn5qncg, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_savi5cfvaqp3a));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
            gm_f_fn_pn5qncg(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_pn5qncg(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_pn5qncg(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7nu71mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7nu71mn F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0dhnude2n5fe));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ptl47cm, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ptl47cm(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_se3b4jalim8at));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
            gm_f_fn_ptl47cm(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7nv0e8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_7nv0e8m F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8ooubi1nm7vq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_28ficoh, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s12scbfvbvl4s));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
            gm_f_fn_28ficoh(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7os4s22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7os4s22 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfjuf1jo6fusr));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_job5dm8, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_job5dm8(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sdqgviehbfoad));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
            gm_f_fn_job5dm8(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7p2ook8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7p2ook8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shc8a6qgqrqaf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_uaj2k5o, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_se4r520bkfair));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(17));
            gm_f_fn_uaj2k5o(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_uaj2k5o(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sav0q47tkm7j2));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(19));
            gm_f_fn_uaj2k5o(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tybihgc69jv3, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_uaj2k5o(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_7peeku4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7peeku4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbvi33ihvufj1));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_5496c1e, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s4a6o0t8isvlm));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(16));
            gm_f_fn_5496c1e(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_5496c1e(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_5496c1e(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7plp021(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7plp021 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su4op6lvvdiua));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_k00ktgh, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_snomh93h7tumv));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(13));
            gm_f_fn_k00ktgh(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_k00ktgh(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7r64gap F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snhskkd5kfgfa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ilp2hnl, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_ilp2hnl(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ilp2hnl(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sk7ckkvc4fg11));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(27));
            gm_f_fn_ilp2hnl(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_7sbr40s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7sbr40s F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saja8vc684oud));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_enbh8tr, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s8bgt7glif1c9));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(33));
            gm_f_fn_enbh8tr(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_enbh8tr(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sqdfr4k3vea5p));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
            gm_f_fn_enbh8tr(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_enbh8tr(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_7sll6e8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7sll6e8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sssjusbjeb8fr));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_rsp2ou4, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sn2t7jm5mf8pm));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
            gm_f_fn_rsp2ou4(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_rsp2ou4(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_7tqfnpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7tqfnpi F;
    GM_STACK_CHECK(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_satkhe9qra7p5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_kpi5c9e, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_kpi5c9e(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sp02qqgucmpt7));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_f_fn_kpi5c9e(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_kpi5c9e(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s9ol82jrvi3ma));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
            gm_f_fn_kpi5c9e(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_kpi5c9e(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
            gm_f_fn_kpi5c9e(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t24 = F.l2;
        if (!t24) goto ie23;
        {
            void * t26 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t26);
        }
        {
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t28);
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
inline __attribute__((aligned(4))) void gm_f_format_7u7i1da(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7u7i1da F;
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sett0rbmg4jjf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_4dgk612, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_4dgk612(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_4dgk612(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s2t2dldl583td));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(23));
            gm_f_fn_4dgk612(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_4dgk612(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sp7cdd0n91vli));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(7));
            gm_f_fn_4dgk612(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_4dgk612(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sqtvhnbm32dvp));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(22));
            gm_f_fn_4dgk612(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_n0787fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_n0787fu F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sddniufplfkku));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8teadog, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s480gcu6od53p));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(60));
            gm_f_fn_8teadog(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tyb2nno7n67r, ((uint8_t *)&t15), &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1})) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_n0hp6t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n0hp6t5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp525daee5j3r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(84));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_g95p62j, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s0iditmthmvpu));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(20));
            gm_f_fn_g95p62j(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_g95p62j(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sjm54r6h90i7e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(62));
            gm_f_fn_g95p62j(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_n18n34e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n18n34e F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg9vhq4ehlfsf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_kvgfhs9, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_kvgfhs9(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_kvgfhs9(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sdi3k3oc2kq9i));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(14));
            gm_f_fn_kvgfhs9(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_n1doesn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n1doesn F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2kc17j0uoe9m));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_b57jit1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sic6g350stk7h));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(20));
            gm_f_fn_b57jit1(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_b57jit1(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_n1il1vf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n1il1vf F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm8uf6b4m0n7p));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_p0m61o8, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s4vnlpe2ovpj8));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
            gm_f_fn_p0m61o8(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_p0m61o8(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_n1vqu08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n1vqu08 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sujs43rrijg57));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(101));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_va5nmdt, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sltjg0sbhbo73));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(12));
            gm_f_fn_va5nmdt(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_va5nmdt(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s4k57chtk2mkc));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(87));
            gm_f_fn_va5nmdt(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_n3gtmrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n3gtmrf F;
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srv5rbhjb8ns5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)40ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_temv198mna2jp t13 = GM_LD(gm_temv198mna2jp, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_p997tra, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sh40na9qbga3l));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_p997tra(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_p997tra(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
            gm_f_fn_p997tra(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_ty467d5cm1d5, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(2));
            gm_f_fn_p997tra(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_p997tra(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
            gm_f_fn_p997tra(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_n537vp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n537vp2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr88r273kmptc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_b2dbnlv, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sn88c2jg5b9gv));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            gm_f_fn_b2dbnlv(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_b2dbnlv(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_shbfu7hj4uhkt));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(15));
            gm_f_fn_b2dbnlv(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6eprd4 F;
    GM_STACK_CHECK(&F, 221u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbg03hl2obk72));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)40ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tq3m5qgvkpje8 t13 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_qoru7gf, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_qoru7gf(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s39mljh669qgl));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_qoru7gf(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_slgmqgkioa4u6));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
            gm_f_fn_qoru7gf(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_qoru7gf(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6tblha F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s43j3q24pq7de));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t13 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_o6goqq3, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_o6goqq3(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_o6goqq3(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s33h2lce46bnm));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(8));
            gm_f_fn_o6goqq3(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_ty6groq37ram, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s8qpjmalo4drt));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(12));
            gm_f_fn_o6goqq3(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_n7840ue(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n7840ue F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su8f8u3vs6aep));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(109));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vc4i3o1, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sjpvc4ek9dber));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(38));
            gm_f_fn_vc4i3o1(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_vc4i3o1(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s5hr2c7nkokp9));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(69));
            gm_f_fn_vc4i3o1(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_n8a3chu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_n8a3chu F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suki713bapaoe));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)8ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_g4oqgn7, (void *)&F.h, cx});
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty6groq37ram, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3sgbpnnlr6sb));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
            gm_f_fn_g4oqgn7(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_n8h20c1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n8h20c1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sctlci97unb16));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_prtmvlf, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sto6ssj6r66e3));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(29));
            gm_f_fn_prtmvlf(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_prtmvlf(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_slgmqgkioa4u6));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_f_fn_prtmvlf(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_prtmvlf(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_nacqkaa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0) {
    struct gm_fr_format_nacqkaa F;
    gm_tsn2omssg6cpf gm_l0; /* args */
    gm_tsn2omssg6cpf gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd70ob31745fn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tsn2omssg6cpf t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_tuebjvs, (void *)&F.h, cx});
            unsigned __int128 t15 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_tuebjvs(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty1ak5vhpqj4, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_nag8m5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nag8m5u F;
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqen1asili489));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_o6jinns, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_spg9vuapom9qd));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(10));
            gm_f_fn_o6jinns(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t18.code, t18.env, ccx19, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_st6hj8v0h80gt));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(5));
            gm_f_fn_o6jinns(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_o6jinns(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_svkkls864r4t4));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(29));
            gm_f_fn_o6jinns(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_o6jinns(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
            gm_f_fn_o6jinns(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_nbg44fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nbg44fl F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6uo8vuok1rk4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_mvq4hoe, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3bgi1rlb5ne1));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(7));
            gm_f_fn_mvq4hoe(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_mvq4hoe(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_nbjjmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nbjjmv4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0hcm1974rbe7));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ifongqa, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_st547kc8a6rg5));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(15));
            gm_f_fn_ifongqa(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ifongqa(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_ssjhh9bdn23jf));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(16));
            gm_f_fn_ifongqa(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_nd352q9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nd352q9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smptjqchq7bt0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_bvc0aop, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sk30h53vgth78));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(22));
            gm_f_fn_bvc0aop(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_bvc0aop(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s9pljh6s0n7in));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(16));
            gm_f_fn_bvc0aop(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_bvc0aop(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s91vt5u8rghg3));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
            gm_f_fn_bvc0aop(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_nf4pnfb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nf4pnfb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scn0a8pnba0ho));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_fqcc8o9, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_fqcc8o9(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_fqcc8o9(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_saq0ier99o235));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(34));
            gm_f_fn_fqcc8o9(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_fqcc8o9(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_fqcc8o9(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_nhoqqfq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nhoqqfq F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skat03ddcievj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_0ate5so, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s7knd69l0mhc0));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(35));
            gm_f_fn_0ate5so(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_0ate5so(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_0ate5so(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_nhrpa25(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nhrpa25 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjc00l2ljprs2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_cpqsmoj, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_cpqsmoj(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_cpqsmoj(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_nivudut(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nivudut F;
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s312ibu3ccl18));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)40ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tgat3d8836orb t13 = GM_LD(gm_tgat3d8836orb, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_er13m96, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 0));
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 4));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_closure t19 = t14;
            gm_node *ccx20 = t19.cx;
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_er13m96(t19.env, ccx20, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t19.code, t19.env, ccx20, &gm_ty467d5cm1d5, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
            gm_f_fn_er13m96(t19.env, ccx20, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_er13m96(t19.env, ccx20, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s2u5posfkbgnq));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
            gm_f_fn_er13m96(t19.env, ccx20, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0) {
                gm_f_fn_er13m96(t19.env, ccx20, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t18)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_njau13r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_njau13r F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7md88e2dla9a));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_b4dmecl, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_smooaouqmm8dj));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_b4dmecl(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_b4dmecl(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sstr4ffsnqfee));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(5));
            gm_f_fn_b4dmecl(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_njg1saa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_njg1saa F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s97bho1civrt3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ftetcsa, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_squr90ahaf9ht));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(12));
            gm_f_fn_ftetcsa(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ftetcsa(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_ftetcsa(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_njo4244(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_njo4244 F;
    GM_STACK_CHECK(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc2u7rsavnn5v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_fs297i6, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_fs297i6(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s9bggu8oepdvs));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(18));
            gm_f_fn_fs297i6(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_fs297i6(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s3e9133b3pasn));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
            gm_f_fn_fs297i6(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_fs297i6(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s0t5omnh472sf));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(20));
            gm_f_fn_fs297i6(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t24 = F.l2;
        if (!t24) goto ie23;
        {
            void * t26 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t26);
        }
        {
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t28);
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
inline __attribute__((aligned(4))) void gm_f_format_nk3ghj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nk3ghj0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4k36hl9k22np));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_t9kg9ja, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sopjc7gjg0pgu));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(31));
            gm_f_fn_t9kg9ja(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_t9kg9ja(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1hbm05s14qo4));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(29));
            gm_f_fn_t9kg9ja(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_nk7pf4i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nk7pf4i F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssm3bl5los9iv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vq4h0uo, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_tybihgc69jv3, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sahim4484f9l7));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
            gm_f_fn_vq4h0uo(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_nkjq1if(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nkjq1if F;
    GM_STACK_CHECK(&F, 349u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s37otbiaffdaa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)80ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tud7oovfvnf43 t13 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_ims1cqb, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_closure t20 = t14;
            gm_node *ccx21 = t20.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_ims1cqb(t20.env, ccx21, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_scs1evojsik8q));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
            gm_f_fn_ims1cqb(t20.env, ccx21, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_ims1cqb(t20.env, ccx21, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sdugj3qbhl84q));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            gm_f_fn_ims1cqb(t20.env, ccx21, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_ims1cqb(t20.env, ccx21, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_scs1evojsik8q));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            gm_f_fn_ims1cqb(t20.env, ccx21, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0) {
                gm_f_fn_ims1cqb(t20.env, ccx21, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t18)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_snukl6jqep6jf));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(3));
            gm_f_fn_ims1cqb(t20.env, ccx21, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t19) + 8) != 0) {
                gm_f_fn_ims1cqb(t20.env, ccx21, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t19)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(1));
            gm_f_fn_ims1cqb(t20.env, ccx21, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t28 = F.l2;
        if (!t28) goto ie27;
        {
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t30);
        }
        {
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t31);
            goto gm_fdone;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_nll5o74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nll5o74 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5oredgotak9m));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_8sr780m, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sd5g4r2t5gcbk));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(24));
            gm_f_fn_8sr780m(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_8sr780m(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_soeo46rfuspd5));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(31));
            gm_f_fn_8sr780m(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_nlu0p4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nlu0p4d F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sh09ul4fvjk00));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_vg4ggqa, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sc4hsc9atc69r));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(32));
            gm_f_fn_vg4ggqa(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_vg4ggqa(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_vg4ggqa(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_nm3fl8o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nm3fl8o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smhvmmegaur89));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_7vtuqmi, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_7vtuqmi(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_smooaouqmm8dj));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_7vtuqmi(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_7vtuqmi(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            gm_f_fn_7vtuqmi(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_norassj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_norassj F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss2v2ursd8pms));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)4ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_t0k15up, (void *)&F.h, cx});
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_smnfu5n5pum1l));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            gm_f_fn_t0k15up(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t16.code, t16.env, ccx17, &gm_ty467d5cm1d5, ((uint8_t *)&t15), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_npjtqgc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_npjtqgc F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2aov9df7ef50));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_pcpohkc, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sehat3g6ic3fn));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(49));
            gm_f_fn_pcpohkc(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_pcpohkc(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_f_fn_pcpohkc(t16.env, ccx17, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t21 = F.l2;
        if (!t21) goto ie20;
        {
            void * t23 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t23);
        }
        {
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t25);
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
inline __attribute__((aligned(4))) void gm_f_format_nptl5af(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nptl5af F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj6oj7rmit9sk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(92));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_74e5eio, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s3ndmfc1bk22f));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(22));
            gm_f_fn_74e5eio(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_74e5eio(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_snvn4jaklkik3));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(64));
            gm_f_fn_74e5eio(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_74e5eio(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sqvepl1893o5g));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
            gm_f_fn_74e5eio(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ns6dben F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgpahsg79gte4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_k0530tt, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s2m4r2316r14d));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(10));
            gm_f_fn_k0530tt(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_k0530tt(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_se0oi28sapu01));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(10));
            gm_f_fn_k0530tt(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_k0530tt(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfm9elkq6bn2e));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_k0530tt(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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
inline __attribute__((aligned(4))) void gm_f_format_nsoplg3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nsoplg3 F;
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5j185b1e4leb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(88));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)48ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t13 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_6oohrk4, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_closure t18 = t14;
            gm_node *ccx19 = t18.cx;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s9c2oj94op2h6));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(9));
            gm_f_fn_6oohrk4(t18.env, ccx19, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_6oohrk4(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_st6hj8v0h80gt));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(5));
            gm_f_fn_6oohrk4(t18.env, ccx19, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_6oohrk4(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s8abg3vemofr6));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
            gm_f_fn_6oohrk4(t18.env, ccx19, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t17) + 8) != 0) {
                gm_f_fn_6oohrk4(t18.env, ccx19, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s4v8j7s5sr5c1));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(65));
            gm_f_fn_6oohrk4(t18.env, ccx19, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t29);
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
inline __attribute__((aligned(4))) void gm_f_format_nva8q4r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nva8q4r F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf9difklo6omg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)16ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t13 = gm_l0;
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_df1ahdv, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_closure t16 = t14;
            gm_node *ccx17 = t16.cx;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_smb8nsuj9ie93));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
            gm_f_fn_df1ahdv(t16.env, ccx17, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_df1ahdv(t16.env, ccx17, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t20 = F.l2;
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t22);
        }
        {
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t23);
            goto gm_fdone;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t24);
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
inline __attribute__((aligned(4))) void gm_f_format_nvg3me5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nvg3me5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdvrfjnpjiquk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t13 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_9ucic2l, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_slfk6iuc3d2pa));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(13));
            gm_f_fn_9ucic2l(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_9ucic2l(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_slgmqgkioa4u6));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_f_fn_9ucic2l(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t16) + 8) != 0) {
                gm_f_fn_9ucic2l(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        uint8_t t22 = F.l2;
        if (!t22) goto ie21;
        {
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t24);
        }
        {
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t25);
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t26);
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
inline __attribute__((aligned(4))) void gm_f_format_nvhh8d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nvhh8d2 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssld67umkdenq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)24ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 789, 15);
        uint64_t t7 = r5;
        gm_tftb1odc739t5 t8 = gm_f_reserve_d1qo8mf(t2, t7);
        gm_b0_1 t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
            t9 = t10;
        } else {
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t11);
            goto gm_fdone;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t13 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t13;
        }
        {
            gm_closure t14 = ((gm_closure){(void *)gm_f_fn_lrva822, (void *)&F.h, cx});
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            gm_closure t17 = t14;
            gm_node *ccx18 = t17.cx;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_ssr91slt3ah1c));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(10));
            gm_f_fn_lrva822(t17.env, ccx18, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (GM_LD(uintptr_t, ((uint8_t *)&t15) + 8) != 0) {
                gm_f_fn_lrva822(t17.env, ccx18, NULL, GM_LD(gm_b16_8, ((uint8_t *)&t15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_svnpflkpvd513));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
            gm_f_fn_lrva822(t17.env, ccx18, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t17.code, t17.env, ccx18, &gm_tyb2nno7n67r, ((uint8_t *)&t16), NULL)) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s1asl1e1tvb0k));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
            gm_f_fn_lrva822(t17.env, ccx18, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t23 = F.l2;
        if (!t23) goto ie22;
        {
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t25);
        }
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out))[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_tabb710d9mq2e *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t27);
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

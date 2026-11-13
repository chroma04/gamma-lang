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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_b16_8 v1; } f1; } gm_tvbp5g7fh5fh0;
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
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvbp5g7fh5fh0) == 24 && _Alignof(gm_tvbp5g7fh5fh0) == 8 && offsetof(gm_tvbp5g7fh5fh0, f0) == 0 && offsetof(gm_tvbp5g7fh5fh0, f1) == 8, "layout of `LiftResult`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
struct gm_fr_else_after_error_349u6it {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* capture */
    gm_b0_1 ret;
};
struct gm_fr_else_expr_5lgn2qp {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* capture */
    void * ret;
};
struct gm_fr_else_join_ri264ji {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_lift_variant_ptko7np {
    gm_frame h;
    gm_tckdobunaulee l9; /* v */
    gm_tvbp5g7fh5fh0 ret;
};
struct gm_fr_make_err_expr_6arm2q7 {
    gm_frame h;
    void * ret;
};
struct gm_fr_opt_unwrap_3qt46b5 {
    gm_frame h;
    void * ret;
};
struct gm_fr_res_unwrap_e6id52i {
    gm_frame h;
    void * ret;
};
struct gm_fr_try_expr_ntgm5mj {
    gm_frame h;
    gm_t4gut2c9fj1kt l16; /* d */
    gm_trqfvmjtt4sij l17; /* edits */
    void * ret;
};
bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_adds_const_e6d4bkb(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_fn_clause_exit_5an7bnc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_else_after_error_349u6it(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_else_expr_5lgn2qp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tkn42u7na3t81 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_else_join_ri264ji(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void gm_f_format_1va9dpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6h7ot9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9811271(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_avnkm9g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fl4q4sd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nhoqqfq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_orf53np(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vfce7nu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_voqg5cl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_lift_variant_ptko7np(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_list_jekodor(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_make_err_expr_6arm2q7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_opt_unwrap_3qt46b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k96mobn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_res_unwrap_e6id52i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_return_target_ijqt4mp(void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_shallow_8ur803a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_try_expr_ntgm5mj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s21284p79u3jq[14];
extern const uint8_t gm_s23siujajsn8e[111];
extern const uint8_t gm_s4fd35uidvgkn[4];
extern const uint8_t gm_sc30ktgbs45q0[4];
extern const uint8_t gm_slv0rdrsocvrt[111];
extern const uint8_t gm_smv2mfn1ji3ad[58];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr6i6ans386ga[45];
extern const char gm_src2955num0q9tm[];
extern const uint8_t gm_stvgh52bfcgit[52];
extern const uint8_t gm_sv37ng458lg2i[50];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_else_after_error_349u6it(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2) {
    struct gm_fr_else_after_error_349u6it F;
    void * gm_l0; /* cx */
    void * gm_l2; /* rhs */
    gm_t08olq97rscn3 gm_l3; /* c */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)a1))[32] != 0)) goto ie4;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
        gm_l3 = t5;
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
            uint32_t t14 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t7, t8, t12, ((uint8_t)0), t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t14;
        }
        {
            void * t15 = gm_l0;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            uint32_t t17 = gm_l4;
            gm_t930q5l38hdap t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
            gm_b0_1 t20 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t15, t16, &t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        {
            void * t22 = gm_l0;
            void * t23 = gm_l2;
            gm_tkn42u7na3t81 t24;
            memset(&t24, 0, sizeof t24);
            void * t25 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t26 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t26);
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
__attribute__((aligned(4))) void * gm_f_else_expr_5lgn2qp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tkn42u7na3t81 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_else_expr_5lgn2qp F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lhs */
    void * gm_l3; /* rhs */
    gm_tkn42u7na3t81 gm_l4; /* expect */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    void * gm_l7; /* l */
    gm_t88rcucg1b7uk gm_l8; /* lt */
    gm_t88rcucg1b7uk gm_l9; /* present */
    gm_tkn42u7na3t81 gm_l10; /* err */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    gm_t9805qtonbsag gm_l14; /* cap */
    gm_t08olq97rscn3 gm_l15; /* c */
    gm_t88rcucg1b7uk gm_l16; /* e */
    uint32_t gm_l17; /* id */
    gm_tkn42u7na3t81 gm_l18; /* rhs_expect */
    gm_t9qkh0rsooc8d gm_l19; /* op */
    gm_t88rcucg1b7uk gm_l20; /* t */
    gm_t88rcucg1b7uk gm_l21; /* e */
    void * gm_l22; /* r */
    gm_t88rcucg1b7uk gm_l23; /* rt */
    uint8_t gm_l24; /* rewraps */
    gm_t88rcucg1b7uk gm_l25; /* ok */
    gm_t88rcucg1b7uk gm_l26; /* ty */
    gm_t88rcucg1b7uk gm_l27; /* lifted */
    GM_STACK_CHECK(&F, 2320u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        gm_tkn42u7na3t81 t7;
        memset(&t7, 0, sizeof t7);
        void * t8 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 96);
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        void * t13 = gm_l7;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_t88rcucg1b7uk t15 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t10, t12, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t15;
    }
    {
        gm_t88rcucg1b7uk t16 = gm_l8;
        gm_l9 = t16;
    }
    {
        gm_tkn42u7na3t81 t17;
        memset(&t17, 0, sizeof t17);
        gm_l10 = t17;
    }
    {
        void * t18 = gm_l6;
        gm_t88rcucg1b7uk t19 = gm_l8;
        gm_t53at80pfdus3 t20;
        gm_f_kind_jcnt4hm((void *)&t20, t18, GM_CONV(gm_csguankko4lmhd, t19));
        gm_t53at80pfdus3 t21 = t20;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
        switch (t28) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma26; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma26; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma25; default: goto ma26; }
ma23:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
        if (t30 != ((uint8_t)13ULL)) goto mn29;
        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t21) + 8));
        gm_l11 = t31;
        {
            gm_t88rcucg1b7uk t33 = gm_l11;
            gm_l9 = t33;
        }
        goto me22;
mn29:;
ma24:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
        if (t35 != ((uint8_t)14ULL)) goto mn34;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t21) + 8) + 0));
        gm_l12 = t36;
        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t21) + 8) + 4));
        gm_l13 = t37;
        {
            gm_t88rcucg1b7uk t39 = gm_l13;
            gm_l9 = t39;
        }
        {
            gm_t88rcucg1b7uk t40 = gm_l12;
            gm_tkn42u7na3t81 t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41), t40);
            ((uint8_t *)&t41)[4] = 1;
            gm_l10 = t41;
        }
        goto me22;
mn34:;
ma25:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
        if (t43 != ((uint8_t)26ULL)) goto mn42;
        {
            void * t45 = gm_l0;
            void * t46 = gm_l3;
            gm_b0_1 t47 = gm_f_else_after_error_349u6it((void *)NULL, cx, NULL, t45, &(*(gm_t1s75i08qtptd *)((uint8_t *)a2)), t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t48 = gm_l0;
            void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
            gm_tl874t2atnlh3 t50 = gm_l5;
            void * t51 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto me22;
mn42:;
ma26:;
        {
            void * t56 = gm_l6;
            gm_t88rcucg1b7uk t57 = gm_l8;
            uint8_t t58 = gm_f_mentions_error_37knu7r(t56, GM_CONV(gm_csguankko4lmhd, t57));
            uint8_t t59 = (uint8_t)!t58;
            if (!t59) goto ie55;
            {
                void * t61 = gm_l0;
                void * t62 = gm_l1;
                gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 120));
                void * t64 = gm_l0;
                gm_t88rcucg1b7uk t65 = gm_l8;
                gm_b16_8 t66 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t64, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t67;
                GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
                gm_tabb710d9mq2e t68;
                gm_f_format_vfce7nu((void *)&t68, (void *)NULL, cx, NULL, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t69 = t68;
                if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_src2955num0q9tm, 50, 36);
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
                gm_b16_8 t71 = t70;
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                gm_b0_1 t73 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t61, t63, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix55;
ie55:;
ix55:;
        }
        {
            void * t74 = gm_l0;
            void * t75 = gm_l3;
            gm_b0_1 t76 = gm_f_else_after_error_349u6it((void *)NULL, cx, NULL, t74, &(*(gm_t1s75i08qtptd *)((uint8_t *)a2)), t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t77 = gm_l0;
            void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
            gm_tl874t2atnlh3 t79 = gm_l5;
            void * t80 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t81 = 0;
            F.ret = t80;
            goto gm_fret;
        }
        goto me22;
mn53:;
ma27:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 38, 5);
me22:;
    }
    {
        void * t82 = gm_l0;
        gm_b0_1 t83 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t9805qtonbsag t84;
        memset(&t84, 0, sizeof t84);
        gm_l14 = t84;
    }
    {
        if (!((((uint8_t *)a2))[32] != 0)) goto ie85;
        gm_t08olq97rscn3 t86 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a2));
        gm_l15 = t86;
        {
            if (!((((uint8_t *)&gm_l10))[4] != 0)) goto ie88;
            gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l10));
            gm_l16 = t89;
            {
                void * t91 = gm_l0;
                gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
                gm_t88rcucg1b7uk t93 = gm_l16;
                gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l15) + 16));
                uint32_t t95 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t91, t92, t93, ((uint8_t)0), t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t95;
            }
            {
                void * t96 = gm_l0;
                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 0));
                uint32_t t98 = gm_l17;
                gm_t930q5l38hdap t99;
                memset(&t99, 0, sizeof t99);
                GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t99 + 8), t98);
                gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l15) + 16));
                gm_b0_1 t101 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t96, t97, &t99, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint32_t t102 = gm_l17;
                gm_t9805qtonbsag t103;
                GM_ST(uint8_t, (uint8_t *)&t103 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t103 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t103), t102);
                ((uint8_t *)&t103)[4] = 1;
                gm_l14 = t103;
            }
            goto ix88;
ie88:;
            {
                void * t105 = gm_l0;
                gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l15) + 16));
                gm_b16_8 t107;
                GM_ST(void *, (uint8_t *)&t107, (void *)(gm_smv2mfn1ji3ad));
                GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(57));
                gm_b0_1 t108 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t105, t106, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix88:;
        }
        goto ix85;
ie85:;
ix85:;
    }
    {
        gm_t88rcucg1b7uk t109 = gm_l9;
        gm_tkn42u7na3t81 t110;
        GM_ST(uint8_t, (uint8_t *)&t110 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t110 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t110), t109);
        ((uint8_t *)&t110)[4] = 1;
        gm_l18 = t110;
    }
    {
        void * t112 = gm_l3;
        uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t112) + 8) + 0));
        if (t113 != ((uint8_t)12ULL)) goto ie111;
        gm_t9qkh0rsooc8d t114 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t112) + 8) + 8) + 0));
        gm_l19 = t114;
        uint8_t t115 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l19) + 0) == ((uint8_t)3ULL)));
        if (!t115) goto ie111;
        {
            gm_tkn42u7na3t81 t117;
            memset(&t117, 0, sizeof t117);
            gm_l18 = t117;
        }
        {
            if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie118;
            gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
            gm_l20 = t119;
            void * t120 = gm_l6;
            gm_t88rcucg1b7uk t121 = gm_l20;
            gm_t53at80pfdus3 t122;
            gm_f_kind_jcnt4hm((void *)&t122, t120, GM_CONV(gm_csguankko4lmhd, t121));
            gm_t53at80pfdus3 t123 = t122;
            uint8_t t124;
            uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&t123) + 0));
            if (t127 != ((uint8_t)14ULL)) goto mn126;
            t124 = ((uint8_t)1);
            goto me125;
mn126:;
            t124 = ((uint8_t)0);
            goto me125;
mn128:;
            gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 71, 28);
me125:;
            if (!t124) goto ie118;
            {
                gm_t88rcucg1b7uk t130 = gm_l20;
                gm_tkn42u7na3t81 t131;
                GM_ST(uint8_t, (uint8_t *)&t131 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t131 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t131), t130);
                ((uint8_t *)&t131)[4] = 1;
                gm_l18 = t131;
            }
            goto ix118;
ie118:;
            void * t133 = gm_l6;
            void * t134 = gm_l0;
            gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t134) + 168));
            gm_t53at80pfdus3 t136;
            gm_f_kind_jcnt4hm((void *)&t136, t133, GM_CONV(gm_csguankko4lmhd, t135));
            gm_t53at80pfdus3 t137 = t136;
            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t137) + 0));
            if (t138 != ((uint8_t)14ULL)) goto ie132;
            gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t137) + 8) + 0));
            gm_l21 = t139;
            {
                void * t141 = gm_l6;
                gm_t88rcucg1b7uk t142 = gm_l21;
                gm_t88rcucg1b7uk t143 = gm_l9;
                gm_t88rcucg1b7uk t144 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t141, t142, t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t145;
                GM_ST(uint8_t, (uint8_t *)&t145 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t145 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t145), t144);
                ((uint8_t *)&t145)[4] = 1;
                gm_l18 = t145;
            }
            goto ix132;
ie132:;
ix132:;
ix118:;
        }
        goto ix111;
ie111:;
        void * t147 = gm_l3;
        uint8_t t148 = ((GM_LD(uint8_t, (((uint8_t *)t147) + 8) + 0) == ((uint8_t)4ULL)));
        if (!t148) goto ie146;
        uint8_t t149;
        if (((((uint8_t *)&gm_l10))[4] != 0)) goto mn151;
        t149 = ((uint8_t)0);
        goto me150;
mn151:;
        t149 = ((uint8_t)1);
        goto me150;
mn152:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 76, 37);
me150:;
        if (!t149) goto ie146;
        {
            void * t154 = gm_l6;
            gm_t88rcucg1b7uk t155 = gm_l9;
            gm_t88rcucg1b7uk t156 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t157;
            GM_ST(uint8_t, (uint8_t *)&t157 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t157 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t157), t156);
            ((uint8_t *)&t157)[4] = 1;
            gm_l18 = t157;
        }
        goto ix146;
ie146:;
ix146:;
ix111:;
    }
    {
        void * t158 = gm_l0;
        void * t159 = gm_l3;
        gm_tkn42u7na3t81 t160 = gm_l18;
        void * t161 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t158, t159, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t161;
    }
    {
        void * t162 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t162);
    }
    {
        void * t163 = gm_l0;
        void * t164 = (void *)(((uint8_t *)t163) + 96);
        void * t165 = gm_l0;
        void * t166 = GM_LD(void *, (((uint8_t *)t165) + 0));
        void * t167 = gm_l22;
        gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t167) + 0));
        gm_t88rcucg1b7uk t169 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t164, t166, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t169;
    }
    {
        uint8_t t170 = 0;
        uint8_t t171;
        if (((((uint8_t *)&gm_l10))[4] != 0)) goto mn173;
        t171 = ((uint8_t)0);
        goto me172;
mn173:;
        t171 = ((uint8_t)1);
        goto me172;
mn174:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 84, 16);
me172:;
        if (t171) {
            gm_t88rcucg1b7uk t175 = gm_l23;
            void * t176 = gm_l6;
            gm_t88rcucg1b7uk t177 = gm_l9;
            gm_t88rcucg1b7uk t178 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t176, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t179 = t175;
            gm_t88rcucg1b7uk t180 = t178;
            uint8_t t181 = (gm_eq9m6mvtmgde(((uint8_t *)&t179), ((uint8_t *)&t180)));
            t170 = t181;
        } else t170 = 0;
        gm_l24 = t170;
    }
    {
        void * t183 = gm_l6;
        gm_t88rcucg1b7uk t184 = gm_l23;
        gm_t53at80pfdus3 t185;
        gm_f_kind_jcnt4hm((void *)&t185, t183, GM_CONV(gm_csguankko4lmhd, t184));
        gm_t53at80pfdus3 t186 = t185;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&t186) + 0));
        if (t187 != ((uint8_t)14ULL)) goto ie182;
        gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t186) + 8) + 4));
        gm_l25 = t188;
        gm_t88rcucg1b7uk t189 = gm_l25;
        gm_t88rcucg1b7uk t190 = gm_l9;
        gm_t88rcucg1b7uk t191 = t189;
        gm_t88rcucg1b7uk t192 = t190;
        uint8_t t193 = (gm_eq9m6mvtmgde(((uint8_t *)&t191), ((uint8_t *)&t192)));
        if (!t193) goto ie182;
        {
            gm_l24 = ((uint8_t)1);
        }
        goto ix182;
ie182:;
ix182:;
    }
    {
        gm_t88rcucg1b7uk t195 = gm_l9;
        gm_l26 = t195;
    }
    {
        void * t197 = gm_l6;
        gm_t88rcucg1b7uk t198 = gm_l23;
        gm_t53at80pfdus3 t199;
        gm_f_kind_jcnt4hm((void *)&t199, t197, GM_CONV(gm_csguankko4lmhd, t198));
        gm_t53at80pfdus3 t200 = t199;
        uint8_t t201 = ((GM_LD(uint8_t, ((uint8_t *)&t200) + 0) == ((uint8_t)5ULL)));
        if (!t201) goto ie196;
        {
            gm_t88rcucg1b7uk t203 = gm_l9;
            gm_l26 = t203;
        }
        goto ix196;
ie196:;
        gm_t88rcucg1b7uk t205 = gm_l23;
        gm_t88rcucg1b7uk t206 = gm_l8;
        gm_t88rcucg1b7uk t207 = t205;
        gm_t88rcucg1b7uk t208 = t206;
        uint8_t t209 = (gm_eq9m6mvtmgde(((uint8_t *)&t207), ((uint8_t *)&t208)));
        if (!t209) goto ie204;
        uint8_t t210;
        if (((((uint8_t *)&gm_l10))[4] != 0)) goto mn212;
        t210 = ((uint8_t)1);
        goto me211;
mn212:;
        t210 = ((uint8_t)0);
        goto me211;
mn213:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 89, 28);
me211:;
        if (!t210) goto ie204;
        {
            gm_t88rcucg1b7uk t215 = gm_l8;
            gm_l26 = t215;
        }
        goto ix204;
ie204:;
        uint8_t t217 = gm_l24;
        if (!t217) goto ie216;
        {
            gm_t88rcucg1b7uk t219 = gm_l23;
            gm_l26 = t219;
        }
        goto ix216;
ie216:;
        void * t221 = gm_l0;
        gm_t88rcucg1b7uk t222 = gm_l8;
        gm_t88rcucg1b7uk t223 = gm_l9;
        gm_t88rcucg1b7uk t224 = gm_l23;
        gm_tkn42u7na3t81 t225 = gm_f_else_join_ri264ji((void *)NULL, cx, NULL, t221, t222, t223, t224);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t226 = t225;
        if (!((((uint8_t *)&t226))[4] != 0)) goto ie220;
        gm_t88rcucg1b7uk t227 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t226));
        gm_l27 = t227;
        {
            void * t229 = gm_l0;
            void * t230 = gm_l7;
            gm_t88rcucg1b7uk t231 = gm_l27;
            void * t232 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t229, t230, t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t232;
        }
        {
            gm_t88rcucg1b7uk t233 = gm_l23;
            gm_l26 = t233;
        }
        goto ix220;
ie220:;
        {
            void * t235 = gm_l0;
            void * t236 = gm_l22;
            gm_t88rcucg1b7uk t237 = gm_l9;
            void * t238 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t235, t236, t237);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t238;
        }
ix220:;
ix216:;
ix204:;
ix196:;
    }
    {
        gm_t88rcucg1b7uk t239 = gm_l26;
        gm_t3dcoc7077t1q t240;
        void * t241 = gm_l7;
        GM_ST(void *, (uint8_t *)&t240 + 0, t241);
        gm_t9805qtonbsag t242 = gm_l14;
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t240 + 8, t242);
        void * t243 = gm_l22;
        GM_ST(void *, (uint8_t *)&t240 + 16, t243);
        gm_t5brlu7fal70j t244;
        memset(&t244, 0, sizeof t244);
        GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)41ULL));
        GM_ST(gm_t3dcoc7077t1q, ((uint8_t *)&t244 + 8), t240);
        gm_tl874t2atnlh3 t245 = gm_l5;
        void * t246 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t239, &t244, t245);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t247 = 0;
        F.ret = t246;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_else_join_ri264ji(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_else_join_ri264ji F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* lt */
    gm_t88rcucg1b7uk gm_l2; /* present */
    gm_t88rcucg1b7uk gm_l3; /* rt */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* e */
    GM_STACK_CHECK(&F, 406u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint8_t t8 = 0;
        void * t9 = gm_l4;
        gm_t88rcucg1b7uk t10 = gm_l3;
        uint8_t t11 = gm_f_has_infer_3o82a0b(t9, GM_CONV(gm_csguankko4lmhd, t10));
        if (!t11) {
            void * t12 = gm_l4;
            gm_t88rcucg1b7uk t13 = gm_l2;
            uint8_t t14 = gm_f_has_infer_3o82a0b(t12, GM_CONV(gm_csguankko4lmhd, t13));
            t8 = t14;
        } else t8 = 1;
        if (!t8) {
            void * t15 = gm_l4;
            gm_t88rcucg1b7uk t16 = gm_l3;
            uint8_t t17 = gm_f_is_error_8k27gd1(t15, GM_CONV(gm_csguankko4lmhd, t16));
            t7 = t17;
        } else t7 = 1;
        if (!t7) {
            void * t18 = gm_l0;
            gm_t88rcucg1b7uk t19 = gm_l3;
            gm_t88rcucg1b7uk t20 = gm_l2;
            gm_th2lqbo77pvsc t21;
            gm_f_coercion_steps_ibqvhta((void *)&t21, (void *)NULL, cx, NULL, t18, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t22 = t21;
            uint8_t t23;
            if (((((uint8_t *)&t22))[16] != 0)) goto mn25;
            t23 = ((uint8_t)0);
            goto me24;
mn25:;
            t23 = ((uint8_t)1);
            goto me24;
mn26:;
            gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 109, 75);
me24:;
            t6 = t23;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            gm_tkn42u7na3t81 t28;
            memset(&t28, 0, sizeof t28);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
        gm_t88rcucg1b7uk t33 = gm_l2;
        gm_t88rcucg1b7uk t34 = gm_l3;
        uint8_t t35 = gm_f_adds_const_e6d4bkb(t32, GM_CONV(gm_csguankko4lmhd, t33), GM_CONV(gm_csguankko4lmhd, t34));
        uint8_t t36 = (uint8_t)!t35;
        if (!t36) goto ie30;
        {
            gm_tkn42u7na3t81 t38;
            memset(&t38, 0, sizeof t38);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t41 = gm_l4;
        gm_t88rcucg1b7uk t42 = gm_l1;
        gm_t53at80pfdus3 t43;
        gm_f_kind_jcnt4hm((void *)&t43, t41, GM_CONV(gm_csguankko4lmhd, t42));
        gm_t53at80pfdus3 t44 = t43;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
        if (t45 != ((uint8_t)14ULL)) goto ie40;
        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t44) + 8) + 0));
        gm_l5 = t46;
        {
            void * t48 = gm_l4;
            gm_t88rcucg1b7uk t49 = gm_l5;
            gm_t88rcucg1b7uk t50 = gm_l3;
            gm_t88rcucg1b7uk t51 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t48, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[4] = 1;
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        void * t54 = gm_l4;
        gm_t88rcucg1b7uk t55 = gm_l3;
        gm_t88rcucg1b7uk t56 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t57), t56);
        ((uint8_t *)&t57)[4] = 1;
        uint8_t t58 = 0;
        F.ret = t57;
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
__attribute__((aligned(4))) void gm_f_lift_variant_ptko7np(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_lift_variant_ptko7np F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* f */
    gm_tep2drsl9408b gm_l3; /* uv */
    gm_t9805qtonbsag gm_l4; /* found */
    int32_t gm_l5; /* count */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l10; /* p */
    GM_STACK_CHECK(&F, 1321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_tdlpv0q69lov1 t5;
        gm_f_union_variants_a7khaqp((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tdlpv0q69lov1 t6 = t5;
        gm_tep2drsl9408b t7;
        if (((((uint8_t *)&t6))[24] != 0)) {
            gm_tep2drsl9408b t8 = GM_LD(gm_tep2drsl9408b, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_t6j6smj7qoctu t9;
            void * t10 = gm_l0;
            gm_t88rcucg1b7uk t11 = gm_l1;
            gm_b16_8 t12 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t12);
            void * t13 = gm_l0;
            gm_t88rcucg1b7uk t14 = gm_l2;
            gm_b16_8 t15 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 16, t15);
            gm_tabb710d9mq2e t16;
            gm_f_format_6h7ot9t((void *)&t16, (void *)NULL, cx, NULL, &t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_src2955num0q9tm, 186, 51);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = t18;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_tvbp5g7fh5fh0 t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t21 + 8), t20);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        gm_t9805qtonbsag t23;
        memset(&t23, 0, sizeof t23);
        gm_l4 = t23;
    }
    {
        gm_l5 = ((int32_t)0LL);
    }
    {
        {
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t26 = t25;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_l6 = t27;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t29 = gm_l7;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t31 = (t29 < t30);
                if (!t31) goto lb28;
                {
                    gm_t3l8idb2m6c1r t34;
                    uint64_t t35 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t34 + 0, t35);
                    uint64_t t36 = gm_l7;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    memcpy((uint8_t *)&t34 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t37 * 72), 72);
                    gm_t3l8idb2m6c1r t39 = t34;
                    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&t39) + 0));
                    gm_l8 = t42;
                    gm_tckdobunaulee t43 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t39) + 8));
                    F.l9 = t43;
                    {
                        if (!(((((uint8_t *)&F.l9) + 16))[4] != 0)) goto ie45;
                        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                        gm_l10 = t46;
                        gm_t88rcucg1b7uk t47 = gm_l10;
                        gm_t88rcucg1b7uk t48 = gm_l1;
                        gm_t88rcucg1b7uk t49 = t47;
                        gm_t88rcucg1b7uk t50 = t48;
                        uint8_t t51 = (gm_eq9m6mvtmgde(((uint8_t *)&t49), ((uint8_t *)&t50)));
                        if (!t51) goto ie45;
                        {
                            int32_t t54 = gm_l5;
                            uint8_t t55 = ((t54 == ((int32_t)0LL)));
                            if (!t55) goto ie53;
                            {
                                uint64_t t57 = gm_l8;
                                uint32_t t58 = (uint32_t)t57;
                                gm_t9805qtonbsag t59;
                                GM_ST(uint8_t, (uint8_t *)&t59 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t59 + 6, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t59), t58);
                                ((uint8_t *)&t59)[4] = 1;
                                gm_l4 = t59;
                            }
                            goto ix53;
ie53:;
ix53:;
                        }
                        {
                            int32_t t60 = gm_l5;
                            int32_t r61; bool o62 = __builtin_add_overflow(t60, ((int32_t)1LL), &r61);
                            if (o62) gm_trap("integer overflow in `+`", gm_src2955num0q9tm, 192, 13);
                            int32_t t63 = r61;
                            gm_l5 = t63;
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    goto me40;
mn41:;
                    gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 189, 5);
me40:;
                }
lc28:;
                {
                    uint64_t t65 = gm_l7;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l7 = t68;
                }
            }
lb28:;
        }
    }
    {
        int32_t t70 = gm_l5;
        uint8_t t71 = ((t70 == ((int32_t)1LL)));
        if (!t71) goto ie69;
        {
            gm_t9805qtonbsag t73 = gm_l4;
            gm_t9805qtonbsag t74 = t73;
            uint32_t t75;
            if (((((uint8_t *)&t74))[4] != 0)) {
                uint32_t t76 = GM_LD(uint32_t, (((uint8_t *)&t74) + 0));
                t75 = t76;
            } else {
                t75 = ((uint32_t)0ULL);
            }
            gm_tvbp5g7fh5fh0 t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t77 + 8), t75);
            uint8_t t78 = 0;
            F.ret = t77;
            goto gm_fret;
        }
        goto ix69;
ie69:;
ix69:;
    }
    {
        int32_t t80 = gm_l5;
        uint8_t t81 = ((t80 == ((int32_t)0LL)));
        if (!t81) goto ie79;
        {
            gm_t6j6smj7qoctu t83;
            void * t84 = gm_l0;
            gm_t88rcucg1b7uk t85 = gm_l2;
            gm_b16_8 t86 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t83 + 0, t86);
            void * t87 = gm_l0;
            gm_t88rcucg1b7uk t88 = gm_l1;
            gm_b16_8 t89 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t83 + 16, t89);
            gm_tabb710d9mq2e t90;
            gm_f_format_voqg5cl((void *)&t90, (void *)NULL, cx, NULL, &t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t91 = t90;
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_src2955num0q9tm, 196, 33);
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
            gm_b16_8 t93 = t92;
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_tvbp5g7fh5fh0 t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 8), t94);
            uint8_t t96 = 0;
            F.ret = t95;
            goto gm_fret;
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        gm_t6j6smj7qoctu t97;
        void * t98 = gm_l0;
        gm_t88rcucg1b7uk t99 = gm_l2;
        gm_b16_8 t100 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t98, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t97 + 0, t100);
        void * t101 = gm_l0;
        gm_t88rcucg1b7uk t102 = gm_l1;
        gm_b16_8 t103 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t97 + 16, t103);
        gm_tabb710d9mq2e t104;
        gm_f_format_fl4q4sd((void *)&t104, (void *)NULL, cx, NULL, &t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t105 = t104;
        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_src2955num0q9tm, 197, 17);
        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
        gm_b16_8 t107 = t106;
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
        gm_tvbp5g7fh5fh0 t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)1ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t109 + 8), t108);
        uint8_t t110 = 0;
        F.ret = t109;
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
__attribute__((aligned(4))) void * gm_f_make_err_expr_6arm2q7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_make_err_expr_6arm2q7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* operand */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t88rcucg1b7uk gm_l7; /* err */
    void * gm_l8; /* v */
    gm_t88rcucg1b7uk gm_l9; /* vt */
    uint32_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* hint */
    GM_STACK_CHECK(&F, 1348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        if (!((((uint8_t *)&gm_l2))[4] != 0)) goto ie5;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
        gm_l5 = t6;
        {
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 96);
            void * t10 = gm_l4;
            gm_t88rcucg1b7uk t11 = gm_l5;
            gm_t88rcucg1b7uk t12 = gm_f_shallow_8ur803a((void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t12;
        }
        {
            void * t14 = gm_l4;
            gm_t88rcucg1b7uk t15 = gm_l6;
            gm_t53at80pfdus3 t16;
            gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
            gm_t53at80pfdus3 t17 = t16;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
            if (t18 != ((uint8_t)14ULL)) goto ie13;
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t17) + 8) + 0));
            gm_l7 = t19;
            {
                void * t21 = gm_l0;
                void * t22 = gm_l1;
                gm_t88rcucg1b7uk t23 = gm_l7;
                gm_tkn42u7na3t81 t24;
                GM_ST(uint8_t, (uint8_t *)&t24 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t24 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t24), t23);
                ((uint8_t *)&t24)[4] = 1;
                void * t25 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t21, t22, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t25;
            }
            {
                void * t26 = gm_l0;
                void * t27 = (void *)(((uint8_t *)t26) + 96);
                void * t28 = gm_l0;
                void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                void * t30 = gm_l8;
                gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t30) + 0));
                gm_t88rcucg1b7uk t32 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t27, t29, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t32;
            }
            {
                void * t34 = gm_l4;
                gm_t88rcucg1b7uk t35 = gm_l9;
                uint8_t t36 = gm_f_has_infer_3o82a0b(t34, GM_CONV(gm_csguankko4lmhd, t35));
                uint8_t t37 = (uint8_t)!t36;
                if (!t37) goto ie33;
                gm_t88rcucg1b7uk t38 = gm_l9;
                gm_t88rcucg1b7uk t39 = gm_l7;
                gm_t88rcucg1b7uk t40 = t38;
                gm_t88rcucg1b7uk t41 = t39;
                uint8_t t42 = (!gm_eq9m6mvtmgde(((uint8_t *)&t40), ((uint8_t *)&t41)));
                if (!t42) goto ie33;
                void * t43 = gm_l0;
                gm_t88rcucg1b7uk t44 = gm_l9;
                gm_t88rcucg1b7uk t45 = gm_l7;
                gm_th2lqbo77pvsc t46;
                gm_f_coercion_steps_ibqvhta((void *)&t46, (void *)NULL, cx, NULL, t43, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t47 = t46;
                uint8_t t48;
                if (((((uint8_t *)&t47))[16] != 0)) goto mn50;
                t48 = ((uint8_t)1);
                goto me49;
mn50:;
                t48 = ((uint8_t)0);
                goto me49;
mn51:;
                gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 209, 60);
me49:;
                if (!t48) goto ie33;
                {
                    void * t53 = gm_l0;
                    gm_t88rcucg1b7uk t54 = gm_l9;
                    gm_t88rcucg1b7uk t55 = gm_l7;
                    gm_tvbp5g7fh5fh0 t56;
                    gm_f_lift_variant_ptko7np((void *)&t56, (void *)NULL, cx, NULL, t53, t54, t55);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tvbp5g7fh5fh0 t57 = t56;
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
                    if (t60 != ((uint8_t)0ULL)) goto mn59;
                    uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&t57) + 8));
                    gm_l10 = t61;
                    {
                        gm_t88rcucg1b7uk t63 = gm_l7;
                        uint32_t t64 = gm_l10;
                        void * t65 = gm_l8;
                        gm_ttggq7ng4kp3p t66;
                        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t66 + 0), t64);
                        GM_ST(void *, ((uint8_t *)&t66 + 8), t65);
                        gm_t5brlu7fal70j t67;
                        memset(&t67, 0, sizeof t67);
                        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)38ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t67 + 8), t66);
                        gm_tl874t2atnlh3 t68 = gm_l3;
                        void * t69 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t63, &t67, t68);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t69;
                    }
                    goto me58;
mn59:;
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
                    if (t71 != ((uint8_t)1ULL)) goto mn70;
                    {
                        void * t73 = gm_l0;
                        void * t74 = gm_l8;
                        gm_t88rcucg1b7uk t75 = gm_l7;
                        void * t76 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t73, t74, t75);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t76;
                    }
                    goto me58;
mn70:;
                    gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 210, 17);
me58:;
                }
                goto ix33;
ie33:;
                {
                    void * t78 = gm_l0;
                    void * t79 = gm_l8;
                    gm_t88rcucg1b7uk t80 = gm_l7;
                    void * t81 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t78, t79, t80);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t81;
                }
ix33:;
            }
            {
                gm_t88rcucg1b7uk t82 = gm_l6;
                void * t83 = gm_l8;
                gm_t5brlu7fal70j t84;
                memset(&t84, 0, sizeof t84);
                GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)39ULL));
                GM_ST(void *, ((uint8_t *)&t84 + 8), t83);
                gm_tl874t2atnlh3 t85 = gm_l3;
                void * t86 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t82, &t84, t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t87 = 0;
                F.ret = t86;
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(0));
        gm_l11 = t88;
    }
    {
        uint8_t t90 = 0;
        uint8_t t91;
        if (((((uint8_t *)&gm_l2))[4] != 0)) goto mn93;
        t91 = ((uint8_t)1);
        goto me92;
mn93:;
        t91 = ((uint8_t)0);
        goto me92;
mn94:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 221, 8);
me92:;
        if (!t91) {
            gm_tkn42u7na3t81 t95 = gm_l2;
            void * t96 = gm_l4;
            gm_t88rcucg1b7uk t97 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t98;
            GM_ST(uint8_t, (uint8_t *)&t98 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t98 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t98), t97);
            ((uint8_t *)&t98)[4] = 1;
            gm_tkn42u7na3t81 t99 = t95;
            gm_tkn42u7na3t81 t100 = t98;
            uint8_t t101 = (gm_eqs65r2l0loh(((uint8_t *)&t99), ((uint8_t *)&t100)));
            t90 = t101;
        } else t90 = 1;
        if (!t90) goto ie89;
        {
            gm_b16_8 t103;
            GM_ST(void *, (uint8_t *)&t103, (void *)(gm_stvgh52bfcgit));
            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(51));
            gm_l11 = t103;
        }
        goto ix89;
ie89:;
ix89:;
    }
    {
        void * t104 = gm_l0;
        gm_tl874t2atnlh3 t105 = gm_l3;
        gm_b16_8 t106 = gm_l11;
        gm_tnpifv20irnph t107;
        GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t106);
        gm_tabb710d9mq2e t108;
        gm_f_format_avnkm9g((void *)&t108, (void *)NULL, cx, NULL, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t109 = t108;
        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_src2955num0q9tm, 222, 20);
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
        gm_b16_8 t111 = t110;
        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
        gm_b0_1 t113 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t104, t105, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t114 = gm_l0;
        void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
        gm_tl874t2atnlh3 t116 = gm_l3;
        void * t117 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t115, t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t118 = 0;
        F.ret = t117;
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
inline __attribute__((aligned(4))) void * gm_f_opt_unwrap_3qt46b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_opt_unwrap_3qt46b5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* inner */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* x */
    GM_STACK_CHECK(&F, 770u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tkn42u7na3t81 t4;
        memset(&t4, 0, sizeof t4);
        void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 96);
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_t88rcucg1b7uk t12 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t7, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t12;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l4;
        gm_t53at80pfdus3 t17;
        gm_f_kind_jcnt4hm((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
        gm_t53at80pfdus3 t18 = t17;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma22; case ((uint8_t)15ULL): goto ma22; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma22; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma22; case ((uint8_t)21ULL): goto ma22; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma22; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma22; case ((uint8_t)26ULL): goto ma21; default: goto ma22; }
ma20:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t26 != ((uint8_t)13ULL)) goto mn25;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t18) + 8));
        gm_l5 = t27;
        gm_t88rcucg1b7uk t28 = gm_l5;
        void * t29 = gm_l3;
        gm_tsplfr33dsvji t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t30 + 0), t29);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 8), ((uint8_t)0));
        gm_t5brlu7fal70j t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)40ULL));
        GM_ST(gm_tsplfr33dsvji, ((uint8_t *)&t31 + 8), t30);
        gm_tl874t2atnlh3 t32 = gm_l2;
        void * t33 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t28, &t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
        goto me19;
mn25:;
ma21:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t36 != ((uint8_t)26ULL)) goto mn35;
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        gm_tl874t2atnlh3 t39 = gm_l2;
        void * t40 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = 0;
        F.ret = t40;
        goto gm_fret;
        goto me19;
mn35:;
ma22:;
        {
            void * t44 = gm_l0;
            gm_tl874t2atnlh3 t45 = gm_l2;
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l4;
            gm_b16_8 t48 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t49;
            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
            gm_tabb710d9mq2e t50;
            gm_f_format_1va9dpi((void *)&t50, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t51 = t50;
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src2955num0q9tm, 11, 28);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
            gm_b16_8 t53 = t52;
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t44, t45, t54);
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
        goto me19;
mn42:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 7, 5);
me19:;
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
inline __attribute__((aligned(4))) void * gm_f_res_unwrap_e6id52i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_res_unwrap_e6id52i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* inner */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* ok */
    GM_STACK_CHECK(&F, 770u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_tkn42u7na3t81 t4;
        memset(&t4, 0, sizeof t4);
        void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 96);
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_t88rcucg1b7uk t12 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t7, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t12;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l4;
        gm_t53at80pfdus3 t17;
        gm_f_kind_jcnt4hm((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
        gm_t53at80pfdus3 t18 = t17;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma22; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma22; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma22; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma22; case ((uint8_t)21ULL): goto ma22; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma22; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma22; case ((uint8_t)26ULL): goto ma21; default: goto ma22; }
ma20:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t26 != ((uint8_t)14ULL)) goto mn25;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t18) + 8) + 4));
        gm_l5 = t27;
        gm_t88rcucg1b7uk t28 = gm_l5;
        void * t29 = gm_l3;
        gm_tsplfr33dsvji t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t30 + 0), t29);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 8), ((uint8_t)1));
        gm_t5brlu7fal70j t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)40ULL));
        GM_ST(gm_tsplfr33dsvji, ((uint8_t *)&t31 + 8), t30);
        gm_tl874t2atnlh3 t32 = gm_l2;
        void * t33 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t28, &t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
        goto me19;
mn25:;
ma21:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
        if (t36 != ((uint8_t)26ULL)) goto mn35;
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        gm_tl874t2atnlh3 t39 = gm_l2;
        void * t40 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = 0;
        F.ret = t40;
        goto gm_fret;
        goto me19;
mn35:;
ma22:;
        {
            void * t44 = gm_l0;
            gm_tl874t2atnlh3 t45 = gm_l2;
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l4;
            gm_b16_8 t48 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t49;
            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
            gm_tabb710d9mq2e t50;
            gm_f_format_nhoqqfq((void *)&t50, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t51 = t50;
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src2955num0q9tm, 25, 28);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
            gm_b16_8 t53 = t52;
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
            gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t44, t45, t54);
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
        goto me19;
mn42:;
ma23:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 21, 5);
me19:;
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
__attribute__((aligned(4))) void * gm_f_try_expr_ntgm5mj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_try_expr_ntgm5mj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* operand */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    void * gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tplpbqf3duvn3 gm_l6; /* target */
    uint32_t gm_l7; /* levels */
    gm_t88rcucg1b7uk gm_l8; /* ret */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* err */
    gm_t88rcucg1b7uk gm_l11; /* ok */
    gm_t88rcucg1b7uk gm_l12; /* f */
    gm_t9805qtonbsag gm_l13; /* lift */
    uint32_t gm_l14; /* i */
    gm_b16_8 gm_l15; /* msg */
    GM_STACK_CHECK(&F, 2615u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 520));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_tl874t2atnlh3 t7 = gm_l2;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr6i6ans386ga));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(44));
            gm_b0_1 t9 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_l3 = t12;
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l1;
        gm_tkn42u7na3t81 t15;
        memset(&t15, 0, sizeof t15);
        void * t16 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t16;
    }
    {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 96);
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = gm_l4;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
        gm_t88rcucg1b7uk t23 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t23;
    }
    {
        void * t24 = gm_l0;
        gm_tplpbqf3duvn3 t25 = gm_f_return_target_ijqt4mp(t24);
        gm_l6 = t25;
    }
    {
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
        gm_l7 = t26;
    }
    {
        void * t27 = gm_l0;
        uint32_t t28 = gm_l7;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s4fd35uidvgkn));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
        gm_tl874t2atnlh3 t30 = gm_l2;
        gm_b0_1 t31 = gm_f_check_fn_clause_exit_5an7bnc((void *)NULL, cx, NULL, t27, t28, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t33 = gm_l7;
        uint8_t t34 = (t33 > ((uint32_t)0ULL));
        if (!t34) goto ie32;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 576) + 0) + 8);
        uint32_t t37 = gm_l7;
        uint64_t t38 = (uint64_t)t37;
        uint64_t r39; bool o40 = __builtin_sub_overflow(t36, t38, &r39);
        if (o40) gm_trap("integer overflow in `-`", gm_src2955num0q9tm, 137, 38);
        uint64_t t41 = r39;
        uint64_t t42 = (uintptr_t)t41;
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uintptr_t, ((((uint8_t *)t43) + 576) + 0) + 8);
        if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_src2955num0q9tm, 137, 23);
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t43) + 576) + 0)) + t42 * 384) + 169));
        if (!t45) goto ie32;
        {
            void * t47 = gm_l0;
            gm_tl874t2atnlh3 t48 = gm_l2;
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sv37ng458lg2i));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(49));
            void * t50 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t47, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 4));
        gm_l8 = t52;
    }
    {
        void * t53 = gm_l3;
        gm_t88rcucg1b7uk t54 = gm_l5;
        gm_t53at80pfdus3 t55;
        gm_f_kind_jcnt4hm((void *)&t55, t53, GM_CONV(gm_csguankko4lmhd, t54));
        gm_t53at80pfdus3 t56 = t55;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        switch (t63) { case ((uint8_t)0ULL): goto ma61; case ((uint8_t)1ULL): goto ma61; case ((uint8_t)2ULL): goto ma61; case ((uint8_t)3ULL): goto ma61; case ((uint8_t)4ULL): goto ma61; case ((uint8_t)5ULL): goto ma61; case ((uint8_t)6ULL): goto ma61; case ((uint8_t)7ULL): goto ma61; case ((uint8_t)8ULL): goto ma61; case ((uint8_t)9ULL): goto ma61; case ((uint8_t)10ULL): goto ma61; case ((uint8_t)11ULL): goto ma61; case ((uint8_t)12ULL): goto ma61; case ((uint8_t)13ULL): goto ma59; case ((uint8_t)14ULL): goto ma60; case ((uint8_t)15ULL): goto ma61; case ((uint8_t)16ULL): goto ma61; case ((uint8_t)17ULL): goto ma61; case ((uint8_t)18ULL): goto ma61; case ((uint8_t)19ULL): goto ma61; case ((uint8_t)20ULL): goto ma61; case ((uint8_t)21ULL): goto ma61; case ((uint8_t)22ULL): goto ma61; case ((uint8_t)23ULL): goto ma61; case ((uint8_t)24ULL): goto ma61; case ((uint8_t)25ULL): goto ma61; case ((uint8_t)26ULL): goto ma58; default: goto ma61; }
ma58:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t65 != ((uint8_t)26ULL)) goto mn64;
        void * t66 = gm_l0;
        void * t67 = GM_LD(void *, (((uint8_t *)t66) + 0));
        gm_tl874t2atnlh3 t68 = gm_l2;
        void * t69 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t67, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t70 = 0;
        F.ret = t69;
        goto gm_fret;
        goto me57;
mn64:;
ma59:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t72 != ((uint8_t)13ULL)) goto mn71;
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t56) + 8));
        gm_l9 = t73;
        void * t75 = gm_l3;
        gm_t88rcucg1b7uk t76 = gm_l8;
        gm_t53at80pfdus3 t77;
        gm_f_kind_jcnt4hm((void *)&t77, t75, GM_CONV(gm_csguankko4lmhd, t76));
        gm_t53at80pfdus3 t78 = t77;
        uint8_t t79;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t78) + 0));
        if (t82 != ((uint8_t)13ULL)) goto mn81;
        t79 = ((uint8_t)1);
        goto me80;
mn81:;
        t79 = ((uint8_t)0);
        goto me80;
mn83:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 143, 28);
me80:;
        if (!t79) goto ie74;
        {
            gm_t88rcucg1b7uk t85 = gm_l9;
            gm_t083kd7b1kkag t86;
            memset((uint8_t *)&t86 + 20, 0, 4);
            void * t87 = gm_l4;
            GM_ST(void *, (uint8_t *)&t86 + 0, t87);
            gm_t9805qtonbsag t88;
            memset(&t88, 0, sizeof t88);
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t86 + 8, t88);
            uint32_t t89 = gm_l7;
            GM_ST(uint32_t, (uint8_t *)&t86 + 16, t89);
            gm_t5brlu7fal70j t90;
            memset(&t90, 0, sizeof t90);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)42ULL));
            GM_ST(gm_t083kd7b1kkag, ((uint8_t *)&t90 + 8), t86);
            gm_tl874t2atnlh3 t91 = gm_l2;
            void * t92 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t85, &t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t93 = 0;
            F.ret = t92;
            goto gm_fret;
        }
        goto ix74;
ie74:;
ix74:;
        goto me57;
mn71:;
ma60:;
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t95 != ((uint8_t)14ULL)) goto mn94;
        gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t56) + 8) + 0));
        gm_l10 = t96;
        gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t56) + 8) + 4));
        gm_l11 = t97;
        void * t99 = gm_l3;
        gm_t88rcucg1b7uk t100 = gm_l8;
        gm_t53at80pfdus3 t101;
        gm_f_kind_jcnt4hm((void *)&t101, t99, GM_CONV(gm_csguankko4lmhd, t100));
        gm_t53at80pfdus3 t102 = t101;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t102) + 0));
        if (t103 != ((uint8_t)14ULL)) goto ie98;
        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t102) + 8) + 0));
        gm_l12 = t104;
        {
            gm_t9805qtonbsag t106;
            memset(&t106, 0, sizeof t106);
            gm_l13 = t106;
        }
        {
            gm_t88rcucg1b7uk t108 = gm_l10;
            gm_t88rcucg1b7uk t109 = gm_l12;
            gm_t88rcucg1b7uk t110 = t108;
            gm_t88rcucg1b7uk t111 = t109;
            uint8_t t112 = (!gm_eq9m6mvtmgde(((uint8_t *)&t110), ((uint8_t *)&t111)));
            if (!t112) goto ie107;
            {
                void * t114 = gm_l0;
                gm_t88rcucg1b7uk t115 = gm_l10;
                gm_t88rcucg1b7uk t116 = gm_l12;
                gm_tvbp5g7fh5fh0 t117;
                gm_f_lift_variant_ptko7np((void *)&t117, (void *)NULL, cx, NULL, t114, t115, t116);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvbp5g7fh5fh0 t118 = t117;
                uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t118) + 0));
                if (t121 != ((uint8_t)0ULL)) goto mn120;
                uint32_t t122 = GM_LD(uint32_t, (((uint8_t *)&t118) + 8));
                gm_l14 = t122;
                {
                    uint32_t t124 = gm_l14;
                    gm_t9805qtonbsag t125;
                    GM_ST(uint8_t, (uint8_t *)&t125 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t125 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t125), t124);
                    ((uint8_t *)&t125)[4] = 1;
                    gm_l13 = t125;
                }
                goto me119;
mn120:;
                uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&t118) + 0));
                if (t127 != ((uint8_t)1ULL)) goto mn126;
                gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                gm_l15 = t128;
                void * t129 = gm_l0;
                gm_tl874t2atnlh3 t130 = gm_l2;
                gm_b16_8 t131 = gm_l15;
                gm_b0_1 t132 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t129, t130, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me119;
mn126:;
                gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 149, 17);
me119:;
            }
            goto ix107;
ie107:;
ix107:;
        }
        {
            gm_t88rcucg1b7uk t133 = gm_l11;
            gm_t083kd7b1kkag t134;
            memset((uint8_t *)&t134 + 20, 0, 4);
            void * t135 = gm_l4;
            GM_ST(void *, (uint8_t *)&t134 + 0, t135);
            gm_t9805qtonbsag t136 = gm_l13;
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t134 + 8, t136);
            uint32_t t137 = gm_l7;
            GM_ST(uint32_t, (uint8_t *)&t134 + 16, t137);
            gm_t5brlu7fal70j t138;
            memset(&t138, 0, sizeof t138);
            GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)42ULL));
            GM_ST(gm_t083kd7b1kkag, ((uint8_t *)&t138 + 8), t134);
            gm_tl874t2atnlh3 t139 = gm_l2;
            void * t140 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t133, &t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t141 = 0;
            F.ret = t140;
            goto gm_fret;
        }
        goto ix98;
ie98:;
ix98:;
        goto me57;
mn94:;
ma61:;
        {
            void * t144 = gm_l0;
            gm_tl874t2atnlh3 t145 = gm_l2;
            void * t146 = gm_l0;
            gm_t88rcucg1b7uk t147 = gm_l5;
            gm_b16_8 t148 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t149;
            GM_ST(gm_b16_8, ((uint8_t *)&t149 + 0), t148);
            gm_tabb710d9mq2e t150;
            gm_f_format_orf53np((void *)&t150, (void *)NULL, cx, NULL, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t151 = t150;
            if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 8, gm_src2955num0q9tm, 157, 28);
            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&t151) + 8));
            gm_b16_8 t153 = t152;
            gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
            gm_b0_1 t155 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t144, t145, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t156 = gm_l0;
            void * t157 = GM_LD(void *, (((uint8_t *)t156) + 0));
            gm_tl874t2atnlh3 t158 = gm_l2;
            void * t159 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t157, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t160 = 0;
            F.ret = t159;
            goto gm_fret;
        }
        goto me57;
mn142:;
ma62:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 141, 5);
me57:;
    }
    {
        void * t162 = gm_l3;
        gm_t88rcucg1b7uk t163 = gm_l8;
        gm_t53at80pfdus3 t164;
        gm_f_kind_jcnt4hm((void *)&t164, t162, GM_CONV(gm_csguankko4lmhd, t163));
        gm_t53at80pfdus3 t165 = t164;
        uint8_t t166;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t165) + 0));
        switch (t172) { case ((uint8_t)0ULL): goto ma170; case ((uint8_t)1ULL): goto ma170; case ((uint8_t)2ULL): goto ma170; case ((uint8_t)3ULL): goto ma170; case ((uint8_t)4ULL): goto ma170; case ((uint8_t)5ULL): goto ma170; case ((uint8_t)6ULL): goto ma170; case ((uint8_t)7ULL): goto ma170; case ((uint8_t)8ULL): goto ma170; case ((uint8_t)9ULL): goto ma170; case ((uint8_t)10ULL): goto ma170; case ((uint8_t)11ULL): goto ma170; case ((uint8_t)12ULL): goto ma170; case ((uint8_t)13ULL): goto ma168; case ((uint8_t)14ULL): goto ma169; case ((uint8_t)15ULL): goto ma170; case ((uint8_t)16ULL): goto ma170; case ((uint8_t)17ULL): goto ma170; case ((uint8_t)18ULL): goto ma170; case ((uint8_t)19ULL): goto ma170; case ((uint8_t)20ULL): goto ma170; case ((uint8_t)21ULL): goto ma170; case ((uint8_t)22ULL): goto ma170; case ((uint8_t)23ULL): goto ma170; case ((uint8_t)24ULL): goto ma170; case ((uint8_t)25ULL): goto ma170; case ((uint8_t)26ULL): goto ma170; default: goto ma170; }
ma168:;
        uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&t165) + 0));
        if (t174 != ((uint8_t)13ULL)) goto mn173;
        t166 = ((uint8_t)1);
        goto me167;
mn173:;
ma169:;
        uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&t165) + 0));
        if (t176 != ((uint8_t)14ULL)) goto mn175;
        t166 = ((uint8_t)1);
        goto me167;
mn175:;
ma170:;
        t166 = ((uint8_t)0);
        goto me167;
mn177:;
ma171:;
        gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 161, 8);
me167:;
        if (!t166) goto ie161;
        {
            void * t179 = gm_l0;
            gm_tl874t2atnlh3 t180 = gm_l2;
            gm_b16_8 t181;
            GM_ST(void *, (uint8_t *)&t181, (void *)(gm_s23siujajsn8e));
            GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(110));
            gm_b0_1 t182 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t179, t180, t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix161;
ie161:;
        void * t184 = gm_l0;
        void * t185 = GM_LD(void *, (((uint8_t *)t184) + 0));
        uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)t185) + 672));
        if (!t186) goto ie183;
        uint32_t t187 = gm_l7;
        uint64_t t188 = (uint64_t)t187;
        void * t189 = gm_l0;
        uint64_t t190 = GM_LD(uintptr_t, ((((uint8_t *)t189) + 576) + 0) + 8);
        uint8_t t191 = ((t188 == t190));
        if (!t191) goto ie183;
        {
            gm_tl874t2atnlh3 t193 = gm_l2;
            gm_b16_8 t194;
            GM_ST(void *, (uint8_t *)&t194, (void *)(gm_slv0rdrsocvrt));
            GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(110));
            gm_f_error_s6hn871((void *)&F.l16, (void *)NULL, cx, NULL, t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t196 = gm_l3;
            gm_t88rcucg1b7uk t197 = gm_l5;
            gm_t53at80pfdus3 t198;
            gm_f_kind_jcnt4hm((void *)&t198, t196, GM_CONV(gm_csguankko4lmhd, t197));
            gm_t53at80pfdus3 t199 = t198;
            uint8_t t200;
            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&t199) + 0));
            if (t203 != ((uint8_t)14ULL)) goto mn202;
            t200 = ((uint8_t)1);
            goto me201;
mn202:;
            t200 = ((uint8_t)0);
            goto me201;
mn204:;
            gm_trap("no match arm matched the scrutinee", gm_src2955num0q9tm, 166, 12);
me201:;
            if (!t200) goto ie195;
            {
                gm_f_list_jekodor((void *)&F.l17, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t206 = (void *)((uint8_t *)&F.l17);
                gm_tbgd14esf0jvi t207;
                memset((uint8_t *)&t207 + 12, 0, 4);
                uint32_t t208 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                uint32_t t209 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                void * t210 = gm_l1;
                uint32_t t211 = GM_LD(uint32_t, ((((uint8_t *)t210) + 120) + 4));
                gm_tl874t2atnlh3 t212;
                GM_ST(uint32_t, ((uint8_t *)&t212 + 0), t208);
                GM_ST(uint32_t, ((uint8_t *)&t212 + 4), t209);
                GM_ST(uint32_t, ((uint8_t *)&t212 + 8), t211);
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t207 + 0, t212);
                gm_b16_8 t213;
                GM_ST(void *, (uint8_t *)&t213, (void *)(gm_sc30ktgbs45q0));
                GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&t207 + 16, t213);
                gm_tftb1odc739t5 t214 = gm_f_push_k96mobn(t206, &t207);
                gm_tftb1odc739t5 t215 = t214;
                if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 1, gm_src2955num0q9tm, 168, 13);
                gm_b0_1 t216 = GM_LD(gm_b0_1, (((uint8_t *)&t215) + 1));
            }
            {
                void * t217 = (void *)((uint8_t *)&F.l17);
                gm_tbgd14esf0jvi t218;
                memset((uint8_t *)&t218 + 12, 0, 4);
                uint32_t t219 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
                void * t220 = gm_l1;
                uint32_t t221 = GM_LD(uint32_t, ((((uint8_t *)t220) + 120) + 8));
                void * t222 = gm_l1;
                uint32_t t223 = GM_LD(uint32_t, ((((uint8_t *)t222) + 120) + 8));
                gm_tl874t2atnlh3 t224;
                GM_ST(uint32_t, ((uint8_t *)&t224 + 0), t219);
                GM_ST(uint32_t, ((uint8_t *)&t224 + 4), t221);
                GM_ST(uint32_t, ((uint8_t *)&t224 + 8), t223);
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t218 + 0, t224);
                gm_b16_8 t225;
                GM_ST(void *, (uint8_t *)&t225, (void *)(gm_s1asl1e1tvb0k));
                GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(1));
                GM_ST(gm_b16_8, (uint8_t *)&t218 + 16, t225);
                gm_tftb1odc739t5 t226 = gm_f_push_k96mobn(t217, &t218);
                gm_tftb1odc739t5 t227 = t226;
                if (((((uint8_t *)&t227))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t227) + 1, gm_src2955num0q9tm, 169, 13);
                gm_b0_1 t228 = GM_LD(gm_b0_1, (((uint8_t *)&t227) + 1));
            }
            {
                void * t229 = (void *)((uint8_t *)&F.l16);
                gm_b16_8 t230;
                GM_ST(void *, (uint8_t *)&t230, (void *)(gm_s21284p79u3jq));
                GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(13));
                gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                gm_b16_8 t232 = t231;
                gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
                gm_b0_1 t234 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t229, t230, t233);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix195;
ie195:;
ix195:;
        }
        {
            void * t235 = gm_l0;
            void * t236 = GM_LD(void *, (((uint8_t *)t235) + 0));
            gm_b0_1 t237 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t236, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l16)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix183;
ie183:;
        {
            void * t239 = gm_l0;
            gm_tl874t2atnlh3 t240 = gm_l2;
            void * t241 = gm_l0;
            gm_t88rcucg1b7uk t242 = gm_l8;
            gm_b16_8 t243 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t241, t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t244;
            GM_ST(gm_b16_8, ((uint8_t *)&t244 + 0), t243);
            gm_tabb710d9mq2e t245;
            gm_f_format_9811271((void *)&t245, (void *)NULL, cx, NULL, t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t246 = t245;
            if (((((uint8_t *)&t246))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t246) + 8, gm_src2955num0q9tm, 174, 24);
            gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&t246) + 8));
            gm_b16_8 t248 = t247;
            gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&t248));
            gm_b0_1 t250 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t239, t240, t249);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix183:;
ix161:;
    }
    {
        void * t251 = gm_l0;
        void * t252 = GM_LD(void *, (((uint8_t *)t251) + 0));
        gm_tl874t2atnlh3 t253 = gm_l2;
        void * t254 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t252, t253);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t255 = 0;
        F.ret = t254;
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

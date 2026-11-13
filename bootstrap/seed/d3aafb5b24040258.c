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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint64_t f0; gm_t0ut88vjdgrt1 f1; } gm_to24qukftd66u;
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to24qukftd66u) == 48 && _Alignof(gm_to24qukftd66u) == 8 && offsetof(gm_to24qukftd66u, f0) == 0 && offsetof(gm_to24qukftd66u, f1) == 8, "layout of `(i: usize, x: MonoRequest)`");
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_codegen_body_tpctt8p {
    gm_frame h;
    gm_tqqhoivahirr0 l3; /* sig */
    gm_t0ut88vjdgrt1 l8; /* r */
    gm_t0ut88vjdgrt1 l9; /* req */
    gm_b0_1 ret;
};
struct gm_fr_entry_point_n2gu79u {
    gm_frame h;
    void * l0; /* s */
    gm_tqqhoivahirr0 l2; /* sig */
    gm_trqfvmjtt4sij l4; /* providers */
    gm_trqfvmjtt4sij l6; /* handlers */
    gm_trqfvmjtt4sij l7; /* handled */
    gm_trqfvmjtt4sij l20; /* rest */
    uint32_t ret;
};
struct gm_fr_fn_olk2bm3 {
    gm_frame h;
    gm_t710lsrq1m8gk l5; /* w */
    gm_b0_1 ret;
};
struct gm_fr_performs_e7lpa6l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_prepare_codegen_4hnlio1 {
    gm_frame h;
    void * l0; /* s */
    gm_t4c7ipjj9s3fp l2; /* seen */
    gm_trqfvmjtt4sij l3; /* work */
    gm_tdgfduqjheqdm l9; /* def */
    gm_tq7qjpkmqmc5s l15; /* gl */
    gm_t6evimt84lndq l17; /* v */
    uint8_t ret;
};
struct gm_fr_value_fns_d67c1v1 {
    gm_frame h;
    gm_t6evimt84lndq l0; /* v */
    gm_t6evimt84lndq l6; /* x */
    gm_b0_1 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_codegen_body_tpctt8p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
__attribute__((aligned(4))) uint8_t gm_f_ensure_body_tbtojbv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_entry_point_n2gu79u(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_eval_typed_m82ie8p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_olk2bm3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_dk5gh28(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handlers_of_type_5gareb4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_15vqrvd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) uint8_t gm_f_performs_e7lpa6l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_prepare_codegen_4hnlio1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_produce_instance_kcqgj3e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_remove_kt0vu6d(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_value_fns_d67c1v1(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_k75gp9b9r49[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1qrqfh656g4q[6];
extern const uint8_t gm_s2oe2s4851fv5[4];
extern const uint8_t gm_s4gt1buiaiaiq[3];
extern const uint8_t gm_s7tvcka4r1prv[10];
extern const uint8_t gm_sdlumbvroiufa[3];
extern const uint8_t gm_sihs6d097ocev[5];
extern const char gm_srcnhb827gevj6u[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stgqtu1f7bco8[8];
extern const uint8_t gm_sub9t1fj6sv37[6];
extern const uint8_t gm_suhbskqupk9jm[15];
extern const uint8_t gm_suhl0206dfbdm[5];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l1 = t3;
    }
    {
        uint64_t t5 = gm_l1;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_t9805qtonbsag t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
        gm_l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_codegen_body_tpctt8p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_codegen_body_tpctt8p F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tiilmfd16v3fc gm_l4; /* inst */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    GM_STACK_CHECK(&F, 963u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_tdgfduqjheqdm t5;
        gm_f_def_of_m512sbu((void *)&t5, t3, t4);
        gm_tdgfduqjheqdm t6 = t5;
        void * t7 = GM_LD(void *, (((uint8_t *)&t6) + 40));
        void * t8 = t7;
        uint8_t t9;
        if ((GM_LD(void *, ((uint8_t *)&t8)) != 0)) goto mn11;
        t9 = ((uint8_t)0);
        goto me10;
mn11:;
        t9 = ((uint8_t)1);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 112, 8);
me10:;
        if (!t9) goto ie2;
        {
            gm_b0_1 t14;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t16 = gm_l1;
        uint64_t t17 = (uint64_t)t16;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 344) + 0) + 8);
        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcnhb827gevj6u, 113, 12);
        gm_tqqhoivahirr0 t21 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 344) + 0)) + t18 * 136));
        F.l3 = t21;
    }
    {
        gm_tqu3m2l3n8mnc t22 = GM_LD(gm_tqu3m2l3n8mnc, (((uint8_t *)&F.l3) + 96));
        gm_tqu3m2l3n8mnc t23 = t22;
        gm_tiilmfd16v3fc t24;
        if (((((uint8_t *)&t23))[24] != 0)) {
            gm_tiilmfd16v3fc t25 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t23) + 0));
            t24 = t25;
        } else {
            {
                {
                    void * t28 = gm_l0;
                    uint32_t t29 = gm_l1;
                    gm_tl874t2atnlh3 t30 = gm_l2;
                    uint8_t t31 = gm_f_ensure_body_tbtojbv((void *)NULL, cx, NULL, t28, t29, t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            gm_b0_1 t32;
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        gm_l4 = t24;
    }
    {
        void * t34 = gm_l0;
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        gm_tl874t2atnlh3 t36 = gm_l2;
        gm_b0_1 t37 = gm_f_codegen_body_tpctt8p((void *)NULL, cx, NULL, t34, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t39 = gm_l0;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t39) + 1320) + 0));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_l5 = t42;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t44 = gm_l6;
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t46 = (t44 < t45);
                if (!t46) goto lb43;
                {
                    gm_to24qukftd66u t49;
                    uint64_t t50 = gm_l6;
                    GM_ST(uint64_t, (uint8_t *)&t49 + 0, t50);
                    uint64_t t51 = gm_l6;
                    uint64_t t52 = (uintptr_t)t51;
                    uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    memcpy((uint8_t *)&t49 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t52 * 40), 40);
                    gm_to24qukftd66u t54 = t49;
                    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&t54) + 0));
                    gm_l7 = t57;
                    gm_t0ut88vjdgrt1 t58 = GM_LD(gm_t0ut88vjdgrt1, (((uint8_t *)&t54) + 8));
                    F.l8 = t58;
                    {
                        uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 4));
                        uint32_t t62 = gm_l1;
                        uint8_t t63 = (!(t61 == t62));
                        if (!t63) goto ie60;
                        {
                            goto lc43;
                        }
                        goto ix60;
ie60:;
ix60:;
                    }
                    {
                        void * t65 = gm_l0;
                        void * t66 = (void *)(((uint8_t *)t65) + 1320);
                        uint64_t t67 = gm_l7;
                        gm_f_remove_kt0vu6d((void *)&F.l9, t66, t67);
                    }
                    {
                        void * t68 = gm_l0;
                        gm_b0_1 t69 = gm_f_produce_instance_kcqgj3e((void *)NULL, cx, NULL, t68, &(*(gm_t0ut88vjdgrt1 *)((uint8_t *)&F.l9)));
                        if (gm_unw.kind) {
                            goto gm_ll47;
                        }
                    }
                    {
                        gm_b0_1 t70;
                        uint8_t t71 = 0;
                        F.ret = t70;
                        goto gm_fret;
                    }
                    goto me55;
mn56:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 119, 5);
me55:;
                }
lc43:;
                {
                    uint64_t t73 = gm_l6;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l6 = t76;
                }
            }
gm_ll47:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb43; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc43; }
            }
            goto gm_lad;
lb43:;
        }
    }
    {
        void * t77 = gm_l0;
        gm_t0ut88vjdgrt1 t78;
        uint32_t t79 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        GM_ST(uint32_t, (uint8_t *)&t78 + 0, t79);
        uint32_t t80 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t78 + 4, t80);
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t78 + 8, t81);
        gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t78 + 24, t82);
        GM_ST(uint32_t, (uint8_t *)&t78 + 36, ((uint32_t)0ULL));
        gm_b0_1 t83 = gm_f_produce_instance_kcqgj3e((void *)NULL, cx, NULL, t77, &t78);
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
__attribute__((aligned(4))) uint32_t gm_f_entry_point_n2gu79u(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_entry_point_n2gu79u F;
    uint32_t gm_l1; /* main */
    gm_closure gm_l3; /* performs */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint64_t gm_l8; /* prelude */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* name */
    uint64_t gm_l12; /* d */
    uint32_t gm_l13; /* f */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    gm_b16_8 gm_l15; /* decls */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l18; /* x */
    void * gm_l19; /* value */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tiilmfd16v3fc gm_l23; /* e */
    uint32_t gm_l24; /* entry */
    void * gm_l25; /* call */
    void * gm_l26; /* w */
    GM_STACK_CHECK(&F, 3307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = F.l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcnhb827gevj6u, 13, 12);
        gm_tqqhoivahirr0 t7 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 344) + 0)) + t4 * 136));
        F.l2 = t7;
    }
    {
        gm_closure t8 = ((gm_closure){(void *)gm_f_performs_e7lpa6l, (void *)&F.h, cx});
        gm_l3 = t8;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_closure t10 = gm_l3;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s1qrqfh656g4q));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        gm_closure t12 = t10;
        gm_node *ccx13 = t12.cx;
        void * t14 = t12.code;
        uint8_t t15;
        if (GM_PTR_TAGGED(t14)) {
            t15 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t14))(t11);
        } else {
            t15 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t14)(t12.env, ccx13, NULL, t11);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t15) goto ie9;
        {
            void * t17 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_suhbskqupk9jm));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(14));
            gm_tftb1odc739t5 t19 = gm_f_push_3ef746b(t17, t18);
            gm_tftb1odc739t5 t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcnhb827gevj6u, 21, 28);
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint8_t t23 = 0;
        uint8_t t24 = 0;
        uint8_t t25 = 0;
        uint8_t t26 = 0;
        uint8_t t27 = 0;
        gm_closure t28 = gm_l3;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s4gt1buiaiaiq));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
        gm_closure t30 = t28;
        gm_node *ccx31 = t30.cx;
        void * t32 = t30.code;
        uint8_t t33;
        if (GM_PTR_TAGGED(t32)) {
            t33 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t32))(t29);
        } else {
            t33 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t32)(t30.env, ccx31, NULL, t29);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t33) {
            gm_closure t34 = gm_l3;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sub9t1fj6sv37));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(5));
            gm_closure t36 = t34;
            gm_node *ccx37 = t36.cx;
            void * t38 = t36.code;
            uint8_t t39;
            if (GM_PTR_TAGGED(t38)) {
                t39 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t38))(t35);
            } else {
                t39 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t38)(t36.env, ccx37, NULL, t35);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t27 = t39;
        } else t27 = 1;
        if (!t27) {
            gm_closure t40 = gm_l3;
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sihs6d097ocev));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
            gm_closure t42 = t40;
            gm_node *ccx43 = t42.cx;
            void * t44 = t42.code;
            uint8_t t45;
            if (GM_PTR_TAGGED(t44)) {
                t45 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t44))(t41);
            } else {
                t45 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t44)(t42.env, ccx43, NULL, t41);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t26 = t45;
        } else t26 = 1;
        if (!t26) {
            gm_closure t46 = gm_l3;
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sdlumbvroiufa));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(2));
            gm_closure t48 = t46;
            gm_node *ccx49 = t48.cx;
            void * t50 = t48.code;
            uint8_t t51;
            if (GM_PTR_TAGGED(t50)) {
                t51 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t50))(t47);
            } else {
                t51 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t50)(t48.env, ccx49, NULL, t47);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t25 = t51;
        } else t25 = 1;
        if (!t25) {
            gm_closure t52 = gm_l3;
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s2oe2s4851fv5));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(3));
            gm_closure t54 = t52;
            gm_node *ccx55 = t54.cx;
            void * t56 = t54.code;
            uint8_t t57;
            if (GM_PTR_TAGGED(t56)) {
                t57 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t56))(t53);
            } else {
                t57 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t56)(t54.env, ccx55, NULL, t53);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t24 = t57;
        } else t24 = 1;
        if (!t24) {
            gm_closure t58 = gm_l3;
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_suhl0206dfbdm));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(4));
            gm_closure t60 = t58;
            gm_node *ccx61 = t60.cx;
            void * t62 = t60.code;
            uint8_t t63;
            if (GM_PTR_TAGGED(t62)) {
                t63 = ((uint8_t (*)(gm_b16_8))GM_PTR_UNTAG(t62))(t59);
            } else {
                t63 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_b16_8))t62)(t60.env, ccx61, NULL, t59);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t23 = t63;
        } else t23 = 1;
        if (!t23) goto ie22;
        {
            void * t65 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s7tvcka4r1prv));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(9));
            gm_tftb1odc739t5 t67 = gm_f_push_3ef746b(t65, t66);
            gm_tftb1odc739t5 t68 = t67;
            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcnhb827gevj6u, 23, 9);
            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 16));
        gm_l5 = t70;
    }
    {
        gm_f_list_p0pjtrh((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t71 = F.l0;
        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
        gm_tf5srh4mp4b3i t73 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t72) + 320));
        gm_tf5srh4mp4b3i t74 = t73;
        uint64_t t75;
        if (((((uint8_t *)&t74))[8] != 0)) {
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&t74) + 0));
            t75 = t76;
        } else {
            uint32_t t77 = gm_l1;
            uint8_t t78 = 0;
            F.ret = t77;
            goto gm_fret;
        }
        gm_l8 = t75;
    }
    {
        {
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l9 = t80;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t82 = gm_l10;
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t84 = (t82 < t83);
                if (!t84) goto lb81;
                {
                    uint64_t t87 = gm_l10;
                    uint64_t t88 = (uintptr_t)t87;
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t88 * 16));
                    gm_l11 = t92;
                    {
                        void * t94 = F.l0;
                        void * t95 = GM_LD(void *, (((uint8_t *)t94) + 0));
                        uint64_t t96 = gm_l8;
                        gm_b16_8 t97 = gm_l11;
                        gm_tf5srh4mp4b3i t98 = gm_f_lookup_59juqoo(t95, t96, t97);
                        gm_tf5srh4mp4b3i t99 = t98;
                        uint64_t t100;
                        if (((((uint8_t *)&t99))[8] != 0)) {
                            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&t99) + 0));
                            t100 = t101;
                        } else {
                            goto lc81;
                        }
                        gm_l12 = t100;
                    }
                    {
                        void * t103 = F.l0;
                        uint64_t t104 = gm_l12;
                        gm_t6qooom8qa2do t105;
                        gm_f_resolve_decl_pus042v((void *)&t105, (void *)NULL, cx, NULL, t103, t104);
                        if (gm_unw.kind) {
                            goto gm_ll85;
                        }
                        gm_t6qooom8qa2do t106 = t105;
                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t106) + 0));
                        if (t107 != ((uint8_t)5ULL)) goto ie102;
                        uint32_t t108 = GM_LD(uint32_t, (((uint8_t *)&t106) + 8));
                        gm_l13 = t108;
                        {
                            uint32_t t110 = gm_l13;
                            uint64_t t111 = (uint64_t)t110;
                            uint64_t t112 = (uintptr_t)t111;
                            void * t113 = F.l0;
                            uint64_t t114 = GM_LD(uintptr_t, ((((uint8_t *)t113) + 344) + 0) + 8);
                            if (t112 >= (uintptr_t)(t114)) gm_trap_index(t112, t114, gm_srcnhb827gevj6u, 32, 15);
                            gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t113) + 344) + 0)) + t112 * 136) + 56));
                            gm_l14 = t115;
                        }
                        {
                            void * t116 = F.l0;
                            gm_t88rcucg1b7uk t117 = gm_l14;
                            gm_b16_8 t118 = gm_f_handlers_of_type_5gareb4((void *)NULL, cx, NULL, t116, t117);
                            if (gm_unw.kind) {
                                goto gm_ll85;
                            }
                            gm_l15 = t118;
                        }
                        {
                            {
                                gm_b16_8 t120 = gm_l15;
                                gm_l16 = t120;
                            }
                            {
                                gm_l17 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t122 = gm_l17;
                                    uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                    uint8_t t124 = (t122 < t123);
                                    if (!t124) goto lb121;
                                    {
                                        uint64_t t127 = gm_l17;
                                        uint64_t t128 = (uintptr_t)t127;
                                        uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                        uint32_t t130 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t128 * 4));
                                        uint32_t t131 = t130;
                                        uint32_t t134 = GM_LD(uint32_t, ((uint8_t *)&t131));
                                        gm_l18 = t134;
                                        {
                                            void * t136 = (void *)((uint8_t *)&F.l7);
                                            uint32_t t137 = gm_l18;
                                            uint64_t t138 = (uint64_t)t137;
                                            uint64_t t139 = (uintptr_t)t138;
                                            void * t140 = F.l0;
                                            uint64_t t141 = GM_LD(uintptr_t, ((((uint8_t *)t140) + 544) + 0) + 8);
                                            if (t139 >= (uintptr_t)(t141)) gm_trap_index(t139, t141, gm_srcnhb827gevj6u, 34, 39);
                                            uint32_t t142 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t140) + 544) + 0)) + t139 * 64) + 0) + 0));
                                            gm_tftb1odc739t5 t143 = gm_f_push_im9d4ro(t136, t142);
                                            gm_tftb1odc739t5 t144 = t143;
                                            if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srcnhb827gevj6u, 34, 26);
                                            gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
                                        }
                                        goto me132;
mn133:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 34, 9);
me132:;
                                    }
lc121:;
                                    {
                                        uint64_t t147 = gm_l17;
                                        uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                                        uint64_t t150 = r148;
                                        gm_l17 = t150;
                                    }
                                }
lb121:;
                            }
                        }
                        {
                            gm_t88rcucg1b7uk t151 = gm_l14;
                            gm_to76jli3bqddq t152;
                            uint32_t t153 = gm_l13;
                            gm_tsth9qu1dv0g3 t154;
                            memset(&t154, 0, sizeof t154);
                            GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t154 + 8), t153);
                            GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t152 + 0, t154);
                            gm_b16_8 t155 = gm_f_empty_slice_g3tddrb_2();
                            GM_ST(gm_b16_8, (uint8_t *)&t152 + 40, t155);
                            gm_t5brlu7fal70j t156;
                            memset(&t156, 0, sizeof t156);
                            GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)34ULL));
                            GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t156 + 8), t152);
                            gm_tl874t2atnlh3 t157 = gm_l5;
                            void * t158 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t151, &t156, t157);
                            if (gm_unw.kind) {
                                goto gm_ll85;
                            }
                            gm_l19 = t158;
                        }
                        {
                            void * t159 = (void *)((uint8_t *)&F.l6);
                            gm_tpe1hv3fekn05 t160;
                            gm_b16_8 t161 = gm_l15;
                            GM_ST(gm_b16_8, (uint8_t *)&t160 + 0, t161);
                            void * t162 = gm_l19;
                            GM_ST(void *, (uint8_t *)&t160 + 16, t162);
                            gm_t4lnf71mruqpt t163;
                            GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t163 + 8), t160);
                            gm_tftb1odc739t5 t164 = gm_f_push_f5fsh94(t159, &t163);
                            gm_tftb1odc739t5 t165 = t164;
                            if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srcnhb827gevj6u, 36, 9);
                            gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
                        }
                        goto ix102;
ie102:;
                        {
                            goto lc81;
                        }
ix102:;
                    }
                    goto me90;
mn91:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 29, 5);
me90:;
                }
lc81:;
                {
                    uint64_t t169 = gm_l10;
                    uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                    uint64_t t172 = r170;
                    gm_l10 = t172;
                }
            }
gm_ll85:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
            }
            goto gm_lad;
lb81:;
        }
    }
    {
        uint64_t t174 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t175 = ((t174 == ((uint64_t)0ULL)));
        if (!t175) goto ie173;
        {
            uint32_t t177 = gm_l1;
            uint8_t t178 = 0;
            F.ret = t177;
            goto gm_fret;
        }
        goto ix173;
ie173:;
ix173:;
    }
    {
        gm_f_list_084fcd6((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t180 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l2) + 64) + 0));
            gm_l21 = t180;
        }
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t182 = gm_l22;
                uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                uint8_t t184 = (t182 < t183);
                if (!t184) goto lb181;
                {
                    uint64_t t187 = gm_l22;
                    uint64_t t188 = (uintptr_t)t187;
                    uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    gm_tiilmfd16v3fc t192 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t188 * 24));
                    gm_l23 = t192;
                    {
                        uint32_t t195 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 0));
                        gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_b16_8 t197 = t196;
                        uint8_t t198 = 0;
                        uint32_t t199 = t195;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t197) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t197))) + i * 4, ((uint8_t *)&t199))) { t198 = 1; break; }
                        uint8_t t200 = (uint8_t)(!t198);
                        if (!t200) goto ie194;
                        {
                            void * t202 = (void *)((uint8_t *)&F.l20);
                            gm_tftb1odc739t5 t203 = gm_f_push_d1r4h19(t202, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l23)));
                            gm_tftb1odc739t5 t204 = t203;
                            if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_srcnhb827gevj6u, 41, 41);
                            gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
                        }
                        goto ix194;
ie194:;
ix194:;
                    }
                    goto me190;
mn191:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 40, 5);
me190:;
                }
lc181:;
                {
                    uint64_t t207 = gm_l22;
                    uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                    uint64_t t210 = r208;
                    gm_l22 = t210;
                }
            }
lb181:;
        }
    }
    {
        void * t211 = F.l0;
        gm_t9qkh0rsooc8d t212;
        GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)0ULL));
        gm_b16_8 t213;
        GM_ST(void *, (uint8_t *)&t213, (void *)(gm_stgqtu1f7bco8));
        GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(7));
        gm_tl874t2atnlh3 t214 = gm_l5;
        uint64_t t215 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 32));
        gm_b16_8 t216 = gm_f_empty_slice_he774nl();
        gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
        gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        gm_b16_8 t219 = t218;
        gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
        gm_tnpifv20irnph t221 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t220);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t222 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t211, t212, t213, t214, t215, t216, t217, t221);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t222;
    }
    {
        gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
        gm_to76jli3bqddq t224;
        uint32_t t225 = gm_l1;
        gm_tsth9qu1dv0g3 t226;
        memset(&t226, 0, sizeof t226);
        GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t226 + 8), t225);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t224 + 0, t226);
        gm_b16_8 t227 = gm_f_empty_slice_g3tddrb_2();
        GM_ST(gm_b16_8, (uint8_t *)&t224 + 40, t227);
        gm_t5brlu7fal70j t228;
        memset(&t228, 0, sizeof t228);
        GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t228 + 8), t224);
        gm_tl874t2atnlh3 t229 = gm_l5;
        void * t230 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t223, &t228, t229);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t230;
    }
    {
        gm_t88rcucg1b7uk t231 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l2) + 56));
        gm_t710lsrq1m8gk t232;
        memset((uint8_t *)&t232 + 4, 0, 4);
        GM_ST(uint32_t, (uint8_t *)&t232 + 0, ((uint32_t)0ULL));
        gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t232 + 8, t233);
        GM_ST(void *, (uint8_t *)&t232 + 24, ((void *)0));
        void * t234 = gm_l25;
        GM_ST(void *, (uint8_t *)&t232 + 32, t234);
        gm_t5brlu7fal70j t235;
        memset(&t235, 0, sizeof t235);
        GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)58ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t235 + 8), t232);
        gm_tl874t2atnlh3 t236 = gm_l5;
        void * t237 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t231, &t235, t236);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l26 = t237;
    }
    {
        uint32_t t238 = gm_l24;
        uint64_t t239 = (uint64_t)t238;
        uint64_t t240 = (uintptr_t)t239;
        void * t241 = F.l0;
        uint64_t t242 = GM_LD(uintptr_t, ((((uint8_t *)t241) + 384) + 0) + 8);
        if (t240 >= (uintptr_t)(t242)) gm_trap_index(t240, t242, gm_srcnhb827gevj6u, 46, 5);
        void * t243 = gm_l26;
        GM_ST(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t241) + 384) + 0)) + t240 * 64) + 40), t243);
    }
    {
        uint32_t t244 = gm_l24;
        uint8_t t245 = 0;
        F.ret = t244;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_olk2bm3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_olk2bm3 F;
    void * gm_l0; /* n */
    uint32_t gm_l1; /* g */
    uint32_t gm_l2; /* g */
    uint32_t gm_l3; /* g */
    uint32_t gm_l4; /* h */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t4lnf71mruqpt gm_l8; /* h */
    gm_b16_8 gm_l9; /* decls */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l12; /* d */
    gm_tl874t2atnlh3 gm_l13; /* cspan */
    void * gm_l14; /* cell */
    GM_STACK_CHECK(&F, 715u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma4; case ((uint8_t)27ULL): goto ma4; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma4; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma6; default: goto ma8; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)34ULL)) goto pa13;
        uint8_t t15 = GM_LD(uint8_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
        if (t15 != ((uint8_t)0ULL)) goto pa13;
        uint32_t t16 = GM_LD(uint32_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 8));
        gm_l1 = t16;
        goto po12;
pa13:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)26ULL)) goto pa17;
        uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t19;
        goto po12;
pa17:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)27ULL)) goto pa20;
        uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t22;
        goto po12;
pa20:;
        goto mn11;
po12:;
        void * t23 = (void *)((uint8_t *)&((struct gm_fr_prepare_codegen_4hnlio1 *)((&F.h)->up))->l3);
        uint32_t t24 = gm_l1;
        gm_tftb1odc739t5 t25 = gm_f_push_im9d4ro(t23, t24);
        gm_tftb1odc739t5 t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcnhb827gevj6u, 67, 61);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        goto me3;
mn11:;
ma5:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)28ULL)) goto mn28;
        uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t30;
        void * t31 = (void *)((uint8_t *)&((struct gm_fr_prepare_codegen_4hnlio1 *)((&F.h)->up))->l3);
        uint32_t t32 = gm_l4;
        uint64_t t33 = (uint64_t)t32;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_prepare_codegen_4hnlio1 *)((&F.h)->up))->l0));
        uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 504) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcnhb827gevj6u, 68, 47);
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 504) + 0)) + t34 * 40) + 24));
        gm_b16_8 t38 = t37;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_tftb1odc739t5 t40 = gm_f_extend_7hs67uj(t31, t39);
        gm_tftb1odc739t5 t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcnhb827gevj6u, 68, 35);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
        goto me3;
mn28:;
ma6:;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t44 != ((uint8_t)58ULL)) goto mn43;
        gm_t710lsrq1m8gk t45 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l5 = t45;
        {
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
            gm_l6 = t47;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l7;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    uint64_t t54 = gm_l7;
                    uint64_t t55 = (uintptr_t)t54;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t4lnf71mruqpt t59 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t55 * 32));
                    gm_l8 = t59;
                    {
                        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                        if (t62 != ((uint8_t)1ULL)) goto ie61;
                        gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l8) + 8) + 0));
                        gm_l9 = t63;
                        {
                            {
                                gm_b16_8 t66 = gm_l9;
                                gm_l10 = t66;
                            }
                            {
                                gm_l11 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t68 = gm_l11;
                                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                    uint8_t t70 = (t68 < t69);
                                    if (!t70) goto lb67;
                                    {
                                        uint64_t t73 = gm_l11;
                                        uint64_t t74 = (uintptr_t)t73;
                                        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint32_t t76 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t74 * 4));
                                        uint32_t t77 = t76;
                                        uint32_t t80 = GM_LD(uint32_t, ((uint8_t *)&t77));
                                        gm_l12 = t80;
                                        {
                                            void * t82 = (void *)((uint8_t *)&((struct gm_fr_prepare_codegen_4hnlio1 *)((&F.h)->up))->l3);
                                            uint32_t t83 = gm_l12;
                                            uint64_t t84 = (uint64_t)t83;
                                            uint64_t t85 = (uintptr_t)t84;
                                            void * t86 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_prepare_codegen_4hnlio1 *)((&F.h)->up))->l0));
                                            uint64_t t87 = GM_LD(uintptr_t, ((((uint8_t *)t86) + 544) + 0) + 8);
                                            if (t85 >= (uintptr_t)(t87)) gm_trap_index(t85, t87, gm_srcnhb827gevj6u, 71, 54);
                                            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t86) + 544) + 0)) + t85 * 64) + 48));
                                            gm_b16_8 t89 = t88;
                                            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                                            gm_tftb1odc739t5 t91 = gm_f_extend_7hs67uj(t82, t90);
                                            gm_tftb1odc739t5 t92 = t91;
                                            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcnhb827gevj6u, 71, 42);
                                            gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                                        }
                                        goto me78;
mn79:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 71, 25);
me78:;
                                    }
lc67:;
                                    {
                                        uint64_t t95 = gm_l11;
                                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                                        uint64_t t98 = r96;
                                        gm_l11 = t98;
                                    }
                                }
lb67:;
                            }
                        }
                        goto ix61;
ie61:;
ix61:;
                    }
                    goto me57;
mn58:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 69, 29);
me57:;
                }
lc48:;
                {
                    uint64_t t100 = gm_l7;
                    uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                    uint64_t t103 = r101;
                    gm_l7 = t103;
                }
            }
lb48:;
        }
        goto me3;
mn43:;
ma7:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t105 != ((uint8_t)14ULL)) goto mn104;
        gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l13 = t106;
        void * t108 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_prepare_codegen_4hnlio1 *)((&F.h)->up))->l0));
        gm_tl874t2atnlh3 t109 = gm_l13;
        void * t110 = gm_l0;
        gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t110) + 0));
        void * t112 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t108, t109, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t113 = t112;
        if (!(GM_LD(void *, ((uint8_t *)&t113)) != 0)) goto ie107;
        void * t114 = GM_LD(void *, ((uint8_t *)&t113));
        gm_l14 = t114;
        {
            void * t116 = gm_l14;
            void * t117 = (void *)((uint8_t *)&((struct gm_fr_prepare_codegen_4hnlio1 *)((&F.h)->up))->l3);
            gm_b0_1 t118 = gm_f_value_fns_d67c1v1((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)(((uint8_t *)t116) + 0)), t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix107;
ie107:;
ix107:;
        goto me3;
mn104:;
ma8:;
        goto me3;
mn119:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 66, 13);
me3:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_performs_e7lpa6l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_performs_e7lpa6l F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tiilmfd16v3fc gm_l3; /* e */
    GM_STACK_CHECK(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)&((struct gm_fr_entry_point_n2gu79u *)((&F.h)->up))->l2) + 64) + 0));
            gm_l1 = t3;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 24));
                    gm_l3 = t15;
                    {
                        void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_entry_point_n2gu79u *)((&F.h)->up))->l0));
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                        uint8_t t20 = gm_f_is_ambient_m41pl1e(t18, t19);
                        if (!t20) goto ie17;
                        void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_entry_point_n2gu79u *)((&F.h)->up))->l0));
                        void * t22 = (void *)(((uint8_t *)t21) + 8);
                        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
                        gm_b16_8 t24 = gm_f_effect_name_prpp0ip(t22, t23);
                        gm_b16_8 t25 = gm_l0;
                        gm_b16_8 t26 = t24;
                        gm_b16_8 t27 = t25;
                        uint8_t t28 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
                        if (!t28) goto ie17;
                        {
                            uint8_t t30 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 15, 9);
me13:;
                }
lc4:;
                {
                    uint64_t t32 = gm_l2;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l2 = t35;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t36 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_prepare_codegen_4hnlio1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_prepare_codegen_4hnlio1 F;
    gm_b16_8 gm_l1; /* roots */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* r */
    gm_tl874t2atnlh3 gm_l7; /* span */
    uint32_t gm_l8; /* f */
    uint32_t gm_l10; /* p */
    void * gm_l11; /* b */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    void * gm_l16; /* init */
    uint32_t gm_l18; /* f */
    GM_STACK_CHECK(&F, 1113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        gm_f_hash_map_70470jk((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 4));
                    uint32_t t14 = t13;
                    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&t14));
                    gm_l6 = t17;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l3);
                        uint32_t t20 = gm_l6;
                        gm_tftb1odc739t5 t21 = gm_f_push_im9d4ro(t19, t20);
                        gm_tftb1odc739t5 t22 = t21;
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcnhb827gevj6u, 56, 22);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 56, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l5;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l5 = t28;
                }
            }
lb4:;
        }
    }
    {
        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_l7 = t29;
    }
    {
        for (;;) {
            void * t31 = (void *)((uint8_t *)&F.l3);
            gm_t9805qtonbsag t32 = gm_f_pop_570g79n(t31);
            gm_t9805qtonbsag t33 = t32;
            if (!((((uint8_t *)&t33))[4] != 0)) goto lb30;
            uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)&t33));
            gm_l8 = t34;
            {
                void * t38 = (void *)((uint8_t *)&F.l2);
                uint32_t t39 = gm_l8;
                gm_tqs5p4n2tq19v t40 = gm_f_get_dk5gh28(t38, t39);
                gm_tqs5p4n2tq19v t41 = t40;
                uint8_t t42;
                if (((((uint8_t *)&t41))[1] != 0)) goto mn44;
                t42 = ((uint8_t)0);
                goto me43;
mn44:;
                t42 = ((uint8_t)1);
                goto me43;
mn45:;
                gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 59, 12);
me43:;
                if (!t42) goto ie37;
                {
                    goto lc30;
                }
                goto ix37;
ie37:;
ix37:;
            }
            {
                void * t47 = (void *)((uint8_t *)&F.l2);
                uint32_t t48 = gm_l8;
                gm_tftb1odc739t5 t49 = gm_f_put_v6g5pup(t47, t48, ((uint8_t)1));
                gm_tftb1odc739t5 t50 = t49;
                if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srcnhb827gevj6u, 60, 9);
                gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
            }
            {
                void * t52 = F.l0;
                uint32_t t53 = gm_l8;
                gm_tl874t2atnlh3 t54 = gm_l7;
                gm_b0_1 t55 = gm_f_codegen_body_tpctt8p((void *)NULL, cx, NULL, t52, t53, t54);
                if (gm_unw.kind) {
                    goto gm_ll35;
                }
            }
            {
                void * t56 = F.l0;
                uint32_t t57 = gm_l8;
                gm_f_def_of_m512sbu((void *)&F.l9, t56, t57);
            }
            {
                if (!(((((uint8_t *)&F.l9) + 48))[4] != 0)) goto ie58;
                uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 48));
                gm_l10 = t59;
                {
                    void * t61 = (void *)((uint8_t *)&F.l3);
                    uint32_t t62 = gm_l10;
                    gm_tftb1odc739t5 t63 = gm_f_push_im9d4ro(t61, t62);
                    gm_tftb1odc739t5 t64 = t63;
                    if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcnhb827gevj6u, 63, 30);
                    gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                }
                goto ix58;
ie58:;
ix58:;
            }
            {
                void * t66 = GM_LD(void *, (((uint8_t *)&F.l9) + 40));
                void * t67 = t66;
                void * t68;
                if ((GM_LD(void *, ((uint8_t *)&t67)) != 0)) {
                    void * t69 = GM_LD(void *, (((uint8_t *)&t67) + 0));
                    t68 = t69;
                } else {
                    goto lc30;
                }
                gm_l11 = t68;
            }
            {
                void * t70 = gm_l11;
                gm_closure t71 = ((gm_closure){(void *)gm_f_fn_olk2bm3, (void *)&F.h, cx});
                gm_f_visit_e3tpkkh(t70, t71);
                if (gm_unw.kind) {
                    goto gm_ll35;
                }
            }
lc30:;
        }
gm_ll35:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb30; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc30; }
        }
        goto gm_lad;
lb30:;
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            void * t73 = F.l0;
            uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 1448) + 0) + 8);
            gm_l12 = t74;
        }
        {
            for (;;) {
                uint64_t t76 = gm_l13;
                uint64_t t77 = gm_l12;
                uint8_t t78 = (t76 < t77);
                if (!t78) goto lb75;
                {
                    uint64_t t81 = gm_l13;
                    uint64_t t82 = t81;
                    uint64_t t85 = GM_LD(uint64_t, ((uint8_t *)&t82));
                    gm_l14 = t85;
                    {
                        uint64_t t87 = gm_l14;
                        uint64_t t88 = (uintptr_t)t87;
                        void * t89 = F.l0;
                        uint64_t t90 = GM_LD(uintptr_t, ((((uint8_t *)t89) + 1448) + 0) + 8);
                        if (t88 >= (uintptr_t)(t90)) gm_trap_index(t88, t90, gm_srcnhb827gevj6u, 82, 15);
                        gm_tq7qjpkmqmc5s t91 = GM_LD(gm_tq7qjpkmqmc5s, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t89) + 1448) + 0)) + t88 * 104));
                        F.l15 = t91;
                    }
                    {
                        void * t92 = GM_LD(void *, (((uint8_t *)&F.l15) + 40));
                        void * t93 = t92;
                        void * t94;
                        if ((GM_LD(void *, ((uint8_t *)&t93)) != 0)) {
                            void * t95 = GM_LD(void *, (((uint8_t *)&t93) + 0));
                            t94 = t95;
                        } else {
                            goto lc75;
                        }
                        gm_l16 = t94;
                    }
                    {
                        void * t97 = F.l0;
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 32));
                        void * t99 = gm_l16;
                        gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 24));
                        gm_tjoi8o798ne9k t101;
                        gm_f_eval_typed_m82ie8p((void *)&t101, (void *)NULL, cx, NULL, t97, t98, t99, t100);
                        if (gm_unw.kind) {
                            goto gm_ll79;
                        }
                        gm_tjoi8o798ne9k t102 = t101;
                        if (!((((uint8_t *)&t102))[48] != 0)) goto ie96;
                        gm_t6evimt84lndq t103 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&t102));
                        F.l17 = t103;
                        {
                            void * t105 = (void *)((uint8_t *)&F.l3);
                            gm_b0_1 t106 = gm_f_value_fns_d67c1v1((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l17)), t105);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                        }
                        {
                            uint64_t t107 = gm_l14;
                            uint64_t t108 = (uintptr_t)t107;
                            void * t109 = F.l0;
                            uint64_t t110 = GM_LD(uintptr_t, ((((uint8_t *)t109) + 1448) + 0) + 8);
                            if (t108 >= (uintptr_t)(t110)) gm_trap_index(t108, t110, gm_srcnhb827gevj6u, 86, 13);
                            uint8_t *t111 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t109) + 1448) + 0)) + t108 * 104) + 96);
                            gm_tjoi8o798ne9k t112;
                            memset((uint8_t *)&t112 + 49, 0, 15);
                            memcpy((uint8_t *)&t112 + 0, ((uint8_t *)&F.l17), 48);
                            GM_ST(uint8_t, (uint8_t *)&t112 + 48, ((uint8_t)1));
                            gm_tq7s98ihonkju t113 = gm_f_new_15vqrvd((void *)NULL, cx, NULL, &t112, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k75gp9b9r49)));
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                            gm_tq7s98ihonkju t114 = t113;
                            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srcnhb827gevj6u, 86, 40);
                            void * t115 = GM_LD(void *, (((uint8_t *)&t114) + 8));
                            GM_ST(void *, t111, t115);
                        }
                        goto ix96;
ie96:;
ix96:;
                    }
                    goto me83;
mn84:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 81, 5);
me83:;
                }
lc75:;
                {
                    uint64_t t117 = gm_l13;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l13 = t120;
                }
            }
gm_ll79:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
            }
            goto gm_lad;
lb75:;
        }
    }
    {
        for (;;) {
            void * t122 = (void *)((uint8_t *)&F.l3);
            gm_t9805qtonbsag t123 = gm_f_pop_570g79n(t122);
            gm_t9805qtonbsag t124 = t123;
            if (!((((uint8_t *)&t124))[4] != 0)) goto lb121;
            uint32_t t125 = GM_LD(uint32_t, ((uint8_t *)&t124));
            gm_l18 = t125;
            {
                void * t129 = (void *)((uint8_t *)&F.l2);
                uint32_t t130 = gm_l18;
                gm_tqs5p4n2tq19v t131 = gm_f_get_dk5gh28(t129, t130);
                gm_tqs5p4n2tq19v t132 = t131;
                uint8_t t133;
                if (((((uint8_t *)&t132))[1] != 0)) goto mn135;
                t133 = ((uint8_t)0);
                goto me134;
mn135:;
                t133 = ((uint8_t)1);
                goto me134;
mn136:;
                gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 91, 12);
me134:;
                if (!t133) goto ie128;
                {
                    goto lc121;
                }
                goto ix128;
ie128:;
ix128:;
            }
            {
                void * t138 = (void *)((uint8_t *)&F.l2);
                uint32_t t139 = gm_l18;
                gm_tftb1odc739t5 t140 = gm_f_put_v6g5pup(t138, t139, ((uint8_t)1));
                gm_tftb1odc739t5 t141 = t140;
                if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 1, gm_srcnhb827gevj6u, 92, 9);
                gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 1));
            }
            {
                void * t143 = F.l0;
                uint32_t t144 = gm_l18;
                gm_tl874t2atnlh3 t145 = gm_l7;
                gm_b0_1 t146 = gm_f_codegen_body_tpctt8p((void *)NULL, cx, NULL, t143, t144, t145);
                if (gm_unw.kind) {
                    goto gm_ll126;
                }
            }
lc121:;
        }
gm_ll126:;
        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb121; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc121; }
        }
        goto gm_lad;
lb121:;
    }
    {
        void * t147 = F.l0;
        void * t148 = GM_LD(void *, (((uint8_t *)t147) + 0));
        void * t149 = GM_LD(void *, (((uint8_t *)t148) + 8));
        uint8_t t150 = gm_f_has_errors_if7ai0m(t149);
        uint8_t t151 = (uint8_t)!t150;
        uint8_t t152 = 0;
        F.ret = t151;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_value_fns_d67c1v1(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1) {
    struct gm_fr_value_fns_d67c1v1 F;
    void * gm_l1; /* out */
    uint32_t gm_l2; /* f */
    void * gm_l3; /* a */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l7; /* x */
    void * gm_l8; /* x */
    void * gm_l9; /* x */
    void * gm_l10; /* p */
    void * gm_l11; /* x */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma3; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t11 != ((uint8_t)11ULL)) goto mn10;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&F.l0) + 16));
        gm_l2 = t12;
        void * t13 = gm_l1;
        uint32_t t14 = gm_l2;
        gm_tftb1odc739t5 t15 = gm_f_push_im9d4ro(t13, t14);
        gm_tftb1odc739t5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srcnhb827gevj6u, 101, 19);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
        goto me2;
mn10:;
ma4:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t19 != ((uint8_t)3ULL)) goto mn18;
        void * t20 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l3 = t20;
        {
            void * t22 = gm_l3;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
            gm_l4 = t23;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l5;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l5;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t6evimt84lndq t35 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 48));
                    F.l6 = t35;
                    {
                        void * t37 = gm_l1;
                        gm_b0_1 t38 = gm_f_value_fns_d67c1v1((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l6)), t37);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 102, 20);
me33:;
                }
lc24:;
                {
                    uint64_t t40 = gm_l5;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l5 = t43;
                }
            }
gm_ll28:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
            }
            goto gm_lad;
lb24:;
        }
        goto me2;
mn18:;
ma5:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t47 != ((uint8_t)7ULL)) goto pa46;
        void * t48 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l7 = t48;
        goto po45;
pa46:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t50 != ((uint8_t)8ULL)) goto pa49;
        void * t51 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l7 = t51;
        goto po45;
pa49:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t53 != ((uint8_t)9ULL)) goto pa52;
        void * t54 = GM_LD(void *, (((uint8_t *)&F.l0) + 16));
        gm_l7 = t54;
        goto po45;
pa52:;
        goto mn44;
po45:;
        void * t55 = gm_l7;
        void * t56 = gm_l1;
        gm_b0_1 t57 = gm_f_value_fns_d67c1v1((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t55)), t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn44:;
ma6:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        if (t59 != ((uint8_t)10ULL)) goto mn58;
        void * t60 = GM_LD(void *, ((((uint8_t *)&F.l0) + 16) + 8));
        gm_l10 = t60;
        void * t62 = gm_l10;
        void * t63 = t62;
        if (!(GM_LD(void *, ((uint8_t *)&t63)) != 0)) goto ie61;
        void * t64 = GM_LD(void *, ((uint8_t *)&t63));
        gm_l11 = t64;
        {
            void * t66 = gm_l11;
            void * t67 = gm_l1;
            gm_b0_1 t68 = gm_f_value_fns_d67c1v1((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)t66)), t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix61;
ie61:;
ix61:;
        goto me2;
mn58:;
ma7:;
        goto me2;
mn69:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcnhb827gevj6u, 100, 5);
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

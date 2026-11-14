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
typedef struct { _Alignas(8) uint8_t b[0]; } gm_b0_8;
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t a[8]; } gm_tg8e8rhc3qio9;
typedef struct { uint8_t a[1]; } gm_tivanu2mfcv6o;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t a[64]; } gm_tsn02dhhitgs4;
typedef struct { gm_b16_8 a[2]; } gm_t0kl141g5m2qf;
typedef struct { gm_b16_8 a[9]; } gm_t142fel6vb03l;
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg8e8rhc3qio9) == 32 && _Alignof(gm_tg8e8rhc3qio9) == 4 && offsetof(gm_tg8e8rhc3qio9, a) == 0, "layout of `[8]u32`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 a[10]; } gm_thr4tus9rimig;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tipro5npl6rd8;
_Static_assert(sizeof(gm_tivanu2mfcv6o) == 1 && _Alignof(gm_tivanu2mfcv6o) == 1 && offsetof(gm_tivanu2mfcv6o, a) == 0, "layout of `[1]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 a[5]; } gm_tjgkb0mi112kg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_tg8e8rhc3qio9 f0; gm_tsn02dhhitgs4 f1; uint64_t f2; uint64_t f3; } gm_tq14qvifgdvpm;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn02dhhitgs4) == 64 && _Alignof(gm_tsn02dhhitgs4) == 1 && offsetof(gm_tsn02dhhitgs4, a) == 0, "layout of `[64]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t0kl141g5m2qf) == 32 && _Alignof(gm_t0kl141g5m2qf) == 8 && offsetof(gm_t0kl141g5m2qf, a) == 0, "layout of `[2][]const string`");
_Static_assert(sizeof(gm_t142fel6vb03l) == 144 && _Alignof(gm_t142fel6vb03l) == 8 && offsetof(gm_t142fel6vb03l, a) == 0, "layout of `[9]string`");
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thr4tus9rimig) == 160 && _Alignof(gm_thr4tus9rimig) == 8 && offsetof(gm_thr4tus9rimig, a) == 0, "layout of `[10]string`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tipro5npl6rd8) == 40 && _Alignof(gm_tipro5npl6rd8) == 8 && offsetof(gm_tipro5npl6rd8, f0) == 0 && offsetof(gm_tipro5npl6rd8, f1) == 4 && offsetof(gm_tipro5npl6rd8, f2) == 8 && offsetof(gm_tipro5npl6rd8, f3) == 24, "layout of `Ran`");
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjgkb0mi112kg) == 80 && _Alignof(gm_tjgkb0mi112kg) == 8 && offsetof(gm_tjgkb0mi112kg, a) == 0, "layout of `[5]string`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; } gm_tji6flc1b40vj;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_th2lqbo77pvsc f2; } gm_tmf08dt0mom0q;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq14qvifgdvpm) == 112 && _Alignof(gm_tq14qvifgdvpm) == 8 && offsetof(gm_tq14qvifgdvpm, f0) == 0 && offsetof(gm_tq14qvifgdvpm, f1) == 32 && offsetof(gm_tq14qvifgdvpm, f2) == 96 && offsetof(gm_tq14qvifgdvpm, f3) == 104, "layout of `Sha256`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tji6flc1b40vj) == 56 && _Alignof(gm_tji6flc1b40vj) == 8 && offsetof(gm_tji6flc1b40vj, f0) == 0 && offsetof(gm_tji6flc1b40vj, f1) == 16 && offsetof(gm_tji6flc1b40vj, f2) == 32, "layout of `TreeEntry`");
_Static_assert(sizeof(gm_tmf08dt0mom0q) == 104 && _Alignof(gm_tmf08dt0mom0q) == 8 && offsetof(gm_tmf08dt0mom0q, f0) == 0 && offsetof(gm_tmf08dt0mom0q, f1) == 40 && offsetof(gm_tmf08dt0mom0q, f2) == 80, "layout of `Refs`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tmf08dt0mom0q v1; } f1; } gm_tv61mo81rn7eb;
_Static_assert(sizeof(gm_tv61mo81rn7eb) == 112 && _Alignof(gm_tv61mo81rn7eb) == 8 && offsetof(gm_tv61mo81rn7eb, f0) == 0 && offsetof(gm_tv61mo81rn7eb, f1) == 8, "layout of `string!Refs`");
struct gm_fr_archive_dir_srvvp3g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_link_chains_sttldqn {
    gm_frame h;
    gm_tji6flc1b40vj l3; /* e */
    gm_trqfvmjtt4sij l5; /* dir */
    gm_tl572d7579hns ret;
    gm_b0_1 w1;
};
struct gm_fr_check_link_glakqpq {
    gm_frame h;
    uint64_t l3; /* depth */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_checkout_dir_mvijcuu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_checkout_j42th18 {
    gm_frame h;
    gm_thr4tus9rimig l7; /* args */
    gm_tl572d7579hns ret;
};
struct gm_fr_chmod_tree_sj4qk6r {
    gm_frame h;
    gm_tdmi4fk9nhqa7 l2; /* args */
    gm_b0_1 ret;
};
struct gm_fr_collect_entries_al4dgqv {
    gm_frame h;
    gm_b16_8 l0; /* root */
    gm_trqfvmjtt4sij l3; /* names */
    gm_trqfvmjtt4sij l4; /* kinds */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_db_tssgvk0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_describe_read_error_uikkr59 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_downloads_vdenqu5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gu7qoap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n1oc8gq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qncp3l3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fetch_mirror_q06gvv0 {
    gm_frame h;
    gm_tdmi4fk9nhqa7 l3; /* init_args */
    gm_t142fel6vb03l l4; /* args */
    gm_tl572d7579hns ret;
};
struct gm_fr_fn_g7467g2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_5dma72c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_6l4llq8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_86gje82 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_g303nmp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_hb9k0f9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_jvjn1fs {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_for_body_ukps8ob {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_for_body_v43n3ub {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_git_uu8ibkt {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* argv */
    gm_tipro5npl6rd8 l8; /* r */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_init_s32ien0 {
    gm_frame h;
    gm_tq14qvifgdvpm ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_0k0up67 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_last_used_6quhcig {
    gm_frame h;
    gm_t913bb735pvt5 ret;
};
struct gm_fr_le_bytes_r7p21h4 {
    gm_frame h;
    gm_t2doi72kmmlil ret;
};
struct gm_fr_link_at_qflj1mk {
    gm_frame h;
    gm_tji6flc1b40vj l5; /* e */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_link_target_lkg20fb {
    gm_frame h;
    gm_tipro5npl6rd8 l2; /* r */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_list_69se6ht {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_8nlen85 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_refs_j3is1o5 {
    gm_frame h;
    gm_tjgkb0mi112kg l1; /* args */
    gm_tmf08dt0mom0q l3; /* refs */
    gm_tv61mo81rn7eb ret;
    gm_b0_1 w0;
};
struct gm_fr_list_vg4gp98 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_read_only_a29eok8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_make_writable_6uascrq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mark_used_5dtkpil {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_parent_4i83qet {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_path_names_j9i34l1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* names */
    gm_trqfvmjtt4sij ret;
    gm_b0_1 w0;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_af3r83c {
    gm_frame h;
    gm_tji6flc1b40vj l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_htd7vjn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_p9io1lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_message_6p6ba86 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_jobmje4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_mkqn41e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ss9nsh7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remote_tags_rssa7s7 {
    gm_frame h;
    gm_tjgkb0mi112kg l1; /* args */
    gm_trqfvmjtt4sij l5; /* tags */
    gm_t470aqkia5212 ret;
    gm_b0_1 w0;
};
struct gm_fr_remove_tree_rp98g1s {
    gm_frame h;
    gm_ttln0d0hd70en ret;
};
struct gm_fr_repo_dir_name_6927pg7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_resolve_within_c8j2tbk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_rev_parse_3l092b4 {
    gm_frame h;
    gm_tdmi4fk9nhqa7 l3; /* args */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_sanitize_modes_mvteloh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sha256_hex_4vhcm7o {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_by_5d2ijcl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_stamp_of_li2ggn0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_store_for_package_347124r {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_sum_ugu720i {
    gm_frame h;
    gm_tq14qvifgdvpm l1; /* s */
    gm_t7mlrlibdq1ja ret;
};
struct gm_fr_tree_checksum_k6o2imm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* entries */
    gm_tq14qvifgdvpm l2; /* h */
    gm_tji6flc1b40vj l7; /* e */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_archive_dir_srvvp3g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_check_link_chains_sttldqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_check_link_glakqpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_checkout_dir_mvijcuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_checkout_j42th18(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_chmod_tree_sj4qk6r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_collect_entries_al4dgqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_db_tssgvk0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_downloads_vdenqu5(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gu7qoap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n1oc8gq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qncp3l3(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s58h1st(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fetch_mirror_q06gvv0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_final_rg5ko3p(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_g7467g2(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_5dma72c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6l4llq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_86gje82(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g303nmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hb9k0f9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_jvjn1fs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ukps8ob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9di2rrs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_anrgoqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c31q7f8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cibac8a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d5h9av5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d801g52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dr7jgt6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gp81iq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hedon8i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ij4da0d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nvg3me5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_u5g4crs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_git_uu8ibkt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_hex_euih242(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out);
__attribute__((aligned(4))) void gm_f_introsort_1bllg4s(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_last_used_6quhcig(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t2doi72kmmlil gm_f_le_bytes_r7p21h4(uint64_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_samvo8i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_snhh6ss(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_link_at_qflj1mk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_link_target_lkg20fb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_69se6ht(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_8nlen85(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_refs_j3is1o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_vg4gp98(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_make_dirs_gbp8nfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_mark_used_5dtkpil(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) void gm_f_parent_4i83qet(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_path_names_j9i34l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_private_name_83ij38e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_af3r83c(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_htd7vjn(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p9io1lk(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_message_6p6ba86(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jobmje4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_mkqn41e(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ss9nsh7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remote_tags_rssa7s7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_repo_dir_name_6927pg7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_resolve_within_c8j2tbk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_rev_parse_3l092b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_run_mj5dobu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sanitize_modes_mvteloh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha256_hex_4vhcm7o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_5d2ijcl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sbt8562(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_shbdnkf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_srei23l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_sut5g99(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_svn701p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stamp_of_li2ggn0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_store_for_package_347124r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_tree_checksum_k6o2imm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_update_pn3fq6a(void * a0, gm_b16_8 a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_c3c901bgg39[96] __attribute__((aligned(8)));
extern void *const gm_hl6hnpe3vbk7[];
extern void *const gm_hl8ph80s8bgt[];
extern void *const gm_hlbfnanio71s[];
extern void *const gm_hlhqfdrl9vfc[];
extern void *const gm_hlj1in77f861[];
extern void *const gm_hllt7e2ogusq[];
extern void *const gm_hlusqsd00u9k[];
extern void *const gm_hlvanl0isvqe[];
extern uint8_t gm_kbhckstt7he[40] __attribute__((aligned(8)));
extern uint8_t gm_kv6tql7n7ah[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0tnnm2dhgmh2[25];
extern const uint8_t gm_s205esok2u8pk[4];
extern const uint8_t gm_s2df2fvelno2h[7];
extern const uint8_t gm_s41sdeqq056ae[9];
extern const uint8_t gm_s433110cvrv0u[5];
extern const uint8_t gm_s48n17gh1k2md[3];
extern const uint8_t gm_s4vd6e3mkiiso[3];
extern const uint8_t gm_s5bh8t3p9l4nk[16];
extern const uint8_t gm_s5hom3aej4i0k[10];
extern const uint8_t gm_s615gqen9ca7q[16];
extern const uint8_t gm_s6kdp6nkaj7o9[12];
extern const uint8_t gm_s6pnfrub0rj1t[6];
extern const uint8_t gm_s91ab3v9oinn7[11];
extern const uint8_t gm_s9ok67plsnbj1[7];
extern const uint8_t gm_s9pknf9g05okt[3];
extern const uint8_t gm_sa23fj423hlk4[6];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_savr1gmccp3v7[20];
extern const uint8_t gm_sb423ab672pgh[8];
extern const uint8_t gm_sb86a9k7pfmnh[22];
extern const uint8_t gm_sbpcdsg6ifd91[5];
extern const uint8_t gm_sdmdremodiqov[25];
extern const uint8_t gm_seplj9699cert[10];
extern const uint8_t gm_sf9hl2ppv6mim[8];
extern const uint8_t gm_sffd375a90o1u[3];
extern const uint8_t gm_sg9gm9epbpvie[22];
extern const uint8_t gm_sgetvvdiqkhsm[18];
extern const uint8_t gm_sghautilepqio[13];
extern const uint8_t gm_si33msf4t0sbn[8];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sksgae11pvk6p[2];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl7p035rs001b[27];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snr2qdtkg0l3t[76];
extern const uint8_t gm_sns2lv549pkqt[53];
extern const uint8_t gm_so48b6sdoerku[18];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soa7cbv241ih1[11];
extern const uint8_t gm_sop5f3b03uipi[9];
extern const uint8_t gm_soso4l0tj1pgt[12];
extern const uint8_t gm_sp39731r4i1pk[10];
extern const uint8_t gm_sp4l42tpo6btg[11];
extern const uint8_t gm_sp7ec3ksfdilk[5];
extern const uint8_t gm_spc0i9srvnmq5[6];
extern const uint8_t gm_spkgb882eim6u[10];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr7sfbbsona7q[10];
extern const uint8_t gm_sr899jbhl9noj[3];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src3onm9035msn9[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcfjrpo4i6b0at[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srdb7m29fcjm3[3];
extern const uint8_t gm_srt7t7qcsma0n[6];
extern const uint8_t gm_srvc4tdn4aqfv[7];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3m5iduoqh8b[5];
extern const uint8_t gm_su2m3cb0nts7p[3];
extern const uint8_t gm_suass5sid4b6e[5];
extern const uint8_t gm_sv4jvo5eoeo8t[7];
extern const uint8_t gm_svbe9btc37i4e[13];
extern const uint8_t gm_svj5vmviieugq[9];
extern const uint8_t gm_svk1fq3llqdta[10];
extern const uint8_t gm_svulttobk4q7t[4];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    uint8_t t4;
    if (((((uint8_t *)&t3))[8] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_db_tssgvk0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_db_tssgvk0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* url */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su2m3cb0nts7p));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_l1;
    gm_b16_8 t6 = gm_f_repo_dir_name_6927pg7((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t7 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t4, t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_describe_read_error_uikkr59 F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    gm_toe5fmr3vvf4d t6 = gm_l1;
    gm_b16_8 t7 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sidpdvffdra02));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(13));
    t1 = t10;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3onm9035msn9, 85, 73);
me2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_downloads_vdenqu5(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_downloads_vdenqu5 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa2h9vonan0ma));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gu7qoap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gu7qoap F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n1oc8gq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n1oc8gq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qncp3l3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qncp3l3 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vogsd72 F;
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
            gm_tftb1odc739t5 t17 = gm_f_regrow_0jmnnpk(t14, t15, t16);
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
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 16);
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
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 16)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_g7467g2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_g7467g2 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
    uint8_t t5 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_5dma72c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_5dma72c F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = (t6 > ((uint64_t)0ULL));
        if (!t7) goto ie5;
        uint8_t t8 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sl3q52ep6aa53, 1) == 0));
        if (!t8) goto ie5;
        {
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_path_names_j9i34l1 *)((&F.h)->up))->l1);
            gm_b16_8 t11 = gm_l1;
            gm_tftb1odc739t5 t12 = gm_f_push_3ef746b(t10, t11);
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcbq9k9kvdokbf, 257, 41);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 256, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6l4llq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_6l4llq8 F;
    gm_b16_8 gm_l0; /* <item> */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_check_link_glakqpq *)((&F.h)->up))->l3));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcbq9k9kvdokbf, 274, 36);
        uint64_t t7 = r5;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_check_link_glakqpq *)((&F.h)->up))->l3), t7);
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 274, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g303nmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_g303nmp F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_link_chains_sttldqn *)((&F.h)->up))->l5);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcbq9k9kvdokbf, 295, 42);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 295, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hb9k0f9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_hb9k0f9 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_for_body_jvjn1fs *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcbq9k9kvdokbf, 139, 42);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 139, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_v43n3ub F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_for_body_ukps8ob *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcbq9k9kvdokbf, 173, 42);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 173, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_s32ien0 F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tg8e8rhc3qio9 t2;
        GM_ST(uint32_t, (uint8_t *)&t2 + 0, ((uint32_t)1779033703ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 4, ((uint32_t)3144134277ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 8, ((uint32_t)1013904242ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 12, ((uint32_t)2773480762ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 16, ((uint32_t)1359893119ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 20, ((uint32_t)2600822924ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 24, ((uint32_t)528734635ULL));
        GM_ST(uint32_t, (uint8_t *)&t2 + 28, ((uint32_t)1541459225ULL));
        gm_tsn02dhhitgs4 t3;
        for (uint64_t i = 0; i < 64; i++) GM_ST(uint8_t, (uint8_t *)&t3 + i * 1, ((uint8_t)0ULL));
        GM_ST(gm_tg8e8rhc3qio9, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 0, t2);
        GM_ST(gm_tsn02dhhitgs4, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 32, t3);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 96, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 104, ((uint64_t)0ULL));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_2kkkskr F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    uint8_t t2 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_6sd3mnh F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 229, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5j14m8ru132p t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            t5 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t7 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)1ULL)));
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_0k0up67 F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    uint8_t t2 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_node *pn3 = gm_find(cx, 2126215853);
        if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
        gm_tih6h4avv5nrb t4;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5j14m8ru132p t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            t5 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t4) + 8));
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        uint8_t t7 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sb1m5es F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t1, t2);
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
inline __attribute__((aligned(4))) void gm_f_list_69se6ht(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_69se6ht F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qncp3l3();
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
inline __attribute__((aligned(4))) void gm_f_list_8nlen85(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_8nlen85 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_gu7qoap();
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
inline __attribute__((aligned(4))) void gm_f_list_vg4gp98(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vg4gp98 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n1oc8gq();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_read_only_a29eok8 F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svulttobk4q7t));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(3));
        gm_b16_8 t3 = gm_l0;
        gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_make_writable_6uascrq F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spc0i9srvnmq5));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
        gm_b16_8 t3 = gm_l0;
        gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_parent_4i83qet(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parent_4i83qet F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_f_parent_lk0iero((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
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
            gm_th2lqbo77pvsc t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 16));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_b16_8 t29 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t29);
        goto gm_fdone;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_af3r83c(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_af3r83c F;
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
            gm_b16_8 t11 = gm_f_empty_slice_qncp3l3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ss9nsh7(t10, ((uint64_t)1ULL), t12);
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
        gm_tji6flc1b40vj t21 = GM_LD(gm_tji6flc1b40vj, ((uint8_t *)a1));
        GM_ST(gm_tji6flc1b40vj, ((uint8_t *)t20 + t18 * 56), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_htd7vjn(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_htd7vjn F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_n1oc8gq();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_mkqn41e(t10, ((uint64_t)1ULL), t12);
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
        gm_t9qkh0rsooc8d t21 = gm_l1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p9io1lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_p9io1lk F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_gu7qoap();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_jobmje4(t10, ((uint64_t)1ULL), t12);
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
        gm_t6j6smj7qoctu t21 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a1));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)t20 + t18 * 32), t21);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_message_6p6ba86(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_read_message_6p6ba86 F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tj5jn79kq9pnm t1 = gm_l0;
    gm_b16_8 t2 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jobmje4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_jobmje4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_mkqn41e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_mkqn41e F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ss9nsh7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ss9nsh7 F;
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
inline __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_tree_rp98g1s F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b0_1 t3 = gm_f_make_writable_6uascrq((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_l0;
        gm_ttln0d0hd70en t5 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = ((((uint8_t *)&t5))[0] != 0);
        F.ret = t5;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sanitize_modes_mvteloh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_sanitize_modes_mvteloh F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so48b6sdoerku));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
        gm_b16_8 t3 = gm_l0;
        gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha256_hex_4vhcm7o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_sha256_hex_4vhcm7o F;
    gm_b16_8 gm_l0; /* data */
    gm_t7mlrlibdq1ja gm_l1; /* d */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_f_sum_ugu720i((void *)&gm_l1, t2);
    }
    {
        void * t3 = ((uint8_t *)&gm_l1);
        if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcfjrpo4i6b0at, 35, 16);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(((uint8_t *)t3 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_b16_8 t6 = gm_f_hex_euih242((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_sort_by_5d2ijcl(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_5d2ijcl F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_1bllg4s(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stamp_of_li2ggn0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_stamp_of_li2ggn0 F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_gp81iq6((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_srcbq9k9kvdokbf, 410, 54);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    F.ret = t5;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sum_ugu720i F;
    gm_b16_8 gm_l0; /* data */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_init_s32ien0((void *)&F.l1);
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = gm_l0;
        gm_f_update_pn3fq6a(t2, t3);
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_f_final_rg5ko3p((void *)&(*(gm_t7mlrlibdq1ja *)gm_out), t4);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_archive_dir_srvvp3g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_archive_dir_srvvp3g F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* sha */
    GM_STACK_CHECK(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        gm_b16_8 t5 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = gm_f_repo_dir_name_6927pg7((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&t9) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t9)));
        uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t16 = (t15 < ((uint64_t)16ULL));
        if (!t16) goto ie14;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t19 = t18;
        t13 = t19;
        goto ix14;
ie14:;
        uint64_t t21 = ((uint64_t)16ULL);
        t13 = t21;
ix14:;
        uint64_t t22 = (uintptr_t)t13;
        if (t12 > t22 || t22 > (uintptr_t)t10) gm_trap_slice(t12, t22, t10, gm_srcbq9k9kvdokbf, 58, 64);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t11 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t12));
        gm_b16_8 t24 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t8, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = 0;
        F.ret = t24;
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
inline __attribute__((aligned(4))) void gm_f_check_link_chains_sttldqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_check_link_chains_sttldqn F;
    gm_b16_8 gm_l0; /* entries */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_b16_8 gm_l4; /* target */
    gm_b16_8 gm_l6; /* <tmp> */
    uint32_t gm_l7; /* hops */
    GM_STACK_CHECK(&F, 527u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_tji6flc1b40vj t15 = GM_LD(gm_tji6flc1b40vj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 56));
                    F.l3 = t15;
                    {
                        gm_th2lqbo77pvsc t17 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 32));
                        gm_b16_8 t18;
                        if (((((uint8_t *)&t17))[16] != 0)) {
                            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 0));
                            t18 = t19;
                        } else {
                            goto lc4;
                        }
                        gm_l4 = t18;
                    }
                    {
                        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        {
                            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            gm_l6 = t21;
                        }
                        gm_hval t22 = ((gm_hval){(void *)&F.h, cx});
                        gm_node wn23 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl8ph80s8bgt, .env = t22.env, .hctx = t22.cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
                        gm_b16_8 t26 = gm_l6;
                        gm_b16_8 t27;
                        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_st3ja0nqi37bv));
                        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(1));
                        gm_b0_1 t28 = gm_f_split_obtinjv_shbdnkf((void *)NULL, (&wn23), NULL, t26, t27);
                        if (gm_unw.kind) {
                            goto gm_ll25;
                        }
                        F.w1 = t28;
                        goto gm_ls29;
gm_ll25:;
                        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we24; }
                        goto gm_ll8;
gm_ls29:;
we24:;
                        gm_b0_1 t30 = F.w1;
                        gm_b0_1 t31 = t30;
                    }
                    {
                        {
                            void * t33 = (void *)((uint8_t *)&F.l5);
                            gm_th2lqbo77pvsc t34;
                            gm_f_pop_vr08sii((void *)&t34, t33);
                        }
                    }
                    {
                        gm_l7 = ((uint32_t)0ULL);
                    }
                    {
                        void * t36 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t37 = gm_l4;
                        gm_b16_8 t38 = gm_l0;
                        void * t39 = (void *)((uint8_t *)&gm_l7);
                        uint8_t t40 = gm_f_resolve_within_c8j2tbk((void *)NULL, cx, NULL, t36, t37, t38, t39);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        uint8_t t41 = (uint8_t)!t40;
                        if (!t41) goto ie35;
                        {
                            gm_t6j6smj7qoctu t43;
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t43 + 0, t44);
                            gm_b16_8 t45 = gm_l4;
                            GM_ST(gm_b16_8, (uint8_t *)&t43 + 16, t45);
                            gm_tabb710d9mq2e t46;
                            gm_f_format_u5g4crs((void *)&t46, (void *)NULL, cx, NULL, &t43);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcbq9k9kvdokbf, 299, 21);
                            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
                            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t48);
                            goto gm_fdone;
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 292, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t50 = gm_l2;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l2 = t53;
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
        gm_b0_1 t54;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t54);
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
__attribute__((aligned(4))) void gm_f_check_link_glakqpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_check_link_glakqpq F;
    gm_b16_8 gm_l0; /* rel */
    gm_b16_8 gm_l1; /* target */
    gm_b16_8 gm_l2; /* outside */
    gm_b16_8 gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* c */
    GM_STACK_CHECK(&F, 572u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_t6j6smj7qoctu t2;
        gm_b16_8 t3 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
        gm_b16_8 t4 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t4);
        gm_tabb710d9mq2e t5;
        gm_f_format_hedon8i((void *)&t5, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcbq9k9kvdokbf, 267, 16);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l2 = t6;
    }
    {
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
        uint8_t t10 = gm_f_starts_with_6vvt01e(t8, t9);
        if (!t10) goto ie7;
        {
            gm_b16_8 t12 = gm_l2;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t13);
            goto gm_fdone;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        F.l3 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t15 = gm_l0;
            gm_l4 = t15;
        }
        gm_hval t16 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn17 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlhqfdrl9vfc, .env = t16.env, .hctx = t16.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t20 = gm_l4;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
        gm_b0_1 t22 = gm_f_split_obtinjv_srei23l((void *)NULL, (&wn17), NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_ll19;
        }
        F.w0 = t22;
        goto gm_ls23;
gm_ll19:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we18; }
        goto gm_lad;
gm_ls23:;
we18:;
        gm_b0_1 t24 = F.w0;
        gm_b0_1 t25 = t24;
    }
    {
        uint64_t t26 = F.l3;
        uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
        if (o28) gm_trap("integer overflow in `-`", gm_srcbq9k9kvdokbf, 275, 5);
        uint64_t t29 = r27;
        F.l3 = t29;
    }
    {
        {
            gm_b16_8 t31 = gm_l1;
            gm_trqfvmjtt4sij t32;
            gm_f_path_names_j9i34l1((void *)&t32, (void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 0));
            gm_l5 = t33;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t35 = gm_l6;
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t37 = (t35 < t36);
                if (!t37) goto lb34;
                {
                    uint64_t t40 = gm_l6;
                    uint64_t t41 = (uintptr_t)t40;
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t41 * 16));
                    gm_l7 = t45;
                    {
                        uint8_t t48 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_snenkhfev166s, 2) == 0));
                        if (!t48) goto ie47;
                        {
                            uint64_t t51 = F.l3;
                            uint8_t t52 = ((t51 == ((uint64_t)0ULL)));
                            if (!t52) goto ie50;
                            {
                                gm_b16_8 t54 = gm_l2;
                                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t55);
                                goto gm_fdone;
                            }
                            goto ix50;
ie50:;
ix50:;
                        }
                        {
                            uint64_t t56 = F.l3;
                            uint64_t r57; bool o58 = __builtin_sub_overflow(t56, ((uint64_t)1ULL), &r57);
                            if (o58) gm_trap("integer overflow in `-`", gm_srcbq9k9kvdokbf, 279, 13);
                            uint64_t t59 = r57;
                            F.l3 = t59;
                        }
                        goto ix47;
ie47:;
                        {
                            uint64_t t61 = F.l3;
                            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                            if (o63) gm_trap("integer overflow in `+`", gm_srcbq9k9kvdokbf, 281, 13);
                            uint64_t t64 = r62;
                            F.l3 = t64;
                        }
ix47:;
                    }
                    goto me43;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 276, 5);
me43:;
                }
lc34:;
                {
                    uint64_t t66 = gm_l6;
                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                    uint64_t t69 = r67;
                    gm_l6 = t69;
                }
            }
lb34:;
        }
    }
    {
        gm_b0_1 t70;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t70);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_checkout_dir_mvijcuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_checkout_dir_mvijcuu F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* rev */
    GM_STACK_CHECK(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_spkgb882eim6u));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
        gm_b16_8 t5 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = gm_f_repo_dir_name_6927pg7((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&t9) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t9)));
        uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t16 = (t15 < ((uint64_t)12ULL));
        if (!t16) goto ie14;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t19 = t18;
        t13 = t19;
        goto ix14;
ie14:;
        uint64_t t21 = ((uint64_t)12ULL);
        t13 = t21;
ix14:;
        uint64_t t22 = (uintptr_t)t13;
        if (t12 > t22 || t22 > (uintptr_t)t10) gm_trap_slice(t12, t22, t10, gm_srcbq9k9kvdokbf, 63, 70);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t11 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t12));
        gm_b16_8 t24 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t8, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = 0;
        F.ret = t24;
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
__attribute__((aligned(4))) void gm_f_checkout_j42th18(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_checkout_j42th18 F;
    gm_b16_8 gm_l0; /* db */
    gm_b16_8 gm_l1; /* rev */
    gm_b16_8 gm_l2; /* dest */
    gm_b16_8 gm_l3; /* parent_dir */
    gm_b16_8 gm_l4; /* tmp */
    gm_b16_8 gm_l5; /* index */
    gm_toe5fmr3vvf4d gm_l6; /* e */
    gm_tpmdr2hmdhsqr gm_l8; /* env */
    gm_t3ujhu6qph7de gm_l9; /* res */
    gm_b16_8 gm_l10; /* e */
    gm_toe5fmr3vvf4d gm_l11; /* e */
    GM_STACK_CHECK(&F, 1408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t3 = gm_l2;
        uint8_t t4 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t6);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t7 = gm_l2;
        gm_th2lqbo77pvsc t8;
        gm_f_parent_4i83qet((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sgetvvdiqkhsm));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(17));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        gm_l3 = t9;
    }
    {
        gm_b16_8 t12 = gm_l3;
        gm_tl572d7579hns t13;
        gm_f_make_dirs_gbp8nfr((void *)&t13, (void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_tl572d7579hns t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
    }
    {
        gm_b16_8 t17 = gm_l3;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s41sdeqq056ae));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(8));
        gm_b16_8 t19 = gm_f_private_name_83ij38e((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        gm_b16_8 t20 = gm_l4;
        gm_tnpifv20irnph t21;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
        gm_tabb710d9mq2e t22;
        gm_f_format_dr7jgt6((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcbq9k9kvdokbf, 188, 14);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
        gm_l5 = t23;
    }
    {
        gm_b16_8 t24 = gm_l4;
        gm_node *pn25 = gm_find(cx, 2126215853);
        if (!pn25) gm_trap("no handler installed for effect `Fs`", gm_srcbq9k9kvdokbf, 189, 5);
        gm_ttln0d0hd70en t26;
        t26 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn25->clauses[8])(pn25->env, pn25->hctx, pn25, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t27;
        if (!((((uint8_t *)&t26))[0] != 0)) {
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 4));
            t27 = t28;
        } else {
            gm_toe5fmr3vvf4d t29 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t26) + 4));
            gm_l6 = t29;
            gm_t6j6smj7qoctu t31;
            gm_b16_8 t32 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t32);
            gm_toe5fmr3vvf4d t33 = gm_l6;
            gm_b16_8 t34 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t31 + 16, t34);
            gm_tabb710d9mq2e t35;
            gm_f_format_5uun4vl((void *)&t35, (void *)NULL, cx, NULL, &t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcbq9k9kvdokbf, 189, 42);
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t37);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s4vd6e3mkiiso));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 0, t38);
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s615gqen9ca7q));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(15));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 16, t39);
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s6kdp6nkaj7o9));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(11));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 32, t40);
        gm_b16_8 t41 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 48, t41);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s41sdeqq056ae));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 64, t42);
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sr899jbhl9noj));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 80, t43);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sffd375a90o1u));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 96, t44);
        gm_b16_8 t45 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 112, t45);
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s9pknf9g05okt));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 128, t46);
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&F.l7 + 144, t47);
    }
    {
        gm_b16_8 t48 = gm_l5;
        gm_tnpifv20irnph t49;
        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
        gm_tabb710d9mq2e t50;
        gm_f_format_ij4da0d((void *)&t50, (void *)NULL, cx, NULL, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcbq9k9kvdokbf, 191, 22);
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        gm_tpmdr2hmdhsqr t53;
        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
        gm_l8 = t53;
    }
    {
        gm_b16_8 t54 = gm_l0;
        gm_th2lqbo77pvsc t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t55), t54);
        ((uint8_t *)&t55)[16] = 1;
        void * t56 = ((uint8_t *)&F.l7);
        if ((uintptr_t)0 > (uintptr_t)10 || (uintptr_t)10 > (uintptr_t)10) gm_trap_slice((uintptr_t)0, (uintptr_t)10, 10, gm_srcbq9k9kvdokbf, 192, 20);
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t56 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)((uintptr_t)10 - (uintptr_t)0));
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        void * t59 = ((uint8_t *)&gm_l8);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcbq9k9kvdokbf, 192, 30);
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t59 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_f_git_uu8ibkt((void *)&gm_l9, (void *)NULL, cx, NULL, &t55, t58, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t63 = gm_l5;
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
            gm_ttln0d0hd70en t65 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t67;
        if (!((((uint8_t *)&gm_l9))[0] != 0)) goto mn69;
        t67 = ((uint8_t)1);
        goto me68;
mn69:;
        t67 = ((uint8_t)0);
        goto me68;
mn70:;
        gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 194, 8);
me68:;
        if (!t67) goto ie66;
        {
            {
                gm_b16_8 t73 = gm_l4;
                gm_ttln0d0hd70en t74 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            {
                gm_t3ujhu6qph7de t76 = gm_l9;
                gm_b16_8 t77;
                if (!((((uint8_t *)&t76))[0] != 0)) {
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                    t77 = t78;
                } else {
                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                    gm_l10 = t79;
                    gm_b16_8 t81 = gm_l10;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t81);
                    goto gm_fdone;
                }
            }
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        gm_b16_8 t82 = gm_l4;
        gm_b16_8 t83 = gm_l2;
        gm_node *pn84 = gm_find(cx, 2126215853);
        if (!pn84) gm_trap("no handler installed for effect `Fs`", gm_srcbq9k9kvdokbf, 198, 5);
        gm_ttln0d0hd70en t85;
        t85 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn84->clauses[7])(pn84->env, pn84->hctx, pn84, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t86;
        if (!((((uint8_t *)&t85))[0] != 0)) {
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 4));
            t86 = t87;
        } else {
            gm_toe5fmr3vvf4d t88 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t85) + 4));
            gm_l11 = t88;
            {
                {
                    gm_b16_8 t91 = gm_l4;
                    gm_ttln0d0hd70en t92 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t91);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            gm_b16_8 t94 = gm_l2;
            uint8_t t95 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t96 = (uint8_t)!t95;
            if (!t96) goto ie93;
            {
                gm_t6j6smj7qoctu t98;
                gm_b16_8 t99 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
                gm_toe5fmr3vvf4d t100 = gm_l11;
                gm_b16_8 t101 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t101);
                gm_tabb710d9mq2e t102;
                gm_f_format_5uun4vl((void *)&t102, (void *)NULL, cx, NULL, &t98);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_srcbq9k9kvdokbf, 201, 39);
                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t104);
                goto gm_fdone;
            }
            goto ix93;
ie93:;
ix93:;
            gm_b0_1 t105 = GM_ZERO(gm_b0_1);
            t86 = t105;
        }
    }
    {
        gm_b0_1 t106;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t106);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_chmod_tree_sj4qk6r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_chmod_tree_sj4qk6r F;
    gm_b16_8 gm_l0; /* mode */
    gm_b16_8 gm_l1; /* root */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srt7t7qcsma0n));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srdb7m29fcjm3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 16, t3);
        gm_b16_8 t4 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 32, t4);
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l2 + 48, t5);
    }
    {
        {
            void * t7 = ((uint8_t *)&F.l2);
            if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcbq9k9kvdokbf, 386, 13);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t7 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
            gm_tipro5npl6rd8 t10;
            gm_f_run_mj5dobu((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
__attribute__((aligned(4))) void gm_f_collect_entries_al4dgqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_collect_entries_al4dgqv F;
    gm_b16_8 gm_l1; /* rel */
    void * gm_l2; /* out */
    gm_b16_8 gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* name */
    gm_b16_8 gm_l10; /* path */
    gm_b16_8 gm_l11; /* entry_rel */
    gm_b16_8 gm_l12; /* target */
    GM_STACK_CHECK(&F, 1172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vg4gp98((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = F.l0;
            gm_l5 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hllt7e2ogusq, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l5;
        gm_b0_1 t9 = gm_f_entries_44o5n9f_s58h1st((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t9;
        goto gm_ls10;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls10:;
we6:;
        gm_b0_1 t11 = F.w0;
        gm_b0_1 t12 = t11;
    }
    {
        {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_l6 = t15;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l7;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    gm_tf1bjg81u0glv t22;
                    uint64_t t23 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t22 + 0, t23);
                    uint64_t t24 = gm_l7;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t25 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t22 + 8, t27);
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t22) + 0));
                    gm_l8 = t30;
                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
                    gm_l9 = t31;
                    {
                        uint8_t t34 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l9)), gm_sp7ec3ksfdilk, 4) == 0));
                        if (!t34) goto ie33;
                        {
                            goto lc16;
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    {
                        gm_b16_8 t36 = F.l0;
                        gm_b16_8 t37 = gm_l9;
                        gm_b16_8 t38 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t36, t37);
                        if (gm_unw.kind) {
                            goto gm_ll20;
                        }
                        gm_l10 = t38;
                    }
                    {
                        gm_b16_8 t39;
                        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        uint8_t t42 = ((t41 == ((uint64_t)0ULL)));
                        if (!t42) goto ie40;
                        gm_b16_8 t44 = gm_l9;
                        gm_b16_8 t45 = t44;
                        t39 = t45;
                        goto ix40;
ie40:;
                        gm_t6j6smj7qoctu t47;
                        gm_b16_8 t48 = gm_l1;
                        GM_ST(gm_b16_8, (uint8_t *)&t47 + 0, t48);
                        gm_b16_8 t49 = gm_l9;
                        GM_ST(gm_b16_8, (uint8_t *)&t47 + 16, t49);
                        gm_tabb710d9mq2e t50;
                        gm_f_format_d801g52((void *)&t50, (void *)NULL, cx, NULL, &t47);
                        if (gm_unw.kind) {
                            goto gm_ll20;
                        }
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcbq9k9kvdokbf, 238, 54);
                        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                        gm_b16_8 t52 = t51;
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                        t39 = t53;
ix40:;
                        gm_l11 = t39;
                    }
                    {
                        uint64_t t54 = gm_l8;
                        uint64_t t55 = (uintptr_t)t54;
                        uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                        if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_srcbq9k9kvdokbf, 239, 15);
                        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t55 * 1) + 0));
                        switch (t63) { case ((uint8_t)0ULL): goto ma59; case ((uint8_t)1ULL): goto ma58; case ((uint8_t)2ULL): goto ma60; case ((uint8_t)3ULL): goto ma61; default: goto ma62; }
ma58:;
                        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t55 * 1) + 0));
                        if (t65 != ((uint8_t)1ULL)) goto mn64;
                        {
                            gm_b16_8 t67 = gm_l10;
                            gm_b16_8 t68 = gm_l11;
                            void * t69 = gm_l2;
                            gm_tl572d7579hns t70;
                            gm_f_collect_entries_al4dgqv((void *)&t70, (void *)NULL, cx, NULL, t67, t68, t69);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            if (((((uint8_t *)&t70))[0] != 0)) {
                                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
                                gm_tl572d7579hns t72;
                                GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                                ((uint8_t *)&t72)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t72 + 8), t71);
                                F.ret = t72;
                                goto gm_fret;
                            }
                            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 8));
                        }
                        goto me57;
mn64:;
ma59:;
                        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t55 * 1) + 0));
                        if (t75 != ((uint8_t)0ULL)) goto mn74;
                        {
                            void * t77 = gm_l2;
                            gm_tji6flc1b40vj t78;
                            gm_b16_8 t79 = gm_l11;
                            GM_ST(gm_b16_8, (uint8_t *)&t78 + 0, t79);
                            gm_b16_8 t80 = gm_l10;
                            GM_ST(gm_b16_8, (uint8_t *)&t78 + 16, t80);
                            gm_th2lqbo77pvsc t81;
                            memset(&t81, 0, sizeof t81);
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t78 + 32, t81);
                            gm_tftb1odc739t5 t82 = gm_f_push_af3r83c(t77, &t78);
                            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcbq9k9kvdokbf, 241, 24);
                            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                        }
                        goto me57;
mn74:;
ma60:;
                        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t55 * 1) + 0));
                        if (t85 != ((uint8_t)2ULL)) goto mn84;
                        {
                            gm_b16_8 t87 = gm_l10;
                            gm_t3ujhu6qph7de t88;
                            gm_f_link_target_lkg20fb((void *)&t88, (void *)NULL, cx, NULL, t87);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            if (((((uint8_t *)&t88))[0] != 0)) {
                                gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
                                gm_tl572d7579hns t90;
                                GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
                                ((uint8_t *)&t90)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t90 + 8), t89);
                                F.ret = t90;
                                goto gm_fret;
                            }
                            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
                            gm_l12 = t91;
                        }
                        {
                            gm_b16_8 t92 = gm_l11;
                            gm_b16_8 t93 = gm_l12;
                            gm_tl572d7579hns t94;
                            gm_f_check_link_glakqpq((void *)&t94, (void *)NULL, cx, NULL, t92, t93);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            if (((((uint8_t *)&t94))[0] != 0)) {
                                gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
                                gm_tl572d7579hns t96;
                                GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                                ((uint8_t *)&t96)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t96 + 8), t95);
                                F.ret = t96;
                                goto gm_fret;
                            }
                            gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 8));
                        }
                        {
                            void * t98 = gm_l2;
                            gm_tji6flc1b40vj t99;
                            gm_b16_8 t100 = gm_l11;
                            GM_ST(gm_b16_8, (uint8_t *)&t99 + 0, t100);
                            gm_b16_8 t101 = gm_l10;
                            GM_ST(gm_b16_8, (uint8_t *)&t99 + 16, t101);
                            gm_b16_8 t102 = gm_l12;
                            gm_th2lqbo77pvsc t103;
                            GM_ST(uint8_t, (uint8_t *)&t103 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t103 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t103 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t103), t102);
                            ((uint8_t *)&t103)[16] = 1;
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t99 + 32, t103);
                            gm_tftb1odc739t5 t104 = gm_f_push_af3r83c(t98, &t99);
                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcbq9k9kvdokbf, 245, 17);
                            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                        }
                        goto me57;
mn84:;
ma61:;
                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t55 * 1) + 0));
                        if (t107 != ((uint8_t)3ULL)) goto mn106;
                        goto me57;
mn106:;
ma62:;
                        gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 239, 9);
me57:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 235, 5);
me28:;
                }
lc16:;
                {
                    uint64_t t110 = gm_l7;
                    uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                    uint64_t t113 = r111;
                    gm_l7 = t113;
                }
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
            }
            goto gm_lad;
lb16:;
        }
    }
    {
        gm_b0_1 t114;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t114);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_db_tssgvk0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_downloads_vdenqu5(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_fetch_mirror_q06gvv0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_fetch_mirror_q06gvv0 F;
    gm_b16_8 gm_l0; /* db */
    gm_b16_8 gm_l1; /* url */
    gm_b16_8 gm_l2; /* p */
    gm_b0_8 gm_l5; /* <temporary array> */
    gm_b16_8 gm_l6; /* e */
    GM_STACK_CHECK(&F, 946u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sbpcdsg6ifd91));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
        gm_b16_8 t5 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) goto ie2;
        {
            gm_b16_8 t10 = gm_l0;
            gm_th2lqbo77pvsc t11;
            gm_f_parent_4i83qet((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t11))[16] != 0)) goto ie9;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_l2 = t12;
            {
                gm_b16_8 t14 = gm_l2;
                gm_tl572d7579hns t15;
                gm_f_make_dirs_gbp8nfr((void *)&t15, (void *)NULL, cx, NULL, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t15))[0] != 0)) {
                    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
                    gm_tl572d7579hns t17;
                    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                    ((uint8_t *)&t17)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 8), t16);
                    F.ret = t17;
                    goto gm_fret;
                }
                gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 8));
            }
            goto ix9;
ie9:;
ix9:;
        }
        {
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_suass5sid4b6e));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 0, t19);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sr899jbhl9noj));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 16, t20);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sv4jvo5eoeo8t));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 32, t21);
            gm_b16_8 t22 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 48, t22);
        }
        {
            {
                gm_th2lqbo77pvsc t24;
                memset(&t24, 0, sizeof t24);
                void * t25 = ((uint8_t *)&F.l3);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcbq9k9kvdokbf, 126, 27);
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t25 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                gm_b16_8 t28 = gm_f_empty_slice_5nn7ph0();
                gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                gm_t3ujhu6qph7de t30;
                gm_f_git_uu8ibkt((void *)&t30, (void *)NULL, cx, NULL, &t24, t27, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t30))[0] != 0)) {
                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                    gm_tl572d7579hns t32;
                    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                    ((uint8_t *)&t32)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
                    F.ret = t32;
                    goto gm_fret;
                }
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s6pnfrub0rj1t));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 0, t34);
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sr899jbhl9noj));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 16, t35);
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_si33msf4t0sbn));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 32, t36);
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sb423ab672pgh));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 48, t37);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s9pknf9g05okt));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 64, t38);
        gm_b16_8 t39 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 80, t39);
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sl7p035rs001b));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(26));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 96, t40);
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s0tnnm2dhgmh2));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(24));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 112, t41);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sb86a9k7pfmnh));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(21));
        GM_ST(gm_b16_8, (uint8_t *)&F.l4 + 128, t42);
    }
    {
    }
    {
        {
            gm_b16_8 t44 = gm_l0;
            gm_th2lqbo77pvsc t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t45), t44);
            ((uint8_t *)&t45)[16] = 1;
            void * t46 = ((uint8_t *)&F.l4);
            if ((uintptr_t)0 > (uintptr_t)9 || (uintptr_t)9 > (uintptr_t)9) gm_trap_slice((uintptr_t)0, (uintptr_t)9, 9, gm_srcbq9k9kvdokbf, 129, 17);
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t46 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)((uintptr_t)9 - (uintptr_t)0));
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
            {
                gm_b0_8 t50;
                gm_l5 = t50;
            }
            void * t51 = ((uint8_t *)&gm_l5);
            if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srcbq9k9kvdokbf, 129, 27);
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t51 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
            gm_b16_8 t53 = t52;
            gm_t3ujhu6qph7de t54;
            gm_f_git_uu8ibkt((void *)&t54, (void *)NULL, cx, NULL, &t45, t48, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t55;
            if (!((((uint8_t *)&t54))[0] != 0)) {
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                t55 = t56;
            } else {
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                gm_l6 = t57;
                gm_t6j6smj7qoctu t59;
                gm_b16_8 t60 = gm_l1;
                GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
                gm_b16_8 t61 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t59 + 16, t61);
                gm_tabb710d9mq2e t62;
                gm_f_format_nvg3me5((void *)&t62, (void *)NULL, cx, NULL, &t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_srcbq9k9kvdokbf, 129, 64);
                gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t64);
                goto gm_fdone;
            }
        }
    }
    {
        gm_b0_1 t65;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t65);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_g7467g2(void * a0, void * a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_5dma72c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6l4llq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_86gje82(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_86gje82 F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* err */
    gm_tibupt4dmcqai gm_l3; /* e */
    GM_STACK_CHECK(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_tibupt4dmcqai t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            t6 = (*(gm_tibupt4dmcqai *)(((uint8_t *)&t5) + 8));
        } else {
            gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t5) + 8));
            gm_l2 = t7;
            gm_t6j6smj7qoctu t9;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->l0));
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t10);
            gm_toe5fmr3vvf4d t11 = gm_l2;
            gm_b16_8 t12 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 16, t12);
            gm_tabb710d9mq2e t13;
            gm_f_format_04pispc((void *)&t13, (void *)NULL, cx, NULL, &t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcbq9k9kvdokbf, 231, 38);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            GM_ST(uint8_t, (uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->ret + 4, 0);
            ((uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->ret)[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->ret + 8, t15);
            uint8_t t16 = ((((uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->ret))[0] != 0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t16;
            goto gm_lad;
        }
        gm_l3 = t6;
    }
    {
        void * t17 = (void *)((uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->l3);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_tabb710d9mq2e t19;
        gm_f_dupe_g9iqpvn((void *)&t19, (void *)NULL, cx, NULL, t18, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kv6tql7n7ah)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcbq9k9kvdokbf, 232, 20);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        gm_tftb1odc739t5 t22 = gm_f_push_3ef746b(t17, t21);
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcbq9k9kvdokbf, 232, 9);
        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
    }
    {
        void * t24 = (void *)((uint8_t *)&((struct gm_fr_collect_entries_al4dgqv *)((&F.h)->up))->l4);
        gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 16));
        gm_tftb1odc739t5 t26 = gm_f_push_htd7vjn(t24, t25);
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcbq9k9kvdokbf, 233, 9);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 230, 5);
me1:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g303nmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hb9k0f9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_jvjn1fs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_jvjn1fs F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* obj */
    gm_b16_8 gm_l6; /* target */
    gm_b16_8 gm_l7; /* t */
    gm_b16_8 gm_l8; /* b */
    GM_STACK_CHECK(&F, 673u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t6 = gm_l1;
            gm_l3 = t6;
        }
        gm_hval t7 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn8 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlj1in77f861, .env = t7.env, .hctx = t7.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t11 = gm_l3;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sksgae11pvk6p));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
        gm_b0_1 t13 = gm_f_split_obtinjv_sbt8562((void *)NULL, (&wn8), NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_ll10;
        }
        F.w0 = t13;
        goto gm_ls14;
gm_ll10:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we9; }
        goto gm_lad;
gm_ls14:;
we9:;
        gm_b0_1 t15 = F.w0;
        gm_b0_1 t16 = t15;
    }
    {
        uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t19 = (t18 < ((uint64_t)2ULL));
        if (!t19) goto ie17;
        {
            gm_b0_1 t21;
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcbq9k9kvdokbf, 141, 17);
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t23 * 16));
        gm_l4 = t25;
    }
    {
        uint64_t t26 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcbq9k9kvdokbf, 142, 16);
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t26 * 16));
        gm_l5 = t28;
    }
    {
        gm_b16_8 t29 = gm_l5;
        gm_l6 = t29;
    }
    {
        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t32 = (t31 > ((uint64_t)2ULL));
        if (!t32) goto ie30;
        uint64_t t33 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcbq9k9kvdokbf, 144, 36);
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t33 * 16) + 8);
        uint8_t t36 = (t35 > ((uint64_t)0ULL));
        if (!t36) goto ie30;
        {
            uint64_t t38 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t39 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcbq9k9kvdokbf, 144, 70);
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t38 * 16));
            gm_l6 = t40;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        gm_b16_8 t42 = gm_l4;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_soa7cbv241ih1));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(10));
        gm_th2lqbo77pvsc t44;
        gm_f_strip_prefix_b4949s8((void *)&t44, t42, t43);
        if (!((((uint8_t *)&t44))[16] != 0)) goto ie41;
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        gm_l7 = t45;
        {
            void * t47 = (void *)(((uint8_t *)&((struct gm_fr_list_refs_j3is1o5 *)((&F.h)->up))->l3) + 0);
            gm_t6j6smj7qoctu t48;
            gm_b16_8 t49 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
            gm_b16_8 t50 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t48 + 16, t50);
            gm_tftb1odc739t5 t51 = gm_f_push_p9io1lk(t47, &t48);
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srcbq9k9kvdokbf, 146, 13);
            gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
        }
        goto ix41;
ie41:;
        gm_b16_8 t54 = gm_l4;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_soso4l0tj1pgt));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(11));
        gm_th2lqbo77pvsc t56;
        gm_f_strip_prefix_b4949s8((void *)&t56, t54, t55);
        if (!((((uint8_t *)&t56))[16] != 0)) goto ie53;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_l8 = t57;
        {
            void * t59 = (void *)(((uint8_t *)&((struct gm_fr_list_refs_j3is1o5 *)((&F.h)->up))->l3) + 40);
            gm_t6j6smj7qoctu t60;
            gm_b16_8 t61 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t61);
            gm_b16_8 t62 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t62);
            gm_tftb1odc739t5 t63 = gm_f_push_p9io1lk(t59, &t60);
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcbq9k9kvdokbf, 148, 13);
            gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
        }
        goto ix53;
ie53:;
        uint8_t t66 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8) == 15) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l4)), gm_s5bh8t3p9l4nk, 15) == 0));
        if (!t66) goto ie65;
        {
            gm_b16_8 t68 = gm_l6;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&((struct gm_fr_list_refs_j3is1o5 *)((&F.h)->up))->l3) + 80)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&((struct gm_fr_list_refs_j3is1o5 *)((&F.h)->up))->l3) + 80)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&((struct gm_fr_list_refs_j3is1o5 *)((&F.h)->up))->l3) + 80)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&((struct gm_fr_list_refs_j3is1o5 *)((&F.h)->up))->l3) + 80)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&((struct gm_fr_list_refs_j3is1o5 *)((&F.h)->up))->l3) + 80)) + 0, t68);
        }
        goto ix65;
ie65:;
ix65:;
ix53:;
ix41:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 137, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ukps8ob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_ukps8ob F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* t */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t6 = gm_l1;
            gm_l3 = t6;
        }
        gm_hval t7 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn8 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl6hnpe3vbk7, .env = t7.env, .hctx = t7.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t11 = gm_l3;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sksgae11pvk6p));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
        gm_b0_1 t13 = gm_f_split_obtinjv_svn701p((void *)NULL, (&wn8), NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_ll10;
        }
        F.w0 = t13;
        goto gm_ls14;
gm_ll10:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we9; }
        goto gm_lad;
gm_ls14:;
we9:;
        gm_b0_1 t15 = F.w0;
        gm_b0_1 t16 = t15;
    }
    {
        uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t19 = (t18 > ((uint64_t)1ULL));
        if (!t19) goto ie17;
        {
            uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srcbq9k9kvdokbf, 175, 38);
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t22 * 16));
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_soa7cbv241ih1));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(10));
            gm_th2lqbo77pvsc t26;
            gm_f_strip_prefix_b4949s8((void *)&t26, t24, t25);
            if (!((((uint8_t *)&t26))[16] != 0)) goto ie21;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_l4 = t27;
            {
                void * t29 = (void *)((uint8_t *)&((struct gm_fr_remote_tags_rssa7s7 *)((&F.h)->up))->l5);
                gm_b16_8 t30 = gm_l4;
                gm_tftb1odc739t5 t31 = gm_f_push_3ef746b(t29, t30);
                if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcbq9k9kvdokbf, 175, 70);
                gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
            }
            goto ix21;
ie21:;
ix21:;
        }
        goto ix17;
ie17:;
ix17:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 171, 5);
me1:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_git_uu8ibkt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_git_uu8ibkt F;
    gm_th2lqbo77pvsc gm_l0; /* git_dir */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l2; /* env */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* d */
    gm_b16_8 gm_l9; /* sub */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* a */
    GM_STACK_CHECK(&F, 1005u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s205esok2u8pk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t4 = gm_f_push_3ef746b(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcbq9k9kvdokbf, 76, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        {
            void * t7 = ((uint8_t *)gm_c3c901bgg39);
            if ((uintptr_t)0 > (uintptr_t)6 || (uintptr_t)6 > (uintptr_t)6) gm_trap_slice((uintptr_t)0, (uintptr_t)6, 6, gm_srcbq9k9kvdokbf, 77, 5);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t7 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)((uintptr_t)6 - (uintptr_t)0));
            gm_l4 = t8;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l5;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 16));
                    gm_l6 = t20;
                    {
                        void * t22 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t23;
                        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s48n17gh1k2md));
                        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t24 = gm_f_push_3ef746b(t22, t23);
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcbq9k9kvdokbf, 78, 9);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    {
                        void * t26 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t27 = gm_l6;
                        gm_tftb1odc739t5 t28 = gm_f_push_3ef746b(t26, t27);
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcbq9k9kvdokbf, 79, 9);
                        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 77, 5);
me18:;
                }
lc9:;
                {
                    uint64_t t31 = gm_l5;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l5 = t34;
                }
            }
lb9:;
        }
    }
    {
        void * t35 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sg9gm9epbpvie));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(21));
        gm_tftb1odc739t5 t37 = gm_f_push_3ef746b(t35, t36);
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcbq9k9kvdokbf, 81, 5);
        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
    }
    {
        void * t39 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t40 = gm_l2;
        gm_tftb1odc739t5 t41 = gm_f_extend_vogsd72(t39, t40);
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcbq9k9kvdokbf, 82, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        void * t43 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t45 = gm_f_push_3ef746b(t43, t44);
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcbq9k9kvdokbf, 83, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
    }
    {
        void * t47 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s4vd6e3mkiiso));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t49 = gm_f_push_3ef746b(t47, t48);
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srcbq9k9kvdokbf, 86, 5);
        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
    }
    {
        void * t51 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sdmdremodiqov));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(24));
        gm_tftb1odc739t5 t53 = gm_f_push_3ef746b(t51, t52);
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcbq9k9kvdokbf, 87, 5);
        gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
    }
    {
        if (!((((uint8_t *)&gm_l0))[16] != 0)) goto ie55;
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
        gm_l7 = t56;
        {
            void * t58 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s5hom3aej4i0k));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(9));
            gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t58, t59);
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcbq9k9kvdokbf, 89, 9);
            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
        }
        {
            void * t62 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t63 = gm_l7;
            gm_tftb1odc739t5 t64 = gm_f_push_3ef746b(t62, t63);
            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcbq9k9kvdokbf, 90, 9);
            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        void * t66 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t67 = gm_l1;
        gm_tftb1odc739t5 t68 = gm_f_extend_vogsd72(t66, t67);
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcbq9k9kvdokbf, 92, 5);
        gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
    }
    {
        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
        gm_f_run_mj5dobu((void *)&F.l8, (void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&F.l8) + 4));
        uint8_t t74 = ((t73 == ((uint32_t)127ULL)));
        if (!t74) goto ie72;
        uint8_t t75 = 0;
        uint8_t t76 = 0;
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_svbe9btc37i4e));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(12));
        uint8_t t79 = gm_f_contains_gkki4kq(t77, t78);
        if (!t79) {
            uint64_t t80 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 24) + 8);
            uint8_t t81 = ((t80 == ((uint64_t)0ULL)));
            t76 = t81;
        } else t76 = 1;
        if (!t76) {
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
            gm_b16_8 t83;
            GM_ST(void *, (uint8_t *)&t83, (void *)(gm_seplj9699cert));
            GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(9));
            uint8_t t84 = gm_f_contains_gkki4kq(t82, t83);
            t75 = t84;
        } else t75 = 1;
        if (!t75) goto ie72;
        {
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_snr2qdtkg0l3t));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(75));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t86);
            goto gm_fdone;
        }
        goto ix72;
ie72:;
ix72:;
    }
    {
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
        if (!t88) goto ie87;
        {
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t90);
            goto gm_fdone;
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(3));
        gm_l9 = t91;
    }
    {
        {
            gm_b16_8 t93 = gm_l1;
            gm_l10 = t93;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t95 = gm_l11;
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t97 = (t95 < t96);
                if (!t97) goto lb94;
                {
                    uint64_t t100 = gm_l11;
                    uint64_t t101 = (uintptr_t)t100;
                    uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t101 * 16));
                    gm_l12 = t105;
                    {
                        gm_b16_8 t108 = gm_l12;
                        gm_b16_8 t109;
                        GM_ST(void *, (uint8_t *)&t109, (void *)(gm_skolmnfc0ccbd));
                        GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(1));
                        uint8_t t110 = gm_f_starts_with_6vvt01e(t108, t109);
                        uint8_t t111 = (uint8_t)!t110;
                        if (!t111) goto ie107;
                        {
                            gm_b16_8 t113 = gm_l12;
                            gm_l9 = t113;
                        }
                        {
                            goto lb94;
                        }
                        goto ix107;
ie107:;
ix107:;
                    }
                    goto me103;
mn104:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 99, 5);
me103:;
                }
lc94:;
                {
                    uint64_t t115 = gm_l11;
                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                    uint64_t t118 = r116;
                    gm_l11 = t118;
                }
            }
lb94:;
        }
    }
    {
        gm_t6j6smj7qoctu t119;
        gm_b16_8 t120 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t120);
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
        gm_b16_8 t122 = gm_f_trim_fdrfe6i(t121);
        GM_ST(gm_b16_8, (uint8_t *)&t119 + 16, t122);
        gm_tabb710d9mq2e t123;
        gm_f_format_d5h9av5((void *)&t123, (void *)NULL, cx, NULL, &t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_srcbq9k9kvdokbf, 105, 13);
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t125);
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
inline __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_last_used_6quhcig(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_last_used_6quhcig F;
    gm_b16_8 gm_l0; /* dir */
    gm_t5j14m8ru132p gm_l1; /* info */
    gm_t5j14m8ru132p gm_l2; /* info */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = gm_f_stamp_of_li2ggn0((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_node *pn5 = gm_find(cx, 2126215853);
        if (!pn5) gm_trap("no handler installed for effect `Fs`", gm_srcbq9k9kvdokbf, 421, 16);
        gm_tih6h4avv5nrb t6;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn5->clauses[5])((void *)&t6, pn5->env, pn5->hctx, pn5, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) goto ie2;
        gm_l1 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t6) + 8));
        {
            int64_t t8 = GM_LD(int64_t, (((uint8_t *)&gm_l1) + 16));
            gm_t913bb735pvt5 t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t9), t8);
            ((uint8_t *)&t9)[8] = 1;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t12 = gm_l0;
        gm_node *pn13 = gm_find(cx, 2126215853);
        if (!pn13) gm_trap("no handler installed for effect `Fs`", gm_srcbq9k9kvdokbf, 422, 16);
        gm_tih6h4avv5nrb t14;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn13->clauses[5])((void *)&t14, pn13->env, pn13->hctx, pn13, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t14))[0] != 0)) goto ie11;
        gm_l2 = (*(gm_t5j14m8ru132p *)(((uint8_t *)&t14) + 8));
        {
            int64_t t16 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + 16));
            gm_t913bb735pvt5 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[8] = 1;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_t913bb735pvt5 t19;
        memset(&t19, 0, sizeof t19);
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
inline __attribute__((aligned(4))) gm_t2doi72kmmlil gm_f_le_bytes_r7p21h4(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_le_bytes_r7p21h4 F;
    uint64_t gm_l0; /* n */
    gm_t2doi72kmmlil gm_l1; /* b */
    uint64_t gm_l2; /* v */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 187u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
    }
    {
        uint64_t t2 = gm_l0;
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            gm_l3 = ((uint64_t)8ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = gm_l3;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        if (t17 >= (uintptr_t)(8)) gm_trap_index(t17, 8, gm_srcbq9k9kvdokbf, 375, 21);
                        uint64_t t18 = gm_l2;
                        uint64_t t19 = gm_l5;
                        uint64_t r20; bool o21 = __builtin_mul_overflow(((uint64_t)8ULL), t19, &r20);
                        if (o21) gm_trap("integer overflow in `*`", gm_srcbq9k9kvdokbf, 375, 38);
                        uint64_t t22 = r20;
                        unsigned __int128 n23 = (unsigned __int128)t22;
                        if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcbq9k9kvdokbf, 375, 29);
                        uint64_t t24 = (uint64_t)(t18 >> (int)n23);
                        uint8_t t25 = (uint8_t)t24;
                        GM_ST(uint8_t, (((uint8_t *)&gm_l1) + t17 * 1), t25);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 375, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
                }
            }
lb5:;
        }
    }
    {
        gm_t2doi72kmmlil t31 = gm_l1;
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_link_at_qflj1mk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_link_at_qflj1mk F;
    gm_b16_8 gm_l0; /* entries */
    gm_b16_8 gm_l1; /* at */
    gm_b16_8 gm_l2; /* joined */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t4;
        gm_f_join_sfknljb((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcbq9k9kvdokbf, 311, 15);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l2 = t5;
    }
    {
        {
            gm_b16_8 t7 = gm_l0;
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
                    uint64_t t14 = gm_l4;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tji6flc1b40vj t19 = GM_LD(gm_tji6flc1b40vj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 56));
                    F.l5 = t19;
                    {
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t23 = gm_l2;
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                        uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t22), ((uint8_t *)&t24)));
                        if (!t25) goto ie21;
                        {
                            memmove(gm_out, (((uint8_t *)&F.l5) + 32), 24);
                            goto gm_fdone;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 312, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t28 = gm_l4;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l4 = t31;
                }
            }
lb8:;
        }
    }
    {
        gm_th2lqbo77pvsc t32;
        memset(&t32, 0, sizeof t32);
        uint8_t t33 = 0;
        F.ret = t32;
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
inline __attribute__((aligned(4))) void gm_f_link_target_lkg20fb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_link_target_lkg20fb F;
    gm_b16_8 gm_l0; /* path */
    gm_t0kl141g5m2qf gm_l1; /* args */
    gm_b16_8 gm_l3; /* out */
    GM_STACK_CHECK(&F, 476u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sop5f3b03uipi));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&gm_l1 + 0, t2);
        gm_b16_8 t3 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&gm_l1 + 16, t3);
    }
    {
        void * t4 = ((uint8_t *)&gm_l1);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcbq9k9kvdokbf, 220, 14);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(((uint8_t *)t4 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_f_run_mj5dobu((void *)&F.l2, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) goto ie7;
        {
            gm_t6j6smj7qoctu t11;
            gm_b16_8 t12 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
            gm_b16_8 t13;
            uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 24) + 8);
            uint8_t t16 = (t15 > ((uint64_t)0ULL));
            if (!t16) goto ie14;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
            gm_b16_8 t19 = gm_f_trim_fdrfe6i(t18);
            gm_b16_8 t20 = t19;
            t13 = t20;
            goto ix14;
ie14:;
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_savr1gmccp3v7));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(19));
            gm_b16_8 t23 = t22;
            t13 = t23;
ix14:;
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t13);
            gm_tabb710d9mq2e t24;
            gm_f_format_04pispc((void *)&t24, (void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcbq9k9kvdokbf, 221, 27);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
        gm_l3 = t27;
    }
    {
        gm_b16_8 t29 = gm_l3;
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(1));
        uint8_t t31 = gm_f_ends_with_biriu5l(t29, t30);
        if (!t31) goto ie28;
        {
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t34 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint64_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `-`", gm_srcbq9k9kvdokbf, 223, 43);
            uint64_t t39 = r37;
            uint64_t t40 = (uintptr_t)t39;
            if (t35 > t40 || t40 > (uintptr_t)t33) gm_trap_slice(t35, t40, t33, gm_srcbq9k9kvdokbf, 223, 36);
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t34 + t35 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40 - t35));
            gm_l3 = t41;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        gm_b16_8 t42 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t42);
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
inline __attribute__((aligned(4))) void gm_f_list_refs_j3is1o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_list_refs_j3is1o5 F;
    gm_b16_8 gm_l0; /* db */
    gm_b16_8 gm_l2; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 488u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sghautilepqio));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sns2lv549pkqt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 16, t3);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sp4l42tpo6btg));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_svk1fq3llqdta));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 48, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s91ab3v9oinn7));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(10));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 64, t6);
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_th2lqbo77pvsc t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[16] = 1;
        void * t9 = ((uint8_t *)&F.l1);
        if ((uintptr_t)0 > (uintptr_t)5 || (uintptr_t)5 > (uintptr_t)5) gm_trap_slice((uintptr_t)0, (uintptr_t)5, 5, gm_srcbq9k9kvdokbf, 135, 24);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t9 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)((uintptr_t)5 - (uintptr_t)0));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_b16_8 t12 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_t3ujhu6qph7de t14;
        gm_f_git_uu8ibkt((void *)&t14, (void *)NULL, cx, NULL, &t8, t11, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t14))[0] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
            gm_tv61mo81rn7eb t16;
            memset(&t16, 0, sizeof t16);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
            F.ret = t16;
            goto gm_fret;
        }
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
        gm_l2 = t17;
    }
    {
        gm_f_list_8nlen85((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_8nlen85((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 40)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t18;
        memset(&t18, 0, sizeof t18);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l3 + 80, t18);
    }
    {
        {
            gm_b16_8 t20 = gm_l2;
            gm_l4 = t20;
        }
        gm_hval t21 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn22 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlbfnanio71s, .env = t21.env, .hctx = t21.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t25 = gm_l4;
        gm_b0_1 t26 = gm_f_lines_9uns0k2_snhh6ss((void *)NULL, (&wn22), NULL, t25);
        if (gm_unw.kind) {
            goto gm_ll24;
        }
        F.w0 = t26;
        goto gm_ls27;
gm_ll24:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we23; }
        goto gm_lad;
gm_ls27:;
we23:;
        gm_b0_1 t28 = F.w0;
        gm_b0_1 t29 = t28;
    }
    {
        const uint8_t *t30 = ((uint8_t *)&F.l3);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tv61mo81rn7eb *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tv61mo81rn7eb *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tv61mo81rn7eb *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tv61mo81rn7eb *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_tv61mo81rn7eb *)gm_out) + 8, t30, 104);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_make_read_only_a29eok8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_make_writable_6uascrq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mark_used_5dtkpil(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_mark_used_5dtkpil F;
    gm_b16_8 gm_l0; /* dir */
    gm_t0kl141g5m2qf gm_l1; /* args */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sa23fj423hlk4));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&gm_l1 + 0, t2);
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = gm_f_stamp_of_li2ggn0((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&gm_l1 + 16, t4);
    }
    {
        {
            void * t6 = ((uint8_t *)&gm_l1);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcbq9k9kvdokbf, 415, 13);
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t6 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            gm_tipro5npl6rd8 t9;
            gm_f_run_mj5dobu((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) void gm_f_path_names_j9i34l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_path_names_j9i34l1 F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlvanl0isvqe, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
        gm_b0_1 t10 = gm_f_split_obtinjv_sut5g99((void *)NULL, (&wn5), NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls11:;
we6:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_remote_tags_rssa7s7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remote_tags_rssa7s7 F;
    gm_b16_8 gm_l0; /* url */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l4; /* out */
    gm_b16_8 gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr7sfbbsona7q));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srvc4tdn4aqfv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 16, t3);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s9ok67plsnbj1));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s9pknf9g05okt));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 48, t5);
        gm_b16_8 t6 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 64, t6);
    }
    {
    }
    {
        gm_th2lqbo77pvsc t7;
        memset(&t7, 0, sizeof t7);
        void * t8 = ((uint8_t *)&F.l1);
        if ((uintptr_t)0 > (uintptr_t)5 || (uintptr_t)5 > (uintptr_t)5) gm_trap_slice((uintptr_t)0, (uintptr_t)5, 5, gm_srcbq9k9kvdokbf, 169, 22);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t8 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)((uintptr_t)5 - (uintptr_t)0));
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        {
            gm_b0_8 t12;
            gm_l2 = t12;
        }
        void * t13 = ((uint8_t *)&gm_l2);
        if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srcbq9k9kvdokbf, 169, 32);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t13 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
        gm_b16_8 t15 = t14;
        gm_t3ujhu6qph7de t16;
        gm_f_git_uu8ibkt((void *)&t16, (void *)NULL, cx, NULL, &t7, t10, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t17;
        if (!((((uint8_t *)&t16))[0] != 0)) {
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            t17 = t18;
        } else {
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_l3 = t19;
            gm_t6j6smj7qoctu t21;
            gm_b16_8 t22 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t21 + 0, t22);
            gm_b16_8 t23 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t21 + 16, t23);
            gm_tabb710d9mq2e t24;
            gm_f_format_cibac8a((void *)&t24, (void *)NULL, cx, NULL, &t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 8, gm_srcbq9k9kvdokbf, 169, 69);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        gm_l4 = t17;
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t28 = gm_l4;
            gm_l6 = t28;
        }
        gm_hval t29 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn30 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlusqsd00u9k, .env = t29.env, .hctx = t29.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t33 = gm_l6;
        gm_b0_1 t34 = gm_f_lines_9uns0k2_samvo8i((void *)NULL, (&wn30), NULL, t33);
        if (gm_unw.kind) {
            goto gm_ll32;
        }
        F.w0 = t34;
        goto gm_ls35;
gm_ll32:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we31; }
        goto gm_lad;
gm_ls35:;
we31:;
        gm_b0_1 t36 = F.w0;
        gm_b0_1 t37 = t36;
    }
    {
        const uint8_t *t38 = ((uint8_t *)&F.l5);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_t470aqkia5212 *)gm_out) + 8, t38, 40);
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
extern __attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_tree_rp98g1s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_repo_dir_name_6927pg7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_repo_dir_name_6927pg7 F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* last */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* found */
    uint64_t gm_l4; /* j */
    gm_b16_8 gm_l5; /* clean */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* k */
    uint8_t gm_l9; /* c */
    uint8_t gm_l10; /* <tmp> */
    uint8_t gm_l11; /* <tmp> */
    uint8_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* start */
    GM_STACK_CHECK(&F, 889u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s433110cvrv0u));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
        gm_l1 = t2;
    }
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t3;
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            uint8_t t7 = gm_l3;
            uint8_t t8 = (uint8_t)!t7;
            if (!t8) goto lb4;
            {
                uint64_t t11 = gm_l2;
                gm_l4 = t11;
            }
            {
                for (;;) {
                    uint64_t t13 = gm_l4;
                    uint8_t t14 = (t13 > ((uint64_t)0ULL));
                    if (!t14) goto lb12;
                    uint64_t t15 = gm_l4;
                    uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
                    if (o17) gm_trap("integer overflow in `-`", gm_srcbq9k9kvdokbf, 27, 29);
                    uint64_t t18 = r16;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcbq9k9kvdokbf, 27, 25);
                    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t19 * 1));
                    gm_tqs5p4n2tq19v t22;
                    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)47ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t22 + 1), ((uint8_t)58ULL));
                    uint8_t t23 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 0, ((uint8_t *)&t21))) t23 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t22) + 1, ((uint8_t *)&t21))) t23 = 1;
                    uint8_t t24 = (uint8_t)(!t23);
                    if (!t24) goto lb12;
                    {
                        uint64_t t27 = gm_l4;
                        uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                        if (o29) gm_trap("integer overflow in `-`", gm_srcbq9k9kvdokbf, 27, 56);
                        uint64_t t30 = r28;
                        gm_l4 = t30;
                    }
lc12:;
                }
lb12:;
            }
            {
                uint64_t t32 = gm_l4;
                uint64_t t33 = gm_l2;
                uint8_t t34 = (t32 < t33);
                if (!t34) goto ie31;
                {
                    uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t37 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t38 = gm_l4;
                    uint64_t t39 = (uintptr_t)t38;
                    uint64_t t40 = gm_l2;
                    uint64_t t41 = (uintptr_t)t40;
                    if (t39 > t41 || t41 > (uintptr_t)t36) gm_trap_slice(t39, t41, t36, gm_srcbq9k9kvdokbf, 29, 20);
                    gm_b16_8 t42;
                    GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t37 + t39 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41 - t39));
                    gm_l1 = t42;
                }
                {
                    gm_l3 = ((uint8_t)1);
                }
                goto ix31;
ie31:;
ix31:;
            }
            {
                uint64_t t43;
                uint64_t t45 = gm_l4;
                uint8_t t46 = (t45 > ((uint64_t)0ULL));
                if (!t46) goto ie44;
                uint64_t t48 = gm_l4;
                uint64_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint64_t)1ULL), &r49);
                if (o50) gm_trap("integer overflow in `-`", gm_srcbq9k9kvdokbf, 32, 24);
                uint64_t t51 = r49;
                uint64_t t52 = t51;
                t43 = t52;
                goto ix44;
ie44:;
                uint64_t t54 = ((uint64_t)0ULL);
                t43 = t54;
ix44:;
                gm_l2 = t43;
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        gm_tabb710d9mq2e t56;
        gm_f_make_2ip78bj((void *)&t56, (void *)NULL, cx, NULL, t55, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbhckstt7he)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcbq9k9kvdokbf, 34, 14);
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
        gm_l5 = t57;
    }
    {
        {
            gm_b16_8 t59 = gm_l1;
            gm_l6 = t59;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t61 = gm_l7;
                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t63 = (t61 < t62);
                if (!t63) goto lb60;
                {
                    uint64_t t66 = gm_l7;
                    uint64_t t67 = gm_l7;
                    uint64_t t68 = (uintptr_t)t67;
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t68 * 1));
                    gm_tf5srh4mp4b3i t71;
                    GM_ST(uint8_t, (uint8_t *)&t71 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t71 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t71 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t71 + 0), t66);
                    GM_ST(uint8_t, ((uint8_t *)&t71 + 8), t70);
                    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&t71) + 0));
                    gm_l8 = t74;
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t71) + 8));
                    gm_l9 = t75;
                    {
                        uint64_t t77 = gm_l8;
                        uint64_t t78 = (uintptr_t)t77;
                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcbq9k9kvdokbf, 36, 9);
                        uint8_t *t80 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t78 * 1);
                        uint8_t t81;
                        uint8_t t83 = 0;
                        uint8_t t84 = 0;
                        uint8_t t85 = 0;
                        {
                            uint8_t t87 = gm_l9;
                            gm_l10 = t87;
                        }
                        uint8_t t88 = 0;
                        uint8_t t89 = gm_l10;
                        uint8_t t90 = (((uint8_t)97ULL) <= t89);
                        if (t90) {
                            uint8_t t91 = gm_l10;
                            uint8_t t92 = (t91 <= ((uint8_t)122ULL));
                            t88 = t92;
                        } else t88 = 0;
                        uint8_t t93 = t88;
                        if (!t93) {
                            {
                                uint8_t t95 = gm_l9;
                                gm_l11 = t95;
                            }
                            uint8_t t96 = 0;
                            uint8_t t97 = gm_l11;
                            uint8_t t98 = (((uint8_t)65ULL) <= t97);
                            if (t98) {
                                uint8_t t99 = gm_l11;
                                uint8_t t100 = (t99 <= ((uint8_t)90ULL));
                                t96 = t100;
                            } else t96 = 0;
                            uint8_t t101 = t96;
                            t85 = t101;
                        } else t85 = 1;
                        if (!t85) {
                            {
                                uint8_t t103 = gm_l9;
                                gm_l12 = t103;
                            }
                            uint8_t t104 = 0;
                            uint8_t t105 = gm_l12;
                            uint8_t t106 = (((uint8_t)48ULL) <= t105);
                            if (t106) {
                                uint8_t t107 = gm_l12;
                                uint8_t t108 = (t107 <= ((uint8_t)57ULL));
                                t104 = t108;
                            } else t104 = 0;
                            uint8_t t109 = t104;
                            t84 = t109;
                        } else t84 = 1;
                        if (!t84) {
                            uint8_t t110 = gm_l9;
                            gm_toh0ootodhjue t111;
                            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)45ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t111 + 1), ((uint8_t)95ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t111 + 2), ((uint8_t)46ULL));
                            uint8_t t112 = 0;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t111) + 0, ((uint8_t *)&t110))) t112 = 1;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t111) + 1, ((uint8_t *)&t110))) t112 = 1;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t111) + 2, ((uint8_t *)&t110))) t112 = 1;
                            uint8_t t113 = (uint8_t)(t112);
                            t83 = t113;
                        } else t83 = 1;
                        if (!t83) goto ie82;
                        uint8_t t115 = gm_l9;
                        uint8_t t116 = t115;
                        t81 = t116;
                        goto ix82;
ie82:;
                        uint8_t t118 = ((uint8_t)95ULL);
                        t81 = t118;
ix82:;
                        GM_ST(uint8_t, t80, t81);
                    }
                    goto me72;
mn73:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 35, 5);
me72:;
                }
lc60:;
                {
                    uint64_t t120 = gm_l7;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    uint64_t t123 = r121;
                    gm_l7 = t123;
                }
            }
lb60:;
        }
    }
    {
        gm_l13 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t125 = gm_l13;
            uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t127 = (t125 < t126);
            if (!t127) goto lb124;
            uint64_t t128 = gm_l13;
            uint64_t t129 = (uintptr_t)t128;
            uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t129 >= (uintptr_t)(t130)) gm_trap_index(t129, t130, gm_srcbq9k9kvdokbf, 39, 33);
            uint8_t t131 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t129 * 1));
            uint8_t t132 = ((t131 == ((uint8_t)46ULL)));
            if (!t132) goto lb124;
            {
                uint64_t t135 = gm_l13;
                uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
                if (o137) gm_trap("integer overflow in `+`", gm_srcbq9k9kvdokbf, 39, 55);
                uint64_t t138 = r136;
                gm_l13 = t138;
            }
lc124:;
        }
lb124:;
    }
    {
        gm_t6j6smj7qoctu t139;
        uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        void * t141 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
        uint64_t t142 = gm_l13;
        uint64_t t143 = (uintptr_t)t142;
        if (t143 > (uintptr_t)t140 || (uintptr_t)t140 > (uintptr_t)t140) gm_trap_slice(t143, (uintptr_t)t140, t140, gm_srcbq9k9kvdokbf, 40, 39);
        gm_b16_8 t144;
        GM_ST(void *, (uint8_t *)&t144, (void *)(((uint8_t *)t141 + t143 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)((uintptr_t)t140 - t143));
        gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
        GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t145);
        gm_b16_8 t146 = gm_l0;
        gm_b16_8 t147 = gm_f_sha256_hex_4vhcm7o((void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&t147) + 8);
        void * t149 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t147)));
        uint64_t t150 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t151 = (uintptr_t)((uint64_t)16ULL);
        if (t150 > t151 || t151 > (uintptr_t)t148) gm_trap_slice(t150, t151, t148, gm_srcbq9k9kvdokbf, 40, 56);
        gm_b16_8 t152;
        GM_ST(void *, (uint8_t *)&t152, (void *)(((uint8_t *)t149 + t150 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(t151 - t150));
        GM_ST(gm_b16_8, (uint8_t *)&t139 + 16, t152);
        gm_tabb710d9mq2e t153;
        gm_f_format_c31q7f8((void *)&t153, (void *)NULL, cx, NULL, &t139);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 8, gm_srcbq9k9kvdokbf, 40, 12);
        gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&t153) + 8));
        gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
        uint8_t t156 = 0;
        F.ret = t155;
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
__attribute__((aligned(4))) uint8_t gm_f_resolve_within_c8j2tbk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_resolve_within_c8j2tbk F;
    void * gm_l0; /* at */
    gm_b16_8 gm_l1; /* target */
    gm_b16_8 gm_l2; /* entries */
    void * gm_l3; /* hops */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* c */
    gm_b16_8 gm_l7; /* next */
    GM_STACK_CHECK(&F, 564u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t3 = gm_l1;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t9 = gm_l1;
            gm_trqfvmjtt4sij t10;
            gm_f_path_names_j9i34l1((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 0));
            gm_l4 = t11;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l5;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l5;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t19 * 16));
                    gm_l6 = t23;
                    {
                        uint8_t t26 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l6)), gm_snenkhfev166s, 2) == 0));
                        if (!t26) goto ie25;
                        {
                            void * t29 = gm_l0;
                            gm_th2lqbo77pvsc t30;
                            gm_f_pop_vr08sii((void *)&t30, t29);
                            uint8_t t31;
                            if (((((uint8_t *)&t30))[16] != 0)) goto mn33;
                            t31 = ((uint8_t)1);
                            goto me32;
mn33:;
                            t31 = ((uint8_t)0);
                            goto me32;
mn34:;
                            gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 325, 16);
me32:;
                            if (!t31) goto ie28;
                            {
                                uint8_t t36 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto ix25;
ie25:;
                        {
                            void * t38 = gm_l0;
                            gm_b16_8 t39 = gm_l6;
                            gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t38, t39);
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srcbq9k9kvdokbf, 327, 13);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        {
                            gm_b16_8 t43 = gm_l2;
                            void * t44 = gm_l0;
                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t44) + 0));
                            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                            gm_th2lqbo77pvsc t47;
                            gm_f_link_at_qflj1mk((void *)&t47, (void *)NULL, cx, NULL, t43, t46);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                            if (!((((uint8_t *)&t47))[16] != 0)) goto ie42;
                            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                            gm_l7 = t48;
                            {
                                void * t50 = gm_l3;
                                void * t51 = gm_l3;
                                uint32_t t52 = GM_LD(uint32_t, ((uint8_t *)t51));
                                uint32_t r53; bool o54 = __builtin_add_overflow(t52, ((uint32_t)1ULL), &r53);
                                if (o54) gm_trap("integer overflow in `+`", gm_srcbq9k9kvdokbf, 329, 17);
                                uint32_t t55 = r53;
                                GM_ST(uint32_t, ((uint8_t *)t50), t55);
                            }
                            {
                                void * t57 = gm_l3;
                                uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)t57));
                                uint8_t t59 = (t58 > ((uint32_t)40ULL));
                                if (!t59) goto ie56;
                                {
                                    uint8_t t61 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto ix56;
ie56:;
ix56:;
                            }
                            {
                                {
                                    void * t63 = gm_l0;
                                    gm_th2lqbo77pvsc t64;
                                    gm_f_pop_vr08sii((void *)&t64, t63);
                                }
                            }
                            {
                                void * t66 = gm_l0;
                                gm_b16_8 t67 = gm_l7;
                                gm_b16_8 t68 = gm_l2;
                                void * t69 = gm_l3;
                                uint8_t t70 = gm_f_resolve_within_c8j2tbk((void *)NULL, cx, NULL, t66, t67, t68, t69);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                                uint8_t t71 = (uint8_t)!t70;
                                if (!t71) goto ie65;
                                {
                                    uint8_t t73 = 0;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto ix65;
ie65:;
ix65:;
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 323, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t75 = gm_l5;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l5 = t78;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        uint8_t t79 = 0;
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
inline __attribute__((aligned(4))) void gm_f_rev_parse_3l092b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_rev_parse_3l092b4 F;
    gm_b16_8 gm_l0; /* db */
    gm_b16_8 gm_l1; /* rev */
    gm_b16_8 gm_l2; /* spec */
    gm_b0_8 gm_l4; /* <temporary array> */
    gm_b16_8 gm_l5; /* out */
    gm_b16_8 gm_l6; /* t */
    GM_STACK_CHECK(&F, 525u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_anrgoqv((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcbq9k9kvdokbf, 158, 13);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l2 = t5;
    }
    {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sp39731r4i1pk));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 0, t6);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_svj5vmviieugq));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(8));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 16, t7);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sf9hl2ppv6mim));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 32, t8);
        gm_b16_8 t9 = gm_l2;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 48, t10);
    }
    {
    }
    {
        gm_b16_8 t11 = gm_l0;
        gm_th2lqbo77pvsc t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t12), t11);
        ((uint8_t *)&t12)[16] = 1;
        void * t13 = ((uint8_t *)&F.l3);
        if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcbq9k9kvdokbf, 160, 20);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t13 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        {
            gm_b0_8 t17;
            gm_l4 = t17;
        }
        void * t18 = ((uint8_t *)&gm_l4);
        if ((uintptr_t)0 > (uintptr_t)0 || (uintptr_t)0 > (uintptr_t)0) gm_trap_slice((uintptr_t)0, (uintptr_t)0, 0, gm_srcbq9k9kvdokbf, 160, 30);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t18 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)((uintptr_t)0 - (uintptr_t)0));
        gm_b16_8 t20 = t19;
        gm_t3ujhu6qph7de t21;
        gm_f_git_uu8ibkt((void *)&t21, (void *)NULL, cx, NULL, &t12, t15, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t22;
        if (!((((uint8_t *)&t21))[0] != 0)) {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
            t22 = t23;
        } else {
            gm_th2lqbo77pvsc t24;
            memset(&t24, 0, sizeof t24);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        gm_l5 = t22;
    }
    {
        gm_b16_8 t26 = gm_l5;
        gm_b16_8 t27 = gm_f_trim_fdrfe6i(t26);
        gm_l6 = t27;
    }
    {
        uint8_t t29 = 0;
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        uint8_t t31 = ((t30 == ((uint64_t)40ULL)));
        if (!t31) {
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint8_t t33 = ((t32 == ((uint64_t)64ULL)));
            t29 = t33;
        } else t29 = 1;
        if (!t29) goto ie28;
        {
            gm_b16_8 t35 = gm_l6;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t35);
            goto gm_fdone;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        gm_th2lqbo77pvsc t36;
        memset(&t36, 0, sizeof t36);
        uint8_t t37 = 0;
        F.ret = t36;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_sanitize_modes_mvteloh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_stamp_of_li2ggn0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_store_for_package_347124r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_store_for_package_347124r F;
    gm_b16_8 gm_l0; /* root */
    gm_b16_8 gm_l1; /* d */
    GM_STACK_CHECK(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_th2lqbo77pvsc t3;
        gm_f_cache_dir_81h90qu((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t3))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
        gm_l1 = t4;
        {
            gm_b16_8 t6 = gm_l1;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s0k6ioi39f878));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
            gm_b16_8 t8 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t9;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t11 = gm_l0;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s2df2fvelno2h));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(6));
        gm_b16_8 t13 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_st3m5iduoqh8b));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
        gm_b16_8 t15 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t16;
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
        uint8_t t17 = 0;
        F.ret = t16;
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
__attribute__((aligned(4))) void gm_f_tree_checksum_k6o2imm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_tree_checksum_k6o2imm F;
    gm_b16_8 gm_l0; /* root */
    gm_tivanu2mfcv6o gm_l3; /* zero */
    gm_t2doi72kmmlil gm_l4; /* ff */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l8; /* target */
    gm_t2doi72kmmlil gm_l9; /* <temporary array> */
    gm_tj5jn79kq9pnm gm_l10; /* err */
    gm_b16_8 gm_l11; /* data */
    gm_t2doi72kmmlil gm_l12; /* <temporary array> */
    gm_t7mlrlibdq1ja gm_l13; /* d */
    GM_STACK_CHECK(&F, 1199u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_69se6ht((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(0));
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_tl572d7579hns t5;
        gm_f_collect_entries_al4dgqv((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
            gm_t3ujhu6qph7de t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
    }
    {
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tl572d7579hns t11;
        gm_f_check_link_chains_sttldqn((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t11))[0] != 0)) {
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_t3ujhu6qph7de t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t12);
            F.ret = t13;
            goto gm_fret;
        }
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 8));
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_closure t16 = ((gm_closure){GM_PTR_TAG(gm_f_fn_g7467g2), NULL, cx});
        gm_f_sort_by_5d2ijcl(t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_init_s32ien0((void *)&F.l2);
    }
    {
        gm_tivanu2mfcv6o t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
        gm_l3 = t17;
    }
    {
        gm_t2doi72kmmlil t18;
        for (uint64_t i = 0; i < 8; i++) GM_ST(uint8_t, (uint8_t *)&t18 + i * 1, ((uint8_t)255ULL));
        gm_l4 = t18;
    }
    {
        {
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l5 = t20;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t22 = gm_l6;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l6;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tji6flc1b40vj t32 = GM_LD(gm_tji6flc1b40vj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t28 * 56));
                    F.l7 = t32;
                    {
                        void * t34 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_f_update_pn3fq6a(t34, t35);
                    }
                    {
                        void * t36 = (void *)((uint8_t *)&F.l2);
                        void * t37 = ((uint8_t *)&gm_l3);
                        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcbq9k9kvdokbf, 356, 18);
                        gm_b16_8 t38;
                        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t37 + (uintptr_t)0 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                        gm_f_update_pn3fq6a(t36, t39);
                    }
                    {
                        if (!(((((uint8_t *)&F.l7) + 32))[16] != 0)) goto ie40;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 32));
                        gm_l8 = t41;
                        {
                            void * t43 = (void *)((uint8_t *)&F.l2);
                            void * t44 = ((uint8_t *)&gm_l4);
                            if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcbq9k9kvdokbf, 358, 22);
                            gm_b16_8 t45;
                            GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t44 + (uintptr_t)0 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                            gm_f_update_pn3fq6a(t43, t46);
                        }
                        {
                        }
                        {
                            void * t47 = (void *)((uint8_t *)&F.l2);
                            {
                                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                gm_t2doi72kmmlil t50 = gm_f_le_bytes_r7p21h4(t49);
                                gm_l9 = t50;
                            }
                            void * t51 = ((uint8_t *)&gm_l9);
                            if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcbq9k9kvdokbf, 359, 22);
                            gm_b16_8 t52;
                            GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t51 + (uintptr_t)0 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                            gm_b16_8 t53 = t52;
                            gm_f_update_pn3fq6a(t47, t53);
                        }
                        {
                            void * t54 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t55 = gm_l8;
                            gm_f_update_pn3fq6a(t54, t55);
                        }
                        goto ix40;
ie40:;
                        {
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                            gm_tgvh15qshf643 t58;
                            gm_f_read_all_r9rh9qd((void *)&t58, (void *)NULL, cx, NULL, t57);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                            gm_b16_8 t59;
                            if (!((((uint8_t *)&t58))[0] != 0)) {
                                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
                                t59 = t60;
                            } else {
                                gm_tj5jn79kq9pnm t61 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t58) + 8));
                                gm_l10 = t61;
                                gm_t6j6smj7qoctu t63;
                                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 16));
                                GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t64);
                                gm_tj5jn79kq9pnm t65 = gm_l10;
                                gm_b16_8 t66 = gm_f_read_message_6p6ba86((void *)NULL, cx, NULL, t65);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t63 + 16, t66);
                                gm_tabb710d9mq2e t67;
                                gm_f_format_04pispc((void *)&t67, (void *)NULL, cx, NULL, &t63);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srcbq9k9kvdokbf, 362, 63);
                                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t69);
                                goto gm_fdone;
                            }
                            gm_l11 = t59;
                        }
                        {
                        }
                        {
                            void * t70 = (void *)((uint8_t *)&F.l2);
                            {
                                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                gm_t2doi72kmmlil t73 = gm_f_le_bytes_r7p21h4(t72);
                                gm_l12 = t73;
                            }
                            void * t74 = ((uint8_t *)&gm_l12);
                            if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcbq9k9kvdokbf, 363, 22);
                            gm_b16_8 t75;
                            GM_ST(void *, (uint8_t *)&t75, (void *)(((uint8_t *)t74 + (uintptr_t)0 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                            gm_b16_8 t76 = t75;
                            gm_f_update_pn3fq6a(t70, t76);
                        }
                        {
                            void * t77 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t78 = gm_l11;
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            gm_f_update_pn3fq6a(t77, t79);
                        }
ix40:;
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 354, 5);
me30:;
                }
lc21:;
                {
                    uint64_t t81 = gm_l6;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    uint64_t t84 = r82;
                    gm_l6 = t84;
                }
            }
gm_ll25:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb21; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc21; }
            }
            goto gm_lad;
lb21:;
        }
    }
    {
        void * t85 = (void *)((uint8_t *)&F.l2);
        gm_f_final_rg5ko3p((void *)&gm_l13, t85);
    }
    {
        void * t86 = ((uint8_t *)&gm_l13);
        if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcbq9k9kvdokbf, 368, 40);
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(((uint8_t *)t86 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        gm_b16_8 t89 = gm_f_hex_euih242((void *)NULL, cx, NULL, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t90;
        GM_ST(gm_b16_8, ((uint8_t *)&t90 + 0), t89);
        gm_tabb710d9mq2e t91;
        gm_f_format_9di2rrs((void *)&t91, (void *)NULL, cx, NULL, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_srcbq9k9kvdokbf, 368, 12);
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8, t93);
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

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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
typedef struct gm_csktnl8ual7c3g { uint32_t f0; _Bool f1; } gm_csktnl8ual7c3g;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csktnl8ual7c3g) == 8 && _Alignof(gm_csktnl8ual7c3g) == 4 && offsetof(gm_csktnl8ual7c3g, f0) == 0 && offsetof(gm_csktnl8ual7c3g, f1) == 4, "layout of `TypeInfo`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; uint32_t f2; } gm_t1ve8lm87pun7;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; int64_t f1; gm_tm98ap4112hee f2; } gm_t856vjvr00kbi;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; uint64_t f2; } gm_tscmbvdaq159i;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_t1ve8lm87pun7) == 20 && _Alignof(gm_t1ve8lm87pun7) == 4 && offsetof(gm_t1ve8lm87pun7, f0) == 0 && offsetof(gm_t1ve8lm87pun7, f1) == 4 && offsetof(gm_t1ve8lm87pun7, f2) == 16, "layout of `Slot((u32, u32, u32), u32)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
_Static_assert(sizeof(gm_t856vjvr00kbi) == 24 && _Alignof(gm_t856vjvr00kbi) == 8 && offsetof(gm_t856vjvr00kbi, f0) == 0 && offsetof(gm_t856vjvr00kbi, f1) == 8 && offsetof(gm_t856vjvr00kbi, f2) == 16, "layout of `Slot(i64, Child)`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; gm_trqfvmjtt4sij f2; } gm_tg1gv7r8d60ki;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tb40qn7hk0tl9 f0; uint8_t f1; uint8_t pe[7]; } gm_th6bh7gc364t4;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tscmbvdaq159i) == 32 && _Alignof(gm_tscmbvdaq159i) == 8 && offsetof(gm_tscmbvdaq159i, f0) == 0 && offsetof(gm_tscmbvdaq159i, f1) == 8 && offsetof(gm_tscmbvdaq159i, f2) == 24, "layout of `Slot(string, usize)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
_Static_assert(sizeof(gm_tg1gv7r8d60ki) == 64 && _Alignof(gm_tg1gv7r8d60ki) == 8 && offsetof(gm_tg1gv7r8d60ki, f0) == 0 && offsetof(gm_tg1gv7r8d60ki, f1) == 8 && offsetof(gm_tg1gv7r8d60ki, f2) == 24, "layout of `Slot(string, List(usize))`");
_Static_assert(sizeof(gm_th6bh7gc364t4) == 48 && _Alignof(gm_th6bh7gc364t4) == 8 && offsetof(gm_th6bh7gc364t4, f0) == 0 && offsetof(gm_th6bh7gc364t4, f1) == 40, "layout of `?SourceLocation`");
typedef struct { uint64_t f0; gm_th6bh7gc364t4 f1; } gm_ts5u7codfocvn;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_ttu09lij36f8c f2; } gm_tsr6pj9uclack;
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_ttu09lij36f8c f0; uint8_t f1; uint8_t pe[7]; } gm_tu3261i9p5758;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; gm_t6ok5tvcjc200 f2; } gm_tu3t6donebmau;
typedef struct { gm_ts5u7codfocvn f0; uint8_t f1; uint8_t pe[7]; } gm_tl9turq9ndgsc;
_Static_assert(sizeof(gm_ts5u7codfocvn) == 56 && _Alignof(gm_ts5u7codfocvn) == 8 && offsetof(gm_ts5u7codfocvn, f0) == 0 && offsetof(gm_ts5u7codfocvn, f1) == 8, "layout of `LiveBlock`");
_Static_assert(sizeof(gm_tsr6pj9uclack) == 48 && _Alignof(gm_tsr6pj9uclack) == 8 && offsetof(gm_tsr6pj9uclack, f0) == 0 && offsetof(gm_tsr6pj9uclack, f1) == 4 && offsetof(gm_tsr6pj9uclack, f2) == 8, "layout of `Slot(u32, ?View)`");
_Static_assert(sizeof(gm_tu3261i9p5758) == 48 && _Alignof(gm_tu3261i9p5758) == 8 && offsetof(gm_tu3261i9p5758, f0) == 0 && offsetof(gm_tu3261i9p5758, f1) == 40, "layout of `??View`");
_Static_assert(sizeof(gm_tu3t6donebmau) == 56 && _Alignof(gm_tu3t6donebmau) == 8 && offsetof(gm_tu3t6donebmau, f0) == 0 && offsetof(gm_tu3t6donebmau, f1) == 4 && offsetof(gm_tu3t6donebmau, f2) == 16, "layout of `Slot(Span, Hover)`");
_Static_assert(sizeof(gm_tl9turq9ndgsc) == 64 && _Alignof(gm_tl9turq9ndgsc) == 8 && offsetof(gm_tl9turq9ndgsc, f0) == 0 && offsetof(gm_tl9turq9ndgsc, f1) == 56, "layout of `?LiveBlock`");
struct gm_fr_deinit_vtu6170 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_360e8h0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_es387ts {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pu15hdv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tklonjv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_find_fi5pe57 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_find_fuvgmeg {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_find_ve3qs5b {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_f2mbmm8 {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_get_fq78978 {
    gm_frame h;
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_get_fqooi5f {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_get_ftjmcbc {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_v07up09 {
    gm_frame h;
    gm_tppo2hmut88dq ret;
};
struct gm_fr_get_v0dhnfj {
    gm_frame h;
    gm_tu3261i9p5758 ret;
};
struct gm_fr_grow_f7u1075 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_grow_ffgu6bs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_grow_fu99cpb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_hash_map_fupfd5g {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ve01i1t {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_vqsve4u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_insert_new_fhcpthq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_vbg72ug {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_vglvkli {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_vhmdu9t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_vm1l89f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_insert_new_vvf6ptg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_key_eq_7nhp7gb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_eq_d5vn1ip {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_eq_l22e6mq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_hash_7bps17u {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_at8t7r1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_futbl4l {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_hebjkbg {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_hj5j24t {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_nkn0rq8 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_key_hash_odgtuo9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_put_f1iksvi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_f99phma {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_ftmshk7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_fv8gs1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_v6g5pup {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_vrooh94 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remove_f5r5ejv {
    gm_frame h;
    gm_tl9turq9ndgsc ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_deinit_vtu6170(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_360e8h0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_es387ts(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tklonjv(void);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_58mj7o5(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_a1bt60v(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_fi5pe57(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_fuvgmeg(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ha0uq33(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hb6lt4h(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hjd0ufa(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_mod8kqd(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_p1k923m(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_pnb83dt(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_shfuv0t(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_u6q3m28(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_uarsnut(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ve3qs5b(void * a0, void * a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_f2mbmm8(void * a0, int64_t a1);
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_fqooi5f(void * a0, int64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_ftjmcbc(void * a0, gm_tn68el8d14vku a1);
__attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_v07up09(void * a0, int64_t a1);
__attribute__((aligned(4))) void gm_f_get_v0dhnfj(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tu3261i9p5758 gm_f_get_v0dhnfj_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_36b3d0o(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_3o0noai(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_6sn05as(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_ai5cphs(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_f7u1075(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_ffgu6bs(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_fu99cpb(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_gsr5sn1(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_fupfd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_vqsve4u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_insert_new_3g6r6p3(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_9oft181(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_anb2gma(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_dj80dk7(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_fhcpthq(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_l14losc(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_pni6tnm(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_r3mi47j(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_t90e9jk(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_vbg72ug(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_vglvkli(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_vhmdu9t(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_vm1l89f(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_insert_new_vvf6ptg(void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_7nhp7gb(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_d5vn1ip(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_l22e6mq(void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_7bps17u(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_at8t7r1(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_futbl4l(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_hebjkbg(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_hj5j24t(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_nkn0rq8(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_odgtuo9(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi_c(void * a0, gm_tiilmfd16v3fc a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f99phma(void * a0, gm_tn68el8d14vku a1, uint64_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ftmshk7(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ftmshk7_c(void * a0, uint32_t a1, gm_tjpqvfd72o62u a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_fv8gs1b(void * a0, uint32_t a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_vrooh94(void * a0, gm_cseahvg8v7p0vs a1, gm_csktnl8ual7c3g a2);
__attribute__((aligned(4))) void gm_f_remove_f5r5ejv(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tl9turq9ndgsc gm_f_remove_f5r5ejv_c(void * a0, uint64_t a1);
inline uint64_t gm_hash0iqiubfm7e(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash467d5cm1d5(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash9m6mvtmgde(const uint8_t *v, uint64_t h);
inline uint64_t gm_hash9v0qhqsmrd(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashb2nno7n67r(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashff85ac10o3(const uint8_t *v, uint64_t h);
inline uint64_t gm_hashsqt22rph28(const uint8_t *v, uint64_t h);
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_360e8h0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_360e8h0 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_es387ts(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_es387ts F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pu15hdv F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tklonjv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tklonjv F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_fq78978 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_p1k923m(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_ttsm01t0d8srg t8;
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
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 16) + 16));
        gm_ttsm01t0d8srg t15;
        GM_ST(gm_b0_1, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[0] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_fupfd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_fupfd5g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_360e8h0();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ve01i1t F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_pu15hdv();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_vqsve4u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_vqsve4u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_es387ts();
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
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_7nhp7gb(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_eq_7nhp7gb F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            void * t8 = gm_l1;
            void * t9 = t6;
            uint8_t t10;
            t10 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t9))(t7, t8);
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
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
        void * t14 = gm_l1;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)t14));
        gm_b16_8 t16 = t13;
        gm_b16_8 t17 = t15;
        uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_d5vn1ip(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_eq_d5vn1ip F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            void * t8 = gm_l1;
            void * t9 = t6;
            uint8_t t10;
            t10 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t9))(t7, t8);
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
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t12));
        void * t14 = gm_l1;
        gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t14));
        gm_tl874t2atnlh3 t16 = t13;
        gm_tl874t2atnlh3 t17 = t15;
        uint8_t t18 = (gm_eqff85ac10o3(((uint8_t *)&t16), ((uint8_t *)&t17)));
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_key_eq_l22e6mq(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_eq_l22e6mq F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            void * t8 = gm_l1;
            void * t9 = t6;
            uint8_t t10;
            t10 = ((uint8_t (*)(void *, void *))GM_PTR_UNTAG(t9))(t7, t8);
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
        uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)t12));
        void * t14 = gm_l1;
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)t14));
        uint8_t t16 = ((t13 == t15));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_7bps17u(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_7bps17u F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
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
        int64_t t13 = GM_LD(int64_t, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        int64_t t15 = t13;
        uint64_t t16 = gm_hashsqt22rph28(((uint8_t *)&t15), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_at8t7r1(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_at8t7r1 F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
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
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        gm_tl874t2atnlh3 t15 = t13;
        uint64_t t16 = gm_hash9v0qhqsmrd(((uint8_t *)&t15), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_futbl4l(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_futbl4l F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
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
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        gm_t88rcucg1b7uk t15 = t13;
        uint64_t t16 = gm_hash9m6mvtmgde(((uint8_t *)&t15), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_hebjkbg(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_hebjkbg F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
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
        uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint64_t t15 = t13;
        uint64_t t16 = gm_hashb2nno7n67r(((uint8_t *)&t15), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_hj5j24t(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_hj5j24t F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
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
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        gm_tl874t2atnlh3 t15 = t13;
        uint64_t t16 = gm_hashff85ac10o3(((uint8_t *)&t15), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_nkn0rq8(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_nkn0rq8 F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
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
        uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        uint32_t t15 = t13;
        uint64_t t16 = gm_hash467d5cm1d5(((uint8_t *)&t15), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_key_hash_odgtuo9(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_key_hash_odgtuo9 F;
    void * gm_l0; /* k */
    uint64_t gm_l1; /* seed */
    void * gm_l2; /* f */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = ((void *)0);
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l2 = t4;
        {
            void * t6 = gm_l2;
            void * t7 = gm_l0;
            uint64_t t8 = gm_l1;
            void * t9 = t6;
            uint64_t t10;
            t10 = ((uint64_t (*)(void *, uint64_t))GM_PTR_UNTAG(t9))(t7, t8);
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
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
        uint64_t t14 = gm_l1;
        gm_b16_8 t15 = t13;
        uint64_t t16 = gm_hash0iqiubfm7e(((uint8_t *)&t15), 0xcbf29ce484222325ULL ^ (uint64_t)t14);
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline uint64_t gm_hash0iqiubfm7e(const uint8_t *v, uint64_t h) {
    uint64_t n = GM_LD(uintptr_t, v + 8); const uint8_t *d = GM_LD(void *, v);
    h = gm_mix64(h, n);
    h = gm_mix_bytes(h, d, n);
    return h;
}
inline uint64_t gm_hash467d5cm1d5(const uint8_t *v, uint64_t h) {
    return gm_mix64(h, (uint64_t)GM_LD(uint32_t, v));
}
inline uint64_t gm_hash9m6mvtmgde(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    return h;
}
inline uint64_t gm_hash9v0qhqsmrd(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 4));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 8));
    return h;
}
inline uint64_t gm_hashb2nno7n67r(const uint8_t *v, uint64_t h) {
    return gm_mix64(h, (uint64_t)GM_LD(uint64_t, v));
}
inline uint64_t gm_hashff85ac10o3(const uint8_t *v, uint64_t h) {
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 0));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 4));
    h = gm_mix64(h, (uint64_t)GM_LD(uint32_t, v + 8));
    return h;
}
inline uint64_t gm_hashsqt22rph28(const uint8_t *v, uint64_t h) {
    return gm_mix64(h, (uint64_t)(int64_t)GM_LD(int64_t, v));
}
inline __attribute__((aligned(4))) void gm_f_deinit_vtu6170(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_vtu6170 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* size */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        {
            gm_l1 = ((uint64_t)64ULL);
        }
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, ((((uint8_t *)t7) + 40) + 8));
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 16));
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, ((((uint8_t *)t10) + 40) + 0));
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = t13;
            uint64_t t15 = gm_l1;
            void * t16 = gm_l0;
            uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 0) + 8);
            uint64_t r18; bool o19 = __builtin_mul_overflow(t15, t17, &r18);
            if (o19) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 219, 64);
            uint64_t t20 = r18;
            void * t21 = t9;
            gm_b0_1 t22;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t21))(t11, t14, t20, ((uint64_t)8ULL));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t23 = gm_l0;
        uint8_t *t24 = (((uint8_t *)t23) + 0);
        gm_b16_8 t25 = gm_f_empty_slice_tklonjv();
        GM_ST(gm_b16_8, t24, t25);
    }
    {
        void * t26 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t26) + 16), ((uint64_t)0ULL));
    }
    {
        void * t27 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t27) + 24), ((uint64_t)0ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_fi5pe57(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_fi5pe57 F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* s */
    GM_STACK_CHECK(&F, 345u);
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
        uint64_t t5 = gm_l2;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_tf5srh4mp4b3i t8;
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
        void * t10 = gm_l1;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_f_key_hash_hebjkbg(t10, t12);
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = gm_l2;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 60, 52);
        uint64_t t18 = r16;
        uint64_t t19 = (uint64_t)(t14 & t18);
        gm_l3 = t19;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb20;
            {
                uint64_t t23 = gm_l3;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
                if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcbsc23is03ttm, 62, 15);
                void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 0)) + t24 * 24);
                gm_l4 = t27;
            }
            {
                void * t29 = gm_l4;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                uint8_t t31 = ((t30 == ((uint8_t)0ULL)));
                if (!t31) goto ie28;
                {
                    gm_tf5srh4mp4b3i t33;
                    memset(&t33, 0, sizeof t33);
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 0));
                uint8_t t38 = ((t37 == ((uint8_t)1ULL)));
                if (!t38) goto ie35;
                void * t39 = gm_l4;
                void * t40 = (void *)(((uint8_t *)t39) + 8);
                void * t41 = gm_l1;
                uint8_t t42 = gm_f_key_eq_l22e6mq(t40, t41);
                if (!t42) goto ie35;
                {
                    uint64_t t44 = gm_l3;
                    gm_tf5srh4mp4b3i t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t45), t44);
                    ((uint8_t *)&t45)[8] = 1;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint64_t t47 = gm_l3;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 65, 14);
                uint64_t t50 = r48;
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 65, 24);
                uint64_t t54 = r52;
                uint64_t t55 = (uint64_t)(t50 & t54);
                gm_l3 = t55;
            }
lc20:;
        }
lb20:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_fuvgmeg(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_fuvgmeg F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* s */
    GM_STACK_CHECK(&F, 345u);
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
        uint64_t t5 = gm_l2;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_tf5srh4mp4b3i t8;
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
        void * t10 = gm_l1;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_f_key_hash_hj5j24t(t10, t12);
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = gm_l2;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 60, 52);
        uint64_t t18 = r16;
        uint64_t t19 = (uint64_t)(t14 & t18);
        gm_l3 = t19;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb20;
            {
                uint64_t t23 = gm_l3;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
                if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcbsc23is03ttm, 62, 15);
                void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 0)) + t24 * 24);
                gm_l4 = t27;
            }
            {
                void * t29 = gm_l4;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                uint8_t t31 = ((t30 == ((uint8_t)0ULL)));
                if (!t31) goto ie28;
                {
                    gm_tf5srh4mp4b3i t33;
                    memset(&t33, 0, sizeof t33);
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 0));
                uint8_t t38 = ((t37 == ((uint8_t)1ULL)));
                if (!t38) goto ie35;
                void * t39 = gm_l4;
                void * t40 = (void *)(((uint8_t *)t39) + 4);
                void * t41 = gm_l1;
                uint8_t t42 = gm_f_key_eq_d5vn1ip(t40, t41);
                if (!t42) goto ie35;
                {
                    uint64_t t44 = gm_l3;
                    gm_tf5srh4mp4b3i t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t45), t44);
                    ((uint8_t *)&t45)[8] = 1;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint64_t t47 = gm_l3;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 65, 14);
                uint64_t t50 = r48;
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 65, 24);
                uint64_t t54 = r52;
                uint64_t t55 = (uint64_t)(t50 & t54);
                gm_l3 = t55;
            }
lc20:;
        }
lb20:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ve3qs5b(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_ve3qs5b F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* s */
    GM_STACK_CHECK(&F, 345u);
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
        uint64_t t5 = gm_l2;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_tf5srh4mp4b3i t8;
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
        void * t10 = gm_l1;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_f_key_hash_odgtuo9(t10, t12);
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = gm_l2;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 60, 52);
        uint64_t t18 = r16;
        uint64_t t19 = (uint64_t)(t14 & t18);
        gm_l3 = t19;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb20;
            {
                uint64_t t23 = gm_l3;
                uint64_t t24 = (uintptr_t)t23;
                void * t25 = gm_l0;
                uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 0) + 8);
                if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcbsc23is03ttm, 62, 15);
                void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 0)) + t24 * 32);
                gm_l4 = t27;
            }
            {
                void * t29 = gm_l4;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                uint8_t t31 = ((t30 == ((uint8_t)0ULL)));
                if (!t31) goto ie28;
                {
                    gm_tf5srh4mp4b3i t33;
                    memset(&t33, 0, sizeof t33);
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                void * t36 = gm_l4;
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 0));
                uint8_t t38 = ((t37 == ((uint8_t)1ULL)));
                if (!t38) goto ie35;
                void * t39 = gm_l4;
                void * t40 = (void *)(((uint8_t *)t39) + 8);
                void * t41 = gm_l1;
                uint8_t t42 = gm_f_key_eq_7nhp7gb(t40, t41);
                if (!t42) goto ie35;
                {
                    uint64_t t44 = gm_l3;
                    gm_tf5srh4mp4b3i t45;
                    GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t45), t44);
                    ((uint8_t *)&t45)[8] = 1;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint64_t t47 = gm_l3;
                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 65, 14);
                uint64_t t50 = r48;
                uint64_t t51 = gm_l2;
                uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                if (o53) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 65, 24);
                uint64_t t54 = r52;
                uint64_t t55 = (uint64_t)(t50 & t54);
                gm_l3 = t55;
            }
lc20:;
        }
lb20:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_f2mbmm8(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_f2mbmm8 F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_a1bt60v(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tppo2hmut88dq t8;
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
        gm_tm98ap4112hee t14 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 24) + 16));
        gm_tppo2hmut88dq t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[8] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_fqooi5f(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_fqooi5f F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_pnb83dt(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tppo2hmut88dq t8;
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
        gm_tm98ap4112hee t14 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 24) + 16));
        gm_tppo2hmut88dq t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[8] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_ftjmcbc(void * a0, gm_tn68el8d14vku a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ftjmcbc F;
    void * gm_l0; /* m */
    gm_tn68el8d14vku gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_mod8kqd(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tf5srh4mp4b3i t8;
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
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 32) + 24));
        gm_tf5srh4mp4b3i t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[8] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tppo2hmut88dq gm_f_get_v07up09(void * a0, int64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_v07up09 F;
    void * gm_l0; /* m */
    int64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_hjd0ufa(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tppo2hmut88dq t8;
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
        gm_tm98ap4112hee t14 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 24) + 16));
        gm_tppo2hmut88dq t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[8] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_get_v0dhnfj(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_v0dhnfj F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_u6q3m28(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tu3261i9p5758 t8;
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
        gm_ttu09lij36f8c t14 = GM_LD(gm_ttu09lij36f8c, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 48) + 8));
        gm_tu3261i9p5758 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 44, 0);
        GM_ST(gm_ttu09lij36f8c, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[40] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tu3261i9p5758 gm_f_get_v0dhnfj_c(void * a0, uint32_t a1) { gm_tu3261i9p5758 gm_r; gm_f_get_v0dhnfj(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_f7u1075(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grow_f7u1075 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* old */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    void * gm_l5; /* raw */
    gm_b16_8 gm_l6; /* slots */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        uint64_t t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        uint64_t t9 = ((uint64_t)8ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint64_t t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 40);
        uint64_t t16 = r14;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie11;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t21 = t20;
        t10 = t21;
        goto ix11;
ie11:;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)2ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 82);
        uint64_t t26 = r24;
        uint64_t t27 = t26;
        t10 = t27;
ix11:;
        t4 = t10;
ix5:;
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)40ULL);
    }
    {
        gm_l4 = ((uint64_t)8ULL);
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((((uint8_t *)t28) + 40) + 8));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((((uint8_t *)t31) + 40) + 0));
        uint64_t t33 = gm_l3;
        uint64_t t34 = gm_l2;
        uint64_t r35; bool o36 = __builtin_mul_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 88, 43);
        uint64_t t37 = r35;
        uint64_t t38 = gm_l4;
        void * t39 = t30;
        void * t40;
        t40 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t39))(t32, t37, t38);
        void * t41 = t40;
        void * t42;
        if ((GM_LD(void *, ((uint8_t *)&t41)) != 0)) {
            void * t43 = GM_LD(void *, (((uint8_t *)&t41) + 0));
            t42 = t43;
        } else {
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t45;
            ((uint8_t *)&t45)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t45 + 1), t44);
            gm_tftb1odc739t5 t46 = t45;
            uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
            F.ret = t45;
            goto gm_fret;
        }
        gm_l5 = t42;
    }
    {
        void * t48 = gm_l5;
        if ((uintptr_t)t48 % 8 != 0) gm_trap_misaligned((uintptr_t)t48, 8, gm_srcbsc23is03ttm, 89, 14);
        void * t49 = t48;
        void * t50 = ((uint8_t *)t49);
        uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t52 = gm_l2;
        uint64_t t53 = (uintptr_t)t52;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t50 + t51 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53 - t51));
        gm_l6 = t54;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t56 = gm_l2;
            gm_l7 = t56;
        }
        {
            for (;;) {
                uint64_t t58 = gm_l8;
                uint64_t t59 = gm_l7;
                uint8_t t60 = (t58 < t59);
                if (!t60) goto lb57;
                {
                    uint64_t t63 = gm_l8;
                    uint64_t t64 = t63;
                    uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&t64));
                    gm_l9 = t67;
                    {
                        uint64_t t69 = gm_l9;
                        uint64_t t70 = (uintptr_t)t69;
                        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srcbsc23is03ttm, 90, 21);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t70 * 40) + 0), ((uint8_t)0ULL));
                    }
                    goto me65;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 90, 5);
me65:;
                }
lc57:;
                {
                    uint64_t t73 = gm_l8;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l8 = t76;
                }
            }
lb57:;
        }
    }
    {
        void * t77 = gm_l0;
        gm_b16_8 t78 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t77) + 0), t78);
    }
    {
        void * t79 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t79) + 16), ((uint64_t)0ULL));
    }
    {
        void * t80 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t80) + 24), ((uint64_t)0ULL));
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l10 = t82;
        }
        {
            for (;;) {
                uint64_t t84 = gm_l11;
                uint64_t t85 = gm_l10;
                uint8_t t86 = (t84 < t85);
                if (!t86) goto lb83;
                {
                    uint64_t t89 = gm_l11;
                    uint64_t t90 = t89;
                    uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)&t90));
                    gm_l12 = t93;
                    {
                        uint64_t t96 = gm_l12;
                        uint64_t t97 = (uintptr_t)t96;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srcbsc23is03ttm, 95, 12);
                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t97 * 40) + 0));
                        uint8_t t100 = ((t99 == ((uint8_t)1ULL)));
                        if (!t100) goto ie95;
                        {
                            void * t102 = gm_l0;
                            uint64_t t103 = gm_l12;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcbsc23is03ttm, 95, 49);
                            void * t106 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 40) + 4);
                            uint64_t t107 = gm_l12;
                            uint64_t t108 = (uintptr_t)t107;
                            uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srcbsc23is03ttm, 95, 62);
                            void * t110 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t108 * 40) + 8);
                            gm_f_insert_new_pni6tnm(t102, t106, t110);
                        }
                        goto ix95;
ie95:;
ix95:;
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 94, 5);
me91:;
                }
lc83:;
                {
                    uint64_t t112 = gm_l11;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    uint64_t t115 = r113;
                    gm_l11 = t115;
                }
            }
lb83:;
        }
    }
    {
        uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t118 = (t117 > ((uint64_t)0ULL));
        if (!t118) goto ie116;
        {
            void * t120 = gm_l0;
            void * t121 = GM_LD(void *, ((((uint8_t *)t120) + 40) + 8));
            void * t122 = GM_LD(void *, (((uint8_t *)t121) + 16));
            void * t123 = gm_l0;
            void * t124 = GM_LD(void *, ((((uint8_t *)t123) + 40) + 0));
            void * t125 = GM_LD(void *, ((uint8_t *)&gm_l1));
            void * t126 = t125;
            uint64_t t127 = gm_l3;
            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r129; bool o130 = __builtin_mul_overflow(t127, t128, &r129);
            if (o130) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 98, 60);
            uint64_t t131 = r129;
            uint64_t t132 = gm_l4;
            void * t133 = t122;
            gm_b0_1 t134;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t133))(t124, t126, t131, t132);
        }
        goto ix116;
ie116:;
ix116:;
    }
    {
        gm_b0_1 t135;
        gm_tftb1odc739t5 t136;
        GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
        ((uint8_t *)&t136)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t136 + 1), t135);
        gm_tftb1odc739t5 t137 = t136;
        uint8_t t138 = ((((uint8_t *)&t137))[0] != 0);
        F.ret = t136;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_ffgu6bs(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grow_ffgu6bs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* old */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    void * gm_l5; /* raw */
    gm_b16_8 gm_l6; /* slots */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        uint64_t t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        uint64_t t9 = ((uint64_t)8ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint64_t t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 40);
        uint64_t t16 = r14;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie11;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t21 = t20;
        t10 = t21;
        goto ix11;
ie11:;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)2ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 82);
        uint64_t t26 = r24;
        uint64_t t27 = t26;
        t10 = t27;
ix11:;
        t4 = t10;
ix5:;
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)32ULL);
    }
    {
        gm_l4 = ((uint64_t)8ULL);
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((((uint8_t *)t28) + 40) + 8));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((((uint8_t *)t31) + 40) + 0));
        uint64_t t33 = gm_l3;
        uint64_t t34 = gm_l2;
        uint64_t r35; bool o36 = __builtin_mul_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 88, 43);
        uint64_t t37 = r35;
        uint64_t t38 = gm_l4;
        void * t39 = t30;
        void * t40;
        t40 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t39))(t32, t37, t38);
        void * t41 = t40;
        void * t42;
        if ((GM_LD(void *, ((uint8_t *)&t41)) != 0)) {
            void * t43 = GM_LD(void *, (((uint8_t *)&t41) + 0));
            t42 = t43;
        } else {
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t45;
            ((uint8_t *)&t45)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t45 + 1), t44);
            gm_tftb1odc739t5 t46 = t45;
            uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
            F.ret = t45;
            goto gm_fret;
        }
        gm_l5 = t42;
    }
    {
        void * t48 = gm_l5;
        if ((uintptr_t)t48 % 8 != 0) gm_trap_misaligned((uintptr_t)t48, 8, gm_srcbsc23is03ttm, 89, 14);
        void * t49 = t48;
        void * t50 = ((uint8_t *)t49);
        uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t52 = gm_l2;
        uint64_t t53 = (uintptr_t)t52;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t50 + t51 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53 - t51));
        gm_l6 = t54;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t56 = gm_l2;
            gm_l7 = t56;
        }
        {
            for (;;) {
                uint64_t t58 = gm_l8;
                uint64_t t59 = gm_l7;
                uint8_t t60 = (t58 < t59);
                if (!t60) goto lb57;
                {
                    uint64_t t63 = gm_l8;
                    uint64_t t64 = t63;
                    uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&t64));
                    gm_l9 = t67;
                    {
                        uint64_t t69 = gm_l9;
                        uint64_t t70 = (uintptr_t)t69;
                        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srcbsc23is03ttm, 90, 21);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t70 * 32) + 0), ((uint8_t)0ULL));
                    }
                    goto me65;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 90, 5);
me65:;
                }
lc57:;
                {
                    uint64_t t73 = gm_l8;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l8 = t76;
                }
            }
lb57:;
        }
    }
    {
        void * t77 = gm_l0;
        gm_b16_8 t78 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t77) + 0), t78);
    }
    {
        void * t79 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t79) + 16), ((uint64_t)0ULL));
    }
    {
        void * t80 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t80) + 24), ((uint64_t)0ULL));
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l10 = t82;
        }
        {
            for (;;) {
                uint64_t t84 = gm_l11;
                uint64_t t85 = gm_l10;
                uint8_t t86 = (t84 < t85);
                if (!t86) goto lb83;
                {
                    uint64_t t89 = gm_l11;
                    uint64_t t90 = t89;
                    uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)&t90));
                    gm_l12 = t93;
                    {
                        uint64_t t96 = gm_l12;
                        uint64_t t97 = (uintptr_t)t96;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srcbsc23is03ttm, 95, 12);
                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t97 * 32) + 0));
                        uint8_t t100 = ((t99 == ((uint8_t)1ULL)));
                        if (!t100) goto ie95;
                        {
                            void * t102 = gm_l0;
                            uint64_t t103 = gm_l12;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcbsc23is03ttm, 95, 49);
                            void * t106 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 32) + 8);
                            uint64_t t107 = gm_l12;
                            uint64_t t108 = (uintptr_t)t107;
                            uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srcbsc23is03ttm, 95, 62);
                            void * t110 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t108 * 32) + 24);
                            gm_f_insert_new_r3mi47j(t102, t106, t110);
                        }
                        goto ix95;
ie95:;
ix95:;
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 94, 5);
me91:;
                }
lc83:;
                {
                    uint64_t t112 = gm_l11;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    uint64_t t115 = r113;
                    gm_l11 = t115;
                }
            }
lb83:;
        }
    }
    {
        uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t118 = (t117 > ((uint64_t)0ULL));
        if (!t118) goto ie116;
        {
            void * t120 = gm_l0;
            void * t121 = GM_LD(void *, ((((uint8_t *)t120) + 40) + 8));
            void * t122 = GM_LD(void *, (((uint8_t *)t121) + 16));
            void * t123 = gm_l0;
            void * t124 = GM_LD(void *, ((((uint8_t *)t123) + 40) + 0));
            void * t125 = GM_LD(void *, ((uint8_t *)&gm_l1));
            void * t126 = t125;
            uint64_t t127 = gm_l3;
            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r129; bool o130 = __builtin_mul_overflow(t127, t128, &r129);
            if (o130) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 98, 60);
            uint64_t t131 = r129;
            uint64_t t132 = gm_l4;
            void * t133 = t122;
            gm_b0_1 t134;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t133))(t124, t126, t131, t132);
        }
        goto ix116;
ie116:;
ix116:;
    }
    {
        gm_b0_1 t135;
        gm_tftb1odc739t5 t136;
        GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
        ((uint8_t *)&t136)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t136 + 1), t135);
        gm_tftb1odc739t5 t137 = t136;
        uint8_t t138 = ((((uint8_t *)&t137))[0] != 0);
        F.ret = t136;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_grow_fu99cpb(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_grow_fu99cpb F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* old */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    void * gm_l5; /* raw */
    gm_b16_8 gm_l6; /* slots */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l1 = t3;
    }
    {
        uint64_t t4;
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        uint64_t t9 = ((uint64_t)8ULL);
        t4 = t9;
        goto ix5;
ie5:;
        uint64_t t10;
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
        uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)2ULL), &r14);
        if (o15) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 40);
        uint64_t t16 = r14;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t18 = (t16 <= t17);
        if (!t18) goto ie11;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t21 = t20;
        t10 = t21;
        goto ix11;
ie11:;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)2ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 85, 82);
        uint64_t t26 = r24;
        uint64_t t27 = t26;
        t10 = t27;
ix11:;
        t4 = t10;
ix5:;
        gm_l2 = t4;
    }
    {
        gm_l3 = ((uint64_t)32ULL);
    }
    {
        gm_l4 = ((uint64_t)8ULL);
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((((uint8_t *)t28) + 40) + 8));
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((((uint8_t *)t31) + 40) + 0));
        uint64_t t33 = gm_l3;
        uint64_t t34 = gm_l2;
        uint64_t r35; bool o36 = __builtin_mul_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 88, 43);
        uint64_t t37 = r35;
        uint64_t t38 = gm_l4;
        void * t39 = t30;
        void * t40;
        t40 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t39))(t32, t37, t38);
        void * t41 = t40;
        void * t42;
        if ((GM_LD(void *, ((uint8_t *)&t41)) != 0)) {
            void * t43 = GM_LD(void *, (((uint8_t *)&t41) + 0));
            t42 = t43;
        } else {
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
            gm_tftb1odc739t5 t45;
            ((uint8_t *)&t45)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t45 + 1), t44);
            gm_tftb1odc739t5 t46 = t45;
            uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
            F.ret = t45;
            goto gm_fret;
        }
        gm_l5 = t42;
    }
    {
        void * t48 = gm_l5;
        if ((uintptr_t)t48 % 8 != 0) gm_trap_misaligned((uintptr_t)t48, 8, gm_srcbsc23is03ttm, 89, 14);
        void * t49 = t48;
        void * t50 = ((uint8_t *)t49);
        uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t52 = gm_l2;
        uint64_t t53 = (uintptr_t)t52;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t50 + t51 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53 - t51));
        gm_l6 = t54;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t56 = gm_l2;
            gm_l7 = t56;
        }
        {
            for (;;) {
                uint64_t t58 = gm_l8;
                uint64_t t59 = gm_l7;
                uint8_t t60 = (t58 < t59);
                if (!t60) goto lb57;
                {
                    uint64_t t63 = gm_l8;
                    uint64_t t64 = t63;
                    uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&t64));
                    gm_l9 = t67;
                    {
                        uint64_t t69 = gm_l9;
                        uint64_t t70 = (uintptr_t)t69;
                        uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srcbsc23is03ttm, 90, 21);
                        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t70 * 32) + 0), ((uint8_t)0ULL));
                    }
                    goto me65;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 90, 5);
me65:;
                }
lc57:;
                {
                    uint64_t t73 = gm_l8;
                    uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                    uint64_t t76 = r74;
                    gm_l8 = t76;
                }
            }
lb57:;
        }
    }
    {
        void * t77 = gm_l0;
        gm_b16_8 t78 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t77) + 0), t78);
    }
    {
        void * t79 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t79) + 16), ((uint64_t)0ULL));
    }
    {
        void * t80 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t80) + 24), ((uint64_t)0ULL));
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l10 = t82;
        }
        {
            for (;;) {
                uint64_t t84 = gm_l11;
                uint64_t t85 = gm_l10;
                uint8_t t86 = (t84 < t85);
                if (!t86) goto lb83;
                {
                    uint64_t t89 = gm_l11;
                    uint64_t t90 = t89;
                    uint64_t t93 = GM_LD(uint64_t, ((uint8_t *)&t90));
                    gm_l12 = t93;
                    {
                        uint64_t t96 = gm_l12;
                        uint64_t t97 = (uintptr_t)t96;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srcbsc23is03ttm, 95, 12);
                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t97 * 32) + 0));
                        uint8_t t100 = ((t99 == ((uint8_t)1ULL)));
                        if (!t100) goto ie95;
                        {
                            void * t102 = gm_l0;
                            uint64_t t103 = gm_l12;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcbsc23is03ttm, 95, 49);
                            void * t106 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 32) + 8);
                            uint64_t t107 = gm_l12;
                            uint64_t t108 = (uintptr_t)t107;
                            uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srcbsc23is03ttm, 95, 62);
                            void * t110 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t108 * 32) + 24);
                            gm_f_insert_new_dj80dk7(t102, t106, t110);
                        }
                        goto ix95;
ie95:;
ix95:;
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 94, 5);
me91:;
                }
lc83:;
                {
                    uint64_t t112 = gm_l11;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    uint64_t t115 = r113;
                    gm_l11 = t115;
                }
            }
lb83:;
        }
    }
    {
        uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t118 = (t117 > ((uint64_t)0ULL));
        if (!t118) goto ie116;
        {
            void * t120 = gm_l0;
            void * t121 = GM_LD(void *, ((((uint8_t *)t120) + 40) + 8));
            void * t122 = GM_LD(void *, (((uint8_t *)t121) + 16));
            void * t123 = gm_l0;
            void * t124 = GM_LD(void *, ((((uint8_t *)t123) + 40) + 0));
            void * t125 = GM_LD(void *, ((uint8_t *)&gm_l1));
            void * t126 = t125;
            uint64_t t127 = gm_l3;
            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r129; bool o130 = __builtin_mul_overflow(t127, t128, &r129);
            if (o130) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 98, 60);
            uint64_t t131 = r129;
            uint64_t t132 = gm_l4;
            void * t133 = t122;
            gm_b0_1 t134;
            ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t133))(t124, t126, t131, t132);
        }
        goto ix116;
ie116:;
ix116:;
    }
    {
        gm_b0_1 t135;
        gm_tftb1odc739t5 t136;
        GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
        ((uint8_t *)&t136)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t136 + 1), t135);
        gm_tftb1odc739t5 t137 = t136;
        uint8_t t138 = ((((uint8_t *)&t137))[0] != 0);
        F.ret = t136;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_hash_map_fupfd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_hash_map_vqsve4u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_insert_new_fhcpthq(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_fhcpthq F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_hj5j24t(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 56) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 56) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_tu3t6donebmau *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 56)) + 1, 0, 3);
        void * t50 = gm_l1;
        gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_t6ok5tvcjc200 t53 = GM_LD(gm_t6ok5tvcjc200, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tu3t6donebmau *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 56)) + 0, ((uint8_t)1ULL));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tu3t6donebmau *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 56)) + 4, t51);
        GM_ST(gm_t6ok5tvcjc200, (uint8_t *)&(*(gm_tu3t6donebmau *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 56)) + 16, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_vbg72ug(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_vbg72ug F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_odgtuo9(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 32) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 32) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_tscmbvdaq159i *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 1, 0, 7);
        void * t50 = gm_l1;
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)t50));
        void * t52 = gm_l2;
        uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tscmbvdaq159i *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 0, ((uint8_t)1ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tscmbvdaq159i *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 8, t51);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tscmbvdaq159i *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 32)) + 24, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_vglvkli(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_vglvkli F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_7bps17u(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 24) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 24) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 1, 0, 7);
        void * t50 = gm_l1;
        int64_t t51 = GM_LD(int64_t, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_tm98ap4112hee t53 = GM_LD(gm_tm98ap4112hee, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 0, ((uint8_t)1ULL));
        GM_ST(int64_t, (uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 8, t51);
        GM_ST(gm_tm98ap4112hee, (uint8_t *)&(*(gm_t856vjvr00kbi *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 24)) + 16, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_vhmdu9t(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_vhmdu9t F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_at8t7r1(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 20) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 20) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_t1ve8lm87pun7 *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 20)) + 1, 0, 3);
        void * t50 = gm_l1;
        gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)t50));
        void * t52 = gm_l2;
        uint32_t t53 = GM_LD(uint32_t, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ve8lm87pun7 *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 20)) + 0, ((uint8_t)1ULL));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t1ve8lm87pun7 *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 20)) + 4, t51);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ve8lm87pun7 *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 20)) + 16, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_vm1l89f(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_vm1l89f F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 384u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_nkn0rq8(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 48) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 48) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_tsr6pj9uclack *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 1, 0, 3);
        void * t50 = gm_l1;
        uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_ttu09lij36f8c t53 = GM_LD(gm_ttu09lij36f8c, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tsr6pj9uclack *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 0, ((uint8_t)1ULL));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tsr6pj9uclack *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 4, t51);
        GM_ST(gm_ttu09lij36f8c, (uint8_t *)&(*(gm_tsr6pj9uclack *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 48)) + 8, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_insert_new_vvf6ptg(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_insert_new_vvf6ptg F;
    void * gm_l0; /* m */
    void * gm_l1; /* k */
    void * gm_l2; /* v */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 396u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 32));
        uint64_t t7 = gm_f_key_hash_odgtuo9(t4, t6);
        uint64_t t8 = (uint64_t)t7;
        uint64_t t9 = gm_l3;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 73, 52);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        gm_l4 = t13;
    }
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcbsc23is03ttm, 74, 11);
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)) + t16 * 64) + 0));
            uint8_t t20 = ((t19 == ((uint8_t)1ULL)));
            if (!t20) goto lb14;
            {
                uint64_t t23 = gm_l4;
                uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 74, 43);
                uint64_t t26 = r24;
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 74, 53);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t26 & t30);
                gm_l4 = t31;
            }
lc14:;
        }
lb14:;
    }
    {
        uint64_t t33 = gm_l4;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l0;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcbsc23is03ttm, 75, 8);
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 64) + 0));
        uint8_t t38 = ((t37 == ((uint8_t)0ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l0;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
            uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 75, 36);
            uint64_t t45 = r43;
            GM_ST(uint64_t, (((uint8_t *)t40) + 24), t45);
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcbsc23is03ttm, 76, 5);
        memset((uint8_t *)&(*(gm_tg1gv7r8d60ki *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 64)) + 1, 0, 7);
        void * t50 = gm_l1;
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)t50));
        void * t52 = gm_l2;
        gm_trqfvmjtt4sij t53 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)t52));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tg1gv7r8d60ki *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 64)) + 0, ((uint8_t)1ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tg1gv7r8d60ki *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 64)) + 8, t51);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tg1gv7r8d60ki *)((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 64)) + 24, t53);
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 16));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 77, 5);
        uint64_t t59 = r57;
        GM_ST(uint64_t, (((uint8_t *)t54) + 16), t59);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint64_t gm_f_key_hash_futbl4l(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi(void * a0, const void * a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_f1iksvi F;
    void * gm_l0; /* m */
    gm_tiilmfd16v3fc gm_l1; /* k */
    uint32_t gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_uarsnut(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 107, 9);
            uint32_t t13 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 40) + 32), t13);
        }
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)4ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t26 = r24;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        uint64_t r29; bool o30 = __builtin_mul_overflow(t28, ((uint64_t)3ULL), &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t31 = r29;
        uint8_t t32 = (t26 > t31);
        if (!t32) goto ie18;
        {
            void * t34 = gm_l0;
            gm_tftb1odc739t5 t35 = gm_f_grow_6sn05as(t34);
            gm_tftb1odc739t5 t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t36) + 1));
                gm_tftb1odc739t5 t38;
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)((uint8_t *)&gm_l1);
        void * t42 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_anb2gma(t40, t41, t42);
    }
    {
        gm_b0_1 t43;
        gm_tftb1odc739t5 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        ((uint8_t *)&t44)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t44 + 1), t43);
        gm_tftb1odc739t5 t45 = t44;
        uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi_c(void * a0, gm_tiilmfd16v3fc a1, uint32_t a2) { return gm_f_put_f1iksvi(a0, &a1, a2); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f99phma(void * a0, gm_tn68el8d14vku a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_f99phma F;
    void * gm_l0; /* m */
    gm_tn68el8d14vku gm_l1; /* k */
    uint64_t gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 246u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_mod8kqd(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 107, 9);
            uint64_t t13 = gm_l2;
            GM_ST(uint64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 32) + 24), t13);
        }
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)4ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t26 = r24;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        uint64_t r29; bool o30 = __builtin_mul_overflow(t28, ((uint64_t)3ULL), &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t31 = r29;
        uint8_t t32 = (t26 > t31);
        if (!t32) goto ie18;
        {
            void * t34 = gm_l0;
            gm_tftb1odc739t5 t35 = gm_f_grow_ai5cphs(t34);
            gm_tftb1odc739t5 t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t36) + 1));
                gm_tftb1odc739t5 t38;
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)((uint8_t *)&gm_l1);
        void * t42 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_l14losc(t40, t41, t42);
    }
    {
        gm_b0_1 t43;
        gm_tftb1odc739t5 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        ((uint8_t *)&t44)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t44 + 1), t43);
        gm_tftb1odc739t5 t45 = t44;
        uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ftmshk7(void * a0, uint32_t a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ftmshk7 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    gm_tjpqvfd72o62u gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_ha0uq33(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 107, 9);
            gm_tjpqvfd72o62u t13 = gm_l2;
            GM_ST(gm_tjpqvfd72o62u, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 40) + 8), t13);
        }
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)4ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t26 = r24;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        uint64_t r29; bool o30 = __builtin_mul_overflow(t28, ((uint64_t)3ULL), &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t31 = r29;
        uint8_t t32 = (t26 > t31);
        if (!t32) goto ie18;
        {
            void * t34 = gm_l0;
            gm_tftb1odc739t5 t35 = gm_f_grow_f7u1075(t34);
            gm_tftb1odc739t5 t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t36) + 1));
                gm_tftb1odc739t5 t38;
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)((uint8_t *)&gm_l1);
        void * t42 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_pni6tnm(t40, t41, t42);
    }
    {
        gm_b0_1 t43;
        gm_tftb1odc739t5 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        ((uint8_t *)&t44)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t44 + 1), t43);
        gm_tftb1odc739t5 t45 = t44;
        uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ftmshk7_c(void * a0, uint32_t a1, gm_tjpqvfd72o62u a2) { return gm_f_put_ftmshk7(a0, a1, &a2); }
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_fv8gs1b(void * a0, uint32_t a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_fv8gs1b F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    gm_tl874t2atnlh3 gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_shfuv0t(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 107, 9);
            gm_tl874t2atnlh3 t13 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 20) + 8), t13);
        }
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)4ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t26 = r24;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        uint64_t r29; bool o30 = __builtin_mul_overflow(t28, ((uint64_t)3ULL), &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t31 = r29;
        uint8_t t32 = (t26 > t31);
        if (!t32) goto ie18;
        {
            void * t34 = gm_l0;
            gm_tftb1odc739t5 t35 = gm_f_grow_3o0noai(t34);
            gm_tftb1odc739t5 t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t36) + 1));
                gm_tftb1odc739t5 t38;
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)((uint8_t *)&gm_l1);
        void * t42 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_t90e9jk(t40, t41, t42);
    }
    {
        gm_b0_1 t43;
        gm_tftb1odc739t5 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        ((uint8_t *)&t44)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t44 + 1), t43);
        gm_tftb1odc739t5 t45 = t44;
        uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_v6g5pup F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint8_t gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_hb6lt4h(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 107, 9);
            uint8_t t13 = gm_l2;
            GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 12) + 8), t13);
        }
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)4ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t26 = r24;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        uint64_t r29; bool o30 = __builtin_mul_overflow(t28, ((uint64_t)3ULL), &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t31 = r29;
        uint8_t t32 = (t26 > t31);
        if (!t32) goto ie18;
        {
            void * t34 = gm_l0;
            gm_tftb1odc739t5 t35 = gm_f_grow_gsr5sn1(t34);
            gm_tftb1odc739t5 t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t36) + 1));
                gm_tftb1odc739t5 t38;
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)((uint8_t *)&gm_l1);
        void * t42 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_3g6r6p3(t40, t41, t42);
    }
    {
        gm_b0_1 t43;
        gm_tftb1odc739t5 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        ((uint8_t *)&t44)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t44 + 1), t43);
        gm_tftb1odc739t5 t45 = t44;
        uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_vrooh94(void * a0, gm_cseahvg8v7p0vs a1, gm_csktnl8ual7c3g a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_vrooh94 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    gm_t9805qtonbsag gm_l2; /* v */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t9805qtonbsag, a2);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t5 = gm_f_find_fuvgmeg(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 107, 9);
            gm_t9805qtonbsag t13 = gm_l2;
            GM_ST(gm_t9805qtonbsag, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 24) + 16), t13);
        }
        {
            gm_b0_1 t14;
            gm_tftb1odc739t5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            gm_tftb1odc739t5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcbsc23is03ttm, 110, 9);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_mul_overflow(t23, ((uint64_t)4ULL), &r24);
        if (o25) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 8);
        uint64_t t26 = r24;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
        uint64_t r29; bool o30 = __builtin_mul_overflow(t28, ((uint64_t)3ULL), &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srcbsc23is03ttm, 110, 27);
        uint64_t t31 = r29;
        uint8_t t32 = (t26 > t31);
        if (!t32) goto ie18;
        {
            void * t34 = gm_l0;
            gm_tftb1odc739t5 t35 = gm_f_grow_36b3d0o(t34);
            gm_tftb1odc739t5 t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_t9qkh0rsooc8d t37 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t36) + 1));
                gm_tftb1odc739t5 t38;
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)((uint8_t *)&gm_l1);
        void * t42 = (void *)((uint8_t *)&gm_l2);
        gm_f_insert_new_9oft181(t40, t41, t42);
    }
    {
        gm_b0_1 t43;
        gm_tftb1odc739t5 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        ((uint8_t *)&t44)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t44 + 1), t43);
        gm_tftb1odc739t5 t45 = t44;
        uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
        F.ret = t44;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_remove_f5r5ejv(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remove_f5r5ejv F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_58mj7o5(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tl9turq9ndgsc t8;
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
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcbsc23is03ttm, 160, 5);
        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 72) + 0), ((uint8_t)2ULL));
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 16));
        uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
        if (o18) gm_trap("integer overflow in `-`", gm_srcbsc23is03ttm, 161, 5);
        uint64_t t19 = r17;
        GM_ST(uint64_t, (((uint8_t *)t14) + 16), t19);
    }
    {
        uint64_t t20 = gm_l2;
        uint64_t t21 = (uintptr_t)t20;
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8);
        if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcbsc23is03ttm, 162, 12);
        gm_ts5u7codfocvn t24 = GM_LD(gm_ts5u7codfocvn, (((uint8_t *)GM_LD(void *, (((uint8_t *)t22) + 0)) + t21 * 72) + 16));
        gm_tl9turq9ndgsc t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 60, 0);
        GM_ST(gm_ts5u7codfocvn, ((uint8_t *)&t25), t24);
        ((uint8_t *)&t25)[56] = 1;
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tl9turq9ndgsc gm_f_remove_f5r5ejv_c(void * a0, uint64_t a1) { gm_tl9turq9ndgsc gm_r; gm_f_remove_f5r5ejv(&gm_r, a0, a1); return gm_r; }
